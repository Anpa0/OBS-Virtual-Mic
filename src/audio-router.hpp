#pragma once

#include "spsc-byte-ring.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <obs.h>
}

class AudioRouter {
public:
    AudioRouter();
    ~AudioRouter();

    bool start(const std::string &fifoPath, size_t mixIndex, std::string &error);
    void stop();
    bool running() const { return running_.load(std::memory_order_acquire); }

private:
    static void rawAudioCallback(void *param, size_t mixIdx, audio_data *data);
    void onAudio(audio_data *data);
    void workerMain();
    void logStatsIfDue();

    static constexpr uint32_t kSampleRate = 48000;
    static constexpr uint32_t kChannels = 2;
    static constexpr size_t kBytesPerFrame = sizeof(float) * kChannels;
    static constexpr size_t kRingBytes = kSampleRate * kBytesPerFrame * 2; // ~2 seconds

    std::string fifoPath_;
    size_t mixIndex_ = 0;
    SpscByteRing ring_{kRingBytes};

    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<uint64_t> callbackCount_{0};
    std::atomic<uint64_t> framesReceived_{0};
    std::atomic<uint64_t> bytesQueued_{0};
    std::atomic<uint64_t> bytesWritten_{0};
    std::atomic<uint64_t> droppedBytes_{0};
    std::atomic<float> peakSample_{0.0f};

    std::thread worker_;
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
};
