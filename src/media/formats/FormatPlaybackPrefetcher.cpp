#include "FormatPlaybackPrefetcher.h"

#include "FormatCache.h"
#include "FormatPluginManager.h"

#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>
#include <utility>

namespace isac::media {
namespace {

Q_LOGGING_CATEGORY(formatPrefetchLog, "isac.format_prefetch")

} // namespace

FormatPlaybackPrefetcher::FormatPlaybackPrefetcher(QString pluginDirectory)
    : m_pluginDirectory(std::move(pluginDirectory))
    , m_worker(&FormatPlaybackPrefetcher::workerLoop, this)
{
}

FormatPlaybackPrefetcher::~FormatPlaybackPrefetcher()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
        m_pending.clear();
    }
    m_wake.notify_one();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void FormatPlaybackPrefetcher::updateUpcomingTracks(
    const std::vector<TrackRequest>& tracks)
{
    {
        std::lock_guard lock(m_mutex);
        m_pending.clear();
        for (const auto& track : tracks) {
            if (track.sourcePath.isEmpty() || track.pluginId.isEmpty() ||
                (track.sourcePath == m_inFlightPath &&
                 track.pluginId == m_inFlightPluginId)) {
                continue;
            }
            const auto duplicate = std::find_if(
                m_pending.begin(), m_pending.end(),
                [&track](const TrackRequest& pending) {
                    return pending.sourcePath == track.sourcePath &&
                           pending.pluginId == track.pluginId;
                });
            if (duplicate == m_pending.end()) {
                m_pending.push_back(track);
            }
        }
    }
    m_wake.notify_one();
}

void FormatPlaybackPrefetcher::workerLoop()
{
    FormatPluginManager plugins(m_pluginDirectory);

    while (true) {
        TrackRequest track;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] {
                return m_stopping || !m_pending.empty();
            });
            if (m_stopping) {
                return;
            }
            track = std::move(m_pending.front());
            m_pending.pop_front();
            m_inFlightPath = track.sourcePath;
            m_inFlightPluginId = track.pluginId;
        }

        prefetch(plugins, track);

        {
            std::lock_guard lock(m_mutex);
            m_inFlightPath.clear();
            m_inFlightPluginId.clear();
        }
    }
}

void FormatPlaybackPrefetcher::prefetch(
    FormatPluginManager& plugins, const TrackRequest& track)
{
    if (!QFileInfo::exists(track.sourcePath)) {
        return;
    }
    if (plugins.pluginIdForFile(track.sourcePath) != track.pluginId) {
        return;
    }

    const auto metadata = plugins.inspect(track.sourcePath);
    if (!metadata.valid) {
        return;
    }
    const QString extension = metadata.decodedFormat.isEmpty()
        ? QStringLiteral("flac")
        : metadata.decodedFormat;
    const QString outputPath = FormatCache::playbackPath(
        track.sourcePath, track.pluginId, extension);
    if (QFileInfo::exists(outputPath)) {
        return;
    }

    QString error;
    if (!plugins.materialize(track.sourcePath, outputPath, &error)) {
        qCWarning(formatPrefetchLog)
            << "Unable to pre-cache format playback:"
            << track.sourcePath << error;
    }
}

} // namespace isac::media
