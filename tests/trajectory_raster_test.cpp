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
    int attempts = 0, stage = 0, redBefore = 0, redExpanded = 0; QImage first; QTimer timer;
    QPointF ringStart, ringEnd;
    const auto redPixels = [](const QImage &image) {
        int count = 0;
        for (int row = 0; row < image.height(); ++row) for (int col = 0; col < image.width(); ++col) {
            const auto pixel = image.pixel(col, row);
            if (qRed(pixel) > 180 && qGreen(pixel) < 160 && qBlue(pixel) < 160) ++count;
        }
        return count;
    };
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
        if (stage == 2) {
            image.save("trajectory-free-rotation.png"); first = image; redBefore = redPixels(image);
            const auto gizmo = TrajectoryBuilder::rotationGizmo(item.camera(), {400, 320}, Qt::gray);
            bool picked = false;
            for (int i = 0; i < 128; ++i) {
                const double angle = i * 2 * M_PI / 128;
                const auto &ring = gizmo->rings[0];
                ringStart = gizmo->center + ring.u * std::cos(angle) + ring.v * std::sin(angle);
                ringEnd = gizmo->center + ring.u * std::cos(angle + .4) + ring.v * std::sin(angle + .4);
                if (gizmo->pick(ringStart) == 1) { picked = true; break; }
            }
            if (!picked) { qCritical() << "X ring not grabbable"; app.exit(1); return; }
            item.hoverAt(ringStart); stage = 3; return;
        }
        if (stage == 3) {
            const int red = redPixels(image);
            qInfo() << "CAD X ring highlight pixels" << redBefore << "->" << red;
            if (red <= redBefore * 1.2) { qCritical() << "Missing ring highlight"; app.exit(1); return; }
            image.save("trajectory-cad-gizmo.png"); first = image;
            if (!item.beginPointerDrag(ringStart, Qt::LeftButton, Qt::NoModifier) || item.rotationHandle() != 1) {
                app.exit(1); return;
            }
            item.dragTo(ringEnd); item.endDrag(); stage = 4; return;
        }
        if (stage == 4) {
            image.save("trajectory-cad-rotated.png"); first = image; redExpanded = redPixels(image);
            item.hoverAt({200, 160});
            if (item.rotationGizmoVisible()) { qCritical() << "Corner control did not collapse"; app.exit(1); return; }
            stage = 5; return;
        }
        const QRect centralRegion(int(image.width() * .3), int(image.height() * .15), int(image.width() * .6), int(image.height() * .6));
        if (redPixels(image) >= redExpanded * .5 || image.copy(centralRegion) != first.copy(centralRegion)) {
            qCritical() << "Collapsed handles still obscure the trajectory"; app.exit(1); return;
        }
        image.save("trajectory-cad-corner-idle.png"); app.exit(0);
    });
    timer.start(250); return app.exec();
}
