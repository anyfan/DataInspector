#include "dataexportworker.h"
#include <QSaveFile>

void DataExportWorker::exportPng(const QString &path, const QImage &image)
{
    QSaveFile file(path);
    QString error;
    if (!m_cancelled.load(std::memory_order_relaxed)) {
        if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG"))
            error = file.errorString().isEmpty() ? QStringLiteral("PNG 编码失败") : file.errorString();
        else if (!m_cancelled.load(std::memory_order_relaxed) && !file.commit()) error = file.errorString();
    }
    emit finished(path, error, m_cancelled.load(std::memory_order_relaxed));
}

void DataExportWorker::resetCancellation()
{
    m_cancelled.store(false, std::memory_order_relaxed);
}

void DataExportWorker::requestCancel()
{
    m_cancelled.store(true, std::memory_order_relaxed);
}

void DataExportWorker::exportWorkbook(
    const QString &path, const QVector<XlsxExportTable> &tables,
    bool zipCompressionEnabled)
{
    XlsxWriteOptions options;
    options.zipCompressionEnabled = zipCompressionEnabled;
    const XlsxWriteResult result = writeXlsxWorkbook(
        path, tables, options,
        [this](int percentage) { emit progress(percentage); },
        [this]() { return m_cancelled.load(std::memory_order_relaxed); });
    emit finished(path, result.error, result.cancelled);
}

void DataExportWorker::exportMat(const QString &path,
                                 const QVector<DataExportTable> &tables)
{
    const MatWriteResult result = writeMatFile(
        path, tables,
        [this](int percentage) { emit progress(percentage); },
        [this]() { return m_cancelled.load(std::memory_order_relaxed); });
    emit finished(path, result.error, result.cancelled);
}
