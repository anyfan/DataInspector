#pragma once

#include "loadedtable.h"
#include <QObject>

// Lives on the loader thread; results own their buffers across queued delivery.
class DataLoadWorker final : public QObject
{
    Q_OBJECT
public slots:
    void loadFile(const QString &path);
signals:
    void progress(const QString &path, int percentage);
    void finished(const QString &path, const QVector<LoadedTable> &tables,
                  int skipped, const QString &error);
private:
    void finishTables(const QString &path, QVector<LoadedTable> tables, int skipped);
    void loadCsv(const QString &path);
    void loadXlsx(const QString &path);
#ifdef ENABLE_MAT
    void loadMat(const QString &path);
#endif
};
