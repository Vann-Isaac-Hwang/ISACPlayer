#include "LibraryScanner.h"

#include "LibraryRepository.h"
#include "TrackMetadata.h"
#include "media/formats/FormatCache.h"
#include "media/formats/FormatPluginManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

namespace isac::library {

LibraryScanner::LibraryScanner(QString databasePath,
                               QString formatPluginDirectory,
                               QObject* parent)
    : QObject(parent)
    , m_databasePath(std::move(databasePath))
    , m_formatPluginDirectory(std::move(formatPluginDirectory))
{
}

LibraryScanner::~LibraryScanner()
{
    m_cancelRequested.store(true, std::memory_order_release);
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
}

bool LibraryScanner::isRunning() const noexcept
{
    return m_thread && m_thread->isRunning();
}

void LibraryScanner::scanDirectory(const QString& path)
{
    const QString absolutePath = QDir(path).absolutePath();
    startScan({}, absolutePath);
}

void LibraryScanner::scanFiles(const QStringList& paths)
{
    QStringList absolutePaths;
    absolutePaths.reserve(paths.size());
    for (const QString& path : paths) {
        absolutePaths.push_back(QFileInfo(path).absoluteFilePath());
    }
    startScan(std::move(absolutePaths), {});
}

void LibraryScanner::cancel()
{
    m_cancelRequested.store(true, std::memory_order_release);
}

void LibraryScanner::startScan(QStringList paths, QString directoryPath)
{
    if (isRunning()) {
        return;
    }
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }

    m_cancelRequested.store(false, std::memory_order_release);
    QPointer<LibraryScanner> guardedThis(this);
    const QString databasePath = m_databasePath;
    const QString formatPluginDirectory = m_formatPluginDirectory;
    auto* thread = QThread::create(
        [guardedThis, databasePath, paths = std::move(paths),
         directoryPath = std::move(directoryPath),
         formatPluginDirectory]() mutable {
            QString error;
            bool cancelled = false;
            int visited = 0;
            int committed = 0;
            LibraryRepository repository(databasePath);
            media::FormatPluginManager formatPlugins(formatPluginDirectory);

            if (!repository.isOpen()) {
                error = repository.lastError();
            } else {
                if (!directoryPath.isEmpty() &&
                    !repository.addDirectory(directoryPath)) {
                    error = repository.lastError();
                }

                std::vector<TrackInfo> batch;
                batch.reserve(64);
                auto processFile = [&](const QString& filePath) {
                    if (guardedThis.isNull() ||
                        guardedThis->m_cancelRequested.load(
                            std::memory_order_acquire)) {
                        cancelled = true;
                        return;
                    }

                    ++visited;
                    const QFileInfo fileInfo(filePath);
                    const auto existing = repository.trackByPath(filePath);
                    const bool isNcm =
                        fileInfo.suffix().compare(
                            QStringLiteral("ncm"),
                            Qt::CaseInsensitive) == 0;
                    if (existing &&
                        existing->fileSize == fileInfo.size() &&
                        existing->modifiedAt == fileInfo.lastModified() &&
                        (!isNcm || (existing->trackNumber > 0 &&
                                    !existing->coverPath.isEmpty()))) {
                        if ((visited % 64) == 0 && !guardedThis.isNull()) {
                            QMetaObject::invokeMethod(
                                guardedThis,
                                [guardedThis, visited] {
                                    if (guardedThis) {
                                        emit guardedThis->progressChanged(visited);
                                    }
                                },
                                Qt::QueuedConnection);
                        }
                        return;
                    }

                    TrackInfo track;
                    const auto pluginMetadata = formatPlugins.inspect(filePath);
                    if (pluginMetadata.valid) {
                        track.filePath = filePath;
                        track.title = pluginMetadata.title.isEmpty()
                            ? QFileInfo(filePath).completeBaseName()
                            : pluginMetadata.title;
                        track.artist = pluginMetadata.artist.isEmpty()
                            ? QStringLiteral("Unknown Artist")
                            : pluginMetadata.artist;
                        track.album = pluginMetadata.album.isEmpty()
                            ? QStringLiteral("Unknown Album")
                            : pluginMetadata.album;
                        track.formatPlugin =
                            formatPlugins.pluginIdForFile(filePath);
                        track.fileSize = fileInfo.size();
                        track.modifiedAt = fileInfo.lastModified();
                        track.durationSeconds =
                            std::max(0, pluginMetadata.durationMilliseconds / 1000);
                        track.trackNumber = std::max(0, pluginMetadata.trackNumber);
                        track.discNumber = std::max(0, pluginMetadata.discNumber);
                        track.year = std::max(0, pluginMetadata.year);
                        const QString coverPath = media::FormatCache::coverPath(
                            filePath, track.formatPlugin,
                            pluginMetadata.coverData);
                        if (!coverPath.isEmpty() &&
                            !QFileInfo::exists(coverPath) &&
                            !media::FormatCache::storeBytes(
                                coverPath, pluginMetadata.coverData,
                                &error)) {
                            return;
                        }
                        track.coverPath = coverPath;
                    } else {
                        track = MetadataReader::readTrack(filePath);
                    }
                    batch.push_back(std::move(track));
                    if (batch.size() == 64) {
                        if (!repository.upsertTracks(batch)) {
                            error = repository.lastError();
                            return;
                        }
                        committed += static_cast<int>(batch.size());
                        batch.clear();
                        if (!guardedThis.isNull()) {
                            QMetaObject::invokeMethod(
                                guardedThis,
                                [guardedThis, committed, visited] {
                                    if (guardedThis) {
                                        emit guardedThis->batchCommitted(committed);
                                        emit guardedThis->progressChanged(visited);
                                    }
                                },
                                Qt::QueuedConnection);
                        }
                    }
                };

                if (!directoryPath.isEmpty()) {
                    QDirIterator iterator(
                        directoryPath,
                        QStringList{"*.mp3", "*.flac", "*.wav", "*.ncm"},
                        QDir::Files,
                        QDirIterator::Subdirectories);
                    while (iterator.hasNext() && !cancelled && error.isEmpty()) {
                        processFile(iterator.next());
                    }
                } else {
                    for (const QString& filePath : paths) {
                        if (cancelled || !error.isEmpty()) {
                            break;
                        }
                        processFile(filePath);
                    }
                }

                if (error.isEmpty() && !batch.empty()) {
                    if (!repository.upsertTracks(batch)) {
                        error = repository.lastError();
                    } else {
                        committed += static_cast<int>(batch.size());
                    }
                }

                if (error.isEmpty() && !cancelled && !directoryPath.isEmpty() &&
                    !repository.markDirectoryScanned(
                        directoryPath, QDateTime::currentDateTimeUtc())) {
                    error = repository.lastError();
                }
            }

            if (!guardedThis.isNull()) {
                QMetaObject::invokeMethod(
                    guardedThis,
                    [guardedThis, cancelled, error, committed, visited] {
                        if (guardedThis) {
                            if (committed > 0) {
                                emit guardedThis->batchCommitted(committed);
                            }
                            emit guardedThis->progressChanged(visited);
                            emit guardedThis->scanFinished(cancelled, error);
                        }
                    },
                    Qt::QueuedConnection);
            }
        });
    m_thread = thread;
    thread->start();
}

} // namespace isac::library
