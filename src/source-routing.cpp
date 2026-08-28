#include "source-routing.hpp"

extern "C" {
#include <obs-module.h>
}

#include <QSet>
#include <algorithm>
#include <vector>

#define VMLOG(level, fmt, ...) blog(level, "[OBS Virtual Mic] " fmt, ##__VA_ARGS__)

namespace {
struct EnumContext {
    std::vector<obs_source_t *> sources;
};

bool collectAudioSource(void *param, obs_source_t *source)
{
    if (!source)
        return true;

    const uint32_t flags = obs_source_get_output_flags(source);
    if ((flags & OBS_SOURCE_AUDIO) == 0)
        return true;

    // Only expose user-facing input sources. Scenes/groups/composites can
    // otherwise duplicate audio already represented by their child sources.
    if (obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT)
        return true;

    auto *ctx = static_cast<EnumContext *>(param);
    ctx->sources.push_back(source);
    return true;
}
}

QStringList SourceRouting::availableAudioSources()
{
    EnumContext ctx;
    obs_enum_sources(collectAudioSource, &ctx);

    QStringList names;
    for (auto *source : ctx.sources) {
        const char *name = obs_source_get_name(source);
        if (name && *name)
            names.push_back(QString::fromUtf8(name));
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QStringList SourceRouting::defaultSourcesFromTrack1()
{
    EnumContext ctx;
    obs_enum_sources(collectAudioSource, &ctx);

    QStringList names;
    for (auto *source : ctx.sources) {
        if ((obs_source_get_audio_mixers(source) & 0x1u) == 0)
            continue;
        const char *name = obs_source_get_name(source);
        if (name && *name)
            names.push_back(QString::fromUtf8(name));
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool SourceRouting::apply(const QStringList &selectedNames, std::string &error)
{
    if (active_)
        restore();

    QSet<QString> selected;
    for (const QString &name : selectedNames)
        selected.insert(name);
    EnumContext ctx;
    obs_enum_sources(collectAudioSource, &ctx);

    if (ctx.sources.empty()) {
        error = "No OBS audio sources are currently available.";
        return false;
    }

    size_t selectedCount = 0;
    for (auto *source : ctx.sources) {
        const char *name = obs_source_get_name(source);
        if (!name || !*name)
            continue;

        const uint32_t oldMixers = obs_source_get_audio_mixers(source);
        saved_.emplace(name, SavedRoute{oldMixers});

        uint32_t newMixers = oldMixers & ~kVirtualMicMixBit;
        if (selected.contains(QString::fromUtf8(name))) {
            newMixers |= kVirtualMicMixBit;
            ++selectedCount;
        }
        if (newMixers != oldMixers)
            obs_source_set_audio_mixers(source, newMixers);
    }

    if (selectedCount == 0) {
        restore();
        error = "No selected virtual-mic sources are currently present in OBS.";
        return false;
    }

    active_ = true;
    VMLOG(LOG_INFO, "Virtual mic source routing applied to OBS mix 6 (%zu selected sources)", selectedCount);
    return true;
}

void SourceRouting::restore()
{
    for (auto &[name, route] : saved_) {
        obs_source_t *source = obs_get_source_by_name(name.c_str());
        if (source) {
            obs_source_set_audio_mixers(source, route.mixers);
            obs_source_release(source);
        }
    }
    saved_.clear();
    if (active_)
        VMLOG(LOG_INFO, "Restored original OBS mix 6 source routing");
    active_ = false;
}
