#include "controller/appcontroller.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>

// Benchmark-only reconstruction of eager candidate expansion, using the same
// current metadata serializer so the comparison isolates candidate-list cost.
static QVariantList expandedConfigurations(const AppController &controller, int rawCount)
{
    QVariantList rawInputs, result;
    for (int row = 0; row < rawCount; ++row)
        rawInputs.append(QVariantMap{{"id", "raw:" + QString::number(row)},
            {"name", controller.signalModel()->groupAt(row) + " / " + controller.signalName(row)}});
    for (const auto &value : controller.dataObjects()) {
        auto config = value.toMap();
        if (config["type"] != "processing") continue;
        auto inputs = rawInputs;
        QHash<QString, QString> rawIds;
        for (const auto &value : config["fields"].toList()) {
            const auto source = value.toMap(); const int row = source["series"].toInt();
            rawIds.insert(source["id"].toString(), row >= 0 ? "raw:" + QString::number(row) : source["id"].toString());
            if (row < 0) inputs.append(QVariantMap{{"id", source["id"]}, {"name", source["name"].toString() + QStringLiteral("（来源已移除）")}});
        }
        QVariantList rules;
        for (const auto &value : config["rules"].toList()) {
            auto rule = value.toMap(); QStringList ids;
            for (const auto &id : rule["inputIds"].toStringList()) ids.append(rawIds.value(id, id));
            rule["inputIds"] = ids; rules.append(rule);
            for (const auto &value : rule["outputs"].toList()) {
                const auto output = value.toMap(); inputs.append(QVariantMap{{"id", output["id"]}, {"name", output["name"]}});
            }
        }
        config["rules"] = rules; config["inputs"] = inputs; result.append(config);
    }
    return result;
}
static bool idle(AppController &controller)
{
    QElapsedTimer timer; timer.start();
    while (controller.loading() || controller.deriving()) {
        if (timer.elapsed() > 30000) return false;
        QCoreApplication::processEvents(); QThread::msleep(1);
    }
    return true;
}
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv); QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"signals", "Raw signal count", "count", "10000"});
    parser.addOption({"configs", "Processing configuration count", "count", "16"});
    parser.addOption({"iterations", "Measured iterations", "count", "7"});
    parser.addOption({"output", "JSON report", "path"}); parser.process(app);
    bool a = false, b = false, c = false;
    const int rawSignalCount = parser.value("signals").toInt(&a), configs = parser.value("configs").toInt(&b), iterations = parser.value("iterations").toInt(&c);
    if (!a || !b || !c || rawSignalCount < 1 || rawSignalCount > 100000 || configs < 1 || configs > 256 || iterations < 1 || iterations > 100) return 2;
    QTemporaryDir dir; QFile source(dir.filePath("inputs.csv")); if (!source.open(QIODevice::WriteOnly)) return 1;
    QByteArray header("time"), values("0");
    for (int i = 0; i < rawSignalCount; ++i) { header += ",s" + QByteArray::number(i); values += ",1"; }
    source.write(header + '\n' + values + '\n'); source.close();
    AppController controller; if (!controller.loadCsv(source.fileName()) || !idle(controller) || controller.signalCount() != rawSignalCount) return 1;
    QString active;
    for (int i = 0; i < configs; ++i) {
        const auto id = controller.createDataObject("config" + QString::number(i), "processing");
        if (id.isEmpty() || controller.addObjectRule(id, {{"name", "scaled"}, {"operation", "scale"}, {"inputs", QVariantList{"raw:" + QString::number(i % rawSignalCount)}}}).isEmpty() || !idle(controller)) return 1;
        if (active.isEmpty()) active = id;
    }
    const auto verify = [&] {
        auto expanded = expandedConfigurations(controller, rawSignalCount);
        const auto summaries = controller.processingConfigurations();
        if (expanded.size() != summaries.size()) return false;
        for (int i = 0; i < expanded.size(); ++i) {
            auto config = expanded[i].toMap();
            if (config["inputs"].toList() != controller.processingRuleInputs(config["id"].toString())) return false;
            config["inputs"] = QVariantList{};
            if (config != summaries[i].toMap()) return false;
        }
        return true;
    };
    if (!verify()) { qCritical("Candidate or summary mismatch"); return 1; }
    QVector<double> eagerMs, lazyMs; qsizetype eagerEntries = 0, lazyEntries = 0;
    for (int i = -1; i < iterations; ++i) { // One warmup; reverse order to reduce ordering bias.
        const auto eager = [&] { QElapsedTimer timer; timer.start(); const auto rows = expandedConfigurations(controller, rawSignalCount); eagerEntries = 0; for (const auto &row : rows) eagerEntries += row.toMap()["inputs"].toList().size(); if (i >= 0) eagerMs.append(timer.nsecsElapsed() / 1e6); };
        const auto lazy = [&] { QElapsedTimer timer; timer.start(); const auto rows = controller.processingConfigurations(); const auto inputs = controller.processingRuleInputs(active); lazyEntries = inputs.size(); for (const auto &row : rows) lazyEntries += row.toMap()["inputs"].toList().size(); if (i >= 0) lazyMs.append(timer.nsecsElapsed() / 1e6); };
        if (i % 2) { lazy(); eager(); } else { eager(); lazy(); }
    }
    std::sort(eagerMs.begin(), eagerMs.end()); std::sort(lazyMs.begin(), lazyMs.end());
#ifdef QT_DEBUG
    const QString build = "Debug";
#else
    const QString build = "Release";
#endif
    const QJsonObject report{{"build", build}, {"rawSignals", rawSignalCount}, {"configurations", configs}, {"iterations", iterations},
        {"candidateAndSummaryEquality", true}, {"eagerMedianMs", eagerMs[eagerMs.size()/2]}, {"lazyMedianMs", lazyMs[lazyMs.size()/2]},
        {"eagerCandidateEntries", double(eagerEntries)}, {"lazyCandidateEntries", double(lazyEntries)}};
    const auto bytes = QJsonDocument(report).toJson(); qInfo().noquote() << bytes;
    if (parser.isSet("output")) { QFile output(parser.value("output")); if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) return 1; }
    return 0;
}
