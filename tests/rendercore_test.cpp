#include "render/plotseriesstore.h"
#include "render/plotlodbuilder.h"
#include "render/plotgeometrybuilder.h"

#include <QtTest>

class RenderCoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void geometryWidthIsIndependentOfSlopeAndSampleSpacing();
    void binaryCursorMatchesOriginalOrderScan();
    void storeGenerationAndSnapshotsAreImmutable();
    void storeQueriesRawSamplesWithoutInterpolation();
    void storeSkipsUnknownIdsAndIgnoresInvalidBounds();
    void lodKeepsBothNeighborsAcrossNarrowViewport();
    void lodPreservesSampleOrderForBucketExtrema();
    void lodKeepsRepeatedTimesAndSplitsAtNan();
    void lodFiltersNonMonotonicSeriesToViewportInInputOrder();
    void lodRejectsInvalidRequests();
    void lodCacheReusesOnlyAnExactRequestKey();
    void geometryBuildsClippedTriangleStrip();
    void geometryOmitsUndrawableSegments();
    void geometryKeepsLodSegmentsIndependent();
    void geometryLineWidthChangesScreenSpaceExpansion();
    void geometryRejectsInvalidViewTransforms();
    void geometryUsesIndependentSeriesWidthsAndStyles();
    void appendSeriesKeepsExistingSnapshots();
    void geometryKeepsFullWidthForOffscreenDiagonalEntry();
    void storeAcceptsWorkerPreparedPoints();
    void storeKeepsLazyColumnSeriesUntilLodBuild();
    void geometryClipsStaleLodFromWiderViewport();
    void geometrySplitsOnExplodedMappedSamples();
    void geometryKeepsStrokeWhenASampleRepeats();
    void geometryDoesNotFillViewportWhenStaleLodClipsToEdges();
};

void RenderCoreTest::geometryWidthIsIndependentOfSlopeAndSampleSpacing()
{
    const GeometryRequest request{{0, 100, 0, 100, 100, 100}, 6};
    for (int degrees : {0, 15, 45, 75, 90}) {
        const double angle = qDegreesToRadians(double(degrees));
        const QPointF direction(qCos(angle), qSin(angle));
        LodResult lod;
        lod.segments.append({1, QColor("red"), {QPointF(50, 50) - direction * 20,
                                                QPointF(50, 50) + direction * 20}});
        const auto geometry = PlotGeometryBuilder::build(lod, request);
        QCOMPARE(geometry.segments.size(), 1);
        const auto &v = geometry.segments.first().vertices;
        QVERIFY(qAbs(QLineF(v[0], v[1]).length() - 6) < 1e-10);
    }
    LodResult corner;
    corner.segments.append({1, QColor("red"), {{10, 50}, {20, 50}, {20, 10}}});
    const auto geometry = PlotGeometryBuilder::build(corner, request);
    const auto &v = geometry.segments.first().vertices;
    QVERIFY(geometry.segments.first().triangleList);
    QVERIFY(v.size() >= 12);
    QCOMPARE(QLineF(v[0],v[1]).length(), 6.0);
    QCOMPARE(QLineF(v[6],v[7]).length(), 6.0);

    LodResult zigzag;
    zigzag.segments.append({1, QColor("red"), {{10,10},{12,90},{14,10},{16,90},{18,10}}});
    const auto sharp = PlotGeometryBuilder::build(zigzag, request);
    const auto &q = sharp.segments.first().vertices;
    QVERIFY(sharp.segments.first().triangleList);
    for (int i=0; i<4; ++i) {
        QVERIFY(qAbs(QLineF(q[6*i],q[6*i+1]).length()-6) < 1e-10);
        QVERIFY(qAbs(QLineF(q[6*i+2],q[6*i+4]).length()-6) < 1e-10);
    }
}

void RenderCoreTest::binaryCursorMatchesOriginalOrderScan()
{
    for (bool allMissing : {false, true}) {
        PlotSeriesInput input{1, {0, 0, 1, 2, 2, 2, 4, 5},
            {qQNaN(), 3, qQNaN(), 7, 8, qQNaN(), 9, qQNaN()}, QColor("red")};
        if (allMissing) input.values.fill(qQNaN());
        PlotSeriesStore fast, reference;
        fast.replaceSeries({input});
        input.monotonicTimeKnown = true;
        input.monotonicTime = false;
        reference.replaceSeries({input});
        for (double target = -1; target <= 6; target += 0.25) {
            const auto a = fast.snapshot({1}), b = reference.snapshot({1});
            QCOMPARE(PlotSeriesStore::nearestX(a, target), PlotSeriesStore::nearestX(b, target));
            const auto actual = PlotSeriesStore::nearestSamples(a, target);
            const auto expected = PlotSeriesStore::nearestSamples(b, target);
            QCOMPARE(actual.size(), expected.size());
            if (!actual.isEmpty()) {
                QCOMPARE(actual.first().x, expected.first().x);
                QCOMPARE(actual.first().y, expected.first().y);
            }
        }
    }
}

void RenderCoreTest::storeGenerationAndSnapshotsAreImmutable()
{
    PlotSeriesStore store;
    store.replaceSeries({{4, {0.0, 1.0}, {10.0, 11.0}, QColor("red")}});
    const PlotSeriesSnapshot oldSnapshot = store.snapshot({4});
    QCOMPARE(oldSnapshot.generation, quint64(1));
    QCOMPARE(oldSnapshot.series.size(), 1);
    QCOMPARE(oldSnapshot.series.at(0)->pointAt(1), QPointF(1.0, 11.0));

    store.replaceSeries({{4, {0.0, 1.0}, {20.0, 21.0}, QColor("blue")}});
    QCOMPARE(store.generation(), quint64(2));
    QCOMPARE(oldSnapshot.series.at(0)->pointAt(1), QPointF(1.0, 11.0));
    QCOMPARE(store.snapshot({4}).series.at(0)->pointAt(1), QPointF(1.0, 21.0));

    store.updateSeriesPen(4, QColor("green"), 7.0, Qt::DotLine);
    const PlotSeriesSnapshot styledSnapshot = store.snapshot({4});
    QCOMPARE(store.generation(), quint64(3));
    QCOMPARE(styledSnapshot.series.at(0)->color, QColor("green"));
    QCOMPARE(styledSnapshot.series.at(0)->lineWidth, 7.0);
    QCOMPARE(styledSnapshot.series.at(0)->lineStyle, Qt::DotLine);
    QCOMPARE(oldSnapshot.series.at(0)->color, QColor("red"));
    QCOMPARE(oldSnapshot.series.at(0)->lineWidth, 2.0);
    QCOMPARE(oldSnapshot.series.at(0)->lineStyle, Qt::SolidLine);

    store.clear();
    QCOMPARE(store.generation(), quint64(4));
    QVERIFY(store.snapshot({4}).series.isEmpty());
}

void RenderCoreTest::storeQueriesRawSamplesWithoutInterpolation()
{
    PlotSeriesStore store;
    store.replaceSeries({
        {1, {0.0, 2.0}, {10.0, 20.0}, QColor("red")},
        {2, {2.0, 0.0}, {40.0, 30.0}, QColor("blue")}
    });
    const PlotSeriesSnapshot snapshot = store.snapshot({2, 1});
    QCOMPARE(snapshot.orderedIds, QVector<PlotSeriesId>({2, 1}));
    QVERIFY(!snapshot.series.at(0)->monotonicTime);
    QVERIFY(snapshot.series.at(1)->monotonicTime);

    const auto nearest = PlotSeriesStore::nearestX(snapshot, 1.0);
    QVERIFY(nearest.has_value());
    QCOMPARE(*nearest, 2.0);

    const QVector<PlotSample> samples = PlotSeriesStore::nearestSamples(snapshot, 1.0);
    QCOMPARE(samples.size(), 2);
    QCOMPARE(samples.at(0).id, 2);
    QCOMPARE(samples.at(0).y, 40.0);
    QCOMPARE(samples.at(1).id, 1);
    QCOMPARE(samples.at(1).y, 10.0);

    const auto bounds = PlotSeriesStore::bounds(snapshot);
    QVERIFY(bounds.has_value());
    QCOMPARE(bounds->xMinimum, 0.0);
    QCOMPARE(bounds->xMaximum, 2.0);
    QCOMPARE(bounds->yMinimum, 10.0);
    QCOMPARE(bounds->yMaximum, 40.0);
}

void RenderCoreTest::storeSkipsUnknownIdsAndIgnoresInvalidBounds()
{
    PlotSeriesStore store;
    store.replaceSeries({{
        8,
        {0.0, 1.0, 2.0},
        {qQNaN(), 7.0, qQNaN()},
        QColor("purple")
    }});

    const PlotSeriesSnapshot snapshot = store.snapshot({99, 8, 8});
    QCOMPARE(snapshot.orderedIds, QVector<PlotSeriesId>({8, 8}));
    QCOMPARE(snapshot.series.size(), 2);
    QVERIFY(!PlotSeriesStore::bounds(store.snapshot({99})).has_value());

    const QVector<PlotSample> samples = PlotSeriesStore::nearestSamples(snapshot, 0.0);
    QCOMPARE(samples.size(), 2);
    QCOMPARE(samples.at(0).x, 1.0);
    QCOMPARE(samples.at(0).y, 7.0);
    QCOMPARE(samples.at(1).x, 1.0);
    QCOMPARE(samples.at(1).y, 7.0);
}

void RenderCoreTest::lodKeepsBothNeighborsAcrossNarrowViewport()
{
    PlotSeriesStore store;
    store.replaceSeries({{7, {0.0, 1.0}, {0.0, 1.0}, QColor("red")}});
    const auto snapshot = store.snapshot({7});
    const LodRequestKey key{snapshot.generation, {7}, 0.4, 0.6, 800, 1};

    const LodResult result = PlotLodBuilder::build(snapshot, key);
    QCOMPARE(result.segments.size(), 1);
    QCOMPARE(result.segments.at(0).points,
             QVector<QPointF>({QPointF(0.0, 0.0), QPointF(1.0, 1.0)}));
}

void RenderCoreTest::lodPreservesSampleOrderForBucketExtrema()
{
    PlotSeriesStore store;
    store.replaceSeries({{3, {0.0, 0.1, 0.2, 0.3},
                          {5.0, 9.0, 1.0, 6.0}, QColor("green")}});
    const auto snapshot = store.snapshot({3});
    const auto result = PlotLodBuilder::build(
        snapshot, {snapshot.generation, {3}, 0.0, 0.3, 1, 1});
    QCOMPARE(result.segments.size(), 1);
    QCOMPARE(result.segments.at(0).points,
             QVector<QPointF>({QPointF(0.1, 9.0), QPointF(0.2, 1.0)}));
}

void RenderCoreTest::lodKeepsRepeatedTimesAndSplitsAtNan()
{
    PlotSeriesStore store;
    store.replaceSeries({{5, {0.0, 0.0, 1.0, 2.0},
                          {1.0, 2.0, qQNaN(), 4.0}, QColor("cyan")}});
    const auto snapshot = store.snapshot({5});
    const auto result = PlotLodBuilder::build(
        snapshot, {snapshot.generation, {5}, 0.0, 2.0, 16, 1});
    QCOMPARE(result.segments.size(), 2);
    QCOMPARE(result.segments.at(0).points.size(), 2);
    QCOMPARE(result.segments.at(0).points.at(0), QPointF(0.0, 1.0));
    QCOMPARE(result.segments.at(0).points.at(1), QPointF(0.0, 2.0));
    QCOMPARE(result.segments.at(1).points, QVector<QPointF>({QPointF(2.0, 4.0)}));
}

void RenderCoreTest::lodFiltersNonMonotonicSeriesToViewportInInputOrder()
{
    PlotSeriesStore store;
    store.replaceSeries({{9, {2.0, 0.5, 1.5, -1.0},
                          {20.0, 5.0, 15.0, -10.0}, QColor("yellow")}});
    const auto snapshot = store.snapshot({9});
    const auto result = PlotLodBuilder::build(
        snapshot, {snapshot.generation, {9}, 0.0, 1.0, 10, 1});
    QCOMPARE(result.segments.size(), 1);
    QCOMPARE(result.segments.at(0).points,
             QVector<QPointF>({QPointF(0.5, 5.0)}));
}

void RenderCoreTest::lodRejectsInvalidRequests()
{
    PlotSeriesStore store;
    store.replaceSeries({{1, {0.0, 1.0}, {0.0, 1.0}, QColor("red")}});
    const auto snapshot = store.snapshot({1});
    QVERIFY(PlotLodBuilder::build(
        snapshot, {snapshot.generation, {1}, 1.0, 1.0, 10, 1}).segments.isEmpty());
    QVERIFY(PlotLodBuilder::build(
        snapshot, {snapshot.generation, {1}, qQNaN(), 1.0, 10, 1}).segments.isEmpty());
    QVERIFY(PlotLodBuilder::build(
        snapshot, {snapshot.generation, {1}, 0.0, 1.0, 0, 1}).segments.isEmpty());
}

void RenderCoreTest::lodCacheReusesOnlyAnExactRequestKey()
{
    PlotSeriesStore store;
    store.replaceSeries({{1, {0.0, 1.0}, {0.0, 1.0}, QColor("red")}});
    const auto snapshot = store.snapshot({1});
    PlotLodCache cache;
    LodRequestKey key{snapshot.generation, {1}, 0.0, 1.0, 100, 1};

    cache.resolve(snapshot, key);
    cache.resolve(snapshot, key);
    QCOMPARE(cache.rebuildCount(), quint64(1));

    key.bucketCount = 101;
    cache.resolve(snapshot, key);
    QCOMPARE(cache.rebuildCount(), quint64(2));
    cache.clear();
    cache.resolve(snapshot, key);
    QCOMPARE(cache.rebuildCount(), quint64(3));
}

void RenderCoreTest::geometryBuildsClippedTriangleStrip()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"), {{-1.0, 0.0}, {2.0, 1.0}}});
    const GeometryRequest request{PlotViewTransform{0.0, 1.0, 0.0, 1.0,
                                                    100.0, 50.0}, 4.0};
    const GeometryResult result = PlotGeometryBuilder::build(lod, request);

    QCOMPARE(result.segments.size(), 1);
    QCOMPARE(result.segments.at(0).vertices.size(), 6);
    for (const QPointF &vertex : result.segments.at(0).vertices) {
        QVERIFY(qIsFinite(vertex.x()));
        QVERIFY(qIsFinite(vertex.y()));
    }
}

void RenderCoreTest::geometryOmitsUndrawableSegments()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"), {{0.0, 0.0}}});
    lod.segments.append({2, QColor("blue"), {{0.5, 0.5}, {0.5, 0.5}}});
    const auto result = PlotGeometryBuilder::build(
        lod, GeometryRequest{PlotViewTransform{0.0, 1.0, 0.0, 1.0,
                                               100.0, 100.0}, 2.0});
    QVERIFY(result.segments.isEmpty());
}

void RenderCoreTest::geometryKeepsLodSegmentsIndependent()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"), {{0.0, 0.0}, {0.5, 0.5}}});
    lod.segments.append({1, QColor("red"), {{0.6, 0.6}, {1.0, 1.0}}});
    const auto result = PlotGeometryBuilder::build(
        lod, GeometryRequest{PlotViewTransform{0.0, 1.0, 0.0, 1.0,
                                               100.0, 100.0}, 2.0});
    QCOMPARE(result.segments.size(), 2);
    QCOMPARE(result.segments.at(0).vertices.size(), 6);
    QCOMPARE(result.segments.at(1).vertices.size(), 6);
}

void RenderCoreTest::geometryLineWidthChangesScreenSpaceExpansion()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"), {{0.0, 0.5}, {1.0, 0.5}}});
    const PlotViewTransform transform{0.0, 1.0, 0.0, 1.0, 100.0, 100.0};
    const auto thin = PlotGeometryBuilder::build(
        lod, GeometryRequest{transform, 2.0});
    const auto thick = PlotGeometryBuilder::build(
        lod, GeometryRequest{transform, 8.0});

    const double thinSpan = qAbs(thin.segments.at(0).vertices.at(0).y()
                                 - thin.segments.at(0).vertices.at(1).y());
    const double thickSpan = qAbs(thick.segments.at(0).vertices.at(0).y()
                                  - thick.segments.at(0).vertices.at(1).y());
    QCOMPARE(thinSpan, 2.0);
    QCOMPARE(thickSpan, 8.0);
}

void RenderCoreTest::geometryRejectsInvalidViewTransforms()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"), {{0.0, 0.0}, {1.0, 1.0}}});
    QVERIFY(PlotGeometryBuilder::build(
        lod, GeometryRequest{PlotViewTransform{1.0, 1.0, 0.0, 1.0,
                                               100.0, 100.0}, 2.0})
                .segments.isEmpty());
    QVERIFY(PlotGeometryBuilder::build(
        lod, GeometryRequest{PlotViewTransform{0.0, 1.0, 0.0, 1.0,
                                               0.0, 100.0}, 2.0})
                .segments.isEmpty());
}

void RenderCoreTest::geometryUsesIndependentSeriesWidthsAndStyles()
{
    PlotSeriesStore store;
    store.replaceSeries({
        {1, {0.0, 10.0}, {0.0, 10.0}, QColor("red"), 2.0, Qt::SolidLine},
        {2, {0.0, 10.0}, {10.0, 0.0}, QColor("blue"), 8.0, Qt::DashLine}
    });
    const PlotSeriesSnapshot snapshot = store.snapshot({1, 2});
    const LodRequestKey key{store.generation(), {1, 2}, 0.0, 10.0, 100, 1};
    const LodResult lod = PlotLodBuilder::build(snapshot, key);
    const GeometryResult geometry = PlotGeometryBuilder::build(
        lod, {{0.0, 10.0, 0.0, 10.0, 100.0, 100.0}, 1.0});

    QCOMPARE(geometry.segments.size(), 2);
    QCOMPARE(geometry.segments.first().seriesId, 1);
    QVERIFY(geometry.segments.first().triangleList);
    const qreal solidThickness = QLineF(geometry.segments.first().vertices.at(0),
                                       geometry.segments.first().vertices.at(1)).length();
    QVERIFY(solidThickness > 1.0 && solidThickness < 3.0);

    int dashedSegments = 0;
    for (const GeometrySegment &segment : geometry.segments) {
        if (segment.seriesId != 2)
            continue;
        ++dashedSegments;
        QVERIFY(segment.triangleList);
        QVERIFY(segment.vertices.size() > 6);
        qreal maximumThickness = 0.0;
        for (int index = 0; index + 5 < segment.vertices.size(); index += 6)
            maximumThickness = qMax(maximumThickness,
                QLineF(segment.vertices.at(index),
                       segment.vertices.at(index + 1)).length());
        QVERIFY(maximumThickness > 7.0 && maximumThickness < 9.0);
    }
    QCOMPARE(dashedSegments, 1);
}

void RenderCoreTest::appendSeriesKeepsExistingSnapshots()
{
    PlotSeriesStore store;
    store.replaceSeries({{0, {0.0, 1.0}, {1.0, 2.0}, QColor("red")}});
    const PlotSeriesDataPtr original = store.snapshot({0}).series.first();

    store.appendSeries({{1, {0.0, 1.0}, {3.0, 4.0}, QColor("blue")}});

    const PlotSeriesSnapshot snapshot = store.snapshot({0, 1});
    QCOMPARE(snapshot.series.size(), 2);
    QCOMPARE(snapshot.series.at(0), original);
    QCOMPARE(snapshot.series.at(1)->id, 1);
}

void RenderCoreTest::geometryKeepsFullWidthForOffscreenDiagonalEntry()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"),
                         {{-1.0, -1.0}, {0.5, 0.5}, {2.0, 2.0}}});
    const auto result = PlotGeometryBuilder::build(
        lod, GeometryRequest{{0.0, 1.0, 0.0, 1.0, 100.0, 100.0}, 4.0});

    QCOMPARE(result.segments.size(), 1);
    const auto &vertices = result.segments.first().vertices;
    QCOMPARE(vertices.size(), 12);
    QCOMPARE(QLineF(vertices.at(0), vertices.at(1)).length(), 4.0);
    QCOMPARE(QLineF(vertices.at(6), vertices.at(7)).length(), 4.0);
}

void RenderCoreTest::storeAcceptsWorkerPreparedPoints()
{
    PlotSeriesInput input;
    input.id = 8;
    input.color = QColor("magenta");
    input.points = {{0.0, 2.0}, {1.0, 4.0}};
    input.monotonicTime = true;

    PlotSeriesStore store;
    store.appendSeries({input});

    const PlotSeriesSnapshot snapshot = store.snapshot({8});
    QCOMPARE(snapshot.series.size(), 1);
    QCOMPARE(snapshot.series.first()->points,
             QVector<QPointF>({{0.0, 2.0}, {1.0, 4.0}}));
    QVERIFY(snapshot.series.first()->monotonicTime);
}

void RenderCoreTest::storeKeepsLazyColumnSeriesUntilLodBuild()
{
    PlotSeriesInput input;
    input.id = 9;
    input.color = QColor("cyan");
    input.time = {0.0, 1.0, 2.0, 3.0};
    input.values = {10.0, 11.0, 12.0, 13.0};
    input.monotonicTime = true;

    PlotSeriesStore store;
    store.appendSeries({input});
    const PlotSeriesSnapshot snapshot = store.snapshot({9});
    QVERIFY(snapshot.series.first()->points.isEmpty());

    const LodResult lod = PlotLodBuilder::build(
        snapshot, LodRequestKey{snapshot.generation, {9}, 1.0, 2.0, 64, 1});
    QCOMPARE(lod.segments.size(), 1);
    QCOMPARE(lod.segments.first().points,
             QVector<QPointF>({{0.0, 10.0}, {1.0, 11.0},
                               {2.0, 12.0}, {3.0, 13.0}}));
}

void RenderCoreTest::geometryClipsStaleLodFromWiderViewport()
{
    LodResult lod;
    QVector<QPointF> points;
    for (int i = 0; i < 64; ++i)
        points.append({i / 63.0, (i % 2) ? 1.0 : 0.0});
    lod.segments.append({1, QColor("red"), points});
    const auto result = PlotGeometryBuilder::build(
        lod, GeometryRequest{{0.49, 0.51, 0.0, 1.0, 800.0, 400.0}, 2.0});
    QVERIFY(!result.segments.isEmpty());
    for (const GeometrySegment &segment : result.segments) {
        for (const QPointF &vertex : segment.vertices) {
            QVERIFY(qIsFinite(vertex.x()));
            QVERIFY(qIsFinite(vertex.y()));
            QVERIFY(vertex.x() > -64.0 && vertex.x() < 864.0);
            QVERIFY(vertex.y() > -64.0 && vertex.y() < 464.0);
        }
    }
}

void RenderCoreTest::geometrySplitsOnExplodedMappedSamples()
{
    LodResult lod;
    lod.segments.append({1, QColor("blue"),
                         {{0.2, 0.5}, {1e20, 0.5}, {0.8, 0.5}}});
    const auto result = PlotGeometryBuilder::build(
        lod, GeometryRequest{{0.0, 1.0, 0.0, 1.0, 100.0, 100.0}, 2.0});
    QVERIFY(result.segments.isEmpty() || result.segments.size() >= 1);
    for (const GeometrySegment &segment : result.segments) {
        for (const QPointF &vertex : segment.vertices) {
            QVERIFY(qIsFinite(vertex.x()));
            QVERIFY(qIsFinite(vertex.y()));
            QVERIFY(qAbs(vertex.x()) < 1e5);
            QVERIFY(qAbs(vertex.y()) < 1e5);
        }
    }
}

void RenderCoreTest::geometryKeepsStrokeWhenASampleRepeats()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"), {
        {10.0, 10.0}, {10.0, 10.0}, {10.0, 90.0}, {12.0, 90.0}, {12.0, 10.0}
    }});
    const auto result = PlotGeometryBuilder::build(
        lod, GeometryRequest{{0.0, 100.0, 0.0, 100.0, 100.0, 100.0}, 2.0});
    QCOMPARE(result.segments.size(), 1);
    QVERIFY(result.segments.first().vertices.size() >= 12);
}

void RenderCoreTest::geometryDoesNotFillViewportWhenStaleLodClipsToEdges()
{
    LodResult lod;
    QVector<QPointF> points;
    for (int i = 0; i < 80; ++i)
        points.append({i / 79.0, (i % 2) ? 50.0 : -50.0});
    lod.segments.append({1, QColor("red"), points});
    const auto result = PlotGeometryBuilder::build(
        lod, GeometryRequest{{0.45, 0.55, 0.0, 1.0, 200.0, 100.0}, 2.0});
    QVERIFY(!result.segments.isEmpty());
    double maximumArea = 0.0;
    const auto triangleArea = [](QPointF a, QPointF b, QPointF c) {
        return qAbs((b.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (b.y() - a.y())) * 0.5;
    };
    for (const GeometrySegment &segment : result.segments) {
        const auto &v = segment.vertices;
        if (segment.triangleList) {
            for (int i = 0; i + 2 < v.size(); i += 3)
                maximumArea = qMax(maximumArea, triangleArea(v[i], v[i + 1], v[i + 2]));
        } else {
            for (int i = 0; i + 3 < v.size(); i += 2) {
                maximumArea = qMax(maximumArea, triangleArea(v[i], v[i + 1], v[i + 2]));
                maximumArea = qMax(maximumArea, triangleArea(v[i + 1], v[i + 3], v[i + 2]));
            }
        }
    }
    QVERIFY2(maximumArea < 400.0, qPrintable(QString::number(maximumArea)));
}

QTEST_GUILESS_MAIN(RenderCoreTest)
#include "rendercore_test.moc"
