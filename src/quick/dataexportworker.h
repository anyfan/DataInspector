#pragma once

#include "xlsxwriter.h"

#include <QObject>

#include <atomic>

class DataExportWorker final : public QObject
{
    Q_OBJECT
public:
    void resetCancellation();
    void requestCancel();

public slots:
    void exportWorkbook(const QString &path,
                        const QVector<XlsxExportTable> &tables);

signals:
    void progress(int percentage);
    void finished(const QString &path, const QString &error, bool cancelled);

private:
    std::atomic_bool m_cancelled{false};
};
