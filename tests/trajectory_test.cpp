#include "appcontroller.h"
#include "plotitem.h"
#include "trajectoryitem.h"
#include "qmltypes.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include "dataloadworker.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QtMath>

class TrajectoryTest : public QObject {
    Q_OBJECT
private slots:
    void sharedFrameAndErrorIsolation();
    void measurementAttitudeConventionsAndGaps();
    void multiControllerSessionRemapAndExport();
    void multiTrajectoryCost();
    void timeBasesAndMissingCoordinates();
    void projectionPreservesSpatialScaleAndPrecision();
    void geographicProjectionUsesMetresAndRawReadings();
    void orientationAxesFollowRotationOnly();
    void cursorUsesRawSamplesAndDoesNotCrossGaps();
    void mixedPlotsAndCursorAndUndo();
    void removalRemapsAxesAndPreservesMode();
    void sessionRoundTripAndLegacyAndInvalidCamera();
    void qmlModeSwitchAndSignalDialog_data();
    void qmlModeSwitchAndSignalDialog();
    void latestBackgroundRequestWins();
    void denseFlightRotationCost();
    void twoParametersProducePlanarPaths();
    void draggingDirectionAndPlanarPan();
    void constrainedRotationKeepsAxisAndViewportPivot();
    void rotationGizmoPickingAndMultipleTurns();
    void cornerGizmoExpandsOnlyWhileUsed();
    void ringCenterCrossingDoesNotInjectHalfTurn();
    void rotationGestureRejectsPreviousFrames();
    void viewAnglesDescribeAbsolutePose();
    void treeSelectionKeepsTimeBindings();
    void defaultGeographicAxesUseSelectedSources();
    void zeroFixOnlyExcludedFromFit();
    void wheelKeepsPointerAnchor();
    void highZoomKeepsAnchorAndAllowsPan();
    void indexedSimplificationPreservesSpikesAndGaps();
    void continuousCameraRequestsPublishFrames();
};
static std::array<PlotSeriesDataPtr, 3> axes(const QVector<double> &time,
    const QVector<double> &x, const QVector<double> &y, const QVector<double> &z)
{
    PlotSeriesStore store;
    store.replaceSeries({{0, time, x, QColor("#0072bd"), 2},
                         {1, time, y, Qt::green, 2}, {2, time, z, Qt::blue, 2}});
    const auto snapshot = store.snapshot({0, 1, 2});
    return {snapshot.series[0], snapshot.series[1], snapshot.series[2]};
}
static void csv(const QString &path, const QByteArray &bytes)
{
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size()));
}
static QPointF ringPoint(const TrajectoryRotationGizmo &gizmo, int axis, double angle)
{
    const auto &ring = gizmo.rings[axis - 1];
    return gizmo.center + ring.u * std::cos(angle) + ring.v * std::sin(angle);
}
static std::optional<double> ringGrabParameter(const TrajectoryRotationGizmo &gizmo, int axis)
{
    for (int step = 0; step < 128; ++step) {
        const double angle = step * 2 * M_PI / 128;
        const auto point = ringPoint(gizmo, axis, angle);
        const auto &ring = gizmo.rings[axis - 1];
        const auto tangent = -ring.u * std::sin(angle) + ring.v * std::cos(angle);
        if (ring.depths[step] < -1e-6 || QLineF(point, gizmo.center).length() < gizmo.radius * .3
            || QLineF(QPointF(), tangent).length() < gizmo.radius * .4) continue;
        // Pick a handle that is reliable even with integer window coordinates.
        bool stable = true;
        for (const auto &offset : {QPointF(), QPointF(2, 0), QPointF(-2, 0), QPointF(0, 2), QPointF(0, -2)})
            stable = stable && gizmo.pick(point + offset) == axis;
        if (stable) return angle;
    }
    return std::nullopt;
}
void TrajectoryTest::timeBasesAndMissingCoordinates()
{
    auto input = axes({0, 1, 2, 3, 4}, {0, 1, qQNaN(), 3, 4}, {0, 2, 2, 6, 8}, {0, 0, 0, 1, 2});
    const auto data = TrajectoryBuilder::build(input);
    QVERIFY(data->valid()); QCOMPARE(data->sampleCount, qsizetype(4));
    QCOMPARE(data->runs, (QVector<QPair<qsizetype, qsizetype>>{{0, 1}, {3, 4}}));
    auto changed = std::make_shared<PlotSeriesData>(*input[1]); changed->timeOffset = .01; input[1] = changed;
    QVERIFY(TrajectoryBuilder::build(input)->error.contains(QStringLiteral("时间基")));
    changed->timeOffset = 0; changed->time = {0, 1, 2, 3, 5};
    QVERIFY(!TrajectoryBuilder::build(input)->valid());
    input = axes({0, 2, 1}, {0, 1, 2}, {0, 1, 2}, {0, 1, 2});
    QVERIFY(!TrajectoryBuilder::build(input)->valid());
    input = axes({0, 1}, {qQNaN(), qQNaN()}, {0, 1}, {0, 1});
    QVERIFY(!TrajectoryBuilder::build(input)->valid());
}
void TrajectoryTest::projectionPreservesSpatialScaleAndPrecision()
{
    const auto data = TrajectoryBuilder::build(axes({0, 1, 2, 3}, {1e12, 1e12 + 10, 1e12, 1e12},
        {0, 0, 10, 0}, {0, 0, 0, 10}));
    TrajectoryCamera top; top.azimuth = 0; top.elevation = 90;
    const auto origin = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 0), top, {400, 300});
    const auto x = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 1), top, {400, 300}) - origin;
    const auto y = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 2), top, {400, 300}) - origin;
    QVERIFY2(std::abs(y.x() + x.y()) < 1e-4, qPrintable(QString("x=%1 y=%2 spanX=%3 spanY=%4").arg(x.x(),0,'g',17).arg(y.y(),0,'g',17).arg(data->maximum[0]-data->minimum[0]).arg(data->maximum[1]-data->minimum[1]))); QVERIFY(y.x() > 100);
    QVERIFY(std::abs(x.x()) < 1e-4 && std::abs(y.y()) < 1e-4);
    TrajectoryCamera front; front.azimuth = 0; front.elevation = 0;
    const auto frontOrigin = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 0), front, {400, 300});
    const auto frontX = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 1), front, {400, 300}) - frontOrigin;
    const auto frontY = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 2), front, {400, 300}) - frontOrigin;
    const auto frontZ = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 3), front, {400, 300}) - frontOrigin;
    QVERIFY(frontY.x() > 0 && std::abs(frontY.y()) < 1e-4);
    QVERIFY(std::abs(frontX.x()) < 1e-4 && std::abs(frontX.y()) < 1e-4);
    QVERIFY(frontZ.y() > 0 && std::abs(frontZ.x()) < 1e-4);
    TrajectoryCamera side = front; side.azimuth = 90;
    const auto sideOrigin = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 0), side, {400, 300});
    const auto sideY = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 1), side, {400, 300}) - sideOrigin;
    QVERIFY(sideY.x() > 0 && std::abs(sideY.y()) < 1e-4);
    const auto preview = TrajectoryBuilder::preview(*data, top, {400, 300}, 2, Qt::gray);
    QVERIFY(!preview.geometry.segments.isEmpty());
    for (const auto &segment : preview.geometry.segments) for (const auto &point : segment.vertices)
        QVERIFY(qIsFinite(point.x()) && qIsFinite(point.y()));
}
void TrajectoryTest::geographicProjectionUsesMetresAndRawReadings()
{
    const auto input = axes({0, 1, 2, 3}, {qQNaN(), 0, 0, .001}, {0, 1, 1.001, 1}, {900, 1000, 1000, 1050});
    const auto data = TrajectoryBuilder::build(input, nullptr, true);
    QVERIFY(data->valid()); QVERIFY(data->geographic);
    QCOMPARE(data->origin, (std::array<double, 3>{0, 1, 1000}));
    QCOMPARE(TrajectoryBuilder::spatialPosition(*data, 1), (std::array<double, 3>{0, 0, 0}));
    const auto east = TrajectoryBuilder::spatialPosition(*data, 2);
    const auto north = TrajectoryBuilder::spatialPosition(*data, 3);
    QVERIFY(std::abs(east[1] - 111.31949078762194) < 1e-6);
    QVERIFY(std::abs(east[0]) < 1e-6);
    QVERIFY(std::abs(north[0] - 110.57427581609332) < 1e-6);
    QCOMPARE(north[2], -50.0);
    QCOMPARE(TrajectoryBuilder::position(*data, 3)[0], .001); // source latitude stays unmodified
    const auto across = TrajectoryBuilder::build(axes({0, 1}, {0, 0}, {179.999, -179.999}, {50, 50}), nullptr, true);
    QVERIFY(across->valid());
    QVERIFY(std::abs(TrajectoryBuilder::spatialPosition(*across, 1)[1] - 222.6389815) < .001);
    const auto pole = TrajectoryBuilder::build(axes({0, 1}, {89.999, 89.999}, {0, 1}, {50, 60}), nullptr, true);
    QVERIFY(pole->valid());
    for (double value : TrajectoryBuilder::spatialPosition(*pole, 1)) QVERIFY(qIsFinite(value));
    QVERIFY(!TrajectoryBuilder::build(axes({0, 1}, {40, 91}, {100, 101}, {1, 2}), nullptr, true)->valid());
    QVERIFY(!TrajectoryBuilder::build(axes({0, 1}, {40, 41}, {100, 181}, {1, 2}), nullptr, true)->valid());
    const auto gap = TrajectoryBuilder::build(axes({0, 1, 2}, {40, qQNaN(), 40.01}, {100, 100, 100.01}, {1, 2, 3}), nullptr, true);
    QVERIFY(gap->valid()); QVERIFY(!TrajectoryBuilder::nearestSample(*gap, 1));

    TrajectoryItem item; item.setWidth(400); item.setHeight(300); item.setAxes(input, true);
    item.setTimeCursor(1, 2.8, 0, 0, 3);
    QTRY_VERIFY(!item.pending()); QVERIFY(item.error().isEmpty());
    const auto marker = item.markers().last().toMap();
    QCOMPARE(marker["rawX"].toDouble(), .001); QCOMPARE(marker["spatialZ"].toDouble(), -50.0);
    QVERIFY(marker["details"].toString().contains(QStringLiteral("纬度=")));
    const auto projected = TrajectoryBuilder::project(*data, north, item.camera(), {400, 300});
    QCOMPARE(marker["x"].toDouble(), projected.x()); QCOMPARE(marker["y"].toDouble(), projected.y());
    item.setAxes(input, false); QTRY_VERIFY(!item.pending()); // toggling mode cannot reuse a stale projection
    QVERIFY(!item.markers().last().toMap()["details"].toString().contains(QStringLiteral("纬度=")));
}
void TrajectoryTest::twoParametersProducePlanarPaths()
{
    auto input = axes({0, 1, 2}, {0, 100, 200}, {0, 20, 40}, {1000, 1020, 1040});
    for (int missing = 0; missing < 3; ++missing) {
        auto pair = input; pair[missing].reset();
        const auto data = TrajectoryBuilder::build(pair);
        QVERIFY(data->valid()); QVERIFY(data->planar);
        const auto first = TrajectoryBuilder::project(*data, TrajectoryBuilder::spatialPosition(*data, 0), {}, {640, 480});
        const auto last = TrajectoryBuilder::project(*data, TrajectoryBuilder::spatialPosition(*data, 2), {}, {640, 480});
        QVERIFY(last.x() > first.x()); QVERIFY(missing == 2 ? last.y() < first.y() : last.y() > first.y());
        TrajectoryCamera rotated; rotated.azimuth = 111; rotated.elevation = -73;
        QCOMPARE(TrajectoryBuilder::project(*data, TrajectoryBuilder::spatialPosition(*data, 2), rotated, {640, 480}), last);
        QCOMPARE(*TrajectoryBuilder::nearestSample(*data, 1.6), qsizetype(2));
        QCOMPARE(TrajectoryBuilder::preview(*data, {}, {640, 480}, 2, Qt::gray).labels.size(), qsizetype(2));
    }
    auto geographic = axes({0, 1}, {40, 40.01}, {109, 109.01}, {1000, 1010}); geographic[2].reset();
    const auto projected = TrajectoryBuilder::build(geographic, nullptr, true);
    QVERIFY(projected->valid()); QVERIFY(projected->planar);
    QCOMPARE(TrajectoryBuilder::spatialPosition(*projected, 1)[2], 0.0);
    QVERIFY(TrajectoryBuilder::spatialPosition(*projected, 1)[0] > 800);
    geographic[0].reset(); QVERIFY(!TrajectoryBuilder::build(geographic, nullptr, true)->valid());

    QTemporaryDir dir; csv(dir.filePath("position.csv"), "t,lat,lon,alt\n0,40,109,1000\n1,40.01,109.01,1010\n");
    AppController source; QVERIFY(source.loadCsv(dir.filePath("position.csv"))); QTRY_VERIFY(!source.loading());
    QVERIFY(source.configureTrajectory(0, true, 0, -1, -1));
    QVERIFY(source.configureTrajectory(0, true, 0, -1, 2, true)); // incomplete selection is editable
    QVERIFY(source.configureTrajectory(0, true, 0, 1, -1, true));
    QVERIFY(source.trajectoryState(0)["planar"].toBool());
    const auto path = dir.filePath("planar.disession"); QVERIFY(source.saveSession(path));
    AppController restored; QVERIFY(restored.restoreSession(path)); QTRY_VERIFY(!restored.restoringSession());
    TrajectoryItem item; item.setWidth(640); item.setHeight(480); restored.attachTrajectory(&item, 0);
    QTRY_VERIFY(!item.pending()); QVERIFY(item.planar()); QVERIFY(item.error().isEmpty());
    const auto end = item.markers().last().toMap();
    QVERIFY(!end["rawZ"].isValid()); QVERIFY(!end["details"].toString().contains(QStringLiteral("高度=")));
    QCOMPARE(end["rawX"].toDouble(), 40.01); QCOMPARE(end["rawY"].toDouble(), 109.01);
}
void TrajectoryTest::draggingDirectionAndPlanarPan()
{
    class InteractiveItem : public TrajectoryItem {
    public:
        using TrajectoryItem::mousePressEvent; using TrajectoryItem::mouseMoveEvent;
        using TrajectoryItem::mouseReleaseEvent;
    };
    InteractiveItem item; item.setWidth(400); item.setHeight(300);
    auto input = axes({0, 1}, {0, 10}, {0, 10}, {0, 10}); item.setAxes(input);
    QTRY_VERIFY(!item.pending());
    const auto initial = item.camera();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(340, 150), QPointF(340, 150), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    item.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove, QPointF(240, 150), QPointF(240, 150), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    item.mouseMoveEvent(&move);
    QCOMPARE(item.camera().azimuth, initial.azimuth);
    QCOMPARE(item.camera().panX, initial.panX - .25);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(100, 150), QPointF(100, 150), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    item.mouseReleaseEvent(&release); QTRY_VERIFY(!item.pending());
    const auto beforeTurn = item.camera();
    const auto data = TrajectoryBuilder::build(input);
    const double scale = TrajectoryBuilder::projectionScale(*data, beforeTurn, {400, 300});
    QMouseEvent middlePress(QEvent::MouseButtonPress, QPointF(200, 150), QPointF(200, 150), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QMouseEvent middleMove(QEvent::MouseMove, QPointF(100, 150), QPointF(100, 150), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
    QMouseEvent middleRelease(QEvent::MouseButtonRelease, QPointF(100, 150), QPointF(100, 150), Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
    item.mousePressEvent(&middlePress); item.mouseMoveEvent(&middleMove); item.mouseReleaseEvent(&middleRelease);
    auto rotated = item.camera(); QVERIFY(rotated.freeRotation);
    // Canvas rotation uses a 135px virtual ball; its control lives independently in the corner.
    const float ballAngle = float(qRadiansToDegrees(std::atan2(100.0 / 135, .5 / (100.0 / 135))));
    const auto turn = QQuaternion::fromAxisAndAngle(QVector3D(0, -1, 0), ballAngle);
    QVERIFY(std::abs(QQuaternion::dotProduct(rotated.orientation(), turn * beforeTurn.orientation())) > .99999f);
    QVERIFY(std::abs(TrajectoryBuilder::projectionScale(*data, rotated, {400, 300}) - scale) < .001);
    const auto translation = turn.rotatedVector(QVector3D(float(beforeTurn.panX * 400 / scale), 0, 0));
    QVERIFY(std::abs(rotated.panX - translation.x() * scale / 400) < .00001);
    QVERIFY(std::abs(rotated.panDepth - translation.z()) < .00001);
    const auto localPivot = beforeTurn.orientation().conjugated().rotatedVector(
        QVector3D(float(-beforeTurn.panX * 400 / scale), 0, 0));
    const std::array<double, 3> pivot{{5 + 10 * localPivot.x(), 5 + 10 * localPivot.y(), 5 + 10 * localPivot.z()}};
    const auto pivotScreen = TrajectoryBuilder::project(*data, pivot, rotated, {400, 300});
    QVERIFY(QLineF(pivotScreen, QPointF(200, 150)).length() < .001);
    QTRY_VERIFY(!item.pending());
    // A subsequent vertical drag is about the displayed horizontal axis, even after rotation.
    QMouseEvent verticalMove(QEvent::MouseMove, QPointF(200, 250), QPointF(200, 250), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
    item.mousePressEvent(&middlePress); item.mouseMoveEvent(&verticalMove); item.mouseReleaseEvent(&middleRelease);
    const auto pitch = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), ballAngle);
    QVERIFY(std::abs(QQuaternion::dotProduct(item.camera().orientation(), pitch * rotated.orientation())) > .99999f);
    QTRY_VERIFY(!item.pending());
    const auto displayed = item.camera();
    auto queued = displayed; queued.freeRotation = false; queued.azimuth = 170;
    item.setCamera(queued); // worker has not committed the new camera yet
    item.mousePressEvent(&middlePress); item.mouseMoveEvent(&middleMove); item.mouseReleaseEvent(&middleRelease);
    QVERIFY(std::abs(QQuaternion::dotProduct(item.camera().orientation(), turn * displayed.orientation())) > .99999f);
    QTRY_VERIFY(!item.pending());
    input[2].reset(); item.setAxes(input); QTRY_VERIFY(!item.pending()); QVERIFY(item.planar());
    const auto planar = item.camera(); item.mousePressEvent(&press); item.mouseMoveEvent(&move); item.mouseReleaseEvent(&release);
    QCOMPARE(item.camera().azimuth, planar.azimuth); QCOMPARE(item.camera().elevation, planar.elevation);
    QCOMPARE(item.camera().panX, planar.panX - .25); QTRY_VERIFY(!item.pending());
    const auto noTurn = item.camera(); item.mousePressEvent(&middlePress); item.mouseMoveEvent(&middleMove);
    QVERIFY(item.camera() == noTurn);
}
void TrajectoryTest::constrainedRotationKeepsAxisAndViewportPivot()
{
    const auto input = axes({0, 1}, {0, 10}, {0, 10}, {0, 10});
    const auto data = TrajectoryBuilder::build(input);
    TrajectoryItem item; item.setWidth(600); item.setHeight(400); item.setAxes(input);
    QTRY_VERIFY(!item.pending());
    for (int preset = 0; preset < 4; ++preset) for (int axis = 1; axis <= 3; ++axis) {
        item.presetView(preset);
        auto before = item.camera(); before.panX = .32; before.panY = -.21; before.panDepth = .4; before.zoom = 2;
        item.setCamera(before); QTRY_VERIFY(!item.pending());
        const double scale = TrajectoryBuilder::projectionScale(*data, before, {600, 400});
        const QVector3D offset(float(before.panX * 600 / scale), float(-before.panY * 400 / scale), float(before.panDepth));
        const auto localPivot = before.orientation().conjugated().rotatedVector(-offset);
        const std::array<double, 3> pivot{{5 + 10 * localPivot.x(), 5 + 10 * localPivot.y(), 5 + 10 * localPivot.z()}};
        const auto gizmo = TrajectoryBuilder::rotationGizmo(before, {600, 400}, Qt::gray);
        const auto parameter = ringGrabParameter(*gizmo, axis); QVERIFY(parameter);
        const auto start = ringPoint(*gizmo, axis, *parameter);
        item.hoverAt(start); QCOMPARE(item.rotationHandle(), axis);
        QVERIFY(item.beginPointerDrag(start, Qt::LeftButton, Qt::NoModifier)); QVERIFY(item.rotating());
        QCOMPARE(item.rotationHandle(), axis);
        const auto &ring = gizmo->rings[axis - 1];
        const bool edgeOn = std::abs(ring.u.x() * ring.v.y() - ring.u.y() * ring.v.x()) < gizmo->radius * gizmo->radius * .15;
        if (edgeOn) {
            const auto tangent = -ring.u * std::sin(*parameter) + ring.v * std::cos(*parameter);
            item.dragTo(start + tangent * .4);
        } else {
            for (int step = 1; step <= 5; ++step) item.dragTo(ringPoint(*gizmo, axis, *parameter + step * .08));
        }
        const auto after = item.camera();
        QVector3D worldAxis; worldAxis[axis - 1] = 1;
        // Rotation around a world axis leaves its direction in camera space unchanged.
        QVERIFY((before.orientation().rotatedVector(worldAxis) - after.orientation().rotatedVector(worldAxis)).length() < 1e-5);
        const auto expected = before.orientation() * QQuaternion::fromAxisAndAngle(worldAxis, float(qRadiansToDegrees(.4)));
        QVERIFY(std::abs(QQuaternion::dotProduct(after.orientation(), expected)) > .99999f);
        QVERIFY(QLineF(TrajectoryBuilder::project(*data, pivot, after, {600, 400}), QPointF(300, 200)).length() < .001);
        QVERIFY(std::abs(TrajectoryBuilder::projectionScale(*data, after, {600, 400}) - scale) < .001);
        item.dragTo(start);
        QVERIFY(std::abs(QQuaternion::dotProduct(item.camera().orientation(), before.orientation())) > .99999f);
        QVERIFY(std::abs(item.camera().panX - before.panX) < 1e-6);
        QVERIFY(std::abs(item.camera().panY - before.panY) < 1e-6);
        item.endDrag(); QVERIFY(!item.rotating()); QTRY_VERIFY(!item.pending());
    }
    QVERIFY(item.beginDrag({300, 200}, true));
    const auto before = item.camera();
    item.dragTo({540, 340}); QVERIFY(item.camera().valid());
    item.dragTo({300, 200});
    QVERIFY(std::abs(QQuaternion::dotProduct(item.camera().orientation(), before.orientation())) > .99999f);
    item.endDrag();
}
void TrajectoryTest::rotationGizmoPickingAndMultipleTurns()
{
    TrajectoryCamera view;
    const auto gizmo = TrajectoryBuilder::rotationGizmo(view, {600, 400}, Qt::gray);
    QCOMPARE(gizmo->pick(gizmo->center), 0);
    QCOMPARE(gizmo->pick(gizmo->center + QPointF(110, 110)), -1);
    QCOMPARE(gizmo->pick({qQNaN(), 0}), -1);
    auto zoomed = view; zoomed.zoom = 80; zoomed.panX = 8; zoomed.panY = -4;
    const auto fixed = TrajectoryBuilder::rotationGizmo(zoomed, {600, 400}, Qt::gray);
    QCOMPARE(fixed->center, gizmo->center); QCOMPARE(fixed->radius, gizmo->radius);
    for (int axis = 1; axis <= 3; ++axis) {
        QCOMPARE(fixed->rings[axis - 1].points, gizmo->rings[axis - 1].points);
        const auto parameter = ringGrabParameter(*gizmo, axis); QVERIFY(parameter);
        const auto point = ringPoint(*gizmo, axis, *parameter);
        QCOMPARE(gizmo->pick(point), axis);
        // Generous picking tolerance accepts slight misses on the thin visible ring.
        QCOMPARE(gizmo->pick(point + QPointF(2, 0)), axis);
    }
    const auto small = TrajectoryBuilder::rotationGizmo(view, {100, 70}, Qt::gray);
    QCOMPARE(small->center, QPointF(25, 52.5));
    for (const auto &ring : small->rings) for (const auto &point : ring.points)
        QVERIFY(QRectF(0, 0, 100, 70).contains(point));
    for (const auto &segment : small->geometry.segments) for (const auto &point : segment.vertices)
        QVERIFY(qIsFinite(point.x()) && qIsFinite(point.y()));

    TrajectoryItem item; item.setWidth(600); item.setHeight(400);
    auto input = axes({0, 1}, {0, 10}, {0, 10}, {0, 10}); item.setAxes(input); item.presetView(1);
    QTRY_VERIFY(!item.pending());
    const auto before = item.camera();
    const auto top = TrajectoryBuilder::rotationGizmo(before, {600, 400}, Qt::gray);
    const auto parameter = ringGrabParameter(*top, 3); QVERIFY(parameter);
    const auto start = ringPoint(*top, 3, *parameter);
    QVERIFY(item.beginPointerDrag(start, Qt::LeftButton, Qt::NoModifier));
    for (int step = 1; step <= 128; ++step) item.dragTo(ringPoint(*top, 3, *parameter + step * .1));
    const auto expected = before.orientation() * QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), float(qRadiansToDegrees(12.8)));
    QVERIFY(std::abs(QQuaternion::dotProduct(item.camera().orientation(), expected)) > .99999f);
    item.endDrag(); QTRY_VERIFY(!item.pending());
    // Shift always pans, even when pressed exactly on an axis ring.
    const auto current = item.camera();
    QVERIFY(item.beginPointerDrag(start, Qt::LeftButton, Qt::ShiftModifier)); QVERIFY(!item.rotating());
    item.dragTo(start + QPointF(30, 0)); item.endDrag();
    QCOMPARE(item.camera().rotation, current.rotation);
    QVERIFY(std::abs(item.camera().panX - current.panX - .05) < 1e-6);
    input[2].reset(); item.setAxes(input); QTRY_VERIFY(!item.pending());
    QVERIFY(item.rotationRect().isEmpty()); QCOMPARE(item.rotationHandleAt({300, 200}), -1);
    QVERIFY(item.beginPointerDrag({300, 200}, Qt::LeftButton, Qt::NoModifier)); QVERIFY(!item.rotating());
    item.endDrag();
}
void TrajectoryTest::cornerGizmoExpandsOnlyWhileUsed()
{
    TrajectoryItem item; item.setWidth(600); item.setHeight(400);
    item.setAxes(axes({0, 1}, {0, 10}, {0, 10}, {0, 10})); QTRY_VERIFY(!item.pending());
    const QPointF center(300, 200), corner = item.rotationRect().center();
    QCOMPARE(corner, QPointF(55, 345)); QVERIFY(item.orientationRect().contains(corner));
    QVERIFY(!item.rotationGizmoVisible()); QCOMPARE(item.rotationHandleAt(center), -1);
    item.hoverAt(corner); QVERIFY(item.rotationGizmoVisible()); QCOMPARE(item.rotationHandle(), 0);
    const auto before = item.camera();
    QVERIFY(item.beginPointerDrag(corner, Qt::LeftButton, Qt::NoModifier)); QVERIFY(item.rotating());
    item.dragTo(center); QVERIFY(item.rotationGizmoVisible());
    item.hoverAt({-1e6, -1e6}); // Leaving while grabbed cannot hide the active handles.
    QVERIFY(item.rotationGizmoVisible());
    item.endDrag(); QVERIFY(!item.rotationGizmoVisible()); QTRY_VERIFY(!item.pending());
    QVERIFY(std::abs(QQuaternion::dotProduct(before.orientation(), item.camera().orientation())) < .999f);
    item.hoverAt(corner); QVERIFY(item.rotationGizmoVisible());
    item.hoverAt(center); QVERIFY(!item.rotationGizmoVisible());
    const auto beforePan = item.camera();
    QVERIFY(item.beginPointerDrag(center, Qt::LeftButton, Qt::NoModifier)); QVERIFY(!item.rotating());
    item.dragTo(center + QPointF(30, 0)); item.endDrag();
    QCOMPARE(item.camera().rotation, beforePan.rotation);
    QVERIFY(std::abs(item.camera().panX - beforePan.panX - .05) < 1e-6);
    QVERIFY(!item.rotationGizmoVisible()); QTRY_VERIFY(!item.pending());
    QVERIFY(item.beginPointerDrag(center, Qt::MiddleButton, Qt::NoModifier)); QVERIFY(item.rotating());
    QVERIFY(!item.rotationGizmoVisible()); item.dragTo(center + QPointF(20, 0)); item.endDrag();
    QTRY_VERIFY(!item.pending());
    item.hoverAt(corner); QVERIFY(item.rotationGizmoVisible());
    item.setHeight(600); QTRY_VERIFY(!item.pending());
    // A resize moves the corner away from the stationary pointer, collapsing the control.
    QVERIFY(!item.rotationGizmoVisible()); QCOMPARE(item.rotationRect().center(), QPointF(55, 545));
}
void TrajectoryTest::ringCenterCrossingDoesNotInjectHalfTurn()
{
    TrajectoryItem item; item.setWidth(600); item.setHeight(400);
    item.setAxes(axes({0, 1}, {0, 10}, {0, 10}, {0, 10})); item.presetView(1);
    QTRY_VERIFY(!item.pending());
    const auto gizmo = TrajectoryBuilder::rotationGizmo(item.camera(), {600, 400}, Qt::gray);
    const auto parameter = ringGrabParameter(*gizmo, 3); QVERIFY(parameter);
    const auto start = ringPoint(*gizmo, 3, *parameter);
    QVERIFY(item.beginPointerDrag(start, Qt::LeftButton, Qt::NoModifier));
    item.dragTo(ringPoint(*gizmo, 3, *parameter + .1));
    const auto beforeCenter = item.camera();
    item.dragTo(gizmo->center);
    item.dragTo(gizmo->center + (gizmo->center - start) * .12);
    QCOMPARE(item.camera().rotation, beforeCenter.rotation);
    item.dragTo(ringPoint(*gizmo, 3, *parameter + M_PI + .1));
    // Resume tracking after the singular centre without jumping by pi.
    const auto turned = beforeCenter.orientation() * QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), float(qRadiansToDegrees(.1)));
    QVERIFY(std::abs(QQuaternion::dotProduct(item.camera().orientation(), turned)) > .99999f);
    item.endDrag(); QTRY_VERIFY(!item.pending());
    const auto nextGizmo = TrajectoryBuilder::rotationGizmo(item.camera(), {600, 400}, Qt::gray);
    const auto nextParameter = ringGrabParameter(*nextGizmo, 3); QVERIFY(nextParameter);
    const auto nextStart = ringPoint(*nextGizmo, 3, *nextParameter);
    QVERIFY(item.beginPointerDrag(nextStart, Qt::LeftButton, Qt::NoModifier));
    item.dragTo(nextGizmo->center + (nextStart - nextGizmo->center) * .2);
    const auto beforeSkipping = item.camera();
    // A sparse mouse event can jump right over the dead zone; inspect the whole segment.
    item.dragTo(nextGizmo->center - (nextStart - nextGizmo->center) * .2);
    QCOMPARE(item.camera().rotation, beforeSkipping.rotation);
    item.endDrag();
}
void TrajectoryTest::rotationGestureRejectsPreviousFrames()
{
    TrajectoryItem item; item.setWidth(600); item.setHeight(400);
    item.setAxes(axes({0, 1}, {0, 10}, {0, 10}, {0, 10})); item.presetView(1);
    QTRY_VERIFY(!item.pending());
    const auto shown = item.camera();
    const auto gizmo = TrajectoryBuilder::rotationGizmo(shown, {600, 400}, Qt::gray);
    const auto parameter = ringGrabParameter(*gizmo, 3); QVERIFY(parameter);
    auto queued = shown; queued.freeRotation = true;
    const auto q = shown.orientation() * QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), 120);
    queued.rotation = {{q.scalar(), q.x(), q.y(), q.z()}};
    item.setCamera(queued); // This obsolete frame may finish after the next press.
    bool sawOldFrame = false;
    TrajectoryCamera zero; zero.azimuth = 0; zero.elevation = 0;
    connect(&item, &TrajectoryItem::previewChanged, this, [&] {
        const auto angles = item.viewAngles();
        const auto displayed = zero.orientation()
            * QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), angles.z())
            * QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), angles.y())
            * QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), angles.x());
        sawOldFrame |= std::abs(QQuaternion::dotProduct(displayed.normalized(), shown.orientation())) < .9f;
    });
    QVERIFY(item.beginPointerDrag(ringPoint(*gizmo, 3, *parameter), Qt::LeftButton, Qt::NoModifier));
    item.dragTo(ringPoint(*gizmo, 3, *parameter + .2)); item.endDrag();
    QTRY_VERIFY(!item.pending()); QVERIFY(!sawOldFrame);
    QVERIFY(std::abs(QQuaternion::dotProduct(item.camera().orientation(), shown.orientation() * QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), float(qRadiansToDegrees(.2))))) > .99999f);
}
void TrajectoryTest::viewAnglesDescribeAbsolutePose()
{
    TrajectoryCamera zero; zero.azimuth = 0; zero.elevation = 0;
    const auto zeroPose = zero.orientation();
    QVERIFY(zero.viewAngles().length() < .001);
    QVERIFY((zeroPose.rotatedVector(QVector3D(1, 0, 0)) - QVector3D(0, 0, -1)).length() < .001);
    QVERIFY((zeroPose.rotatedVector(QVector3D(0, 1, 0)) - QVector3D(1, 0, 0)).length() < .001);
    QVERIFY((zeroPose.rotatedVector(QVector3D(0, 0, 1)) - QVector3D(0, -1, 0)).length() < .001);
    auto top = zero; top.elevation = 90;
    QVERIFY2((top.viewAngles() - QVector3D(0, 90, 0)).length() < .001,
             qPrintable(QString("top=%1,%2,%3").arg(top.viewAngles().x()).arg(top.viewAngles().y()).arg(top.viewAngles().z())));
    auto side = zero; side.azimuth = 90;
    QVERIFY((side.viewAngles() - QVector3D(0, 0, 90)).length() < .001);
    for (const auto &angles : {QVector3D(0, 0, 0), QVector3D(30, 0, 0), QVector3D(0, 30, 0), QVector3D(0, 0, 30), QVector3D(20, 30, 40), QVector3D(-45, -25, 135), QVector3D(35, 90, 50), QVector3D(35, -90, 50)}) {
        const auto q = zeroPose * QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), angles.z())
            * QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), angles.y())
            * QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), angles.x());
        TrajectoryCamera view; view.freeRotation = true; view.rotation = {{q.scalar(), q.x(), q.y(), q.z()}};
        const auto actual = view.viewAngles();
        const auto rebuilt = zeroPose * QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), actual.z())
            * QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), actual.y())
            * QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), actual.x());
        QVERIFY(std::abs(QQuaternion::dotProduct(q.normalized(), rebuilt.normalized())) > .99999f);
        if (std::abs(angles.y()) < 89) QVERIFY((actual - angles).length() < .001);
    }
    TrajectoryItem item; item.setWidth(600); item.setHeight(400);
    item.setAxes(axes({0, 1}, {0, 10}, {0, 10}, {0, 10})); item.presetView(2);
    QTRY_VERIFY(!item.pending()); QVERIFY(item.viewAngles().length() < .001);
    for (int gesture = 0; gesture < 2; ++gesture) {
        const auto gizmo = TrajectoryBuilder::rotationGizmo(item.camera(), {600, 400}, Qt::gray);
        const auto parameter = ringGrabParameter(*gizmo, 3); QVERIFY(parameter);
        const auto absoluteBefore = item.viewAngles();
        QVERIFY(item.beginPointerDrag(ringPoint(*gizmo, 3, *parameter), Qt::LeftButton, Qt::NoModifier));
        QVERIFY((item.viewAngles() - absoluteBefore).length() < .001);
        const auto &ring = gizmo->rings[2];
        const bool edgeOn = std::abs(ring.u.x() * ring.v.y() - ring.u.y() * ring.v.x()) < gizmo->radius * gizmo->radius * .15;
        if (edgeOn) {
            const auto tangent = -ring.u * std::sin(*parameter) + ring.v * std::cos(*parameter);
            item.dragTo(ringPoint(*gizmo, 3, *parameter) + tangent * .3);
        } else item.dragTo(ringPoint(*gizmo, 3, *parameter + .3));
        item.endDrag(); QTRY_VERIFY(!item.pending());
        QVERIFY(std::abs(item.viewAngles().z() - qRadiansToDegrees(.3 * (gesture + 1))) < .001);
    }
    item.fitView(); QTRY_VERIFY(!item.pending());
    QVERIFY(std::abs(item.viewAngles().z() - qRadiansToDegrees(.6)) < .001);
    item.presetView(2); QTRY_VERIFY(!item.pending()); QVERIFY(item.viewAngles().length() < .001);
}
void TrajectoryTest::treeSelectionKeepsTimeBindings()
{
    QTemporaryDir dir; csv(dir.filePath("signals.csv"), "t,x,y,z,w\n0,0,1,2,3\n1,10,11,12,13\n");
    AppController c; QVERIFY(c.loadCsv(dir.filePath("signals.csv"))); QTRY_VERIFY(!c.loading());
    c.setLayout(1, 2); c.selectSignal(3);
    c.enterTrajectoryMode(1); QCOMPARE(c.activePlotIndex(), 1);
    QVERIFY(c.trajectoryState(1)["geographic"].toBool());
    QCOMPARE(c.signalModel()->checkedCount(), 0);
    c.toggleSignal(0); c.selectSignal(1);
    QVERIFY(c.trajectoryState(1)["planar"].toBool());
    c.toggleSignal(2); c.selectSignal(3);
    QCOMPARE(c.signalModel()->checkedCount(), 4); QCOMPARE(c.trajectorySignalOptions(1).size(), 5);
    QCOMPARE(c.trajectoryState(1)["x"].toInt(), 0); QCOMPARE(c.trajectoryState(1)["z"].toInt(), 2);
    QVERIFY(c.bindTrajectoryAxis(1, 0, 0)); QVERIFY(c.bindTrajectoryAxis(1, 1, 1));
    QVERIFY(c.bindTrajectoryAxis(1, 2, 2)); QVERIFY(!c.trajectoryState(1)["planar"].toBool());
    QVERIFY(c.bindTrajectoryAxis(1, 1, 3)); // replacing an axis keeps the old source available
    QVERIFY(c.plotSignalEnabled(1, 1)); QCOMPARE(c.signalModel()->checkedCount(), 4);
    QVERIFY(c.bindTrajectoryAxis(1, 0, 2)); // swap an already assigned source
    QCOMPARE(c.trajectoryState(1)["x"].toInt(), 2); QCOMPARE(c.trajectoryState(1)["z"].toInt(), 0);
    QVERIFY(c.bindTrajectoryAxis(1, 2, -1)); QVERIFY(c.plotSignalEnabled(1, 0));
    c.toggleSignal(2); QCOMPARE(c.trajectoryState(1)["x"].toInt(), -1);
    QCOMPARE(c.signalModel()->checkedCount(), 3); QVERIFY(!c.bindTrajectoryAxis(1, 0, 2));
    QVERIFY(!c.bindTrajectoryAxis(1, 3, 0));
    for (int row = 0; row < c.signalModel()->rowCount(); ++row) {
        const auto index = c.signalModel()->index(row);
        const int source = c.signalModel()->data(index, SignalModel::IndexRole).toInt();
        if (source >= 0) QCOMPARE(c.signalModel()->data(index, SignalModel::CheckedRole).toBool(), source != 2);
    }
    const auto path = dir.filePath("pool.disession"); QVERIFY(c.saveSession(path));
    AppController restored; QVERIFY(restored.restoreSession(path)); QTRY_VERIFY(!restored.restoringSession());
    QCOMPARE(restored.trajectorySignalOptions(1), c.trajectorySignalOptions(1));
    QCOMPARE(restored.trajectoryState(1)["y"].toInt(), 3);
    c.setActivePlot(0); QCOMPARE(c.signalModel()->checkedCount(), 1); QVERIFY(c.plotSignalEnabled(0, 3));
    c.setActivePlot(1); QCOMPARE(c.signalModel()->checkedCount(), 3);
    c.clearPlotSignals(1); QCOMPARE(c.signalModel()->checkedCount(), 0); QCOMPARE(c.trajectorySignalOptions(1).size(), 1);
    QVERIFY(c.configureTrajectory(1, false, -1, -1, -1)); QCOMPARE(c.signalModel()->checkedCount(), 0);
    c.setActivePlot(0); c.enterTrajectoryMode(0); QCOMPARE(c.signalModel()->checkedCount(), 1); // seed existing time sources
    c.selectSignal(0); c.selectSignal(1); c.clearPlotSignals(0);
    QVERIFY(c.configureTrajectory(0, false, -1, -1, -1));
    QCOMPARE(c.signalModel()->checkedCount(), 1); QVERIFY(c.plotSignalEnabled(0, 3));
}
void TrajectoryTest::defaultGeographicAxesUseSelectedSources()
{
    QTemporaryDir dir; csv(dir.filePath("selected.csv"), "t,latitude,longitude,height,other\n0,40,109,1000,1\n1,41,110,1010,2\n");
    AppController c; QVERIFY(c.loadCsv(dir.filePath("selected.csv"))); QTRY_VERIFY(!c.loading());
    c.setLayout(1, 2); c.enterTrajectoryMode(0);
    QVERIFY(c.trajectoryState(0)["geographic"].toBool());
    c.selectSignal(1); c.selectSignal(0); c.selectSignal(2); c.selectSignal(3);
    QCOMPARE(c.trajectoryState(0)["x"].toInt(), 1);
    QCOMPARE(c.trajectoryState(0)["y"].toInt(), 0);
    QCOMPARE(c.trajectoryState(0)["z"].toInt(), 2);
    c.toggleSignal(0); QCOMPARE(c.trajectoryState(0)["y"].toInt(), -1);
    c.toggleSignal(0); QCOMPARE(c.trajectoryState(0)["y"].toInt(), 0);
    QVERIFY(c.bindTrajectoryAxis(0, 1, -1));
    c.enterTrajectoryMode(0); QCOMPARE(c.trajectoryState(0)["y"].toInt(), -1);
    QVERIFY(c.configureTrajectory(0, false, 1, -1, 2, false));
    c.enterTrajectoryMode(0); QVERIFY(!c.trajectoryState(0)["geographic"].toBool());
    QCOMPARE(c.trajectoryState(0)["x"].toInt(), 1);
    c.setActivePlot(1); c.selectSignal(0); c.selectSignal(1); c.selectSignal(2);
    c.enterTrajectoryMode(1); QVERIFY(c.trajectoryState(1)["geographic"].toBool());
    QCOMPARE(c.trajectoryState(1)["x"].toInt(), 0);
    QCOMPARE(c.trajectoryState(1)["y"].toInt(), 1);
    QCOMPARE(c.trajectoryState(1)["z"].toInt(), 2);
    c.clearPlotSignals(1); c.selectSignal(2);
    QCOMPARE(c.trajectoryState(1)["x"].toInt(), 2);
    QCOMPARE(c.trajectoryState(0)["x"].toInt(), 1);
}
void TrajectoryTest::zeroFixOnlyExcludedFromFit()
{
    const auto clean = TrajectoryBuilder::build(axes({0, 1, 2}, {40, 40.001, 40.002},
        {109, 109.002, 109.004}, {1000, 1010, 1020}), nullptr, true);
    const auto withZero = TrajectoryBuilder::build(axes({0, 1, 2, 3, 4}, {0, 40, 0, 40.001, 40.002},
        {0, 109, 0, 109.002, 109.004}, {0, 1000, 0, 1010, 1020}), nullptr, true);
    QVERIFY(clean->valid()); QVERIFY(withZero->valid());
    QCOMPARE(withZero->origin, clean->origin); QCOMPARE(withZero->minimum, clean->minimum); QCOMPARE(withZero->maximum, clean->maximum);
    QCOMPARE(withZero->sampleCount, qsizetype(5)); QCOMPARE(withZero->runs.size(), qsizetype(1));
    QCOMPARE(TrajectoryBuilder::nearestSample(*withZero, 2).value(), qsizetype(2));
    QCOMPARE(TrajectoryBuilder::position(*withZero, 2)[0], 0.0); QCOMPARE(TrajectoryBuilder::position(*withZero, 2)[1], 0.0);
    for (const auto &level : withZero->levels) QVERIFY(level.runs[0].contains(2)); // retained outlier and its connecting path
    const auto p = TrajectoryBuilder::spatialPosition(*clean, 1);
    const TrajectoryCamera camera;
    QCOMPARE(TrajectoryBuilder::project(*withZero, p, camera, {800, 600}), TrajectoryBuilder::project(*clean, p, camera, {800, 600}));
    const auto onlyZero = TrajectoryBuilder::build(axes({0, 1}, {0, 0}, {0, 0}, {0, 10}), nullptr, true);
    QVERIFY(onlyZero->valid()); QVERIFY(qIsFinite(TrajectoryBuilder::projectionScale(*onlyZero, camera, {800, 600})));
    const auto equator = TrajectoryBuilder::build(axes({0, 1}, {0, 0}, {109, 109.01}, {0, 10}), nullptr, true);
    QVERIFY(equator->valid()); QVERIFY(equator->maximum[1] > 1000); // one zero coordinate is a legitimate fix
    const auto xyz = TrajectoryBuilder::build(axes({0, 1}, {0, 10}, {0, 10}, {0, 10}));
    QCOMPARE(xyz->minimum, (std::array<double, 3>{0, 0, 0})); // generic XYZ unchanged
}
void TrajectoryTest::wheelKeepsPointerAnchor()
{
    class WheelItem : public TrajectoryItem { public: using TrajectoryItem::wheelEvent; };
    const auto input = axes({0, 1, 2}, {0, 10, 20}, {0, 20, 10}, {0, 10, 20});
    const auto data = TrajectoryBuilder::build(input);
    WheelItem item; item.setWidth(800); item.setHeight(600); item.setAxes(input);
    auto camera = item.camera(); camera.panX = .1; camera.panY = -.1; item.setCamera(camera); QTRY_VERIFY(!item.pending());
    const auto point = TrajectoryBuilder::spatialPosition(*data, 1);
    const auto anchor = TrajectoryBuilder::project(*data, point, camera, {800, 600});
    QWheelEvent wheel(anchor, anchor, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    item.wheelEvent(&wheel);
    QVERIFY(item.camera().zoom > camera.zoom);
    QVERIFY(QLineF(anchor, TrajectoryBuilder::project(*data, point, item.camera(), {800, 600})).length() < 1e-7);
    item.zoomAt(anchor, -120);
    QVERIFY(std::abs(item.camera().zoom - camera.zoom) < 1e-10);
    QVERIFY(QLineF(anchor, TrajectoryBuilder::project(*data, point, item.camera(), {800, 600})).length() < 1e-7);
    auto plane = input; plane[2].reset(); item.setAxes(plane); item.fitView(); QTRY_VERIFY(!item.pending());
    const auto flat = TrajectoryBuilder::build(plane);
    const auto flatAnchor = TrajectoryBuilder::project(*flat, TrajectoryBuilder::spatialPosition(*flat, 1), item.camera(), {800, 600});
    item.zoomAt(flatAnchor, 240);
    QVERIFY(QLineF(flatAnchor, TrajectoryBuilder::project(*flat, TrajectoryBuilder::spatialPosition(*flat, 1), item.camera(), {800, 600})).length() < 1e-7);
}
void TrajectoryTest::highZoomKeepsAnchorAndAllowsPan()
{
    const auto input = axes({0, 1, 2}, {0, 10, 20}, {0, 20, 10}, {0, 10, 20});
    for (bool planar : {false, true}) {
        auto sources = input;
        if (planar) sources[2].reset();
        const auto data = TrajectoryBuilder::build(sources);
        TrajectoryItem item; item.setWidth(800); item.setHeight(600); item.setAxes(sources);
        QTRY_VERIFY(!item.pending());
        const auto point = TrajectoryBuilder::spatialPosition(*data, 0);
        const auto anchor = TrajectoryBuilder::project(*data, point, item.camera(), {800, 600});
        for (int i = 0; i < 40; ++i) {
            item.zoomAt(anchor, 120);
            QVERIFY2(QLineF(anchor, TrajectoryBuilder::project(*data, point, item.camera(), {800, 600})).length() < 1e-7,
                     "Repeated pointer zoom must keep its anchor beyond the old four-viewport pan limit");
        }
        QCOMPARE(item.camera().zoom, 100.0);
        QVERIFY(std::max(std::abs(item.camera().panX), std::abs(item.camera().panY)) > 4);
        // A pan started while the zoom preview is pending must retain the latest requested zoom.
        const auto beforePan = item.camera();
        QVERIFY(item.beginDrag({400, 300}, false));
        item.dragTo({240, 420}); item.endDrag();
        QCOMPARE(item.camera().zoom, beforePan.zoom);
        QVERIFY(std::abs(item.camera().panX - beforePan.panX + .2) < 1e-9);
        QVERIFY(std::abs(item.camera().panY - beforePan.panY - .2) < 1e-9);
        for (const auto &delta : {QPointF(-160, 0), QPointF(160, 0), QPointF(0, -120), QPointF(0, 120)}) {
            QTRY_VERIFY(!item.pending());
            const auto before = TrajectoryBuilder::project(*data, point, item.camera(), {800, 600});
            QVERIFY(item.beginDrag({400, 300}, false)); item.dragTo(QPointF(400, 300) + delta); item.endDrag();
            QVERIFY(QLineF(before + delta, TrajectoryBuilder::project(*data, point, item.camera(), {800, 600})).length() < 1e-7);
        }
        item.fitView(); QCOMPARE(item.camera().zoom, 1.0); QCOMPARE(item.camera().panX, 0.0); QCOMPARE(item.camera().panY, 0.0);
    }
}
void TrajectoryTest::indexedSimplificationPreservesSpikesAndGaps()
{
    QVector<double> time, x, y, z;
    for (int i = 0; i < 2000; ++i) {
        time.append(i); x.append(i); y.append(std::sin(i * .01));
        z.append(i == 733 ? 800 : i == 999 ? qQNaN() : 0);
    }
    const auto data = TrajectoryBuilder::build(axes(time, x, y, z)); QVERIFY(data->valid());
    QCOMPARE(data->runs.size(), qsizetype(2));
    for (const auto &level : data->levels) {
        QCOMPARE(level.runs.size(), qsizetype(2)); QVERIFY(level.runs[0].contains(733));
        QCOMPARE(level.runs[0].last(), qsizetype(998)); QCOMPARE(level.runs[1].first(), qsizetype(1000));
        for (const auto &indices : level.runs) for (qsizetype j = 1; j < indices.size(); ++j) {
            const auto &a = data->normalized[indices[j - 1]], &b = data->normalized[indices[j]];
            for (qsizetype i = indices[j - 1] + 1; i < indices[j]; ++i) {
                double length = 0, dot = 0;
                for (int axis = 0; axis < 3; ++axis) {
                    const double d = double(b[axis]) - a[axis]; length += d * d;
                    dot += (double(data->normalized[i][axis]) - a[axis]) * d;
                }
                const double t = length > 0 ? qBound(0.0, dot / length, 1.0) : 0;
                double squared = 0;
                for (int axis = 0; axis < 3; ++axis) {
                    const double d = double(data->normalized[i][axis]) - a[axis] - t * (double(b[axis]) - a[axis]);
                    squared += d * d;
                }
                QVERIFY(std::sqrt(squared) <= level.tolerance + 1e-10);
            }
        }
    }
}
void TrajectoryTest::orientationAxesFollowRotationOnly()
{
    const auto data = TrajectoryBuilder::build(axes({0, 1}, {0, 1000}, {0, 100}, {0, 10}));
    TrajectoryCamera view;
    const auto initial = TrajectoryBuilder::preview(*data, view, {400, 300}, 2, Qt::gray);
    view.panX = .5; view.panY = -.2; view.zoom = 5;
    const auto panned = TrajectoryBuilder::preview(*data, view, {400, 300}, 2, Qt::gray);
    QCOMPARE(panned.labels, initial.labels);
    view.azimuth = 25;
    const auto rotated = TrajectoryBuilder::preview(*data, view, {400, 300}, 2, Qt::gray);
    QVERIFY(rotated.labels != initial.labels); QCOMPARE(rotated.labels.size(), qsizetype(3));
    for (const auto &label : initial.labels) {
        const auto map = label.toMap();
        QVERIFY(map["x"].toDouble() < 100); QVERIFY(map["y"].toDouble() > 200);
        QVERIFY(!map["text"].toString().contains('['));
    }
    // There are no outer grey box edges; only the tiny origin tick is grey.
    for (const auto &segment : initial.geometry.segments) if (segment.color == Qt::gray)
        for (const auto &point : segment.vertices) {
            QVERIFY(std::abs(point.x() - 55) < 4); QVERIFY(std::abs(point.y() - 245) < 4);
        }
}
void TrajectoryTest::cursorUsesRawSamplesAndDoesNotCrossGaps()
{
    const auto data = TrajectoryBuilder::build(axes({0, 1, 2, 3, 4}, {0, 1, qQNaN(), 3, 4},
        {0, 10, 20, 30, 40}, {0, .01, .02, .03, .04}));
    QCOMPARE(*TrajectoryBuilder::nearestSample(*data, .8), qsizetype(1));
    QCOMPARE(*TrajectoryBuilder::nearestSample(*data, .5), qsizetype(0));
    QVERIFY(!TrajectoryBuilder::nearestSample(*data, 2));
    QVERIFY(!TrajectoryBuilder::nearestSample(*data, -1));
    QVector<double> time, x, y, z;
    for (int i = 0; i < 10000; ++i) { time.append(i); x.append(i * .001); y.append(0); z.append(0); }
    const auto dense = TrajectoryBuilder::build(axes(time, x, y, z));
    const auto preview = TrajectoryBuilder::preview(*dense, {}, {200, 200}, 2, Qt::gray);
    QVERIFY(!preview.geometry.segments.isEmpty());
    const auto index = TrajectoryBuilder::nearestSample(*dense, 1234.2);
    QCOMPARE(*index, qsizetype(1234)); QCOMPARE(TrajectoryBuilder::position(*dense, *index)[0], x[1234]);
}
void TrajectoryTest::mixedPlotsAndCursorAndUndo()
{
    QTemporaryDir dir; const auto path = dir.filePath("xyz.csv");
    csv(path, "t,X,Y,Z\n0,0,10,20\n1,1,11,21\n2,2,12,22\n3,3,13,23\n4,4,14,24\n");
    AppController c; QVERIFY(c.loadCsv(path)); QTRY_VERIFY(!c.loading()); c.setLayout(1, 2);
    PlotItem a, b; a.setWidth(320); a.setHeight(240); b.setWidth(320); b.setHeight(240);
    c.attachPlot(&a, 0); c.attachPlot(&b, 1);
    c.selectSignal(1);
    const auto originalBindings = a.visibleSeriesIds();
    QVERIFY(c.configureTrajectory(0, true, 0, 1, 2));
    TrajectoryItem item; item.setWidth(320); item.setHeight(240); c.attachTrajectory(&item, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!item.pending(), 5000); QVERIFY2(item.error().isEmpty(), qPrintable(item.error()));
    b.restoreCursorState(2, 1.2, 2.8);
    QTRY_COMPARE(item.markers().size(), qsizetype(4));
    QCOMPARE(item.markers()[2].toMap()["time"].toDouble(), 1.0);
    QCOMPARE(item.markers()[2].toMap()["rawY"].toDouble(), 11.0);
    const auto before = item.camera(); auto view = before; view.azimuth = 17; view.zoom = 2;
    const double xmin = b.xMinimum(), xmax = b.xMaximum();
    item.setCamera(view); QVERIFY(c.canUndoView());
    QCOMPARE(b.xMinimum(), xmin); QCOMPARE(b.xMaximum(), xmax);
    c.undoView(); QVERIFY(item.camera() == before);
    item.setCamera(view); c.fitPlots(true, true, false);
    QCOMPARE(item.camera().zoom, 1.0); QCOMPARE(b.xMinimum(), xmin); QCOMPARE(b.xMaximum(), xmax);
    QVERIFY(c.configureTrajectory(0, false, 0, 1, 2));
    QVERIFY(!c.trajectoryState(0)["enabled"].toBool());
    QCOMPARE(a.visibleSeriesIds(), originalBindings);
}
void TrajectoryTest::removalRemapsAxesAndPreservesMode()
{
    QTemporaryDir dir; csv(dir.filePath("one.csv"), "t,A\n0,1\n1,2\n");
    csv(dir.filePath("xyz.csv"), "t,X,Y,Z\n0,0,0,0\n1,1,2,3\n");
    AppController c; QCOMPARE(c.loadFiles(QVariantList{dir.filePath("one.csv"), dir.filePath("xyz.csv")}), 2);
    QTRY_VERIFY(!c.loading()); QVERIFY(c.configureTrajectory(0, true, 1, 2, 3));
    c.selectSignal(0); QCOMPARE(c.trajectorySignalOptions(0).size(), 5);
    QVERIFY(c.removeFile("one.csv")); auto state = c.trajectoryState(0);
    QCOMPARE(state["x"].toInt(), 0); QCOMPARE(state["z"].toInt(), 2);
    QCOMPARE(c.trajectorySignalOptions(0).size(), 4);
    QVERIFY(c.removeFile("xyz.csv")); state = c.trajectoryState(0);
    QVERIFY(state["enabled"].toBool()); QCOMPARE(state["x"].toInt(), -1);
    QCOMPARE(c.trajectorySignalOptions(0).size(), 1);
    QVERIFY(c.saveSession(dir.filePath("empty.disession")));
}
void TrajectoryTest::sessionRoundTripAndLegacyAndInvalidCamera()
{
    QTemporaryDir dir; csv(dir.filePath("xyz.csv"), "t,lat,lon,alt\n0,40,109,1000\n1,40.01,109.01,1100\n");
    AppController source; QVERIFY(source.loadCsv(dir.filePath("xyz.csv"))); QTRY_VERIFY(!source.loading());
    QVERIFY(source.configureTrajectory(0, true, 0, 1, 2, true));
    TrajectoryItem item; item.setWidth(320); item.setHeight(240); source.attachTrajectory(&item, 0);
    TrajectoryCamera view; view.azimuth = 73; view.elevation = 39; view.zoom = 20; view.panX = -11.2; view.panY = 9.3;
    const auto rotation = (QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), 37) * view.orientation()).normalized();
    view.freeRotation = true; view.rotation = {{rotation.scalar(), rotation.x(), rotation.y(), rotation.z()}};
    view.viewScale = 1.7; view.panDepth = .2;
    item.setCamera(view); const auto path = dir.filePath("scene.disession"); QVERIFY(source.saveSession(path));
    SessionDocument document; QString error; QVERIFY(readSessionDocument(path, &document, &error));
    QVERIFY(document.plots[0].trajectory.camera == view);
    QVERIFY(document.plots[0].trajectory.geographic);
    AppController restored; QVERIFY(restored.restoreSession(path)); QTRY_VERIFY(!restored.restoringSession());
    QVERIFY(restored.trajectoryState(0)["enabled"].toBool());
    QVERIFY(restored.trajectoryState(0)["geographic"].toBool());
    TrajectoryItem late; late.setWidth(320); late.setHeight(240); restored.attachTrajectory(&late, 0);
    QVERIFY(late.camera() == view); QTRY_VERIFY(!late.pending()); QVERIFY(late.error().isEmpty());
    auto legacy = sessionToJson(document); legacy["version"] = 1;
    auto plots = legacy["plots"].toArray(); auto plot = plots[0].toObject(); plot.remove("trajectory"); plots[0] = plot; legacy["plots"] = plots;
    SessionDocument parsed; QVERIFY(sessionFromJson(legacy, &parsed, &error)); QVERIFY(!parsed.plots[0].trajectory.enabled);
    auto v2 = sessionToJson(document); v2["version"] = 2;
    auto v2plots = v2["plots"].toArray(); auto v2plot = v2plots[0].toObject();
    auto v2trajectory = v2plot["trajectory"].toObject(); v2trajectory.remove("geographic");
    v2plot["trajectory"] = v2trajectory; v2plots[0] = v2plot; v2["plots"] = v2plots;
    QVERIFY(sessionFromJson(v2, &parsed, &error)); QVERIFY(!parsed.plots[0].trajectory.geographic);
    auto v3 = sessionToJson(document); v3["version"] = 3;
    QVERIFY(sessionFromJson(v3, &parsed, &error)); QVERIFY(!parsed.plots[0].trajectory.camera.freeRotation);
    auto v4 = sessionToJson(document); v4["version"] = 4;
    auto oldPlots = v4["plots"].toArray(); auto oldPlot = oldPlots[0].toObject();
    auto oldTrajectory = oldPlot["trajectory"].toObject(); oldTrajectory.remove("signals");
    oldPlot["trajectory"] = oldTrajectory; oldPlots[0] = oldPlot; v4["plots"] = oldPlots;
    QVERIFY(sessionFromJson(v4, &parsed, &error)); QVERIFY(parsed.plots[0].trajectory.camera == view);
    QCOMPARE(parsed.plots[0].trajectory.signalIds, (QVector<int>{0, 1, 2}));
    auto invalid = sessionToJson(document); plots = invalid["plots"].toArray(); plot = plots[0].toObject();
    auto trajectory = plot["trajectory"].toObject(); trajectory["camera"] = QJsonArray{0, 25, 0, 0, 0};
    plot["trajectory"] = trajectory; plots[0] = plot; invalid["plots"] = plots;
    QVERIFY(!sessionFromJson(invalid, &parsed, &error));
    trajectory["camera"] = QJsonArray{0, 25, 1, TrajectoryCamera::MaximumTranslation + 1, 0};
    plot["trajectory"] = trajectory; plots[0] = plot; invalid["plots"] = plots;
    QVERIFY(!sessionFromJson(invalid, &parsed, &error));
    trajectory["camera"] = QJsonArray{0, 25, 1, 0, 0}; trajectory["axes"] = QJsonArray{0, 1, 99};
    plot["trajectory"] = trajectory; plots[0] = plot; invalid["plots"] = plots;
    QVERIFY(!sessionFromJson(invalid, &parsed, &error));
    invalid = sessionToJson(document); plots = invalid["plots"].toArray(); plot = plots[0].toObject();
    trajectory = plot["trajectory"].toObject(); trajectory["geographic"] = "yes";
    plot["trajectory"] = trajectory; plots[0] = plot; invalid["plots"] = plots;
    QVERIFY(!sessionFromJson(invalid, &parsed, &error));
    invalid = sessionToJson(document); plots = invalid["plots"].toArray(); plot = plots[0].toObject();
    trajectory = plot["trajectory"].toObject(); trajectory["rotation"] = QJsonArray{2, 0, 0, 0};
    plot["trajectory"] = trajectory; plots[0] = plot; invalid["plots"] = plots;
    QVERIFY(!sessionFromJson(invalid, &parsed, &error));
    for (const auto &available : QList<QJsonArray>{QJsonArray{0, 0, 1, 2}, QJsonArray{0, 1}, QJsonArray{0, 1, 99}}) {
        invalid = sessionToJson(document); plots = invalid["plots"].toArray(); plot = plots[0].toObject();
        trajectory = plot["trajectory"].toObject(); trajectory["signals"] = available;
        plot["trajectory"] = trajectory; plots[0] = plot; invalid["plots"] = plots;
        QVERIFY(!sessionFromJson(invalid, &parsed, &error));
    }
}
void TrajectoryTest::qmlModeSwitchAndSignalDialog_data()
{
    QTest::addColumn<bool>("compiled");
    QTest::newRow("source") << false;
    QTest::newRow("compiled") << true;
}
void TrajectoryTest::qmlModeSwitchAndSignalDialog()
{
    QFETCH(bool, compiled);
    static const bool registered = [] {
        qmlRegisterTypesAndRevisions<PlotItemQmlRegistration, TrajectoryItemQmlRegistration,
            AppControllerQmlRegistration, SignalModelQmlRegistration>("DataInspector", 1);
        return true;
    }();
    Q_UNUSED(registered);
    QTemporaryDir dir; QByteArray flight("t,latitude,longitude,altitude\n");
    for (int i = 0; i <= 180; ++i) {
        const double angle = i * .045;
        flight += QByteArray::number(i) + ',' + QByteArray::number(40.86 + .005 * std::sin(angle), 'f', 8)
            + ',' + QByteArray::number(109.56 + .007 * std::cos(angle), 'f', 8)
            + ',' + QByteArray::number(1325 + i, 'f', 3) + '\n';
    }
    csv(dir.filePath("xyz.csv"), flight);
    AppController c; QVERIFY(c.loadCsv(dir.filePath("xyz.csv"))); QTRY_VERIFY(!c.loading());
    QQmlEngine engine;
    const QString directory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../qml");
    QQmlComponent component(&engine, compiled ? QUrl("qrc:/qt/qml/DataInspectorTrajectoryTest/QuickPlot.qml")
                                            : QUrl::fromLocalFile(directory + "/QuickPlot.qml"));
    QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"controller", QVariant::fromValue(&c)}, {"plotIndex", 0}}));
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    QQuickWindow window; auto *quick = qobject_cast<QQuickItem *>(object.get());
    quick->setParentItem(window.contentItem()); quick->setWidth(640); quick->setHeight(480);
    window.resize(640, 480); window.show();
    auto *menu = object->findChild<QObject *>("trajectoryModeMenuItem"); QVERIFY(menu);
    QVERIFY(QMetaObject::invokeMethod(menu, "triggered"));
    auto *dialog = object->findChild<QObject *>("trajectoryAxesDialog"); QVERIFY(dialog);
    QTRY_VERIFY(object->property("trajectoryMode").toBool());
    QVERIFY(!dialog->property("visible").toBool());
    QCOMPARE(c.signalModel()->checkedCount(), 0);
    QVERIFY(c.trajectoryState(0)["geographic"].toBool());
    c.toggleSignal(0); QCOMPARE(c.trajectoryState(0)["x"].toInt(), 0);
    c.toggleSignal(1); QVERIFY(c.bindTrajectoryAxis(0, 0, 0)); QVERIFY(c.bindTrajectoryAxis(0, 1, 1));
    QVERIFY(c.trajectoryState(0)["planar"].toBool());
    QCOMPARE(c.signalModel()->checkedCount(), 2);
    c.selectSignal(2); QCOMPARE(c.trajectoryState(0)["z"].toInt(), 2);
    QVERIFY(c.bindTrajectoryAxis(0, 2, 2));
    QVERIFY(!c.trajectoryState(0)["planar"].toBool());
    auto *axesButton = object->findChild<QObject *>("trajectoryAxesButton"); QVERIFY(axesButton);
    QVERIFY(QMetaObject::invokeMethod(axesButton, "clicked")); QTRY_VERIFY(dialog->property("visible").toBool());
    auto *mode = object->findChild<QObject *>("trajectoryCoordinateMode"); QVERIFY(mode); mode->setProperty("currentIndex", 1);
    QTRY_VERIFY(dialog->property("height").toDouble() <= window.height());
    if (QGuiApplication::platformName() != "offscreen") {
        QTest::qWait(100);
        QVERIFY(window.grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath("trajectory-geographic-dialog.png")));
    }
    for (const auto &entry : QList<QPair<QString, int>>{{"trajectoryAxisX", 1}, {"trajectoryAxisY", 2}, {"trajectoryAxisZ", 3}}) {
        auto *combo = object->findChild<QObject *>(entry.first); QVERIFY(combo); combo->setProperty("currentIndex", entry.second);
    }
    auto *confirm = object->findChild<QObject *>("trajectoryConfirmButton"); QVERIFY(confirm);
    QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_VERIFY(object->property("trajectoryMode").toBool());
    QVERIFY(c.trajectoryState(0)["geographic"].toBool());
    QTRY_VERIFY(object->findChild<TrajectoryItem *>("trajectoryItem"));
    auto *item = object->findChild<TrajectoryItem *>("trajectoryItem");
    QTRY_VERIFY_WITH_TIMEOUT(!item->pending(), 5000); QVERIFY(item->error().isEmpty());
    QVERIFY(item->height() >= 480 - 80); QVERIFY(item->width() >= 640 - 12);
    // Deliver real window events through QML, rather than invoking the C++ handlers directly.
    const auto initialView = item->camera();
    const QPoint dragStart = item->mapToScene(QPointF(item->width() * .5, item->height() * .5)).toPoint();
    QTest::mousePress(&window, Qt::MiddleButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(&window, dragStart + QPoint(40, 20));
    QTest::mouseRelease(&window, Qt::MiddleButton, Qt::NoModifier, dragStart + QPoint(40, 20));
    QTRY_VERIFY(item->camera().freeRotation);
    QVERIFY(std::abs(QQuaternion::dotProduct(item->camera().orientation(), initialView.orientation())) < .999f);
    QTRY_VERIFY(!item->pending());
    const auto beforeAxesDrag = item->camera();
    QVERIFY(!item->orientationRect().isEmpty());
    const QPoint axesStart = item->mapToScene(item->rotationRect().center()).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, axesStart);
    QTest::mouseMove(&window, axesStart + QPoint(30, -10));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, axesStart + QPoint(30, -10));
    QVERIFY(std::abs(QQuaternion::dotProduct(item->camera().orientation(), beforeAxesDrag.orientation())) < .999f);
    QTRY_VERIFY(!item->pending());
    const auto beforePan = item->camera();
    const QPoint panStart = item->mapToScene(QPointF(item->width() * .82, item->height() * .5)).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, panStart);
    QTest::mouseMove(&window, panStart + QPoint(20, 0));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, panStart + QPoint(20, 0));
    QCOMPARE(item->camera().rotation, beforePan.rotation);
    QVERIFY(std::abs(item->camera().panX - beforePan.panX - 20 / item->width()) < 1e-6);
    QTRY_VERIFY(!item->pending());
    QVERIFY(!object->findChild<QObject *>("trajectoryRotationAxis"));
    auto *pivotReference = object->findChild<QQuickItem *>("trajectoryRotationCenter"); QVERIFY(pivotReference);
    auto *angleFeedback = object->findChild<QQuickItem *>("trajectoryRotationFeedback"); QVERIFY(angleFeedback);
    QVERIFY(!pivotReference->isVisible());
    for (int axis = 1; axis <= 3; ++axis) {
        const auto beforeAxis = item->camera();
        const auto absoluteAngles = item->viewAngles();
        const auto gizmo = TrajectoryBuilder::rotationGizmo(beforeAxis, {item->width(), item->height()}, Qt::gray);
        const auto parameter = ringGrabParameter(*gizmo, axis); QVERIFY(parameter);
        const QPoint ringStart = item->mapToScene(ringPoint(*gizmo, axis, *parameter)).toPoint();
        const QPoint ringEnd = item->mapToScene(ringPoint(*gizmo, axis, *parameter + .3)).toPoint();
        QTest::mouseMove(&window, ringStart);
        QTRY_COMPARE(item->rotationHandle(), axis);
        QVERIFY(item->rotationGizmoVisible());
        if (QGuiApplication::platformName() != "offscreen" && axis == 1) {
            QTest::qWait(100);
            QVERIFY(window.grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath("trajectory-cad-handles.png")));
        }
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, ringStart);
        QVERIFY((angleFeedback->property("angles").value<QVector3D>() - absoluteAngles).length() < .001);
        QCOMPARE(item->rotationHandle(), axis);
        QVERIFY(item->rotating()); QVERIFY(pivotReference->isVisible());
        QVERIFY(QLineF(pivotReference->position() + QPointF(13, 13), QPointF(item->width() * .5, item->height() * .5)).length() < .01);
        QTest::mouseMove(&window, ringEnd);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, ringEnd);
        QVERIFY(!pivotReference->isVisible());
        QVector3D worldAxis; worldAxis[axis - 1] = 1;
        QVERIFY((beforeAxis.orientation().rotatedVector(worldAxis) - item->camera().orientation().rotatedVector(worldAxis)).length() < 1e-5);
        QVERIFY(std::abs(QQuaternion::dotProduct(beforeAxis.orientation(), item->camera().orientation())) < .999f);
        QTRY_VERIFY(!item->pending());
        QVERIFY((angleFeedback->property("angles").value<QVector3D>() - item->viewAngles()).length() < .001);
        QVERIFY(angleFeedback->property("text").toString().contains(QStringLiteral("视角")));
        const auto feedbackText = angleFeedback->property("text").toString();
        QVERIFY(feedbackText.contains("<font color='#d94b4b'>X "));
        QVERIFY(feedbackText.contains("<font color='#27945b'>Y "));
        QVERIFY(feedbackText.contains("<font color='#397bc5'>Z "));
        const auto beforeShift = item->camera();
        QTest::mousePress(&window, Qt::LeftButton, Qt::ShiftModifier, dragStart);
        QVERIFY(!item->rotating());
        QTest::mouseMove(&window, dragStart + QPoint(20, 0));
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::ShiftModifier, dragStart + QPoint(20, 0));
        QCOMPARE(item->camera().rotation, beforeShift.rotation);
        QVERIFY(std::abs(item->camera().panX - beforeShift.panX - 20 / item->width()) < 1e-6);
        QTRY_VERIFY(!item->pending());
    }
    // The merged corner's empty centre supports direct free rotation.
    const auto beforeFree = item->camera();
    const QPoint freeStart = item->mapToScene(item->rotationRect().center()).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, freeStart);
    QVERIFY(item->rotating()); QCOMPARE(item->rotationHandle(), 0);
    QTest::mouseMove(&window, freeStart + QPoint(20, 10));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, freeStart + QPoint(20, 10));
    QVERIFY(std::abs(QQuaternion::dotProduct(item->camera().orientation(), beforeFree.orientation())) < .999f);
    QTRY_VERIFY(!item->pending());
    QTest::mouseMove(&window, dragStart); QTRY_VERIFY(!item->rotationGizmoVisible());
    if (QGuiApplication::platformName() != "offscreen") {
        QTest::qWait(100);
        QVERIFY(window.grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath("trajectory-cad-corner-idle.png")));
    }
    const auto beforeCenterPan = item->camera();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, dragStart); QVERIFY(!item->rotating());
    QTest::mouseMove(&window, dragStart + QPoint(20, 0));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, dragStart + QPoint(20, 0));
    QCOMPARE(item->camera().rotation, beforeCenterPan.rotation);
    QVERIFY(!item->rotationGizmoVisible()); QTRY_VERIFY(!item->pending());
    const auto beforeAlt = item->camera();
    QTest::mousePress(&window, Qt::LeftButton, Qt::AltModifier, dragStart);
    QVERIFY(item->rotating());
    QTest::mouseMove(&window, dragStart + QPoint(40, 20));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::AltModifier, dragStart + QPoint(40, 20));
    QVERIFY(std::abs(QQuaternion::dotProduct(item->camera().orientation(), beforeAlt.orientation())) < .999f);
    QTRY_VERIFY(!item->pending());
    const auto beforeWheel = item->camera();
    const QPointF anchor(item->width() * .75, item->height() * .25);
    const QPointF wheelScene = item->mapToScene(anchor);
    QWheelEvent wheel(wheelScene, window.mapToGlobal(wheelScene.toPoint()), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&window, &wheel);
    QTRY_VERIFY(item->camera().zoom > beforeWheel.zoom);
    QVERIFY(std::abs(item->camera().panX - (.25 - 1.15 * (.25 - beforeWheel.panX))) < 1e-6);
    QVERIFY(std::abs(item->camera().panY - (-.25 - 1.15 * (-.25 - beforeWheel.panY))) < 1e-6);
    QTRY_VERIFY(!item->pending());
    // Repeater delegates belong to the visual tree, not necessarily QObject's ownership tree.
    QQuickItem *bindingX = nullptr;
    QVector<QQuickItem *> search{quick};
    while (!search.isEmpty()) {
        auto *candidate = search.takeLast();
        if (candidate->objectName() == "trajectoryBindingAxis0") { bindingX = candidate; break; }
        for (auto *child : candidate->childItems()) search.append(child);
    }
    QVERIFY(bindingX); QVERIFY(bindingX->width() >= 28); QVERIFY(bindingX->isVisible());
    QCOMPARE(bindingX->property("count").toInt(), 4);
    QCOMPARE(bindingX->property("sourceId").toInt(), 0);
    QCOMPARE(bindingX->property("sourceName").toString(), QString("latitude"));
    QCOMPARE(bindingX->property("currentIndex").toInt(), 1);
    QVERIFY(c.renameSignal(0, QStringLiteral("纬度 latitude updated")));
    QCOMPARE(bindingX->property("sourceName").toString(), QStringLiteral("纬度 latitude updated"));
    QCOMPARE(bindingX->property("currentIndex").toInt(), 1);
    QVERIFY(c.renameSignal(0, QStringLiteral("latitude")));
    QCOMPARE(bindingX->property("sourceName").toString(), QStringLiteral("latitude"));
    const auto choosePopupRow = [&](int row) {
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                         bindingX->mapToScene(QPointF(bindingX->width() / 2, bindingX->height() / 2)).toPoint());
        auto *popup = bindingX->property("popup").value<QObject *>(); QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        auto *list = popup->property("contentItem").value<QQuickItem *>(); QVERIFY(list);
        QQuickItem *chosen = nullptr;
        QTRY_VERIFY(([&] {
            QVector<QQuickItem *> pending{list};
            QSet<int> rows;
            while (!pending.isEmpty()) {
                auto *entry = pending.takeLast();
                if (entry->property("text").isValid() && entry->property("index").isValid()) {
                    const int option = entry->property("index").toInt();
                    if (option >= 0 && option < 4) {
                        const auto expected = c.trajectorySignalOptions(0)[option].toMap()["label"].toString();
                        if (entry->property("text").toString() != expected) return false;
                        rows.insert(option); if (option == row) chosen = entry;
                    }
                }
                for (auto *child : entry->childItems()) pending.append(child);
            }
            return rows.size() == 4 && chosen;
        })());
        if (compiled && row == 2 && QGuiApplication::platformName() != "offscreen") {
            QTest::qWait(100);
            QVERIFY(window.grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath("trajectory-binding-popup-ui.png")));
        }
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                         chosen->mapToScene(QPointF(chosen->width() / 2, chosen->height() / 2)).toPoint());
        QTRY_VERIFY(!popup->property("visible").toBool());
    };
    choosePopupRow(2); // Actual popup selection swaps X/Y, including the displayed names and indices.
    QCOMPARE(c.trajectoryState(0)["x"].toInt(), 1);
    QCOMPARE(c.trajectoryState(0)["y"].toInt(), 0);
    QCOMPARE(bindingX->property("sourceName").toString(), QString("longitude"));
    QCOMPARE(bindingX->property("currentIndex").toInt(), 2);
    choosePopupRow(1);
    QCOMPARE(c.trajectoryState(0)["x"].toInt(), 0);
    QCOMPARE(bindingX->property("sourceName").toString(), QString("latitude"));
    QVERIFY(QMetaObject::invokeMethod(bindingX, "activated", Q_ARG(int, 0)));
    QCOMPARE(c.trajectoryState(0)["x"].toInt(), -1); QCOMPARE(c.signalModel()->checkedCount(), 3);
    QVERIFY(QMetaObject::invokeMethod(bindingX, "activated", Q_ARG(int, 1)));
    QCOMPARE(c.trajectoryState(0)["x"].toInt(), 0); QTRY_VERIFY(!item->pending());
    if (QQuickWindow::graphicsApi() != QSGRendererInterface::Software
        && QGuiApplication::platformName() != "offscreen") {
        QTest::qWait(100);
        const auto image = window.grabWindow(); QVERIFY(!image.isNull());
        QVERIFY(image.save(QDir(QCoreApplication::applicationDirPath()).filePath("trajectory-geographic-ui.png")));
    }
    QVERIFY(QMetaObject::invokeMethod(axesButton, "clicked")); QTRY_VERIFY(dialog->property("visible").toBool());
    auto *heightAxis = object->findChild<QObject *>("trajectoryAxisZ"); QVERIFY(heightAxis); heightAxis->setProperty("currentIndex", 0);
    QVERIFY(QMetaObject::invokeMethod(confirm, "clicked")); QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_VERIFY(!item->pending()); QVERIFY(item->planar()); QVERIFY(item->orientationRect().isEmpty());
    quick->setWidth(320); quick->setHeight(240); window.resize(320, 240);
    QTRY_VERIFY(!item->pending()); QVERIFY(item->height() >= 160); QVERIFY(item->width() >= 308);
    if (QQuickWindow::graphicsApi() != QSGRendererInterface::Software && QGuiApplication::platformName() != "offscreen") {
        QTest::qWait(100);
        QVERIFY(window.grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath("trajectory-planar-small-ui.png")));
    }

    // The same window event chain exercises management and measurement controls
    // in both source QML and qmlcachegen builds.
    quick->setWidth(900); quick->setHeight(600); window.resize(900, 600);
    csv(dir.filePath("wing.csv"), "t,lat,lon,height,roll,pitch,yaw\n0,40.87,109.57,1325,0,0,0\n180,40.88,109.58,1505,20,10,90\n");
    QVERIFY(c.loadCsv(dir.filePath("wing.csv"))); QTRY_VERIFY(!c.loading());
    auto *add = object->findChild<QObject *>("trajectoryAdd"); QVERIFY(add);
    QVERIFY(QMetaObject::invokeMethod(add, "clicked")); QCOMPARE(c.trajectoryState(0)["active"].toInt(), 1);
    for (int row = 3; row < 9; ++row) c.selectSignal(row);
    auto *propertiesButton = object->findChild<QObject *>("trajectoryProperties"); QVERIFY(propertiesButton);
    QVERIFY(QMetaObject::invokeMethod(propertiesButton, "clicked"));
    auto *properties = object->findChild<QObject *>("trajectoryPropertiesDialog"); QVERIFY(properties);
    QTRY_VERIFY(properties->property("visible").toBool());
    auto *name = object->findChild<QObject *>("trajectoryName"); QVERIFY(name); name->setProperty("text", "Wing UI");
    auto *attitudeMode = object->findChild<QObject *>("trajectoryAttitudeMode"); QVERIFY(attitudeMode); attitudeMode->setProperty("currentIndex", 1);
    properties->setProperty("selectedSources", QVariantList{6, 7, 8, -1});
    QVERIFY(QMetaObject::invokeMethod(properties, "accepted")); QVERIFY(QMetaObject::invokeMethod(properties, "close"));
    QCOMPARE(c.trajectoryState(0)["name"].toString(), QString("Wing UI"));
    QCOMPARE(c.trajectoryState(0)["attitudeMode"].toInt(), 1);
    QTRY_VERIFY(!item->pending()); QVERIFY(item->error().isEmpty());
    item->fitView(); QTRY_VERIFY(!item->pending());
    c.setCursorMode(1);
    if (auto *plot = object->findChild<PlotItem *>()) plot->setCursorPosition(90, 1);
    QCOMPARE(item->markers().size() >= 4, true);
    QVERIFY(item->markers().last().toMap().contains("attitude"));
    if (QGuiApplication::platformName() != "offscreen") {
        QTest::qWait(100); QVERIFY(window.grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath("trajectory-multi-attitude-ui.png")));
    }
    auto *selector = object->findChild<QObject *>("trajectorySelector"); QVERIFY(selector);
    selector->setProperty("currentIndex", 0); QVERIFY(QMetaObject::invokeMethod(selector, "activated", Q_ARG(int, 0)));
    QCOMPARE(c.trajectoryState(0)["active"].toInt(), 0);
    auto *visibility = object->findChild<QObject *>("trajectoryVisible"); QVERIFY(visibility);
    visibility->setProperty("checked", false); QVERIFY(QMetaObject::invokeMethod(visibility, "clicked")); QTRY_VERIFY(!item->pending());
    QCOMPARE(item->markers().size() >= 2, true);
    auto *remove = object->findChild<QObject *>("trajectoryRemove"); QVERIFY(remove);
    QVERIFY(QMetaObject::invokeMethod(remove, "clicked")); QTRY_VERIFY(!item->pending());
    QCOMPARE(c.trajectoryState(0)["tracks"].toList().size(), 1);
    QCOMPARE(c.trajectoryState(0)["name"].toString(), QString("Wing UI"));
    QVERIFY(c.configureTrajectory(0, false, 0, 1, 2, true));
    QTRY_VERIFY(!object->property("trajectoryMode").toBool());
    object.reset();
}
void TrajectoryTest::latestBackgroundRequestWins()
{
    QVector<double> time, x, y, z;
    for (int i = 0; i < 100000; ++i) { time.append(i); x.append(i); y.append(std::sin(i * .01)); z.append(i * .1); }
    const auto input = axes(time, x, y, z);
    TrajectoryItem item; item.setWidth(320); item.setHeight(240); item.setAxes(input);
    TrajectoryCamera last;
    for (int i = 0; i < 20; ++i) { last.azimuth = i; last.zoom = 1 + i * .05; item.setCamera(last); }
    QTRY_VERIFY_WITH_TIMEOUT(!item.pending(), 10000); QVERIFY(item.error().isEmpty());
    const auto data = TrajectoryBuilder::build(input);
    const auto expected = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 0), last, {320, 240});
    QCOMPARE(item.markers().first().toMap()["x"].toDouble(), expected.x());
    auto dying = std::make_unique<TrajectoryItem>(); dying->setWidth(320); dying->setHeight(240); dying->setAxes(input);
    dying.reset(); // A cancelled job must not access a destroyed GUI object.
}
void TrajectoryTest::denseFlightRotationCost()
{
    constexpr int count = 1000000;
    QVector<double> time, x, y, z;
    time.reserve(count); x.reserve(count); y.reserve(count); z.reserve(count);
    for (int i = 0; i < count; ++i) {
        const double t = i / double(count - 1);
        time.append(t); x.append(std::sin(t * 20) * 20000);
        y.append(std::cos(t * 20) * 18000); z.append(1000 + t * 2000);
    }
    const auto input = axes(time, x, y, z);
    QElapsedTimer timer; timer.start(); const auto data = TrajectoryBuilder::build(input);
    const double buildMs = timer.nsecsElapsed() / 1e6;
    QVERIFY(data->valid()); timer.restart();
    for (int i = 0; i < 10; ++i) {
        TrajectoryCamera view; view.azimuth = -45 + i * 3;
        const auto preview = TrajectoryBuilder::preview(*data, view, {1200, 800}, 2, Qt::gray);
        QVERIFY(!preview.geometry.segments.isEmpty());
        QVERIFY(preview.projectedSamples < 10000); // cost follows shape detail, not raw sample count
    }
    qInfo("Million-sample flight: build %.2f ms; rotation preview average %.2f ms", buildMs, timer.nsecsElapsed() / 1e7);
}
void TrajectoryTest::continuousCameraRequestsPublishFrames()
{
    QVector<double> time, x, y, z;
    for (int i = 0; i < 200000; ++i) {
        const double t = i * .0001; time.append(t); x.append(std::sin(t)); y.append(std::cos(t)); z.append(t * .01);
    }
    TrajectoryItem item; item.setWidth(800); item.setHeight(600); item.setAxes(axes(time, x, y, z));
    QTRY_VERIFY_WITH_TIMEOUT(!item.pending(), 10000);
    int changedWhileMoving = 0, requests = 0;
    bool moving = true;
    connect(&item, &TrajectoryItem::markersChanged, this, [&] { if (moving) ++changedWhileMoving; });
    QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&] {
        auto view = item.camera(); view.azimuth = std::remainder(view.azimuth + 3, 360.0); item.setCamera(view);
        if (++requests == 60) { moving = false; timer.stop(); }
    });
    timer.start(5); QTRY_VERIFY_WITH_TIMEOUT(!moving, 10000);
    QVERIFY2(changedWhileMoving >= 3, "Continuous camera changes must not starve visible previews");
    QTRY_VERIFY_WITH_TIMEOUT(!item.pending(), 10000);
}

void TrajectoryTest::sharedFrameAndErrorIsolation()
{
    TrajectorySource a; a.id = "a"; a.name = "A"; a.geographic = true;
    a.axes = axes({0, 1, 2}, {22, 22.001, 22.002}, {114, 114.001, 114.002}, {100, 110, 120});
    TrajectorySource b = a; b.id = "b"; b.name = "B";
    b.axes = axes({0, .5, 1, 1.5, 2}, {22.01, 22.011, 22.012, 22.013, 22.014}, {114.01, 114.011, 114.012, 114.013, 114.014}, {200, 210, 220, 230, 240});
    TrajectorySource bad = a; bad.id = "bad"; bad.name = "Bad";
    auto shifted = std::make_shared<PlotSeriesData>(*bad.axes[1]); shifted->timeOffset = .1; bad.axes[1] = shifted;
    const auto frame = TrajectoryBuilder::buildFrame({bad, a, b});
    QVERIFY(!frame->paths[0]->valid()); QVERIFY(frame->bounds); QVERIFY(frame->bounds->valid());
    QCOMPARE(frame->paths[1]->origin, frame->paths[2]->origin);
    QCOMPARE(frame->bounds->origin, (std::array<double, 3>{22, 114, 100}));
    const auto startB = TrajectoryBuilder::spatialPosition(*frame->paths[2], 0);
    QVERIFY(startB[0] > 1000); QVERIFY(startB[1] > 1000); QCOMPARE(startB[2], -100.0);
    QVERIFY(frame->bounds->maximum[0] >= startB[0]); QVERIFY(frame->bounds->minimum[2] <= -140);
    TrajectoryCamera view;
    QVERIFY(QLineF(TrajectoryBuilder::project(*frame->bounds, {0, 0, 0}, view, {800, 600}),
        TrajectoryBuilder::project(*frame->bounds, startB, view, {800, 600})).length() > 50);
    QCOMPARE(TrajectoryBuilder::nearestSample(*frame->paths[1], .6).value(), qsizetype(1));
    QCOMPARE(TrajectoryBuilder::nearestSample(*frame->paths[2], .6).value(), qsizetype(1));
    TrajectoryItem item; item.setWidth(800); item.setHeight(600); item.setSources({bad, a, b});
    item.setTimeCursor(1, .6, 1, 0, 2); QTRY_VERIFY(!item.pending());
    QVERIFY(item.error().contains("Bad")); QCOMPARE(item.markers().size(), 6);
    QCOMPARE(item.markers()[2].toMap()["time"].toDouble(), 1.0);
    QCOMPARE(item.markers()[5].toMap()["time"].toDouble(), .5);
    a.visible = false; const auto hidden = TrajectoryBuilder::buildFrame({a, b});
    QCOMPARE(hidden->bounds->origin, frame->bounds->origin);
    QCOMPARE(hidden->bounds->minimum, hidden->paths[1]->minimum);
    const auto reused = TrajectoryBuilder::buildFrame({a, b}, nullptr, hidden.get());
    QCOMPARE(reused->paths[0], hidden->paths[0]); QCOMPARE(reused->paths[1], hidden->paths[1]);
    const auto removedOrigin = TrajectoryBuilder::buildFrame({b}, nullptr, hidden.get());
    QCOMPARE(removedOrigin->bounds->origin, (std::array<double, 3>{22.01, 114.01, 200}));
    QCOMPARE(TrajectoryBuilder::spatialPosition(*removedOrigin->paths[0], 0), (std::array<double, 3>{0, 0, 0}));
    item.setSources({a, b}); QTRY_VERIFY(!item.pending()); QCOMPARE(item.markers().size(), 3);
    b.visible = false; item.setSources({a, b}); QTRY_VERIFY(!item.pending()); QVERIFY(item.markers().isEmpty());
}
void TrajectoryTest::measurementAttitudeConventionsAndGaps()
{
    TrajectorySource source; source.attitude.mode = 1;
    const auto make = [&](double roll, double pitch, double yaw) {
        const auto input = axes({0, 1}, {roll, roll}, {pitch, pitch}, {yaw, yaw});
        source.attitudeSources = {input[0], input[1], input[2], {}};
        return TrajectoryBuilder::attitudeSample(*TrajectoryBuilder::buildAttitude(source), .5).value().bodyToNavigation;
    };
    const auto zero = make(0, 0, 0);
    QCOMPARE(zero.rotatedVector({1, 0, 0}), QVector3D(1, 0, 0));
    QCOMPARE(zero.rotatedVector({0, 1, 0}), QVector3D(0, 1, 0));
    QCOMPARE(zero.rotatedVector({0, 0, 1}), QVector3D(0, 0, 1));
    for (double angle : {-90., 90.}) {
        const float sign = angle > 0 ? 1 : -1;
        QVERIFY((make(angle, 0, 0).rotatedVector({0, 1, 0}) - QVector3D(0, 0, sign)).length() < 1e-5);
        QVERIFY((make(0, angle, 0).rotatedVector({1, 0, 0}) - QVector3D(0, 0, -sign)).length() < 1e-5);
        QVERIFY((make(0, 0, angle).rotatedVector({1, 0, 0}) - QVector3D(0, sign, 0)).length() < 1e-5);
    }
    const auto expected = QQuaternion::fromAxisAndAngle(0, 0, 1, 63) * QQuaternion::fromAxisAndAngle(0, 1, 0, -21) * QQuaternion::fromAxisAndAngle(1, 0, 0, 37);
    QVERIFY(std::abs(QQuaternion::dotProduct(make(37, -21, 63), expected)) > .99999);
    source.attitude.radians = true;
    QVERIFY(std::abs(QQuaternion::dotProduct(make(qDegreesToRadians(37.), qDegreesToRadians(-21.), qDegreesToRadians(63.)), expected)) > .99999);
    source.attitude.radians = false; source.attitude.order = 1;
    const auto xyz = QQuaternion::fromAxisAndAngle(1, 0, 0, 37) * QQuaternion::fromAxisAndAngle(0, 1, 0, -21) * QQuaternion::fromAxisAndAngle(0, 0, 1, 63);
    QVERIFY(std::abs(QQuaternion::dotProduct(make(37, -21, 63), xyz)) > .99999);
    source.attitude.navigationToBody = true;
    QVERIFY(std::abs(QQuaternion::dotProduct(make(37, -21, 63), xyz.conjugated())) > .99999);
    source.attitude.mode = 2;
    for (bool scalarLast : {false, true}) for (bool inverse : {false, true}) {
        source.attitude.scalarLast = scalarLast; source.attitude.navigationToBody = inverse;
        const auto q = inverse ? expected.conjugated() : expected;
        const QVector<double> values = scalarLast ? QVector<double>{q.x(), q.y(), q.z(), q.scalar()} : QVector<double>{q.scalar(), q.x(), q.y(), q.z()};
        PlotSeriesStore store; QVector<PlotSeriesInput> inputs;
        for (int a = 0; a < 4; ++a) inputs.append({a, {0, 1, 2, 3, 4}, {values[a] * 2, values[a] * 2, qQNaN(), values[a], values[a]}, Qt::red, 2});
        store.replaceSeries(inputs); const auto snapshots = store.snapshot({0, 1, 2, 3});
        for (int a = 0; a < 4; ++a) source.attitudeSources[a] = snapshots.series[a];
        const auto data = TrajectoryBuilder::buildAttitude(source);
        QVERIFY(TrajectoryBuilder::attitudeSample(*data, .7));
        QVERIFY(std::abs(QQuaternion::dotProduct(TrajectoryBuilder::attitudeSample(*data, .7)->bodyToNavigation, expected)) > .99999);
        QVERIFY(!TrajectoryBuilder::attitudeSample(*data, 2)); QVERIFY(!TrajectoryBuilder::attitudeSample(*data, 2.5));
        QVERIFY(!TrajectoryBuilder::attitudeSample(*data, -1));
    }
    source.attitude.mode = 1; source.attitude.order = 0; source.attitude.navigationToBody = false;
    const auto att = axes({0, .5, 1, 1.5, 2}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 30, qQNaN(), 90, 90});
    source.attitudeSources = {att[0], att[1], att[2], {}}; source.id = "aircraft"; source.name = "Aircraft"; source.color = Qt::magenta;
    source.axes = axes({0, 1, 2}, {0, 10, 20}, {0, 0, 0}, {0, 0, 0});
    TrajectoryItem item; item.setWidth(800); item.setHeight(600); item.setSources({source});
    item.setTimeCursor(1, .4, 0, 0, 2); QTRY_VERIFY(!item.pending());
    auto marker = item.markers().last().toMap(); QCOMPARE(marker["time"].toDouble(), 0.);
    QCOMPARE(marker["attitudeTime"].toDouble(), .5); QCOMPARE(marker["attitudeDelta"].toDouble(), .5);
    const auto qBefore = marker["attitude"]; item.presetView(3); QTRY_VERIFY(!item.pending());
    QCOMPARE(item.markers().last().toMap()["attitude"], qBefore); // camera never substitutes measurement
    item.setTimeCursor(1, 1, 0, 0, 2); QVERIFY(!item.markers().last().toMap().contains("attitude"));
    QVERIFY(item.attitudeStatus().contains(QStringLiteral("姿态缺口")));
    const auto bounds = TrajectoryBuilder::build(source.axes);
    TrajectoryCamera camera; camera.azimuth = camera.elevation = 0;
    const auto geometry = TrajectoryBuilder::attitudeGeometry(expected, *bounds, camera, {800, 600}, {200, 200}, Qt::red);
    camera.zoom = 100;
    const auto zoomed = TrajectoryBuilder::attitudeGeometry(expected, *bounds, camera, {800, 600}, {200, 200}, Qt::red);
    QCOMPARE(geometry.segments[0].vertices, zoomed.segments[0].vertices);
    auto wrong = std::make_shared<PlotSeriesData>(*source.attitudeSources[1]); wrong->timeOffset = .1; source.attitudeSources[1] = wrong;
    QVERIFY(TrajectoryBuilder::buildAttitude(source)->error.contains(QStringLiteral("时间基")));
}
void TrajectoryTest::multiControllerSessionRemapAndExport()
{
    QTemporaryDir dir; const auto left = dir.filePath("left.csv"), right = dir.filePath("right.csv");
    csv(left, "time,lat,lon,height,roll,pitch,yaw\n0,22,114,100,0,0,0\n1,22.001,114.001,110,0,0,90\n");
    csv(right, "time,lat,lon,height,roll,pitch,yaw\n0,22.01,114.01,200,0,0,0\n0.5,22.011,114.011,210,10,20,30\n1,22.012,114.012,220,0,0,90\n");
    AppController c; PlotItem time; TrajectoryItem item; item.setWidth(800); item.setHeight(600);
    c.attachPlot(&time, 0); c.attachTrajectory(&item, 0);
    QCOMPARE(c.loadFiles(QVariantList{left, right}), 2); QTRY_VERIFY(!c.loading()); c.enterTrajectoryMode(0);
    for (int i = 0; i < 6; ++i) c.selectSignal(i);
    QVERIFY(c.configureAttitude(0, 1, 3, 4, 5, -1, false, 0, false, false));
    QVERIFY(c.styleTrajectory(0, "Leader", Qt::magenta, 4, true)); QVERIFY(c.addTrajectory(0));
    for (int i = 6; i < 12; ++i) c.selectSignal(i);
    QCOMPARE(c.trajectoryState(0)["x"].toInt(), 6); QCOMPARE(c.trajectoryState(0)["z"].toInt(), 8);
    QVERIFY(c.configureAttitude(0, 1, 9, 10, 11, -1, false, 0, false, false));
    QVERIFY(c.styleTrajectory(0, "Wing", Qt::cyan, 3, true));
    const auto stableId = c.trajectoryState(0)["tracks"].toList()[1].toMap()["id"];
    time.setCursorMode(PlotItem::SingleCursor); time.setCursorPosition(.6, 1); QTRY_VERIFY(!item.pending());
    QCOMPARE(item.markers().size(), 6);
    const auto saved = dir.filePath("multi.disession"); QVERIFY(c.saveSession(saved));
    SessionDocument doc; QString error; QVERIFY2(readSessionDocument(saved, &doc, &error), qPrintable(error));
    QCOMPARE(doc.plots[0].trajectory.entries().size(), 2); QCOMPARE(doc.plots[0].trajectory.active, 1);
    QCOMPARE(doc.plots[0].trajectory.entries()[0].attitude.sources[0], 3);
    QVERIFY(c.addTrajectory(0)); QVERIFY(c.configureTrajectory(0, true, 0, 1, 2, true));
    QVERIFY(c.configureAttitude(0, 1, 3, 4, 5, -1, false, 0, false, false)); // duplicated sources must export once
    const auto exported = dir.filePath("multi.xlsx"); QVERIFY(c.exportXlsx(exported, AppController::PlottedSignals, false)); QTRY_VERIFY(!c.exporting());
    DataLoadWorker loader; QVector<LoadedTable> tables;
    connect(&loader, &DataLoadWorker::finished, this, [&](const QString &, const QVector<LoadedTable> &loaded, int, const QString &error) { QVERIFY2(error.isEmpty(), qPrintable(error)); tables = loaded; });
    loader.loadFile(exported); QCOMPARE(tables.size(), 2); QCOMPARE(tables[0].values.size(), 6); QCOMPARE(tables[1].values.size(), 6);
    AppController restored; QVERIFY(restored.restoreSession(saved)); QTRY_VERIFY(!restored.restoringSession());
    QCOMPARE(restored.trajectoryState(0)["tracks"].toList().size(), 2);
    QVERIFY(restored.selectTrajectory(0, 0)); QCOMPARE(restored.trajectoryState(0)["name"].toString(), QString("Leader"));
    QVERIFY(restored.removeFile(restored.signalModel()->groupAt(0)));
    QVERIFY(restored.selectTrajectory(0, 1)); QCOMPARE(restored.trajectoryState(0)["x"].toInt(), 0);
    QCOMPARE(restored.trajectoryState(0)["attitudeSources"].toList()[0].toInt(), 3);
    QCOMPARE(restored.trajectoryState(0)["tracks"].toList()[1].toMap()["id"], stableId);
    QVERIFY(restored.removeTrajectory(0, 0)); QCOMPARE(restored.trajectoryState(0)["active"].toInt(), 0);
    QCOMPARE(restored.trajectoryState(0)["name"].toString(), QString("Wing"));
    const auto base = sessionToJson(doc);
    const auto reject = [&](QJsonObject track) {
        auto json = base; auto plots = json["plots"].toArray(); auto plot = plots[0].toObject(); auto trajectory = plot["trajectory"].toObject();
        auto tracks = trajectory["tracks"].toArray(); tracks[0] = track; trajectory["tracks"] = tracks; plot["trajectory"] = trajectory; plots[0] = plot; json["plots"] = plots;
        SessionDocument parsed; QString reason; return !sessionFromJson(json, &parsed, &reason);
    };
    const auto first = base["plots"].toArray()[0].toObject()["trajectory"].toObject()["tracks"].toArray()[0].toObject();
    auto invalid = first; invalid["visible"] = 1; QVERIFY(reject(invalid));
    invalid = first; invalid["id"] = stableId.toString(); QVERIFY(reject(invalid));
    invalid = first; auto attitude = invalid["attitude"].toObject(); attitude["order"] = 9; invalid["attitude"] = attitude; QVERIFY(reject(invalid));
    invalid = first; invalid["geographic"] = false; QVERIFY(reject(invalid));
    // A malformed restore never changes the active session.
    auto broken = base; broken["version"] = 99; csv(dir.filePath("broken.disession"), QJsonDocument(broken).toJson());
    QVERIFY(!restored.restoreSession(dir.filePath("broken.disession"))); QCOMPARE(restored.signalCount(), 6);
    for (int version = 1; version <= 5; ++version) {
        auto legacy = base; legacy["version"] = version; SessionDocument parsed;
        QVERIFY2(sessionFromJson(legacy, &parsed, &error), qPrintable(error));
        QCOMPARE(parsed.plots[0].trajectory.entries().size(), 1);
        QCOMPARE(parsed.plots[0].trajectory.attitude.mode, 0);
    }
}
void TrajectoryTest::multiTrajectoryCost()
{
    QVector<double> time, x, y, z;
    for (int i = 0; i < 100000; ++i) { time.append(i * .01); x.append(22 + i * 1e-7); y.append(114 + std::sin(i * .0001) * .01); z.append(100 + std::cos(i * .001) * 10); }
    QVector<TrajectorySource> sources;
    for (int i = 0; i < 8; ++i) { TrajectorySource source; source.geographic = true; source.axes = axes(time, x, y, z); sources.append(source); }
    QElapsedTimer timer; timer.start(); const auto frame = TrajectoryBuilder::buildFrame(sources);
    qInfo() << "Eight geographic trajectories, 100000 samples each, preparation ms" << timer.elapsed();
    for (const auto &path : frame->paths) QVERIFY(path->valid());
    timer.restart(); qsizetype samples = 0;
    for (int turn = 0; turn < 20; ++turn) {
        TrajectoryCamera camera; camera.azimuth += turn * 3;
        for (const auto &path : frame->paths) samples += TrajectoryBuilder::preview(*path, camera, {1200, 800}, 2, Qt::gray, nullptr, true, frame->bounds.get(), false).projectedSamples;
    }
    qInfo() << "Eight trajectories, 20 interactive frames ms" << timer.elapsed() << "projected samples" << samples;
    QVERIFY(samples > 0); // measurement, no unsubstantiated fps promise
}
QTEST_MAIN(TrajectoryTest)
#include "trajectory_test.moc"
