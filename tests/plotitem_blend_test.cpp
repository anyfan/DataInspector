// GPU regression for binding order, amplitude ordering, selection and alpha.
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
    window.resize(480, 160);
    window.setColor(Qt::white);
    auto store = std::make_shared<PlotSeriesStore>();
    const QVector<double> times{0.0, 1.0};
    store->replaceSeries({{1, times, {0.5, 0.5}, QColor(255, 0, 0), 6},
                          {2, times, {0.5, 0.5}, QColor(0, 0, 255), 6}});
    PlotItem opaque(window.contentItem()), translucent(window.contentItem());
    PlotItem alternating(window.contentItem());
    for (auto *plot : {&opaque, &translucent, &alternating}) {
        plot->setWidth(128); plot->setHeight(128); plot->setY(16);
        plot->setSeriesStore(store);
        plot->setVisibleSeries({1, 2});
        plot->setXRange(0, 1); plot->setYRange(0, 1);
    }
    opaque.setX(16); translucent.setX(176);
    alternating.setX(336);
    alternating.setBlendMode(PlotItem::AmplitudeLayers);
    auto amplitudeStore = std::make_shared<PlotSeriesStore>();
    amplitudeStore->replaceSeries({
        {1, {0.0, 1.0}, {0.5, 0.5}, QColor(255, 0, 0), 6},
        {2, {0.0, 0.1, 0.9, 1.0}, {-0.9, 0.5, 0.5, 0.9}, QColor(0, 0, 255), 6}});
    alternating.setSeriesStore(amplitudeStore);
    opaque.setBlendMode(PlotItem::OpaqueBlend);
    translucent.setBlendMode(PlotItem::OpaqueBlend);
    auto alphaStore = std::make_shared<PlotSeriesStore>();
    alphaStore->replaceSeries({{1, times, {0.5, 0.5}, QColor(255, 0, 0, 128), 6},
                               {2, times, {0.5, 0.5}, QColor(0, 0, 255, 128), 6}});
    translucent.setSeriesStore(alphaStore);
    window.show();
    int attempts = 0;
    int stage = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (opaque.lodPending() || translucent.lodPending() || alternating.lodPending()) {
            if (++attempts > 10) { qCritical() << "LOD timeout"; app.exit(1); }
            return;
        }
        const QImage image = window.grabWindow().convertToFormat(QImage::Format_RGB32);
        if (image.isNull()) { qCritical() << "No raster capture"; app.exit(2); return; }
        image.save("plotitem-blend.png");
        const double ratio = image.width() / 480.0;
        if (stage > 0) {
            const auto pixel = image.pixel(qRound(400 * ratio), qRound(80 * ratio));
            if (!(qBlue(pixel) > 200 && qRed(pixel) < 60)) { app.exit(1); return; }
            if (stage == 1) {
                // Remove the large excursions from the view: equal amplitudes
                // return to their original order, placing blue last.
                alternating.setHighlightedSeries(-1);
                alternating.setXRange(0.2, 0.8);
                stage = 2;
                return;
            }
            app.exit(0);
            return;
        }
        const QRgb opaquePixel = image.pixel(qRound((16 + 64) * ratio), qRound((16 + 64) * ratio));
        const QRgb translucentPixel = image.pixel(qRound((176 + 64) * ratio), qRound((16 + 64) * ratio));
        qInfo() << "opaque overlap" << QColor(opaquePixel).name()
                << "translucent overlap" << QColor(translucentPixel).name();
        // Standard source-over alpha: red then blue over a white background.
        const bool opaqueOk = qBlue(opaquePixel) > 200 && qRed(opaquePixel) < 60;
        const bool translucentOk = qAbs(qRed(translucentPixel) - 127) <= 3
            && qAbs(qGreen(translucentPixel) - 63) <= 3
            && qAbs(qBlue(translucentPixel) - 191) <= 3;
        bool smallAmplitudeInFront = true;
        for (int x : {10, 32, 54, 76, 98, 118}) {
            const auto pixel = image.pixel(qRound((336 + x) * ratio), qRound(80 * ratio));
            const bool red = qRed(pixel) > 200 && qBlue(pixel) < 60;
            smallAmplitudeInFront &= red;
        }
        if (!(opaqueOk && translucentOk && smallAmplitudeInFront)) { app.exit(1); return; }
        alternating.setHighlightedSeries(2);
        stage = 1;
    });
    timer.start(1000);
    return app.exec();
}
