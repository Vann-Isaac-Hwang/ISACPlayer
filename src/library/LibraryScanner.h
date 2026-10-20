#pragma once

#include "LibraryTypes.h"

#include <QObject>
#include <QThread>
#include <atomic>

namespace isac::library {

class LibraryScanner final : public QObject {
    Q_OBJECT
public:
    explicit LibraryScanner(QString databasePath, QObject* parent = nullptr);
    ~LibraryScanner() override;

    bool isRunning() const noexcept;
    void scanDirectory(const QString& path);
    void scanFiles(const QStringList& paths);
    void cancel();

signals:
    void batchCommitted(int insertedOrUpdated);
    void progressChanged(int filesVisited);
    void scanFinished(bool cancelled, const QString& error);

private:
    void startScan(QStringList paths, QString directoryPath);

    QString m_databasePath;
    QThread* m_thread = nullptr;
    std::atomic<bool> m_cancelRequested{false};
};

} // namespace isac::library
