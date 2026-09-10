#include "render/plotseriesstore.h"
#include "render/plotlodbuilder.h"

#include <QtTest>

class RenderCoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void storeGenerationAndSnapshotsAreImmutable();
    void storeQueriesRawSamplesWithoutInterpolation();
    void storeSkipsUnknownIdsAndIgnoresInvalidBounds();
    void lodKeepsBothNeighborsAcrossNarrowViewport();
    void lodPreservesSampleOrderForBucketExtrema();
    void lodKeepsRepeatedTimesAndSplitsAtNan();
    void lodFiltersNonMonotonicSeriesToViewportInInputOrder();
    void lodRejectsInvalidRequests();
    void lodCacheReusesOnlyAnExactRequestKey();
};

void RenderCoreTest::storeGenerationAndSnapshotsAreImmutable()
{
    PlotSeriesStore store;
    store.replaceSeries({{4, {0.0, 1.0}, {10.0, 11.0}, QColor("red")}});
    const PlotSeriesSnapshot oldSnapshot = store.snapshot({4});
    QCOMPARE(oldSnapshot.generation, quint64(1));
    QCOMPARE(oldSnapshot.series.size(), 1);
    QCOMPARE(oldSnapshot.series.at(0)->points.at(1), QPointF(1.0, 11.0));

    store.replaceSeries({{4, {0.0, 1.0}, {20.0, 21.0}, QColor("blue")}});
    QCOMPARE(store.generation(), quint64(2));
    QCOMPARE(oldSnapshot.series.at(0)->points.at(1), QPointF(1.0, 11.0));
    QCOMPARE(store.snapshot({4}).series.at(0)->points.at(1), QPointF(1.0, 21.0));

    store.clear();
    QCOMPARE(store.generation(), quint64(3));
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

QTEST_GUILESS_MAIN(RenderCoreTest)
#include "rendercore_test.moc"
