#include "plotitem.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTimer>
#include <QImage>
#include <QSurfaceFormat>
#include <QDebug>
#include <QPainter>
#include <QFile>
#include <QTextStream>

int main(int argc, char **argv)
{
    QSurfaceFormat format; format.setSamples(4); QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    const bool dense = app.arguments().contains(QStringLiteral("--dense"));
    const int csvIndex = app.arguments().indexOf(QStringLiteral("--csv"));
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
    const int count = dense ? 20000 : 25;
    for (int i=0; i<count; ++i) {
        times.append(.1 + .8*i/(count - 1.0));
        rising.append(dense ? ((i % 37) < 18 ? .1 : .9) : (i%2 ? .9 : .1));
        falling.append(1-rising.last());
    }
    if (dense) {
        times.clear(); rising.clear(); falling.clear();
        if (csvIndex >= 0 && csvIndex + 1 < app.arguments().size()) {
            QFile file(app.arguments()[csvIndex + 1]);
            if (!file.open(QIODevice::ReadOnly)) return 4;
            QTextStream stream(&file);
            stream.readLine();
            while (!stream.atEnd()) {
                const auto fields = stream.readLine().split(',');
                if (fields.size() != 2) return 4;
                times.append(fields[0].toDouble()); rising.append(fields[1].toDouble());
            }
        } else {
            for (int i = 0; i < 247511; ++i) {
                times.append(87.065 + i * .025);
                rising.append(20 + (i % 40) / 2 * 50 + (i % 2) * 20);
            }
        }
        falling = rising;
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
    if (dense) {
        window.resize(1920, 1000);
        first.setX(40); first.setY(40); first.setWidth(1840); first.setHeight(900);
        first.setXRange(0, 6400); first.setYRange(-40, 1050);
        second.setVisible(false);
    }
    window.show();
    int attempts = 0;
    int densePhase = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (first.lodPending() || second.lodPending()) {
            if (++attempts > 10) { qCritical() << "LOD timeout"; app.exit(1); }
            return;
        }
        const QImage image = window.grabWindow().convertToFormat(QImage::Format_RGB32);
        if (image.isNull()) { qCritical() << "No raster capture"; app.exit(2); return; }
        image.save("plotitem-raster.png");
        if (dense) {
            const double scale = image.width() / 1920.0;
            int minTop = image.height(), maxTop = 0, minBottom = image.height(), maxBottom = 0;
            for (int x = qCeil((40 + 3300.0 / 6400 * 1840) * scale);
                 x < qFloor((40 + 3900.0 / 6400 * 1840) * scale); ++x) {
                int top = -1, bottom = -1;
                for (int row = qCeil(40 * scale); row < qFloor(940 * scale); ++row) {
                    if (qRed(image.pixel(x, row)) < 223) {
                        if (top < 0) top = row;
                        bottom = row;
                    }
                }
                if (top < 0) { app.exit(3); return; }
                minTop = qMin(minTop, top); maxTop = qMax(maxTop, top);
                minBottom = qMin(minBottom, bottom); maxBottom = qMax(maxBottom, bottom);
            }
            qInfo() << "dense constant-envelope edge variation:" << maxTop - minTop << maxBottom - minBottom;
            if (maxTop != minTop || maxBottom != minBottom) { app.exit(1); return; }
            if (++densePhase < 5) {
                first.setYRange(-40 + densePhase * .3, 1050 + densePhase * .3);
                return;
            }
            app.exit(0);
            return;
        }
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
