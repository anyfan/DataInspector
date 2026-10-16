#pragma once

#include "loadedtable.h"
#include <QObject>
#include <atomic>
#include <QThread>

// Lives on the loader thread; results own their buffers across queued delivery.
class DataLoadWorker final : public QObject
{
    Q_OBJECT
public:
    void requestCancel() { m_cancelled.store(true); }
    void resetCancellation() { m_cancelled.store(false); }
    bool isCancelled() const { return m_cancelled.load() || QThread::currentThread()->isInterruptionRequested(); }
public slots:
    void loadFile(const QString &path);
signals:
    void progress(const QString &path, int percentage);
    void finished(const QString &path, const QVector<LoadedTable> &tables,
                  int skipped, const QString &error);
private:
    std::atomic_bool m_cancelled{false};
    void finishTables(const QString &path, QVector<LoadedTable> tables, int skipped);
    void loadCsv(const QString &path);
    void loadXlsx(const QString &path);
#ifdef ENABLE_MAT
    void loadMat(const QString &path);
#endif
};
