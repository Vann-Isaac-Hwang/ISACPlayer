#include "LibraryScanner.h"

#include "LibraryRepository.h"
#include "TrackMetadata.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

namespace isac::library {

LibraryScanner::LibraryScanner(QString databasePath, QObject* parent)
    : QObject(parent)
    , m_databasePath(std::move(databasePath))
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
    auto* thread = QThread::create(
        [guardedThis, databasePath, paths = std::move(paths),
         directoryPath = std::move(directoryPath)]() mutable {
            QString error;
            bool cancelled = false;
            int visited = 0;
            int committed = 0;
            LibraryRepository repository(databasePath);

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
                    if (existing &&
                        existing->fileSize == fileInfo.size() &&
                        existing->modifiedAt == fileInfo.lastModified()) {
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

                    batch.push_back(MetadataReader::readTrack(filePath));
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
                        QStringList{"*.mp3", "*.flac", "*.wav"},
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
