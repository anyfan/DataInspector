#include "dataloadworker.h"
#include "signalmodel.h"
#include "render/plotgeometrybuilder.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QThread>
#include <QtMath>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

static double milliseconds(QElapsedTimer &timer) { return timer.nsecsElapsed() / 1e6; }
static QJsonObject timings(QVector<double> values)
{
    std::sort(values.begin(), values.end());
    return {{"median_ms", values[values.size() / 2]},
            {"p95_ms", values[qMin(values.size() - 1, qsizetype(qCeil(values.size() * .95) - 1))]}};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("CPU preparation/search/LOD/geometry benchmark; does not measure GPU frame rate.");
    parser.addHelpOption();
    parser.addOptions({{"samples", "Synthetic samples per series (1..10000000)", "count", "1000000"},
                       {"signals", "Synthetic series count (1..64)", "count", "4"},
                       {"iterations", "View changes (1..128)", "count", "20"},
                       {"tree-signals", "Search model signals (1..100000)", "count", "100000"},
                       {"input", "Optional CSV/TXT/XLSX/MAT file instead of synthetic data", "file"},
                       {"output", "JSON report path (otherwise stdout)", "file"}});
    parser.process(app);
    const int samples = parser.value("samples").toInt();
    const int seriesCount = parser.value("signals").toInt();
    const int iterations = parser.value("iterations").toInt();
    const int treeSignals = parser.value("tree-signals").toInt();
    if (samples < 1 || samples > 10000000 || seriesCount < 1 || seriesCount > 64
        || qint64(samples) * seriesCount > 80000000 || iterations < 1 || iterations > 128
        || treeSignals < 1 || treeSignals > 100000) return 2;
    QJsonObject report{{"qt_version", qVersion()}, {"logical_cpus", QThread::idealThreadCount()},
#ifdef QT_NO_DEBUG
                       {"build", "Release"},
#else
                       {"build", "Debug"},
#endif
                       {"scope", "CPU timings; no GPU frame-rate measurement"}};
    QElapsedTimer timer;
    timer.start();
    QVector<PlotSeriesInput> inputs;
    if (!parser.value("input").isEmpty()) {
        DataLoadWorker loader;
        QString failure;
        QObject::connect(&loader, &DataLoadWorker::finished, &app,
            [&](const QString &, const QVector<LoadedTable> &tables, int, const QString &error) {
                failure = error;
                for (const auto &table : tables) for (int i = 0; i < table.signalNames.size(); ++i) {
                    PlotSeriesInput input;
                    input.id = inputs.size(); input.time = table.time; input.values = table.values[i];
                    input.rangeIndex = table.rangeIndexes.value(i);
                    input.monotonicTimeKnown = true; input.monotonicTime = table.monotonicTimes.value(i, false);
                    input.color = QColor("#0072bd"); inputs.append(std::move(input));
                }
            });
        loader.loadFile(parser.value("input"));
        if (!failure.isEmpty() || inputs.isEmpty()) { QTextStream(stderr) << failure << '\n'; return 1; }
        report["load_ms"] = milliseconds(timer);
    } else {
        QVector<double> time(samples);
        for (int i = 0; i < samples; ++i) time[i] = i * .01;
        for (int j = 0; j < seriesCount; ++j) {
            PlotSeriesInput input;
            input.id = j; input.time = time; input.values.resize(samples); input.color = QColor("#0072bd");
            for (int i = 0; i < samples; ++i) input.values[i] = qSin(i * .013 + j) + (i % 10001 == 0 ? 3 : 0);
            inputs.append(std::move(input));
        }
        report["synthetic_generation_ms"] = milliseconds(timer);
    }
    timer.restart();
    PlotSeriesStore store;
    store.replaceSeries(inputs);
    report["store_index_ms"] = milliseconds(timer);
    QVector<int> ids;
    qint64 sampleCount = 0;
    for (const auto &input : inputs) { ids.append(input.id); sampleCount += input.time.size(); }
    inputs.clear(); // Release temporary input vectors before measuring steady-state memory.
    report["series"] = ids.size(); report["total_samples"] = double(sampleCount);
    const auto snapshot = store.snapshot(ids);
    const auto bounds = PlotSeriesStore::timeBounds(snapshot);
    if (!bounds || bounds->second <= bounds->first) return 1;
    PlotLodResultCache cache;
    QVector<double> cold, warm, geometry;
    QVector<LodRequestKey> keys;
    const double extent = bounds->second - bounds->first;
    for (int i = 0; i < iterations; ++i) {
        const double start = bounds->first + extent * .5 * i / iterations;
        LodRequestKey key{snapshot.generation, ids, start, start + extent * .5, 1200, 1};
        keys.append(key);
        timer.restart();
        auto lod = std::make_shared<LodResult>(PlotLodBuilder::build(snapshot, key));
        cold.append(milliseconds(timer));
        cache.insert(snapshot, lod);
        timer.restart();
        const auto result = PlotGeometryBuilder::build(*lod, {{key.xMinimum, key.xMaximum, -2, 5, 1200, 600}, 2});
        geometry.append(milliseconds(timer));
        if (result.segments.isEmpty()) return 1;
    }
    int hits = 0;
    for (const auto &key : keys) {
        timer.restart(); const auto result = cache.find(snapshot, key);
        warm.append(milliseconds(timer)); if (result) ++hits;
    }
    report["lod_cold"] = timings(cold); report["cache_lookup"] = timings(warm);
    report["geometry"] = timings(geometry); report["cache_hits"] = hits;
    report["cache_bytes"] = double(cache.retainedBytes());
    QStringList names, groups;
    for (int i = 0; i < treeSignals; ++i) {
        names.append(QStringLiteral("Signal%1").arg(i)); groups.append(QStringLiteral("flight.mat/p%1").arg(i / 100));
    }
    SignalModel model;
    timer.restart(); model.setNames(names, groups);
    report["tree_build_ms"] = milliseconds(timer);
    QVector<double> search;
    for (int i = 0; i < iterations; ++i) {
        timer.restart(); model.setFilter(QStringLiteral("Signal%1").arg(i)); search.append(milliseconds(timer));
    }
    report["tree_search"] = timings(search); report["tree_signals"] = treeSignals;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        report["working_set_bytes"] = double(counters.WorkingSetSize);
        report["peak_working_set_bytes"] = double(counters.PeakWorkingSetSize);
    }
#endif
    const QByteArray json = QJsonDocument(report).toJson();
    if (parser.value("output").isEmpty()) QTextStream(stdout) << json;
    else {
        QSaveFile output(parser.value("output"));
        if (!output.open(QIODevice::WriteOnly) || output.write(json) != json.size() || !output.commit()) return 1;
    }
    return 0;
}
