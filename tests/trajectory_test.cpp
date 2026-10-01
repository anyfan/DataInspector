#include "appcontroller.h"
#include "plotitem.h"
#include "trajectoryitem.h"
#include "qmltypes.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QMouseEvent>

class TrajectoryTest : public QObject {
    Q_OBJECT
private slots:
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
    void treeSelectionKeepsTimeBindings();
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
    QVERIFY2(std::abs(x.x() + y.y()) < 1e-8, qPrintable(QString("x=%1 y=%2 spanX=%3 spanY=%4").arg(x.x(),0,'g',17).arg(y.y(),0,'g',17).arg(data->maximum[0]-data->minimum[0]).arg(data->maximum[1]-data->minimum[1]))); QVERIFY(x.x() > 100);
    QVERIFY(std::abs(x.y()) < 1e-8 && std::abs(y.x()) < 1e-8);
    TrajectoryCamera front; front.azimuth = 0; front.elevation = 0;
    const auto frontOrigin = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 0), front, {400, 300});
    const auto frontX = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 1), front, {400, 300}) - frontOrigin;
    const auto frontY = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 2), front, {400, 300}) - frontOrigin;
    const auto frontZ = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 3), front, {400, 300}) - frontOrigin;
    QVERIFY(frontX.x() > 0 && std::abs(frontX.y()) < 1e-8);
    QVERIFY(std::abs(frontY.x()) < 1e-8 && std::abs(frontY.y()) < 1e-8);
    QVERIFY(frontZ.y() < 0 && std::abs(frontZ.x()) < 1e-8);
    TrajectoryCamera side = front; side.azimuth = 90;
    const auto sideOrigin = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 0), side, {400, 300});
    const auto sideY = TrajectoryBuilder::project(*data, TrajectoryBuilder::position(*data, 2), side, {400, 300}) - sideOrigin;
    QVERIFY(sideY.x() > 0 && std::abs(sideY.y()) < 1e-8);
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
    QVERIFY(std::abs(east[0] - 111.31949078762194) < 1e-6);
    QVERIFY(std::abs(east[1]) < 1e-6);
    QVERIFY(std::abs(north[1] - 110.57427581609332) < 1e-6);
    QCOMPARE(north[2], 50.0);
    QCOMPARE(TrajectoryBuilder::position(*data, 3)[0], .001); // source latitude stays unmodified
    const auto across = TrajectoryBuilder::build(axes({0, 1}, {0, 0}, {179.999, -179.999}, {50, 50}), nullptr, true);
    QVERIFY(across->valid());
    QVERIFY(std::abs(TrajectoryBuilder::spatialPosition(*across, 1)[0] - 222.6389815) < .001);
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
    QCOMPARE(marker["rawX"].toDouble(), .001); QCOMPARE(marker["spatialZ"].toDouble(), 50.0);
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
        QVERIFY(last.x() > first.x()); QVERIFY(last.y() < first.y());
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
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(200, 150), QPointF(200, 150), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    item.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove, QPointF(100, 150), QPointF(100, 150), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
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
    const auto turn = QQuaternion::fromAxisAndAngle(QVector3D(0, -1, 0), 40);
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
    const auto pitch = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 40);
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
void TrajectoryTest::treeSelectionKeepsTimeBindings()
{
    QTemporaryDir dir; csv(dir.filePath("signals.csv"), "t,x,y,z,w\n0,0,1,2,3\n1,10,11,12,13\n");
    AppController c; QVERIFY(c.loadCsv(dir.filePath("signals.csv"))); QTRY_VERIFY(!c.loading());
    c.setLayout(1, 2); c.selectSignal(3);
    c.enterTrajectoryMode(1); QCOMPARE(c.activePlotIndex(), 1);
    QCOMPARE(c.signalModel()->checkedCount(), 0);
    c.toggleSignal(0); c.selectSignal(1); c.toggleSignal(2); c.selectSignal(3);
    QCOMPARE(c.signalModel()->checkedCount(), 4); QCOMPARE(c.trajectorySignalOptions(1).size(), 5);
    QCOMPARE(c.trajectoryState(1)["x"].toInt(), -1); QCOMPARE(c.trajectoryState(1)["z"].toInt(), -1);
    QVERIFY(c.bindTrajectoryAxis(1, 0, 0)); QVERIFY(c.bindTrajectoryAxis(1, 1, 1));
    QVERIFY(c.trajectoryState(1)["planar"].toBool());
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
    QVERIFY(equator->valid()); QVERIFY(equator->maximum[0] > 1000); // one zero coordinate is a legitimate fix
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
    c.toggleSignal(0); QCOMPARE(c.trajectoryState(0)["x"].toInt(), -1);
    c.toggleSignal(1); QVERIFY(c.bindTrajectoryAxis(0, 0, 0)); QVERIFY(c.bindTrajectoryAxis(0, 1, 1));
    QVERIFY(c.trajectoryState(0)["planar"].toBool());
    QCOMPARE(c.signalModel()->checkedCount(), 2);
    c.selectSignal(2); QCOMPARE(c.trajectoryState(0)["z"].toInt(), -1);
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
    QVERIFY(item->height() >= 480 - 50); QVERIFY(item->width() >= 640 - 12);
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
    const QPoint axesStart = item->mapToScene(item->orientationRect().center()).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, axesStart);
    QTest::mouseMove(&window, axesStart + QPoint(30, -10));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, axesStart + QPoint(30, -10));
    QVERIFY(std::abs(QQuaternion::dotProduct(item->camera().orientation(), beforeAxesDrag.orientation())) < .999f);
    QTRY_VERIFY(!item->pending());
    const auto beforePan = item->camera();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(&window, dragStart + QPoint(20, 0));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, dragStart + QPoint(20, 0));
    QCOMPARE(item->camera().rotation, beforePan.rotation);
    QVERIFY(std::abs(item->camera().panX - beforePan.panX - 20 / item->width()) < 1e-6);
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
    QTRY_VERIFY(!item->pending()); QVERIFY(item->height() >= 190); QVERIFY(item->width() >= 308);
    if (QQuickWindow::graphicsApi() != QSGRendererInterface::Software && QGuiApplication::platformName() != "offscreen") {
        QTest::qWait(100);
        QVERIFY(window.grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath("trajectory-planar-small-ui.png")));
    }
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
QTEST_MAIN(TrajectoryTest)
#include "trajectory_test.moc"
