#include "dataexportworker.h"

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
