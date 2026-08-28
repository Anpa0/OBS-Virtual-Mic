# OBS Virtual Mic for Linux

**Beta 0.3.3** · Linux only · OBS Studio 32.x

A plugin that adds a **Start Virtual Mic** button to the OBS **Controls** dock, directly below **Start Virtual Camera**.

When enabled, it creates a temporary microphone input named **OBS Virtual Mic** that can be selected in Discord, Zoom, browsers, games, and any other PipeWire/PulseAudio-aware application. The device exists only while Virtual Mic is running, and disappears when it is stopped or OBS closes.

> [!WARNING]
> **Beta software.** The core virtual microphone, per-source routing, Flatpak support, and Controls-dock integration all work, but the plugin is under active development. UI integration and audio-backend details may change before a stable release.

---

## Features

- **Start Virtual Mic** button and matching settings gear in the OBS Controls dock.
- Creates **OBS Virtual Mic** as an input device only — no dummy speaker or sink is exposed.
- Choose exactly which OBS audio sources are sent to the virtual microphone.
- Source selections persist between OBS sessions and can be changed while Virtual Mic is running.
- 48 kHz stereo float PCM internally, with no encoding or compression — very little CPU or GPU overhead.
- Works with native and Flatpak OBS installations, using a host-visible FIFO to cross the Flatpak sandbox boundary.
- Automatically removes the virtual microphone when stopped or when OBS exits.

## How it works

Pressing **Start Virtual Mic** creates an input-only PipeWire/PulseAudio source named `OBS Virtual Mic`. Selected OBS audio sources are routed through a temporary private mix and written as raw PCM to that source:

```text
Selected OBS audio sources
            │
            ▼
Temporary OBS mix (Track 6)
            │
            ▼
OBS raw audio callback
            │
            ▼
Lock-free audio buffer
            │
            ▼
Host-visible FIFO
            │
            ▼
PipeWire / PulseAudio source (module-pipe-source)
            │
            ▼
"OBS Virtual Mic" input device
            │
            ▼
Discord · Zoom · browsers · games
```

No audio codec is involved anywhere in this path.

### Device lifecycle

| State | `OBS Virtual Mic` device |
| --- | --- |
| OBS closed | Not present |
| OBS running, Virtual Mic stopped | Not present |
| Virtual Mic started | Present as an input device |
| Virtual Mic stopped | Removed |
| OBS closed while Virtual Mic active | Removed automatically |

## Source selection

Click the **gear icon** beside **Start Virtual Mic** to choose which OBS audio sources to include. A microphone-only setup might look like:

| Source | Included |
| --- | :---: |
| Mic/Aux | ✓ |
| Desktop Audio | ✗ |
| Game Capture Audio | ✗ |
| Media Source | ✗ |

Or you can build a mixed virtual microphone:

| Source | Included |
| --- | :---: |
| Mic/Aux | ✓ |
| Desktop Audio | ✓ |
| Media Source | ✓ |

Virtual Mic source selection is independent of OBS Track 1 routing.

### Track 6 implementation

Beta 0.3.x uses **OBS Mix / Track 6** as a temporary private mixing bus while Virtual Mic is active. On start, the plugin:

1. Saves every audio source's existing Track 6 routing.
2. Routes only the selected Virtual Mic sources to Track 6.
3. Captures OBS Mix 6 and sends it to the virtual microphone.
4. Restores the original Track 6 routing when Virtual Mic stops or OBS exits.

Tracks 1–5 are never modified.

> [!IMPORTANT]
> Do not manually change Track 6 routing while Virtual Mic is active — the plugin will restore the Track 6 state that existed before Virtual Mic started.

---

## Installation

Most users do **not** need to build the plugin. Download the latest prebuilt Linux archive from the project's **Releases** section; it already contains the expected OBS directory structure:

```text
obs-virtual-mic/
└── bin/
    └── 64bit/
        └── obs-virtual-mic.so
```

Extract the **entire `obs-virtual-mic` folder** — not just the `.so` — into the plugins directory for the OBS build you actually run:

| OBS installation | Plugins directory |
| --- | --- |
| Flatpak | `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/` |
| Native | `~/.config/obs-studio/plugins/` |

The plugin should then be at, for example:

```text
~/.config/obs-studio/plugins/obs-virtual-mic/bin/64bit/obs-virtual-mic.so
```

Restart OBS. **Start Virtual Mic** should appear in the Controls dock, directly below **Start Virtual Camera**.

## Using the plugin

1. Launch OBS.
2. Find **Start Virtual Mic** below **Start Virtual Camera** in the Controls dock.
3. Click the **gear icon** beside it.
4. Select the OBS audio sources you want to send through the virtual microphone.
5. Click **Start Virtual Mic**. The button changes to **Stop Virtual Mic** while active.
6. In Discord, Zoom, or another app, select **OBS Virtual Mic** as the input device.
7. Click **Stop Virtual Mic** when finished.

## Uninstall

If you installed from source with the included script:

```bash
./scripts/uninstall-user.sh
```

Otherwise, delete the `obs-virtual-mic` directory from the appropriate plugin path above and restart OBS.

---

## Building from source

Everything below is only for users who want to compile the plugin themselves.

### Requirements

- Linux
- OBS Studio development headers / CMake package files
- Qt 6 development files
- CMake and Ninja
- A C++20-capable compiler
- PipeWire with PulseAudio compatibility, and `pactl` available at runtime

Developed and tested against OBS Studio 32.x. Build against an OBS development package close to the version you actually run.

### Dependencies by distribution

| Distribution | Command |
| --- | --- |
| **Fedora** | `sudo dnf install obs-studio-devel cmake ninja-build gcc-c++ qt6-qtbase-devel pulseaudio-utils` |
| **Debian / Ubuntu** | `sudo apt install build-essential cmake ninja-build libobs-dev qt6-base-dev pulseaudio-utils` |
| **Arch** | `sudo pacman -S --needed base-devel cmake ninja obs-studio qt6-base libpulse pipewire-pulse` |

> [!NOTE]
> On Debian and Ubuntu, the repository OBS version varies by release. If `libobs-dev` is substantially older than the OBS you actually run — for example a newer Flatpak OBS — build against a matching OBS SDK instead.

### Build

```bash
cmake -S . -B build -G Ninja
cmake --build build -j
```

### Immutable / atomic distributions

On **Bazzite, Fedora Silverblue/Kinoite, Fedora Atomic**, and similar image-based systems, use a mutable development container such as **Distrobox** or **Toolbox** rather than layering development packages onto the host.

A Fedora-based container is convenient, since `obs-studio-devel` provides the required headers and CMake files. Install the dependencies from the table above inside the container, then build from a directory shared with the host. The resulting `obs-virtual-mic.so` can be installed into the host's OBS plugin directory — Flatpak path for Flatpak OBS, native path otherwise.

### Installing a manually built plugin

The build produces `build/obs-virtual-mic.so`. Use the included installer, which checks for Flatpak OBS first and falls back to the native user plugin directory:

```bash
./scripts/install-user.sh
```

Or install it by hand:

```bash
# Flatpak OBS
mkdir -p ~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/obs-virtual-mic/bin/64bit
cp build/obs-virtual-mic.so \
  ~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/obs-virtual-mic/bin/64bit/

# Native OBS
mkdir -p ~/.config/obs-studio/plugins/obs-virtual-mic/bin/64bit
cp build/obs-virtual-mic.so \
  ~/.config/obs-studio/plugins/obs-virtual-mic/bin/64bit/
```

Restart OBS afterwards.

---

## Troubleshooting

### The plugin does not appear in OBS

Check the current OBS log for lines beginning with `[OBS Virtual Mic]`. Confirm the `.so` is installed in the plugin directory belonging to the OBS build you actually run — Flatpak and native OBS use different user directories.

### CMake cannot find `libobs`

If CMake reports a missing `libobsConfig.cmake`, the OBS development files are absent or outside CMake's search path. Install the appropriate development package, delete the `build` directory, and configure again.

### OBS Virtual Mic does not appear as an input

Verify that `pactl` can see it:

```bash
pactl list short sources | grep -i obs
```

A running Virtual Mic should show an `OBS_VirtualMic` source.

### OBS Virtual Mic appears as an output device

Current beta versions create an input-only source. If you still see an output/sink, an older alpha build is probably still installed:

```bash
pactl list short sinks | grep -i obs
```

A current beta build should not create an OBS Virtual Mic sink.

### The microphone exists but has no audio

Make sure at least one source is selected in the Virtual Mic settings gear, then check the OBS log for diagnostics. Healthy output shows increasing callback and frame counts, non-zero `written` bytes, and a non-zero `peak` while audio is playing:

```text
callbacks=...
frames=...
written=...
peak=...
```

### Flatpak OBS creates the mic, but it is silent

Current beta builds use a host-visible FIFO so that both Flatpak OBS and the host PipeWire/PulseAudio service can reach the same pipe. Older alpha builds used `/run/user/...` and could create the microphone while failing to move audio across the Flatpak namespace. Make sure you are on Beta 0.3.3 or newer.

### Ninja repeatedly regenerates `build.ninja`

If the project was extracted from an archive with timestamps newer than your system clock, Ninja may rerun CMake repeatedly and eventually report `build.ninja` as still dirty. Normalize the timestamps, then rebuild:

```bash
find . -type f -not -path './build/*' -exec touch {} +
rm -rf build
cmake -S . -B build -G Ninja
cmake --build build -j
```

---

## Current limitations

- Linux only.
- Beta 0.3.x temporarily reserves **OBS Track 6** while Virtual Mic is active.
- The Controls-dock row is inserted through OBS's Qt widget hierarchy, because OBS provides no public frontend API for adding buttons inside the built-in Controls dock. A future OBS UI update may require the placement logic to be adjusted.
- The virtual-device backend uses PipeWire's PulseAudio compatibility layer and `pactl` rather than creating a native PipeWire node.
- Source selection is per-source only; there is no independent gain/pan mixer yet.

## Windows support

Windows support is **planned** but not yet available. Windows uses a different audio-device architecture, so it will require a separate virtual audio backend rather than the current PipeWire/PulseAudio implementation. Progress will be posted through future releases and project updates.

## License

See [`LICENSE`](LICENSE).
