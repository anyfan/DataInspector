#include "dataexportworker.h"
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>

void DataExportWorker::exportLogicalPackage(const QString &directory, const QString &format,
    const QVector<DataExportTable> &tables, const QByteArray &manifest)
{
    QString error; bool cancelled = false;
    const QFileInfo target(directory);
    if (!target.isAbsolute() || target.exists() || !target.dir().exists()) {
        emit finished(directory, QStringLiteral("导出目录必须为尚不存在的绝对路径，父目录须存在"), false); return;
    }
    QTemporaryDir staging(target.dir().filePath(QStringLiteral(".di-export-XXXXXX")));
    if (!staging.isValid()) { emit finished(directory, QStringLiteral("无法创建临时导出目录"), false); return; }
    const QString path = staging.filePath("data." + format);
    const auto progress = [this](int value) { emit this->progress(value); };
    const auto cancel = [this] { return m_cancelled.load(); };
    if (format == "mat") { const auto r = writeMatFile(path, tables, progress, cancel); error = r.error; cancelled = r.cancelled; }
    else { const auto r = writeXlsxWorkbook(path, tables, {}, progress, cancel); error = r.error; cancelled = r.cancelled; }
    if (error.isEmpty() && !cancelled) {
        QSaveFile file(staging.filePath("manifest.json"));
        if (!file.open(QIODevice::WriteOnly) || file.write(manifest) != manifest.size() || !file.commit()) error = file.errorString();
        if (cancel()) cancelled = true;
        else if (error.isEmpty()) {
            if (!QDir().rename(staging.path(), directory)) error = QStringLiteral("发布导出目录失败，目标可能已经存在");
            else staging.setAutoRemove(false);
        }
    }
    emit finished(directory, error, cancelled);
}

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
