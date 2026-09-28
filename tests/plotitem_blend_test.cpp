// GPU check for PlotItem::blendMode: two overlapping horizontal lines drawn
// with Darken must produce min(red, blue) = near-black in the overlap, while
// Opaque keeps the colour of the last drawn series.
#include "plotitem.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTimer>
#include <QImage>
#include <QDebug>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (QQuickWindow::graphicsApi() == QSGRendererInterface::Software) {
        qInfo() << "GPU blend test requires a hardware scene graph backend";
        return 77;
    }
    QQuickWindow window;
    window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    window.setOpacity(0);
    window.resize(320, 160);
    window.setColor(Qt::white);
    auto store = std::make_shared<PlotSeriesStore>();
    const QVector<double> times{0.0, 1.0};
    store->replaceSeries({{1, times, {0.5, 0.5}, QColor(255, 0, 0), 6},
                          {2, times, {0.5, 0.5}, QColor(0, 0, 255), 6}});
    PlotItem opaque(window.contentItem()), darken(window.contentItem());
    for (auto *plot : {&opaque, &darken}) {
        plot->setWidth(128); plot->setHeight(128); plot->setY(16);
        plot->setSeriesStore(store);
        plot->setVisibleSeries({1, 2});
        plot->setXRange(0, 1); plot->setYRange(0, 1);
    }
    opaque.setX(16); darken.setX(176);
    opaque.setBlendMode(PlotItem::OpaqueBlend);
    darken.setBlendMode(PlotItem::DarkenBlend);
    window.show();
    int attempts = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (opaque.lodPending() || darken.lodPending()) {
            if (++attempts > 10) { qCritical() << "LOD timeout"; app.exit(1); }
            return;
        }
        const QImage image = window.grabWindow().convertToFormat(QImage::Format_RGB32);
        if (image.isNull()) { qCritical() << "No raster capture"; app.exit(2); return; }
        image.save("plotitem-blend.png");
        const double ratio = image.width() / 320.0;
        const QRgb opaquePixel = image.pixel(qRound((16 + 64) * ratio), qRound((16 + 64) * ratio));
        const QRgb darkenPixel = image.pixel(qRound((176 + 64) * ratio), qRound((16 + 64) * ratio));
        qInfo() << "opaque overlap" << QColor(opaquePixel).name()
                << "darken overlap" << QColor(darkenPixel).name();
        // Opaque: blue (drawn last) wins. Darken: min(red, blue) -> black.
        const bool opaqueOk = qBlue(opaquePixel) > 200 && qRed(opaquePixel) < 60;
        const bool darkenOk = qRed(darkenPixel) < 60 && qGreen(darkenPixel) < 60 && qBlue(darkenPixel) < 60;
        app.exit(opaqueOk && darkenOk ? 0 : 1);
    });
    timer.start(1000);
    return app.exec();
}
