# Architecture — 0.2.0

## Audio path

OBS Mix 1 -> obs_add_raw_audio_callback -> lock-free SPSC ring buffer -> worker thread -> private FIFO -> PipeWire/PulseAudio module-pipe-source -> OBS Virtual Mic (Audio Source)

Unlike 0.1.x, there is no null sink and therefore no application-facing virtual output device.

## Device lifecycle

Start:
1. Create `/run/user/$UID/obs-virtual-mic-$PID.fifo` with mode 0600.
2. Load `module-pipe-source` through `pactl`, with 48 kHz stereo float32 audio and source name `OBS_VirtualMic`.
3. Start the FIFO writer worker.
4. Register OBS raw-audio callback for mix 1.

Stop/shutdown:
1. Remove OBS audio callback and stop writer.
2. Unload the exact PipeWire/Pulse module ID created by this plugin.
3. Remove the FIFO.

The FIFO path is placed under `/run/user/$UID` so it can be reached by both a Flatpak OBS process and the user's host PipeWire server.


## 0.3.0 Flatpak FIFO path

For `module-pipe-source`, the FIFO is opened by the host PipeWire/PulseAudio server, not by OBS itself. A path under `/run/user/$UID` may refer to different mount-namespace views when OBS runs as a Flatpak. Version 0.3.0 therefore creates the FIFO beneath `QStandardPaths::CacheLocation`; in Flatpak this is under `~/.var/app/com.obsproject.Studio/cache/...`, which resolves to the same host filesystem object for both processes.


## 0.3.0 source-routing bus

To provide source selection independently from Track 1 without reimplementing OBS's timestamped audio mixer, the plugin temporarily reserves Mix 6 while active. It snapshots each input audio source's mixer mask, sets bit 5 only for user-selected Virtual Mic sources, captures raw Mix 6 at 48 kHz stereo float PCM, then restores the exact original masks on stop/shutdown. This is intentionally isolated from Tracks 1–5, but Track 6 should not be edited manually while Virtual Mic is active.
