#include "trajectoryitem.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTimer>
#include <QImage>
#include <QDebug>
#include <QtMath>
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (QQuickWindow::graphicsApi() == QSGRendererInterface::Software) return 77;
    QQuickWindow window; window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    window.setOpacity(0); window.resize(400, 320); window.setColor(Qt::white);
    QVector<double> time, x, y, z;
    for (int i = 0; i < 500; ++i) { const double t = i / 499.0; time.append(t); x.append(std::cos(t * 10)); y.append(std::sin(t * 10)); z.append(t * 2); }
    PlotSeriesStore store; store.replaceSeries({{0, time, x, QColor("#0072bd"), 3}, {1, time, y, Qt::green, 3}, {2, time, z, Qt::blue, 3}});
    const auto snapshot = store.snapshot({0, 1, 2});
    TrajectoryItem item(window.contentItem()); item.setWidth(400); item.setHeight(320); item.setLineWidth(3);
    item.setAxes({snapshot.series[0], snapshot.series[1], snapshot.series[2]}); window.show();
    int attempts = 0, stage = 0; QImage first; QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (++attempts > 50) { qCritical() << "Trajectory render timeout"; app.exit(1); return; }
        if (item.pending()) return;
        if (!item.error().isEmpty()) { qCritical() << item.error(); app.exit(1); return; }
        const auto image = window.grabWindow().convertToFormat(QImage::Format_RGB32);
        if (image.isNull()) { app.exit(2); return; }
        int ink = 0;
        for (int row = 0; row < image.height(); ++row) for (int col = 0; col < image.width(); ++col) {
            const auto pixel = image.pixel(col, row);
            if (qRed(pixel) < 30 && qGreen(pixel) > 90 && qGreen(pixel) < 140 && qBlue(pixel) > 160) ++ink;
        }
        qInfo() << "Trajectory view" << stage << "curve pixels" << ink;
        if (ink < 300) { app.exit(1); return; }
        if (stage == 0) { first = image; stage = 1; item.presetView(1); return; }
        if (first == image) { app.exit(1); return; }
        if (stage == 1) {
            image.save("trajectory-top.png"); first = image; stage = 2;
            auto camera = item.camera();
            const auto rotation = (QQuaternion::fromAxisAndAngle(QVector3D(.3f, 1, .4f), 47) * camera.orientation()).normalized();
            camera.freeRotation = true;
            camera.rotation = {{rotation.scalar(), rotation.x(), rotation.y(), rotation.z()}};
            camera.viewScale = 1; camera.panX = .08;
            item.setCamera(camera); return;
        }
        image.save("trajectory-free-rotation.png"); app.exit(0);
    });
    timer.start(250); return app.exec();
}
