#include "render/plotseriesstore.h"
#include "render/plotaxisutils.h"
#include "render/plotlodbuilder.h"
#include "render/plotgeometrybuilder.h"

#include <QtTest>

class RenderCoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void indexedMeanAbsoluteMatchesRawSamples();
    void geometryWidthIsIndependentOfSlopeAndSampleSpacing();
    void binaryCursorMatchesOriginalOrderScan();
    void storeGenerationAndSnapshotsAreImmutable();
    void unchangedPensPreserveSnapshots();
    void indexedTimeBoundsIncludeMissingValues();
    void lodHandlesExtremeBucketArithmetic();
    void paddedRangesRemainFinite();
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
    void indexedBoundsMatchRawScan();
    void indexedBoundsAvoidMillionPointScan();
    void snapshotIndexSurvivesMutations();
    void storeDetectsUnsortedPointInput();
    void unchangedTimeOffsetsPreserveSnapshots();
    void storeKeepsLazyColumnSeriesUntilLodBuild();
    void geometryClipsStaleLodFromWiderViewport();
    void geometrySplitsOnExplodedMappedSamples();
    void geometryKeepsStrokeWhenASampleRepeats();
    void geometryDoesNotFillViewportWhenStaleLodClipsToEdges();
};

void RenderCoreTest::indexedMeanAbsoluteMatchesRawSamples()
{
    for (bool monotonic : {true, false}) {
        PlotSeriesInput input;
        input.id = 1;
        input.timeOffset = 10;
        for (int i = 0; i < 4096; ++i) {
            input.time.append(monotonic ? i : (i * 137) % 4096);
            input.values.append(i % 19 == 0 ? std::numeric_limits<double>::quiet_NaN()
                                           : (i % 2 ? -2.0 : 4.0));
        }
        PlotSeriesStore store;
        store.replaceSeries({input});
        const auto series = store.snapshot({1}).series.first();
        for (const auto range : {qMakePair(10.0, 4105.0), qMakePair(611.0, 3311.0)}) {
            double expected = 0;
            int count = 0;
            for (int i = 0; i < series->sampleCount(); ++i) {
                const auto p = series->pointAt(i);
                if (p.x() >= range.first && p.x() <= range.second && qIsFinite(p.y())) {
                    expected += qAbs(p.y());
                    ++count;
                }
            }
            double actual = 0;
            QVERIFY(series->rangeIndex->bounds(*series, range.first, range.second, nullptr, &actual));
            QVERIFY(qAbs(actual - expected / count) < 1e-12);
        }
    }
}

void RenderCoreTest::indexedBoundsMatchRawScan()
{
    for (bool monotonic : {true, false}) {
        PlotSeriesInput input;
        input.id = 7;
        for (int i = 0; i < 4097; ++i) {
            input.time.append(monotonic ? i / 3 : (i * 137) % 4097);
            input.values.append(i % 17 == 0 ? qQNaN() : qSin(i * .1) * i);
        }
        PlotSeriesStore store;
        store.replaceSeries({input});
        const auto original = store.snapshot({7});
        QVERIFY(store.addTimeOffset({7}, 12.5));
        const auto shifted = store.snapshot({7});
        QCOMPARE(original.series[0]->rangeIndex, shifted.series[0]->rangeIndex);
        for (const auto &snapshot : {original, shifted}) {
            for (int range = -1; range < 40; ++range) {
                const double lo = range * 99.75;
                const double hi = lo + (range % 3 == 0 ? 0 : 1280.25);
                std::optional<PlotBounds> expected;
                for (qsizetype i = 0; i < snapshot.series[0]->sampleCount(); ++i) {
                    const auto p = snapshot.series[0]->pointAt(i);
                    if (!qIsFinite(p.x()) || !qIsFinite(p.y()) || p.x() < lo || p.x() > hi) continue;
                    if (!expected) expected = PlotBounds{p.x(), p.x(), p.y(), p.y()};
                    else {
                        expected->xMinimum = qMin(expected->xMinimum, p.x());
                        expected->xMaximum = qMax(expected->xMaximum, p.x());
                        expected->yMinimum = qMin(expected->yMinimum, p.y());
                        expected->yMaximum = qMax(expected->yMaximum, p.y());
                    }
                }
                const auto actual = PlotSeriesStore::bounds(snapshot, lo, hi);
                QCOMPARE(bool(actual), bool(expected));
                if (expected) {
                    QCOMPARE(actual->xMinimum, expected->xMinimum);
                    QCOMPARE(actual->xMaximum, expected->xMaximum);
                    QCOMPARE(actual->yMinimum, expected->yMinimum);
                    QCOMPARE(actual->yMaximum, expected->yMaximum);
                }
            }
        }
    }
}

void RenderCoreTest::indexedBoundsAvoidMillionPointScan()
{
    PlotSeriesInput input;
    input.id = 0;
    const int count = 1000000;
    input.time.resize(count); input.values.resize(count);
    for (int i = 0; i < count; ++i) { input.time[i] = i; input.values[i] = i % 123; }
    PlotSeriesStore store;
    store.replaceSeries({input});
    const auto snapshot = store.snapshot({0});
    PlotBoundsQueryStats stats;
    const auto bounds = PlotSeriesStore::bounds(snapshot, 1, count - 2, &stats);
    QVERIFY(bounds);
    QCOMPARE(bounds->yMinimum, 0.0); QCOMPARE(bounds->yMaximum, 122.0);
    QVERIFY(stats.rawSamples < 2 * PlotRangeIndex::blockSize);
    QVERIFY(stats.indexNodes < 64);
    QVERIFY(snapshot.series[0]->rangeIndex->storageBytes() < count);
    qInfo() << "million-point fit:" << stats.rawSamples << "raw samples,"
            << stats.indexNodes << "index nodes; index bytes:"
            << snapshot.series[0]->rangeIndex->storageBytes();
}

void RenderCoreTest::snapshotIndexSurvivesMutations()
{
    PlotSeriesStore store;
    QVector<PlotSeriesInput> inputs;
    for (int i = 0; i < 10000; ++i) inputs.append({i * 2, {0}, {double(i)}, QColor("red")});
    store.replaceSeries(inputs);
    const auto original = store.snapshot({19998, -1, 0, 19998});
    QCOMPARE(original.orderedIds, QVector<int>({19998, 0, 19998}));
    QCOMPARE(original.series[0]->pointAt(0).y(), 9999.0);
    store.updateSeriesPen(19998, QColor("blue"), 4, Qt::DashLine);
    QVERIFY(store.addTimeOffset({19998}, 2.0));
    QCOMPARE(store.snapshot({19998}).series[0]->pointAt(0).x(), 2.0);
    QCOMPARE(original.series[0]->pointAt(0).x(), 0.0);
    store.appendSeries({{30000, {0}, {42}, QColor("green")}});
    QCOMPARE(store.snapshot({30000}).series[0]->pointAt(0).y(), 42.0);
    store.removeSeries({0});
    const auto remapped = store.snapshot({9999, 0});
    QCOMPARE(remapped.series[0]->pointAt(0).y(), 42.0);
    QCOMPARE(remapped.series[1]->pointAt(0).y(), 1.0);
    store.clear();
    QVERIFY(store.snapshot({0, 9999}).series.isEmpty());
    store.replaceSeries({{8, {0}, {1}, QColor("red")}, {8, {0}, {2}, QColor("blue")}});
    QCOMPARE(store.snapshot({8}).series[0]->pointAt(0).y(), 1.0);
}

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

void RenderCoreTest::storeDetectsUnsortedPointInput()
{
    for (const QVector<QPointF> &points : {
             QVector<QPointF>{{0, 1}, {10, 2}, {2, 3}},
             QVector<QPointF>{{0, 1}, {10, 2}, {qQNaN(), 8}, {2, 3}}}) {
        PlotSeriesInput input;
        input.id = 1;
        input.points = points;
        PlotSeriesStore store;
        store.replaceSeries({input});
        const auto snapshot = store.snapshot({1});
        QVERIFY(!snapshot.series.first()->monotonicTime);
        const auto times = PlotSeriesStore::timeBounds(snapshot);
        QVERIFY(times.has_value());
        QCOMPARE(times->first, 0.0);
        QCOMPARE(times->second, 10.0);
        QCOMPARE(PlotSeriesStore::nearestX(snapshot, 2.0), std::optional<double>(2.0));
        const auto bounds = PlotSeriesStore::bounds(snapshot, 1.0, 3.0);
        QVERIFY(bounds.has_value());
        QCOMPARE(bounds->yMinimum, 3.0);
        QCOMPARE(bounds->yMaximum, 3.0);
    }
}

void RenderCoreTest::unchangedTimeOffsetsPreserveSnapshots()
{
    PlotSeriesStore store;
    store.replaceSeries({{1, {0, 1}, {2, 3}, QColor("red")},
                         {2, {0, 1}, {4, 5}, QColor("blue")}});
    QVERIFY(store.addTimeOffset({1}, 2.0, true));
    const auto before = store.snapshot({1, 2});
    QVERIFY(store.addTimeOffset({1}, 2.0, true));
    QCOMPARE(store.generation(), before.generation);
    QCOMPARE(store.snapshot({1, 2}).series, before.series);
    QVERIFY(store.addTimeOffset({1, 2}, 2.0, true));
    const auto after = store.snapshot({1, 2});
    QCOMPARE(after.generation, before.generation + 1);
    QCOMPARE(after.series.first(), before.series.first());
    QCOMPARE(after.series.last()->timeOffset, 2.0);
    QCOMPARE(before.series.last()->timeOffset, 0.0);
    QVERIFY(store.addTimeOffset({1, 2}, 0.0));
    QCOMPARE(store.snapshot({1, 2}).series, after.series);
    QCOMPARE(store.generation(), after.generation);
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

void RenderCoreTest::unchangedPensPreserveSnapshots()
{
    PlotSeriesStore store;
    store.replaceSeries({{1, {0}, {1}, QColor("red")}, {2, {0}, {2}, QColor("blue")}});
    const auto original = store.snapshot({1, 2});
    for (int i = 0; i < 100; ++i) store.updateSeriesPen(1, QColor("red"), 2, Qt::SolidLine);
    store.updateSeriesPen(999, QColor("green"), 3, Qt::DashLine);
    QCOMPARE(store.generation(), original.generation);
    QCOMPARE(store.snapshot({1, 2}).series, original.series);
    store.updateSeriesPen(1, QColor("green"), 3, Qt::DashLine);
    const auto changed = store.snapshot({1, 2});
    QCOMPARE(changed.generation, original.generation + 1);
    QCOMPARE(changed.series[1], original.series[1]);
    QCOMPARE(original.series[0]->color, QColor("red"));
    QCOMPARE(changed.series[0]->rangeIndex, original.series[0]->rangeIndex);
    // Copying a store must still detach its pointer array on a real edit.
    auto copy = store;
    copy.updateSeriesPen(1, QColor("yellow"), 5, Qt::DotLine);
    QCOMPARE(store.snapshot({1}).series[0]->color, QColor("green"));
}

void RenderCoreTest::indexedTimeBoundsIncludeMissingValues()
{
    PlotSeriesInput input;
    input.id = 4;
    constexpr int count = 1000000;
    input.time.resize(count); input.values.resize(count);
    for (int i = 0; i < count; ++i) {
        input.time[i] = (i * qint64(7919)) % count;
        input.values[i] = qQNaN();
    }
    PlotSeriesStore store;
    store.replaceSeries({input});
    const auto before = store.snapshot({4});
    QVERIFY(!before.series[0]->monotonicTime);
    PlotBoundsQueryStats stats;
    QCOMPARE(PlotSeriesStore::timeBounds(before, &stats),
             (std::optional<QPair<double, double>>(qMakePair(0.0, double(count - 1)))));
    QCOMPARE(stats.rawSamples, qsizetype(0)); QCOMPARE(stats.indexNodes, qsizetype(1));
    QVERIFY(!PlotSeriesStore::bounds(before)); // All Y values are missing.
    QVERIFY(store.addTimeOffset({4}, 12.5));
    const auto shifted = store.snapshot({4});
    QCOMPARE(shifted.series[0]->rangeIndex, before.series[0]->rangeIndex);
    QCOMPARE(PlotSeriesStore::timeBounds(shifted),
             (std::optional<QPair<double, double>>(qMakePair(12.5, count - 1 + 12.5))));
    auto legacy = std::make_shared<PlotSeriesData>(*shifted.series[0]);
    legacy->rangeIndex.reset();
    PlotBoundsQueryStats legacyStats;
    QCOMPARE(PlotSeriesStore::timeBounds({0, {4}, {legacy}}, &legacyStats),
             PlotSeriesStore::timeBounds(shifted));
    QCOMPARE(legacyStats.rawSamples, qsizetype(count));
    qInfo() << "unordered million-point time bounds:" << stats.rawSamples << "raw samples,"
            << stats.indexNodes << "summary; legacy scans:" << legacyStats.rawSamples;
    store.replaceSeries({{5, {qQNaN(), 3, -2, qInf()}, {1, qQNaN(), qQNaN(), 1}, QColor("red")}});
    QCOMPARE(PlotSeriesStore::timeBounds(store.snapshot({5})),
             (std::optional<QPair<double, double>>(qMakePair(-2.0, 3.0))));
}

void RenderCoreTest::lodHandlesExtremeBucketArithmetic()
{
    const double huge = std::numeric_limits<double>::max();
    const double tiny = std::numeric_limits<double>::denorm_min();
    for (const auto &times : {QVector<double>{-huge, huge}, QVector<double>{0, 8 * tiny}}) {
        PlotSeriesStore store;
        store.replaceSeries({{1, times, {2, 2}, QColor("red")}});
        const auto snapshot = store.snapshot({1});
        const double lo = times[0] < 0 ? -1.0 : 0.0;
        const double hi = times[0] < 0 ? 1.0 : 8 * tiny;
        const auto lod = PlotLodBuilder::build(snapshot, {snapshot.generation, {1}, lo, hi, 128, 1});
        QCOMPARE(lod.segments.size(), 1);
        QCOMPARE(lod.segments[0].points, QVector<QPointF>({{times[0], 2}, {times[1], 2}}));
        QVERIFY(PlotLodBuilder::build(snapshot,
            {snapshot.generation, {1}, -huge, huge, 128, 1}).segments.isEmpty());
    }
}

void RenderCoreTest::paddedRangesRemainFinite()
{
    const double huge = std::numeric_limits<double>::max();
    for (const auto &bounds : {qMakePair(1e20, 1e20 + 1e6), qMakePair(1e20, 1e20),
                              qMakePair(huge, huge), qMakePair(-huge, -huge)}) {
        const auto range = paddedPlotRange(bounds.first, bounds.second, .02, .5);
        QVERIFY(range); QVERIFY(qIsFinite(range->second - range->first));
        QVERIFY(range->first < range->second);
        QVERIFY(range->first <= bounds.first); QVERIFY(range->second >= bounds.second);
        const auto ticks = makePlotAxisTicks(range->first, range->second);
        QVERIFY(ticks.size() >= 2 && ticks.size() <= 12);
    }
    QVERIFY(!paddedPlotRange(-huge, huge, .02, .5));
    QVERIFY(!paddedPlotRange(qQNaN(), 1, .02, .5));
}

QTEST_GUILESS_MAIN(RenderCoreTest)
#include "rendercore_test.moc"
