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
    const int csvIndex = app.arguments().indexOf(QStringLiteral("--step-csv"));
    const bool checkStep = csvIndex >= 0;
    const int denseCsvIndex = app.arguments().indexOf(QStringLiteral("--dense-csv"));
    const bool checkDense = denseCsvIndex >= 0 || app.arguments().contains(QStringLiteral("--dense"));
    const bool compressed = app.arguments().contains(QStringLiteral("--compressed"));
    const bool zoomed = app.arguments().contains(QStringLiteral("--zoomed"));
    const bool narrow = app.arguments().contains(QStringLiteral("--narrow"));
    const bool selected = app.arguments().contains(QStringLiteral("--selected"));
    const bool sawtooth = app.arguments().contains(QStringLiteral("--sawtooth"));
    const bool steppedMillis = app.arguments().contains(QStringLiteral("--stepped-millis"));
    const double denseMinimum = narrow ? 2600
        : zoomed ? (denseCsvIndex >= 0 ? 3250 : 1250) : 0;
    const double denseMaximum = narrow ? denseMinimum + 500 : zoomed ? denseMinimum + 1450 : 6400;
    if (checkStep && checkDense) return 4;
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
    if (checkStep || denseCsvIndex >= 0) {
        const int pathIndex = checkStep ? csvIndex : denseCsvIndex;
        if (pathIndex + 1 >= app.arguments().size()) return 4;
        QFile file(app.arguments()[pathIndex + 1]);
        if (!file.open(QIODevice::ReadOnly)) return 4;
        QTextStream stream(&file);
        stream.readLine();
        times.clear(); rising.clear(); falling.clear();
        while (!stream.atEnd()) {
            const auto fields = stream.readLine().split(',');
            if (fields.size() != (checkStep ? 3 : 2)) return 4;
            bool ok[3];
            const double t = fields[0].toDouble(&ok[0]);
            const double a = fields[1].toDouble(&ok[1]);
            const double b = checkStep ? fields[2].toDouble(&ok[2]) : a;
            if (!ok[0] || !ok[1] || (checkStep && !ok[2])) return 4;
            times.append(t); rising.append(a); falling.append(b);
        }
        if (times.size() < 2) return 4;
    } else if (checkDense) {
        times.clear(); rising.clear(); falling.clear();
        for (int i = 0; i < 247511; ++i) {
            times.append(87.065 + i * (steppedMillis ? .04 : .025));
            rising.append(steppedMillis ? 30 + ((i * 40 / 100) % 10) * 100
                          : sawtooth ? 30 + (i % 40) * (900.0 / 39)
                                  : 20 + (i % 40) / 2 * 50 + (i % 2) * 20);
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
    if (checkStep) {
        window.resize(1920, 1000);
        first.setY(20); second.setY(510);
        for (auto *plot : {&first, &second}) {
            plot->setX(30); plot->setWidth(1860); plot->setHeight(460);
            plot->setXRange(290, 1010);
            plot->fitY();
        }
    }
    if (checkDense) {
        window.resize(1920, 1000);
        first.setX(40); first.setY(40); first.setWidth(1840); first.setHeight(900);
        first.setXRange(denseMinimum, denseMaximum);
        first.setYRange(compressed ? -8000 : -40, compressed ? 198000 : 1050);
        second.setVisible(false);
        if (selected) first.setHighlightedSeries(1);
    }
    window.show();
    int attempts = 0;
    int phase = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (first.lodPending() || second.lodPending()) {
            if (++attempts > 10) { qCritical() << "LOD timeout"; app.exit(1); }
            return;
        }
        const QImage image = window.grabWindow().convertToFormat(QImage::Format_RGB32);
        if (image.isNull()) { qCritical() << "No raster capture"; app.exit(2); return; }
        image.save("plotitem-raster.png");
        if (checkDense) {
            const double scale = image.width() / 1920.0;
            int minTop = image.height(), maxTop = 0, minBottom = image.height(), maxBottom = 0;
            const double probeMinimum = narrow ? denseMinimum + 50 : zoomed && denseCsvIndex < 0 ? 1600 : 3300;
            const double probeMaximum = narrow ? denseMaximum - 50 : zoomed && denseCsvIndex < 0 ? 2600 : 3900;
            for (int x = qCeil((40 + (probeMinimum - denseMinimum) / (denseMaximum - denseMinimum) * first.width()) * scale);
                 x < qFloor((40 + (probeMaximum - denseMinimum) / (denseMaximum - denseMinimum) * first.width()) * scale); ++x) {
                int top = -1, bottom = -1;
                for (int y = qCeil(40 * scale); y < qFloor(940 * scale); ++y) {
                    if (qRed(image.pixel(x, y)) < 223) {
                        if (top < 0) top = y;
                        bottom = y;
                    }
                }
                if (top < 0) { app.exit(3); return; }
                minTop = qMin(minTop, top); maxTop = qMax(maxTop, top);
                minBottom = qMin(minBottom, bottom); maxBottom = qMax(maxBottom, bottom);
            }
            qInfo() << "dense phase" << phase << "edge variation" << maxTop - minTop << maxBottom - minBottom;
            if (maxTop != minTop || maxBottom != minBottom) { app.exit(1); return; }
            if (++phase < 10) {
                const double shift = (phase % 5) * (compressed ? 68.0 : .3);
                first.setYRange((compressed ? -8000 : -40) + shift,
                               (compressed ? 198000 : 1050) + shift);
                first.setWidth(phase < 5 ? 1840 : 1200);
                return;
            }
            app.exit(0);
            return;
        }
        if (checkStep) {
            const double scale = image.width() / 1920.0;
            int missing = 0;
            double maximumStepWidth = 0;
            for (int series = 0; series < 2; ++series) {
                const auto &values = series == 0 ? rising : falling;
                const auto &plot = series == 0 ? first : second;
                qsizetype jump = 1;
                for (qsizetype i = 2; i < values.size(); ++i)
                    if (qAbs(values[i] - values[i - 1]) > qAbs(values[jump] - values[jump - 1]))
                        jump = i;
                for (int percent = 10; percent <= 90; ++percent) {
                    const double f = percent / 100.0;
                    const double t = times[jump - 1] * (1 - f) + times[jump] * f;
                    const double value = values[jump - 1] * (1 - f) + values[jump] * f;
                    const int x = qRound((plot.x() + (t - plot.xMinimum())
                        / (plot.xMaximum() - plot.xMinimum()) * plot.width()) * scale);
                    const int y = qRound((plot.y() + (plot.yMaximum() - value)
                        / (plot.yMaximum() - plot.yMinimum()) * plot.height()) * scale);
                    bool ink = false;
                    double inkWidth = 0;
                    for (int dx = -qCeil(3 * scale); dx <= qCeil(3 * scale); ++dx) {
                        if (image.rect().contains(x + dx, y)) {
                            ink |= qRed(image.pixel(x + dx, y)) < 128;
                            inkWidth += (255 - qRed(image.pixel(x + dx, y))) / 255.0;
                        }
                    }
                    maximumStepWidth = qMax(maximumStepWidth, inkWidth);
                    if (!ink) ++missing;
                }
                qInfo() << "step series" << series << "times" << times[jump - 1] << times[jump]
                        << "values" << values[jump - 1] << values[jump];
            }
            qInfo() << "missing step probes" << missing << "of 162";
            qInfo() << "maximum step ink width" << maximumStepWidth << "expected" << 2 * scale;
            app.exit(missing == 0 && maximumStepWidth <= 2 * scale + .75 ? 0 : 1);
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
