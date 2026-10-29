#pragma once

#include <QString>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace isac::media {

class FormatPluginManager;

class FormatPlaybackPrefetcher final {
public:
    struct TrackRequest {
        QString sourcePath;
        QString pluginId;
    };

    explicit FormatPlaybackPrefetcher(QString pluginDirectory);
    ~FormatPlaybackPrefetcher();

    FormatPlaybackPrefetcher(const FormatPlaybackPrefetcher&) = delete;
    FormatPlaybackPrefetcher& operator=(const FormatPlaybackPrefetcher&) = delete;

    void updateUpcomingTracks(const std::vector<TrackRequest>& tracks);

private:
    void workerLoop();
    void prefetch(FormatPluginManager& plugins, const TrackRequest& track);

    QString m_pluginDirectory;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<TrackRequest> m_pending;
    QString m_inFlightPath;
    QString m_inFlightPluginId;
    bool m_stopping = false;
    std::thread m_worker;
};

} // namespace isac::media
