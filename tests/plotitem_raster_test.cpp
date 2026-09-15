#include "plotitem.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTimer>
#include <QImage>
#include <QSurfaceFormat>
#include <QDebug>
#include <QPainter>

int main(int argc, char **argv)
{
    QSurfaceFormat format; format.setSamples(4); QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    if (QQuickWindow::graphicsApi() == QSGRendererInterface::Software) {
        qInfo() << "GPU raster test requires a hardware scene graph backend";
        return 77;
    }
    QQuickWindow window;
    window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    window.setOpacity(0);
    window.resize(320, 160);
    window.setColor(Qt::white);
    auto store = std::make_shared<PlotSeriesStore>();
    QVector<double> times, rising, falling;
    for (int i=0; i<25; ++i) {
        times.append(.1 + .8*i/24.0);
        rising.append(i%2 ? .9 : .1);
        falling.append(1-rising.last());
    }
    store->replaceSeries({{1, times, rising, QColor("black"), 2},
                          {2, times, falling, QColor("black"), 2}});
    PlotItem first(window.contentItem()), second(window.contentItem());
    for (auto *plot : {&first, &second}) {
        plot->setWidth(128); plot->setHeight(128); plot->setY(16);
        plot->setSeriesStore(store);
    }
    first.setX(16); second.setX(176);
    first.setVisibleSeries({1}); second.setVisibleSeries({2});
    first.fitY(); second.fitY();
    first.setXRange(0,1); second.setXRange(0,1);
    window.show();
    int attempts = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (first.lodPending() || second.lodPending()) {
            if (++attempts > 10) { qCritical() << "LOD timeout"; app.exit(1); }
            return;
        }
        const QImage image = window.grabWindow().convertToFormat(QImage::Format_RGB32);
        if (image.isNull()) { qCritical() << "No raster capture"; app.exit(2); return; }
        image.save("plotitem-raster.png");
        double inkA=0, inkB=0, inkReference=0, difference=0;
        const double ratio = image.width()/320.0;
        const int n=qRound(128*ratio), x1=qRound(16*ratio), x2=qRound(176*ratio), y=qRound(16*ratio);
        QImage reference(image.size(), QImage::Format_RGB32);
        reference.fill(Qt::white);
        QPolygonF centerline;
        for (int i=0; i<times.size(); ++i)
            centerline.append({(16+times[i]*128)*ratio,
                               (16+(1-(rising[i]-.06)/.88)*128)*ratio});
        {
            QPainter painter(&reference);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(Qt::black, 2*ratio, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            painter.drawPolyline(centerline);
        }
        for (int row=0; row<n; ++row) for (int col=0; col<n; ++col) {
            const int a=255-qRed(image.pixel(x1+col,y+row));
            const int b=255-qRed(image.pixel(x2+col,y+n-1-row));
            inkReference+=255-qRed(reference.pixel(x1+col,y+row));
            inkA+=a; inkB+=b; difference+=qAbs(a-b);
        }
        qInfo() << "ink /" << inkA << "ink backslash" << inkB << "reference ink" << inkReference << "coverage ratio" << inkA/qMax(inkReference,1.0) << "mirror error" << difference/qMax(inkA,1.0);
        app.exit(inkA > 0 && qAbs(inkA-inkB)/inkA < .03 && inkA/inkReference > .9 && inkA/inkReference < 1.1 ? 0 : 1);
    });
    timer.start(1000);
    return app.exec();
}
