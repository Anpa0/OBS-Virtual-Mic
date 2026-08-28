#pragma once

#include <QStringList>
#include <cstdint>
#include <string>
#include <unordered_map>

extern "C" {
#include <obs.h>
}

class SourceRouting {
public:
    static constexpr size_t kVirtualMicMixIndex = 5; // OBS Track/Mix 6
    static constexpr uint32_t kVirtualMicMixBit = (1u << kVirtualMicMixIndex);

    bool apply(const QStringList &selectedNames, std::string &error);
    void restore();
    bool active() const { return active_; }

    static QStringList availableAudioSources();
    static QStringList defaultSourcesFromTrack1();

private:
    struct SavedRoute {
        uint32_t mixers = 0;
    };

    std::unordered_map<std::string, SavedRoute> saved_;
    bool active_ = false;
};
