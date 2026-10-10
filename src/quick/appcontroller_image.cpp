#include "appcontroller.h"
#include "plotitem.h"
#include "trajectoryitem.h"
#include "dataexportworker.h"
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QtMath>

bool AppController::exportPlotImage(QObject *object, const QVariant &filePath, int scale)
{
    auto *item = qobject_cast<QQuickItem *>(object);
    const QUrl url = filePath.toUrl();
    QString path = url.isLocalFile() ? url.toLocalFile() : filePath.toString();
    if (m_loading || m_exporting || m_restoringSession || deriving() || !m_exporter || !item || !item->window()
        || !item->isVisible() || item->width() <= 0 || item->height() <= 0 || path.isEmpty()
        || scale < 1 || scale > 4) return false;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += QStringLiteral(".png");
    const QSize size(qCeil(item->width() * scale), qCeil(item->height() * scale));
    if (size.width() > 8192 || size.height() > 8192) {
        setStatus(QStringLiteral("图片尺寸超过 8192 像素，请减小窗口或导出倍率")); return false;
    }
    m_imageExporting = true; m_imageExportCancelled = false;
    beginExport(QStringLiteral("PNG"));
    auto *timer = new QTimer(this);
    timer->setInterval(16);
    const QPointer<QQuickItem> target(item);
    connect(timer, &QTimer::timeout, this, [this, timer, target, path, size, attempts = 0]() mutable {
        bool pending = false;
        for (const auto &plot : m_plots) if (plot && plot->lodPending()) pending = true;
        for (const auto &plot : m_trajectoryPlots) if (plot && plot->isVisible() && plot->pending()) pending = true;
        if (!m_imageExportCancelled && pending && ++attempts < 300) return;
        timer->stop(); timer->deleteLater();
        const auto finish = [this](bool success, const QString &message) {
            m_exporting = false;
            m_imageExporting = false;
            if (success) setExportProgress(100);
            setStatus(message); emit exportingChanged(); emit imageExportFinished(success, message);
        };
        if (m_imageExportCancelled) { finish(false, QStringLiteral("已取消图片导出")); return; }
        if (!target || !target->window() || !target->isVisible() || !target->window()->isVisible() || pending) {
            finish(false, QStringLiteral("图片导出失败：视图已关闭或曲线准备超时")); return;
        }
        // grabToImage's targetSize is logical; the render target also applies the
        // window DPR. Keep the public export size independent of desktop scaling.
        const qreal dpr = target->window()->effectiveDevicePixelRatio();
        const QSize captureSize(qMax(1, qFloor(size.width() / dpr)),
                                qMax(1, qFloor(size.height() / dpr)));
        const auto grab = target->grabToImage(captureSize);
        if (!grab) { finish(false, QStringLiteral("图片导出失败：无法获取绘图区")); return; }
        connect(grab.data(), &QQuickItemGrabResult::ready, this, [this, grab, path, size, finish] {
            if (m_imageExportCancelled) { finish(false, QStringLiteral("已取消图片导出")); return; }
            setExportProgress(50);
            const QImage captured = grab->image();
            QMetaObject::invokeMethod(m_exporter, [exporter = m_exporter, path, size, captured] {
                QImage image = captured.size() == size ? captured
                    : captured.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                image.setDevicePixelRatio(1);
                exporter->exportPng(path, image);
            }, Qt::QueuedConnection);
        }, Qt::SingleShotConnection);
    });
    timer->start();
    return true;
}
