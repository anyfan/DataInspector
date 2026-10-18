#include "io/dataloadworker.h"
#include "models/objectdefinition.h"
#include "../tests/support/merge_reference.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QMap>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

static QJsonObject memory()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)))
        return {{"workingSetBytes", double(counters.WorkingSetSize)}, {"peakWorkingSetBytes", double(counters.PeakWorkingSetSize)}, {"privateBytes", double(counters.PrivateUsage)}};
#endif
    return {};
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv); QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"input", "MAT/CSV input", "path"}); parser.addOption({"mode", "legacy or shared", "mode", "shared"});
    parser.addOption({"output", "JSON report", "path"}); parser.process(app);
    const bool shared = parser.value("mode") == "shared";
    if (!shared && parser.value("mode") != "legacy") return 2;
    QVector<LoadedTable> tables; QString error; DataLoadWorker loader;
    QObject::connect(&loader, &DataLoadWorker::finished, [&](const QString &, const QVector<LoadedTable> &result, int, const QString &why) { tables = result; error = why; });
    QElapsedTimer load; load.start(); loader.loadFile(parser.value("input"));
    if (!error.isEmpty() || tables.isEmpty()) { qCritical().noquote() << error; return 1; }
    const qint64 loadMs = load.elapsed();
    QVector<PlotSeriesInput> inputs; QVector<int> ids; QMap<QString, QVector<int>> groups;
    for (const auto &table : tables) for (int column = 0; column < table.values.size(); ++column) {
        PlotSeriesInput input; input.id = inputs.size(); input.time = table.time; input.values = table.values[column];
        input.monotonicTimeKnown = true; input.monotonicTime = table.monotonicTimes.value(column, false); input.rangeIndex = table.rangeIndexes.value(column);
        groups[table.signalNames[column]].append(input.id); inputs.append(input); ids.append(input.id);
    }
    PlotSeriesStore store; store.appendSeries(inputs); inputs.clear(); tables.clear();
    QVector<DataObject> objects; int output = ids.size();
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) if (it.value().size() > 1) {
        DataObject object; object.id = "o" + QString::number(objects.size()); object.name = it.key();
        ObjectRule rule; rule.id = object.id + "r"; rule.name = it.key(); rule.operation = "merge";
        for (int source : it.value()) { const auto id = "f" + QString::number(source); object.fields.append({id, id, "scalar", source}); rule.inputs.append(id); }
        rule.outputs.append({rule.id + "out", rule.name, output++}); object.rules.append(rule); objects.append(object);
    }
    const auto before = memory(); const auto snapshot = store.snapshot(ids); std::atomic_bool cancelled{false};
    QElapsedTimer timer; timer.start(); const auto result = (shared ? evaluateObjects(objects, snapshot, cancelled) : evaluateMergeReference(objects, snapshot, cancelled)); const qint64 elapsed = timer.elapsed();
    const auto after = memory(); QCryptographicHash hash(QCryptographicHash::Sha256); double samples = 0;
    for (const auto &series : result.series) {
        samples += series->sampleCount(); hash.addData(QByteArrayView(reinterpret_cast<const char *>(series->time.constData()), series->time.size() * sizeof(double)));
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(series->values.constData()), series->values.size() * sizeof(double)));
    }
    QJsonObject report{{"mode", parser.value("mode")}, {"input", parser.value("input")}, {"loadMs", loadMs}, {"evaluationMs", elapsed},
        {"rawSignals", ids.size()}, {"mergeRules", objects.size()}, {"outputSamples", samples}, {"errorCount", result.errors.size()},
        {"sha256", QString::fromLatin1(hash.result().toHex())}, {"timelineBuilds", double(result.mergeTimelineBuilds)},
        {"timelineHits", double(result.mergeTimelineHits)}, {"timelineCacheBytes", double(result.mergeTimelineBytes)}, {"before", before}, {"after", after}};
    QFile file(parser.value("output")); if (!file.open(QIODevice::WriteOnly)) return 1;
    file.write(QJsonDocument(report).toJson()); qInfo().noquote() << QJsonDocument(report).toJson(QJsonDocument::Compact); return 0;
}
