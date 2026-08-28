#include "audio-router.hpp"

extern "C" {
#include <obs-module.h>
}

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#define VMLOG(level, fmt, ...) blog(level, "[OBS Virtual Mic] " fmt, ##__VA_ARGS__)

AudioRouter::AudioRouter() = default;
AudioRouter::~AudioRouter() { stop(); }

bool AudioRouter::start(const std::string &fifoPath, size_t mixIndex, std::string &error)
{
    if (running())
        return true;
    if (mixIndex >= MAX_AUDIO_MIXES) {
        error = "Invalid OBS audio mix index.";
        return false;
    }

    fifoPath_ = fifoPath;
    mixIndex_ = mixIndex;
    ring_.clear();
    callbackCount_ = 0;
    framesReceived_ = 0;
    bytesQueued_ = 0;
    bytesWritten_ = 0;
    droppedBytes_ = 0;
    peakSample_ = 0.0f;
    stopRequested_ = false;

    audio_convert_info conversion{};
    conversion.samples_per_sec = kSampleRate;
    conversion.format = AUDIO_FORMAT_FLOAT;
    conversion.speakers = SPEAKERS_STEREO;
    conversion.allow_clipping = false;

    running_ = true;
    try {
        worker_ = std::thread(&AudioRouter::workerMain, this);
    } catch (const std::exception &e) {
        running_ = false;
        error = std::string("Could not start audio writer thread: ") + e.what();
        return false;
    }

    obs_add_raw_audio_callback(mixIndex_, &conversion, &AudioRouter::rawAudioCallback, this);
    VMLOG(LOG_INFO,
          "Audio router started for OBS mix %zu at 48 kHz stereo float PCM; waiting for an application to open the virtual mic",
          mixIndex_ + 1);
    return true;
}

void AudioRouter::stop()
{
    if (!running_.exchange(false))
        return;

    obs_remove_raw_audio_callback(mixIndex_, &AudioRouter::rawAudioCallback, this);
    stopRequested_ = true;
    wakeCv_.notify_all();
    if (worker_.joinable())
        worker_.join();

    VMLOG(LOG_INFO,
          "Audio router stopped: callbacks=%llu frames=%llu queued=%llu written=%llu dropped=%llu peak=%.4f",
          (unsigned long long)callbackCount_.load(),
          (unsigned long long)framesReceived_.load(),
          (unsigned long long)bytesQueued_.load(),
          (unsigned long long)bytesWritten_.load(),
          (unsigned long long)droppedBytes_.load(),
          peakSample_.load());
    ring_.clear();
}

void AudioRouter::rawAudioCallback(void *param, size_t, audio_data *data)
{
    static_cast<AudioRouter *>(param)->onAudio(data);
}

void AudioRouter::onAudio(audio_data *data)
{
    if (!running_.load(std::memory_order_relaxed) || !data || !data->data[0] || data->frames == 0)
        return;

    callbackCount_.fetch_add(1, std::memory_order_relaxed);
    framesReceived_.fetch_add(data->frames, std::memory_order_relaxed);

    const float *samples = reinterpret_cast<const float *>(data->data[0]);
    const size_t sampleCount = static_cast<size_t>(data->frames) * kChannels;
    float peak = 0.0f;
    for (size_t i = 0; i < sampleCount; ++i)
        peak = std::max(peak, std::fabs(samples[i]));
    float oldPeak = peakSample_.load(std::memory_order_relaxed);
    while (peak > oldPeak && !peakSample_.compare_exchange_weak(oldPeak, peak, std::memory_order_relaxed)) {}

    const size_t bytes = static_cast<size_t>(data->frames) * kBytesPerFrame;
    const size_t written = ring_.write(data->data[0], bytes);
    bytesQueued_.fetch_add(written, std::memory_order_relaxed);
    if (written < bytes)
        droppedBytes_.fetch_add(bytes - written, std::memory_order_relaxed);

    wakeCv_.notify_one();
}

void AudioRouter::workerMain()
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);

    std::vector<uint8_t> chunk(8192);
    size_t pendingOffset = 0;
    size_t pendingBytes = 0;
    int fd = -1;
    bool announcedWaiting = false;
    auto nextStats = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (fd < 0) {
            fd = ::open(fifoPath_.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                if (errno == ENXIO || errno == ENOENT) {
                    if (!announcedWaiting) {
                        VMLOG(LOG_INFO, "Virtual mic source exists; waiting for a client (Discord/Zoom/etc.) to start capturing it");
                        announcedWaiting = true;
                    }
                    std::unique_lock<std::mutex> lock(wakeMutex_);
                    wakeCv_.wait_for(lock, std::chrono::milliseconds(100));
                    if (std::chrono::steady_clock::now() >= nextStats) {
                        logStatsIfDue();
                        nextStats = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                    }
                    continue;
                }
                VMLOG(LOG_ERROR, "open('%s', O_WRONLY) failed: %s", fifoPath_.c_str(), std::strerror(errno));
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                continue;
            }

            announcedWaiting = false;
            VMLOG(LOG_INFO, "Connected audio writer to virtual microphone FIFO");
            // Discard stale audio accumulated while no app was listening.
            ring_.clear();
            pendingBytes = 0;
            pendingOffset = 0;
        }

        if (pendingBytes == 0) {
            pendingBytes = ring_.read(chunk.data(), chunk.size());
            pendingOffset = 0;
            if (pendingBytes == 0) {
                std::unique_lock<std::mutex> lock(wakeMutex_);
                wakeCv_.wait_for(lock, std::chrono::milliseconds(10), [this] {
                    return stopRequested_.load(std::memory_order_acquire) || ring_.size() > 0;
                });
                if (std::chrono::steady_clock::now() >= nextStats) {
                    logStatsIfDue();
                    nextStats = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                }
                continue;
            }
        }

        const ssize_t n = ::write(fd, chunk.data() + pendingOffset, pendingBytes);
        if (n > 0) {
            bytesWritten_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
            pendingOffset += static_cast<size_t>(n);
            pendingBytes -= static_cast<size_t>(n);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{fd, POLLOUT, 0};
            ::poll(&pfd, 1, 10);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            const int savedErrno = errno;
            VMLOG(LOG_INFO, "Virtual mic client disconnected (%s); waiting for the next capture client",
                  n == 0 ? "EOF" : std::strerror(savedErrno));
            ::close(fd);
            fd = -1;
            pendingBytes = 0;
            pendingOffset = 0;
        }

        if (std::chrono::steady_clock::now() >= nextStats) {
            logStatsIfDue();
            nextStats = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
    }

    if (fd >= 0)
        ::close(fd);
}

void AudioRouter::logStatsIfDue()
{
    VMLOG(LOG_INFO,
          "Audio stats: callbacks=%llu frames=%llu queued=%llu written=%llu dropped=%llu peak=%.4f",
          (unsigned long long)callbackCount_.load(std::memory_order_relaxed),
          (unsigned long long)framesReceived_.load(std::memory_order_relaxed),
          (unsigned long long)bytesQueued_.load(std::memory_order_relaxed),
          (unsigned long long)bytesWritten_.load(std::memory_order_relaxed),
          (unsigned long long)droppedBytes_.load(std::memory_order_relaxed),
          peakSample_.load(std::memory_order_relaxed));
}
