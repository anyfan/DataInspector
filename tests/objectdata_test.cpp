#include "controller/appcontroller.h"
#include "items/plotitem.h"
#include "models/objectdefinition.h"
#include "qmltypes.h"
#include "io/xlsxreader.h"
#include "io/xlsxwriter.h"
#include "render/plotgeometrybuilder.h"
#include <QFile>
#include <QElapsedTimer>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

class ObjectDataTest : public QObject {
    Q_OBJECT
private slots:
    void bitsAndSignedFields();
    void mergeSamples();
    void identifyPacketLoss();
    void packetLoss();
    void packetLossCounters();
    void packetLossRateModes();
    void packetLossLifecycle();
    void mergeSharedTimelineMatchesLegacy();
    void dependencyPlanPreservesIndependentBranches();
    void expressionsAndTimeBases();
    void ownershipAndSchema();
    void controllerLifecycleAndRestore();
    void removingFilesCleansDerivedSignalsAndLegends();
    void editsPreserveUnrelatedResultsAndMergePendingObjects();
    void linkedTrajectoriesAndRuleEdits();
    void staircaseKeepsPulse();
    void objectManagerQml();
    void ruleInputsQml_data();
    void ruleInputsQml();
    void ruleEditorLargeSignalNavigation();
    void objectFieldPolicy();
    void independentProcessingAndLegacyMigration();
};
static ObjectRule bitRule(int width, int start, int bits, bool split = true, bool signedValue = false)
{
    ObjectRule rule; rule.id = "rule"; rule.name = "flags"; rule.operation = split ? "bits" : "bitfield";
    rule.wordBits = width; rule.startBit = start; rule.bitCount = bits; rule.signedField = signedValue; rule.inputs = {"source"};
    for (int i = 0; i < (split ? bits : 1); ++i) rule.outputs.append({"out" + QString::number(i), "bit" + QString::number(i), i + 1});
    return rule;
}
static PlotSeriesSnapshot rawSnapshot(QVector<double> values)
{
    PlotSeriesInput input; input.id = 0; input.values = values;
    for (qsizetype i = 0; i < values.size(); ++i) input.time.append(double(i));
    PlotSeriesStore store; store.appendSeries({input}); return store.snapshot({0});
}
void ObjectDataTest::independentProcessingAndLegacyMigration()
{
    QTemporaryDir dir;
    const auto sourcePath = dir.filePath("independent.csv");
    QFile source(sourcePath); QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("time,A,B\n0,2,8\n1,3,9\n"); source.close();
    AppController controller;
    QVERIFY(controller.loadCsv(sourcePath)); QTRY_VERIFY(!controller.loading());
    const auto object = controller.createDataObject("device");
    const auto field = controller.bindObjectField(object, {}, "A", "scalar", 0);
    QVERIFY(!field.isEmpty());
    QVERIFY(controller.addObjectRule(object, {{"name", "must be independent"}, {"inputs", QVariantList{field}}, {"operation", "scale"}}).isEmpty());
    const auto processing = controller.createDataObject("scaling", "processing");
    const auto rule = controller.addObjectRule(processing, {{"name", "scaled"}, {"inputs", QVariantList{"raw:0"}}, {"operation", "scale"}, {"factor", 2}});
    QVERIFY(!rule.isEmpty()); QTRY_VERIFY(!controller.deriving());
    const auto first = controller.processingConfigurations().first().toMap()["rules"].toList().first().toMap();
    const auto output = first["outputs"].toList().first().toMap();
    QCOMPARE(output["preview"].toString(), QString("4 / 6"));
    const auto child = controller.addObjectRule(processing, {{"name", "child"}, {"inputs", QVariantList{output["id"]}}, {"operation", "scale"}, {"factor", 3}});
    QVERIFY(!child.isEmpty()); QTRY_VERIFY(!controller.deriving());
    auto preview = [&] { return controller.processingConfigurations().first().toMap()["rules"].toList().last().toMap()["outputs"].toList().first().toMap()["preview"].toString(); };
    QCOMPARE(preview(), QString("12 / 18"));
    QVERIFY(!controller.bindObjectField(object, field, "B", "scalar", 1).isEmpty()); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(preview(), QString("12 / 18"));
    const auto objectCopy = controller.duplicateDataObject(object); QVERIFY(!objectCopy.isEmpty());
    QCOMPARE(controller.signalCount(), 4);
    QVERIFY(controller.removeDataObject(object)); QTRY_VERIFY(!controller.deriving());
    QVERIFY(controller.removeDataObject(objectCopy)); QTRY_VERIFY(!controller.deriving());
    QVERIFY(controller.objectConfigurations().isEmpty());
    QCOMPARE(preview(), QString("12 / 18"));
    QVERIFY(!controller.removeObjectRule(processing, rule)); // A live downstream still protects its input.
    const auto copy = controller.duplicateDataObject(processing); QVERIFY(!copy.isEmpty()); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.signalCount(), 6);
    QVERIFY(controller.removeDataObject(copy)); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.signalCount(), 4); QCOMPARE(preview(), QString("12 / 18"));
    for (int section : {0, 1}) {
        controller.setSignalBrowserSection(section);
        auto *model = controller.signalModel();
        int leaves = 0;
        for (int row = 0; row < model->rowCount(); ++row) {
            const int sourceRow = model->data(model->index(row), SignalModel::IndexRole).toInt();
            if (sourceRow < 0) continue;
            ++leaves;
            QCOMPARE(controller.isDerivedSignal(sourceRow), section == 1);
        }
        QCOMPARE(leaves, 2);
    }
    const auto sessionPath = dir.filePath("independent.disession");
    QVERIFY(controller.saveSession(sessionPath));
    QVERIFY(!controller.sessionModified());
    controller.setSignalBrowserSection(0); QVERIFY(!controller.sessionModified());
    QFile saved(sessionPath); QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto json = QJsonDocument::fromJson(saved.readAll()).object();
    QCOMPARE(json["version"].toInt(), 17);
    QVERIFY(json["objects"].toArray().isEmpty());
    QCOMPARE(json["processing"].toArray().size(), 1);
    QCOMPARE(json["signals"].toArray()[2].toObject()["processingId"].toString(), processing);
    SessionDocument parsed; QString error;
    QVERIFY2(sessionFromJson(json, &parsed, &error), qPrintable(error));
    auto invalid = json; invalid.remove("processing"); QVERIFY(!sessionFromJson(invalid, &parsed, &error));
    invalid = json; invalid["objects"] = json["processing"]; QVERIFY(!sessionFromJson(invalid, &parsed, &error));
    invalid = json;
    auto signalEntries = invalid["signals"].toArray(); auto bad = signalEntries[2].toObject();
    bad["processingId"] = "missing"; signalEntries[2] = bad; invalid["signals"] = signalEntries;
    QVERIFY(!sessionFromJson(invalid, &parsed, &error));

    // Rebuild an actual v14 object-owned fixture from the same raw identities.
    auto legacy = json; legacy["version"] = 14; legacy.remove("processing");
    auto definition = json["processing"].toArray()[0].toObject(); definition["type"] = "general";
    legacy["objects"] = QJsonArray{definition};
    signalEntries = legacy["signals"].toArray();
    for (int i = 0; i < signalEntries.size(); ++i) {
        auto signal = signalEntries[i].toObject(); signal["objectId"] = signal.take("processingId"); signalEntries[i] = signal;
    }
    legacy["signals"] = signalEntries;
    QVERIFY2(sessionFromJson(legacy, &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.objects.size(), 2);
    QVERIFY(parsed.objects[0].rules.isEmpty());
    QCOMPARE(parsed.objects[1].rules[0].id, rule);
    QCOMPARE(parsed.objects[1].rules[0].outputs[0].id, output["id"].toString());
    QVERIFY(parsed.objects[0].fields[0].id != parsed.objects[1].fields[0].id);
    const auto legacyPath = dir.filePath("legacy.disession");
    QFile old(legacyPath); QVERIFY(old.open(QIODevice::WriteOnly)); old.write(QJsonDocument(legacy).toJson()); old.close();
    AppController restored;
    QVERIFY(restored.restoreSession(legacyPath)); QTRY_VERIFY(!restored.loading() && !restored.deriving());
    QCOMPARE(restored.processingConfigurations().size(), 1);
    QVERIFY(restored.removeDataObject(processing)); QTRY_VERIFY(!restored.deriving()); // Old object id, not new processing id.
    QCOMPARE(restored.signalCount(), 4);
    QCOMPARE(restored.processingConfigurations()[0].toMap()["rules"].toList().last().toMap()["outputs"].toList().first().toMap()["preview"].toString(), QString("12 / 18"));
    QVERIFY(restored.removeFile("independent.csv")); QTRY_VERIFY(!restored.deriving());
    QCOMPARE(restored.signalCount(), 0);
    QVERIFY(restored.processingConfigurations()[0].toMap()["rules"].toList().isEmpty());
}
void ObjectDataTest::identifyPacketLoss()
{
    QTemporaryDir dir;
    QVector<XlsxExportTable> tables;
    const QVector<QVector<double>> times{{0, 1, 2, 5}, {0, 1, 2, 5}, {0, .5, 1}, {20, 21, 22, 25}};
    for (int t = 0; t < times.size(); ++t) {
        XlsxExportTable table; table.name = QString("part%1").arg(t);
        for (int c = 0; c < (t == 0 ? 2 : 1); ++c) {
            auto data = std::make_shared<PlotSeriesData>(); data->time = times[t]; data->values.fill(c, times[t].size());
            table.series.append({QString("value%1").arg(c), data});
        }
        tables.append(table);
    }
    const auto path = dir.filePath("groups.xlsx"); QVERIFY(writeXlsxWorkbook(path, tables).error.isEmpty());
    AppController controller; QVERIFY(controller.loadCsv(path)); QTRY_VERIFY(!controller.loading() && !controller.deriving());
    QCOMPARE(controller.signalCount(), 5); QCOMPARE(controller.dataObjects().size(), 0);
    QVERIFY(!controller.identifyPacketLoss("missing"));
    const auto detector = controller.createDataObject("丢包检测", "packetloss"); QVERIFY(!detector.isEmpty());
    QVERIFY(controller.identifyPacketLoss(detector, "count")); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.signalCount(), 8); QCOMPARE(controller.dataObjects().size(), 1);
    auto object = controller.dataObjects()[0].toMap(); const auto objectId = object["id"].toString();
    QCOMPARE(object["fields"].toList().size(), 3); auto rules = object["rules"].toList(); QCOMPARE(rules.size(), 3);
    QCOMPARE(rules[0].toMap()["samplePeriod"].toDouble(), 1.0); QCOMPARE(rules[1].toMap()["samplePeriod"].toDouble(), .5);
    QCOMPARE(rules[2].toMap()["samplePeriod"].toDouble(), 1.0);
    QCOMPARE(rules[0].toMap()["outputs"].toList()[0].toMap()["preview"].toString(), QString("0 / 0 / 0 / 2"));
    const auto rule = rules[0].toMap();
    QVERIFY(controller.editObjectRule(objectId, rule["id"].toString(), {{"name", rule["name"]}, {"operation", "packetloss"},
        {"inputs", rule["inputIds"]}, {"samplePeriod", .5}})); QTRY_VERIFY(!controller.deriving());
    // A rule bound to any equivalent raw column covers the same group.
    const auto representative = object["fields"].toList()[0].toMap();
    QVERIFY(!controller.bindObjectField(detector, representative["id"].toString(), representative["name"].toString(), "scalar", 1).isEmpty());
    QTRY_VERIFY(!controller.deriving());
    QVERIFY(controller.identifyPacketLoss(detector, "count")); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.signalCount(), 8);
    QCOMPARE(controller.dataObjects()[0].toMap()["rules"].toList()[0].toMap()["id"], rule["id"]);
    QCOMPARE(controller.dataObjects()[0].toMap()["rules"].toList()[0].toMap()["samplePeriod"].toDouble(), .5);
    QVERIFY(!controller.loadCsv(path)); QCOMPARE(controller.dataObjects().size(), 1);
    QFile other(dir.filePath("other.csv")); QVERIFY(other.open(QIODevice::WriteOnly)); other.write("time,value\n0,1\n1,2\n"); other.close();
    QVERIFY(controller.loadCsv(other.fileName())); QTRY_VERIFY(!controller.loading() && !controller.deriving());
    QCOMPARE(controller.dataObjects().size(), 1); QCOMPARE(controller.signalCount(), 9);
    QVERIFY(controller.identifyPacketLoss(detector, "count")); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.dataObjects().size(), 1); QCOMPARE(controller.signalCount(), 10);
    QCOMPARE(controller.dataObjects()[0].toMap()["rules"].toList()[0].toMap()["samplePeriod"].toDouble(), .5);
    const auto session = dir.filePath("groups.disession"); QVERIFY(controller.saveSession(session));
    AppController restored; QVERIFY(restored.restoreSession(session)); QTRY_VERIFY(!restored.loading() && !restored.deriving());
    QCOMPARE(restored.dataObjects().size(), 1); QCOMPARE(restored.signalCount(), 10);
    const auto restoredRule = restored.dataObjects()[0].toMap()["rules"].toList()[0].toMap();
    QCOMPARE(restoredRule["id"], rule["id"]); QCOMPARE(restoredRule["samplePeriod"].toDouble(), .5);
    QCOMPARE(restoredRule["outputs"].toList()[0].toMap()["preview"].toString(), QString("0 / 1 / 2 / 7"));
    QVERIFY(!restored.sessionModified());
    SessionDocument document; QString reason; QVERIFY(readSessionDocument(session, &document, &reason));
    auto legacy = sessionToJson(document); legacy["version"] = 12;
    SessionDocument parsed; QVERIFY(!sessionFromJson(legacy, &parsed, &reason));
    const auto copy = restored.duplicateDataObject(detector); QVERIFY(!copy.isEmpty()); QTRY_VERIFY(!restored.deriving());
    QCOMPARE(restored.dataObjects().last().toMap()["type"].toString(), QString("packetloss"));
    QCOMPARE(restored.dataObjects().last().toMap()["rules"].toList()[0].toMap()["samplePeriod"].toDouble(), .5);
    controller.clear(); QCOMPARE(controller.dataObjects().size(), 0);
    QVERIFY(controller.loadCsv(other.fileName())); QTRY_VERIFY(!controller.loading());
    const auto full = controller.createDataObject("full", "packetloss");
    for (int i = 0; i < 256; ++i) QVERIFY(!controller.addDataObjectField(full).isEmpty());
    QVERIFY(controller.identifyPacketLoss(full, "count")); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.dataObjects()[0].toMap()["fields"].toList().size(), 256);
    QCOMPARE(controller.dataObjects()[0].toMap()["rules"].toList().size(), 0); QCOMPARE(controller.signalCount(), 1);
    QVERIFY(controller.status().contains(QStringLiteral("未提交")));
    QVERIFY(controller.identifyPacketLoss(full, "count")); controller.setSignalPen(0, QColor("red"), 2, Qt::SolidLine);
    QTRY_VERIFY(!controller.deriving());
    QVERIFY(controller.status().contains(QStringLiteral("数据已变更")));
    QCOMPARE(controller.dataObjects()[0].toMap()["rules"].toList().size(), 0);
    QVERIFY(controller.identifyPacketLoss(full, "count")); controller.clear(); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.dataObjects().size(), 0); QCOMPARE(controller.signalCount(), 0);
    QFile single(dir.filePath("single.csv")); QVERIFY(single.open(QIODevice::WriteOnly)); single.write("time,value\n1,2\n"); single.close();
    QVERIFY(controller.loadCsv(single.fileName())); QTRY_VERIFY(!controller.loading() && !controller.deriving());
    QCOMPARE(controller.dataObjects().size(), 0); QCOMPARE(controller.signalCount(), 1);
    const auto singleObject = controller.createDataObject("single", "packetloss");
    QVERIFY(controller.identifyPacketLoss(singleObject)); QTRY_VERIFY(!controller.deriving());
    object = controller.dataObjects()[0].toMap(); const auto singleRule = object["rules"].toList()[0].toMap();
    QVERIFY(!singleRule["error"].toString().isEmpty()); QCOMPARE(controller.signalCount(), 2);
    QCOMPARE(controller.estimateObjectSamplePeriod(object["id"].toString(), singleRule["inputIds"].toStringList()[0]), 0.0);
    QVERIFY(controller.editObjectRule(object["id"].toString(), singleRule["id"].toString(), {{"name", "single"}, {"operation", "packetloss"},
        {"inputs", singleRule["inputIds"]}, {"samplePeriod", 1}})); QTRY_VERIFY(!controller.deriving());
    QVERIFY(controller.dataObjects()[0].toMap()["rules"].toList()[0].toMap()["error"].toString().isEmpty());
}
void ObjectDataTest::packetLoss()
{
    ObjectRule rule; rule.id = "loss"; rule.name = "loss"; rule.operation = "packetloss";
    rule.inputs = {"input"}; rule.outputs = {{"lost", "lost", 1}, {"rate", "rate", 2}};
    DataObject object{"o", "object", {{"input", "input", "scalar", 0}}, {rule}};
    std::atomic_bool cancel{false}; QString error;
    auto run = [&](QVector<double> time, double period = 0) {
        PlotSeriesInput raw; raw.id = 0; raw.time = time; raw.values.fill(qQNaN(), time.size()); raw.timeOffset = 10;
        PlotSeriesStore store; store.appendSeries({raw}); object.rules[0].samplePeriod = period;
        return evaluateObjects({object}, store.snapshot({0}), cancel);
    };
    auto result = run({0, .1, .2, .5, .6, .7});
    QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series.size(), 2);
    QCOMPARE(result.series[0]->values, QVector<double>({0, 0, 0, 2, 2, 2}));
    QVERIFY(qAbs(result.series[1]->values[3] - 100.0 / 3) < 1e-10);
    QCOMPARE(result.series[1]->values.last(), 25.0);
    QVERIFY(qAbs(result.states["loss"].samplePeriod - .1) < 1e-12);
    QCOMPARE(result.series[0]->timeOffset, 10.0); QVERIFY(result.series[0]->step);
    result = run({0, .1, .2, .5, .6, .7}, .05);
    QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series[0]->values.last(), 9.0);
    result = run({0, .1001, .1999, .3, .4001});
    QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series[0]->values.last(), 0.0);
    result = run({0}, .1); QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series[0]->values, QVector<double>{0});
    QVERIFY(!run({0}).errors.isEmpty());
    for (const auto &time : {QVector<double>{0, 0, .1}, QVector<double>{0, .2, .1}, QVector<double>{0, qQNaN()}, QVector<double>{0, qInf()}}) {
        result = run(time, .1); QVERIFY(!result.errors.isEmpty());
        QVERIFY(result.series[0]->values.isEmpty()); QVERIFY(result.series[1]->values.isEmpty());
    }
    QVERIFY(!run({0, 1}, 1e-30).errors.isEmpty());
    run({0, 1}, .1);
    QVERIFY(validateObjects({object}, 3, &error));
    QVector<DataObject> restored; const auto json = objectsToJson({object});
    QVERIFY(objectsFromJson(json, 3, &restored, &error)); QCOMPARE(restored[0].rules[0].samplePeriod, .1);
    QVERIFY(!objectsFromJson(json, 3, &restored, &error, 11));
    auto bad = json[0].toObject(); auto rules = bad["rules"].toArray(); auto r = rules[0].toObject();
    r["samplePeriod"] = "0.1"; rules[0] = r; bad["rules"] = rules;
    QVERIFY(!objectsFromJson(QJsonArray{bad}, 3, &restored, &error));
    object.rules[0].samplePeriod = -1; QVERIFY(!validateObjects({object}, 3, &error));
    cancel = true; QVERIFY(run({0, 1}).series.isEmpty());
}
void ObjectDataTest::packetLossRateModes()
{
    ObjectRule rule; rule.id = "loss"; rule.name = "loss"; rule.operation = "packetloss";
    rule.samplePeriod = 1; rule.inputs = {"input"}; rule.outputs = {{"lost", "lost", 1}, {"rate", "rate", 2}};
    DataObject object{"o", "processing", {{"input", "input", "scalar", 0}}, {rule}}; object.type = "processing";
    std::atomic_bool cancel{false};
    auto run = [&](QVector<double> time, QVector<double> values = {}) {
        PlotSeriesInput raw; raw.id = 0; raw.time = time; raw.values = values.isEmpty() ? QVector<double>(time.size(), qQNaN()) : values;
        raw.timeOffset = 100;
        PlotSeriesStore store; store.appendSeries({raw});
        return evaluateObjects({object}, store.snapshot({0}), cancel);
    };
    auto &r = object.rules[0];
    const QVector<double> time{0, 1, 2, 5, 6, 7, 8};
    const auto cumulative = run(time); QVERIFY(cumulative.errors.isEmpty());
    QVERIFY(qAbs(cumulative.series[1]->values.last() - 200.0 / 9) < 1e-10);
    r.rateMode = "window"; r.rateWindowSeconds = 2;
    auto result = run(time); QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.series[0]->values, cumulative.series[0]->values);
    QVERIFY(qAbs(result.series[1]->values[3] - 200.0 / 3) < 1e-10);
    QCOMPARE(result.series[1]->values[4], 50.0); QCOMPARE(result.series[1]->values[5], 0.0);
    QCOMPARE(result.series[1]->values.first(), 0.0); QCOMPARE(result.series[1]->timeOffset, 100.0);
    r.rateWindowSeconds = 1; result = run(time); QCOMPARE(result.series[1]->values[4], 0.0);
    r.rateWindowSeconds = 100; result = run(time); QCOMPARE(result.series[1]->values, cumulative.series[1]->values);
    r.rateWindowSeconds = 2; r.lossMode = "sequence";
    result = run({0, 1, 2, 3, 4}, {10, 11, 14, 14, 15}); QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.series[1]->values[2], 50.0);
    QVERIFY(qAbs(result.series[1]->values[3] - 200.0 / 3) < 1e-10);
    QCOMPARE(result.series[1]->values.last(), 0.0);
    r.lossMode = "stride"; r.expectedStride = 10;
    result = run({0, 1, 2, 3, 4}, {100, 110, 123, 133, 143}); QVERIFY(result.errors.isEmpty());
    QVERIFY(qAbs(result.series[1]->values[2] - 300.0 / 23) < 1e-10);
    QCOMPARE(result.series[1]->values.last(), 0.0);
    r.lossMode = "rate"; r.counterRate = 10;
    result = run({0, 1, 3, 4}, {100, 108, 125, 135}); QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.series[1]->values[2], 15.0); QCOMPARE(result.series[1]->values[3], 10.0);
    r.lossMode = "sequence"; r.counterModulus = 256;
    result = run({0, 1, 2, 3, 4, 5, 6}, {254, 255, 0, 1, 4, 5, 6}); QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.series[1]->values[4], 50.0); QCOMPARE(result.series[1]->values.last(), 0.0);
    QString error; QVector<DataObject> restored;
    auto json = objectsToJson({object}); QVERIFY(objectsFromJson(json, 3, &restored, &error));
    QCOMPARE(restored[0].rules[0].rateMode, QString("window")); QCOMPARE(restored[0].rules[0].rateWindowSeconds, 2.0);
    QVERIFY(!objectsFromJson(json, 3, &restored, &error, 15));
    auto legacy = json[0].toObject(); auto rules = legacy["rules"].toArray(); auto legacyRule = rules[0].toObject();
    legacyRule.remove("rateMode"); legacyRule.remove("rateWindowSeconds"); rules[0] = legacyRule; legacy["rules"] = rules;
    QVERIFY(objectsFromJson(QJsonArray{legacy}, 3, &restored, &error, 15));
    QCOMPARE(restored[0].rules[0].rateMode, QString("cumulative"));
    QVERIFY(!objectsFromJson(QJsonArray{legacy}, 3, &restored, &error, 16));
    for (double invalid : {0.0, -1.0, qInf(), qQNaN()}) {
        r.rateWindowSeconds = invalid; QVERIFY(!validateObjects({object}, 3, &error));
    }
    r.rateWindowSeconds = 2; r.rateMode = "invalid"; QVERIFY(!validateObjects({object}, 3, &error));

    r = rule; r.outputs.resize(1); r.rateWindowSeconds = 2;
    for (const auto &mode : {QString("count"), QString("cumulative"), QString("window")}) {
        r.rateMode = mode;
        result = run(time); QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series.size(), 1);
        QVERIFY(validateObjects({object}, 2, &error));
        if (mode == "count") QCOMPARE(result.series[0]->values, cumulative.series[0]->values);
        else if (mode == "cumulative") QCOMPARE(result.series[0]->values, cumulative.series[1]->values);
        else { QVERIFY(qAbs(result.series[0]->values[3] - 200.0 / 3) < 1e-10); QCOMPARE(result.series[0]->values.last(), 0.0); }
        QVERIFY(!objectsFromJson(objectsToJson({object}), 2, &restored, &error, 16));
    }

    QTemporaryDir dir; QFile file(dir.filePath("loss.csv")); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("time,A\n0,1\n1,2\n2,3\n5,4\n6,5\n7,6\n"); file.close();
    AppController controller; QVERIFY(controller.loadCsv(file.fileName())); QTRY_VERIFY(!controller.loading());
    const auto id = controller.createDataObject("rates", "processing");
    const auto ruleId = controller.addObjectRule(id, {{"name", "loss"}, {"operation", "packetloss"}, {"inputs", QVariantList{"raw:0"}}, {"samplePeriod", 1}, {"rateMode", "cumulative"}});
    QVERIFY(!ruleId.isEmpty()); QTRY_VERIFY(!controller.deriving());
    auto definition = [&] { return controller.processingConfigurations()[0].toMap()["rules"].toList()[0].toMap(); };
    const auto outputs = definition()["outputs"].toList();
    QVERIFY(outputs[0].toMap()["name"].toString().contains(QStringLiteral("累计丢包率")));
    const int row = outputs[0].toMap()["series"].toInt(); controller.setSignalPen(row, QColor("magenta"), 4, Qt::DashLine);
    QVERIFY(controller.editObjectRule(id, ruleId, {{"name", "loss"}, {"operation", "packetloss"}, {"inputs", QVariantList{"raw:0"}}, {"rateMode", "window"}, {"rateWindowSeconds", 2}}));
    QTRY_VERIFY(!controller.deriving());
    QCOMPARE(definition()["outputs"].toList()[0].toMap()["id"], outputs[0].toMap()["id"]);
    QVERIFY(controller.signalName(row).contains(QStringLiteral("窗口丢包率"))); QCOMPARE(controller.signalColor(row), QColor("magenta"));
    const auto path = dir.filePath("rates.disession"); QVERIFY(controller.saveSession(path));
    AppController recovered; QVERIFY(recovered.restoreSession(path)); QTRY_VERIFY(!recovered.loading() && !recovered.deriving());
    const auto recoveredRule = recovered.processingConfigurations()[0].toMap()["rules"].toList()[0].toMap();
    QCOMPARE(recoveredRule["rateMode"].toString(), QString("window")); QCOMPARE(recoveredRule["rateWindowSeconds"].toDouble(), 2.0);
    QCOMPARE(recovered.signalColor(row), QColor("magenta"));
    SessionDocument legacyDocument; QVERIFY(readSessionDocument(path, &legacyDocument, &error));
    auto &legacyPair = legacyDocument.objects[0].rules[0];
    const int legacyCountRow = legacyDocument.series.size();
    legacyPair.outputs.prepend({"legacyLost", QStringLiteral("loss.累计丢包数"), legacyCountRow});
    auto legacySignal = legacyDocument.series[row]; legacySignal.outputId = "legacyLost";
    legacySignal.name = legacySignal.originalName = QStringLiteral("loss.累计丢包数");
    legacyDocument.series.append(legacySignal);
    const auto legacyPath = dir.filePath("legacy-pair.disession"); QVERIFY(writeSessionDocument(legacyPath, legacyDocument, &error));
    AppController legacyController; QVERIFY(legacyController.restoreSession(legacyPath)); QTRY_VERIFY(!legacyController.loading() && !legacyController.deriving());
    const auto legacyId = legacyController.processingConfigurations()[0].toMap()["id"].toString();
    QVERIFY(!legacyController.duplicateDataObject(legacyId).isEmpty()); QTRY_VERIFY(!legacyController.deriving());
    QCOMPARE(legacyController.processingConfigurations().last().toMap()["rules"].toList()[0].toMap()["outputs"].toList().size(), 2);

    const auto defaultId = controller.createDataObject("default", "processing");
    QVERIFY(controller.identifyPacketLoss(defaultId)); QTRY_VERIFY(!controller.deriving());
    const auto defaultRule = controller.processingConfigurations().last().toMap()["rules"].toList()[0].toMap();
    QCOMPARE(defaultRule["rateMode"].toString(), QString("window"));
    QCOMPARE(defaultRule["outputs"].toList().size(), 1);
    QVERIFY(defaultRule["outputs"].toList()[0].toMap()["name"].toString().contains(QStringLiteral("窗口丢包率")));
    QVERIFY(controller.identifyPacketLoss(id, "window", 3)); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.processingConfigurations()[0].toMap()["rules"].toList().size(), 2);
    QVERIFY(controller.identifyPacketLoss(id, "window", 3)); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.processingConfigurations()[0].toMap()["rules"].toList().size(), 2);
}
void ObjectDataTest::packetLossCounters()
{
    ObjectRule rule; rule.id = "loss"; rule.name = "loss"; rule.operation = "packetloss";
    rule.lossMode = "sequence"; rule.inputs = {"input"};
    rule.outputs = {{"lost", "lost", 1}, {"rate", "rate", 2}};
    DataObject object{"o", "object", {{"input", "input", "scalar", 0}}, {rule}};
    std::atomic_bool cancel{false}; QString error;
    auto run = [&](QVector<double> values, QVector<double> time = {}) {
        if (time.isEmpty()) for (int i = 0; i < values.size(); ++i) time.append(i);
        PlotSeriesInput raw; raw.id = 0; raw.time = time; raw.values = values; raw.timeOffset = 10;
        PlotSeriesStore store; store.appendSeries({raw});
        return evaluateObjects({object}, store.snapshot({0}), cancel);
    };
    auto &r = object.rules[0];
    auto result = run({10, 11, 14, 14, 15});
    QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series[0]->values, QVector<double>({0, 0, 2, 2, 2}));
    QVERIFY(qAbs(result.series[1]->values.last() - 100.0 / 3) < 1e-10);
    QCOMPARE(result.states["loss"].duplicateSamples, 1);
    QCOMPARE(result.series[0]->timeOffset, 10.0); QVERIFY(result.series[0]->step);
    r.counterModulus = 256;
    result = run({254, 255, 0, 1, 4}); QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series[0]->values.last(), 2.0);
    result = run({254, 1}); QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series[0]->values.last(), 2.0);
    for (const auto &values : {QVector<double>{100, 0}, QVector<double>{0, 128}, QVector<double>{0, 256},
        QVector<double>{0, qQNaN()}, QVector<double>{0, qInf()}, QVector<double>{0, 1.5}, QVector<double>{0, -1}}) {
        result = run(values); QVERIFY(!result.errors.isEmpty()); QVERIFY(result.series[0]->values.isEmpty());
    }
    r.counterModulus = 0; QVERIFY(!run({10, 0}).errors.isEmpty());
    r.counterStep = 2;
    result = run({0, 2, 8}); QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series[0]->values.last(), 2.0);
    QVERIFY(!run({0, 3}).errors.isEmpty());
    r.counterStep = 1; r.lossMode = "stride"; r.expectedStride = 10;
    result = run({100, 110, 123, 133}); QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series[0]->values.last(), 3.0);
    QVERIFY(qAbs(result.series[1]->values.last() - 300.0 / 34) < 1e-10);
    QVERIFY(!run({100, 109}).errors.isEmpty());
    r.lossMode = "rate"; r.counterRate = 1000; r.counterTolerance = 1;
    result = run({100, 109, 120}, {0, .01, .02}); QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series[0]->values.last(), 0.0);
    result = run({100, 108, 125}, {0, .01, .03}); QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.series[0]->values, QVector<double>({0, 2, 5}));
    QVERIFY(qAbs(result.series[1]->values.last() - 100.0 / 6) < 1e-10);
    result = run({100, 100}, {0, .01}); QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series[0]->values.last(), 10.0);
    QCOMPARE(result.series[1]->values.last(), 100.0);
    QVERIFY(!run({100, 120}, {0, .01}).errors.isEmpty());
    QVERIFY(!run({100, 110}, {0, 0}).errors.isEmpty());
    r.counterModulus = 256;
    result = run({250, 4}, {0, .01}); QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series[0]->values.last(), 0.0);
    QVERIFY(!run({250, 4}, {0, 1}).errors.isEmpty()); // Multiple wraps cannot be inferred.
    QVERIFY(validateObjects({object}, 3, &error));
    const auto json = objectsToJson({object}); QVector<DataObject> restored;
    QVERIFY(objectsFromJson(json, 3, &restored, &error));
    QCOMPARE(restored[0].rules[0].lossMode, QString("rate")); QCOMPARE(restored[0].rules[0].counterRate, 1000.0);
    QCOMPARE(restored[0].rules[0].counterModulus, 256.0); QCOMPARE(restored[0].rules[0].counterTolerance, 1.0);
    QVERIFY(!objectsFromJson(json, 3, &restored, &error, 13));
    auto legacyObject = json[0].toObject(); auto legacyRules = legacyObject["rules"].toArray();
    auto legacyRule = legacyRules[0].toObject();
    for (const auto &key : {"lossMode", "counterStep", "expectedStride", "counterModulus", "counterRate", "counterTolerance"}) legacyRule.remove(key);
    legacyRules[0] = legacyRule; legacyObject["rules"] = legacyRules;
    QVERIFY(objectsFromJson(QJsonArray{legacyObject}, 3, &restored, &error, 13));
    QCOMPARE(restored[0].rules[0].lossMode, QString("time"));
    QVERIFY(!objectsFromJson(QJsonArray{legacyObject}, 3, &restored, &error, 14));
    PlotSeriesInput input; input.id = 0; input.time = {0, .01}; input.values = {250, 4};
    PlotSeriesStore keyStore; keyStore.appendSeries({input}); const auto snapshot = keyStore.snapshot({0});
    const auto evaluated = evaluateObjects({object}, snapshot, cancel);
    auto publishedSnapshot = snapshot; publishedSnapshot.series += evaluated.series;
    QVERIFY(evaluated.errors.isEmpty()); QVERIFY(invalidatedObjectRules({object}, publishedSnapshot, evaluated.states).isEmpty());
    for (double ObjectRule::*member : {&ObjectRule::counterStep, &ObjectRule::expectedStride, &ObjectRule::counterModulus,
        &ObjectRule::counterRate, &ObjectRule::counterTolerance}) {
        const double before = r.*member; r.*member = before + 1;
        QVERIFY(invalidatedObjectRules({object}, publishedSnapshot, evaluated.states).contains("loss")); r.*member = before;
    }
    r.lossMode = "sequence"; QVERIFY(invalidatedObjectRules({object}, publishedSnapshot, evaluated.states).contains("loss")); r.lossMode = "rate";

    auto bad = json[0].toObject(); auto rules = bad["rules"].toArray(); auto config = rules[0].toObject();
    config["counterRate"] = "1000"; rules[0] = config; bad["rules"] = rules;
    QVERIFY(!objectsFromJson(QJsonArray{bad}, 3, &restored, &error));
    r.counterStep = 0; QVERIFY(!validateObjects({object}, 3, &error)); r.counterStep = 1;
    r.counterModulus = 1; QVERIFY(!validateObjects({object}, 3, &error)); r.counterModulus = 256;
    r.counterRate = qInf(); QVERIFY(!validateObjects({object}, 3, &error)); r.counterRate = 1000;
    cancel = true; QVERIFY(run({250, 4}).series.isEmpty());
}
void ObjectDataTest::packetLossLifecycle()
{
    QTemporaryDir dir; QFile file(dir.filePath("loss.csv"));
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("time,value\n0,1\n1,1\n2,1\n5,1\n6,1\n7,1\n"); file.close();
    AppController controller; QVERIFY(controller.loadCsv(file.fileName())); QTRY_VERIFY(!controller.loading() && !controller.deriving());
    const auto object = controller.createDataObject("packets", "processing");
    const auto field = controller.bindObjectField(object, {}, "input", "scalar", 0);
    QVariantMap config{{"name", "loss"}, {"operation", "packetloss"}, {"inputs", QVariantList{field}}, {"rateMode", "count"}};
    const auto id = controller.addObjectRule(object, config); QVERIFY(!id.isEmpty()); QTRY_VERIFY(!controller.deriving());
    const auto currentRule = [&] { return controller.dataObjects()[0].toMap()["rules"].toList()[0].toMap(); };
    QCOMPARE(currentRule()["effectiveSamplePeriod"].toDouble(), 1.0);
    const auto outputs = currentRule()["outputs"].toList(); QCOMPARE(outputs.size(), 1);
    QCOMPARE(outputs[0].toMap()["preview"].toString(), QString("0 / 0 / 0 / 2 / 2 / 2"));
    config["samplePeriod"] = .5; QVERIFY(controller.editObjectRule(object, id, config)); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(currentRule()["effectiveSamplePeriod"].toDouble(), .5);
    QCOMPARE(currentRule()["outputs"].toList()[0].toMap()["id"], outputs[0].toMap()["id"]);
    QCOMPARE(currentRule()["outputs"].toList()[0].toMap()["preview"].toString(), QString("0 / 1 / 2 / 7 / 8 / 9"));
    const auto path = dir.filePath("loss.disession"); QVERIFY(controller.saveSession(path));
    AppController restored; QVERIFY(restored.restoreSession(path)); QTRY_VERIFY(!restored.loading() && !restored.deriving());
    const auto savedRule = restored.dataObjects()[0].toMap()["rules"].toList()[0].toMap();
    QCOMPARE(savedRule["samplePeriod"].toDouble(), .5); QCOMPARE(savedRule["outputs"], currentRule()["outputs"]);
    QVERIFY(!restored.sessionModified());
    QVERIFY(!restored.duplicateDataObject(object).isEmpty()); QTRY_VERIFY(!restored.deriving());
    QCOMPARE(restored.dataObjects()[1].toMap()["rules"].toList()[0].toMap()["samplePeriod"].toDouble(), .5);
    config["lossMode"] = "rate"; config["counterRate"] = 1; config["counterModulus"] = 256;
    config["counterTolerance"] = .5; config["counterStep"] = 1; config["expectedStride"] = 2;
    QVERIFY(controller.editObjectRule(object, id, config)); QTRY_VERIFY(!controller.deriving());
    QVERIFY(controller.saveSession(path));
    AppController counterRestored; QVERIFY(counterRestored.restoreSession(path)); QTRY_VERIFY(!counterRestored.loading() && !counterRestored.deriving());
    const auto counterRule = counterRestored.dataObjects()[0].toMap()["rules"].toList()[0].toMap();
    QCOMPARE(counterRule["lossMode"].toString(), QString("rate")); QCOMPARE(counterRule["counterTolerance"].toDouble(), .5);
    QCOMPARE(counterRule["counterModulus"].toDouble(), 256.0);
    QVERIFY(!counterRestored.duplicateDataObject(object).isEmpty()); QTRY_VERIFY(!counterRestored.deriving());
    QCOMPARE(counterRestored.dataObjects()[1].toMap()["rules"].toList()[0].toMap()["expectedStride"].toDouble(), 2.0);

}
void ObjectDataTest::mergeSharedTimelineMatchesLegacy()
{
    QVector<PlotSeriesInput> raw; QVector<int> ids; QVector<DataObject> objects;
    const QVector<double> first{2, 0, 2, 4}, second{1, 2, 3, 4};
    for (int group = 0; group < 6; ++group) {
        for (int part = 0; part < 2; ++part) {
            PlotSeriesInput s; s.id = group * 2 + part; s.time = part ? second : first;
            for (double time : std::as_const(s.time)) s.values.append(group == 2 && time == 4 ? qQNaN() : time + group);
            if (group == 3 && part == 1) s.values[1] += 1; // Conflict remains visible.
            if (group == 4) s.timeOffset = 10;
            raw.append(s); ids.append(s.id);
        }
        DataObject object; object.id = "o" + QString::number(group); object.name = object.id;
        object.fields = {{object.id + "a", "a", "scalar", group * 2}, {object.id + "b", "b", "scalar", group * 2 + 1}};
        ObjectRule rule; rule.id = object.id + "r"; rule.name = "merge"; rule.operation = "merge";
        rule.inputs = {object.fields[0].id, object.fields[1].id}; rule.outputs = {{rule.id + "out", "out", 12 + group}};
        object.rules = {rule}; objects.append(object);
    }
    PlotSeriesStore store; store.appendSeries(raw); const auto snapshot = store.snapshot(ids); std::atomic_bool cancel{false};
    QVERIFY(snapshot.series[0]->time.constData() == snapshot.series[2]->time.constData());
    const auto legacy = evaluateObjects(objects, snapshot, cancel, 0, nullptr, false);
    const auto shared = evaluateObjects(objects, snapshot, cancel);
    QCOMPARE(shared.errors, legacy.errors); QVERIFY(shared.mergeTimelineHits >= 3); QCOMPARE(shared.series.size(), legacy.series.size());
    for (int i = 0; i < shared.series.size(); ++i) {
        QCOMPARE(shared.series[i]->time, legacy.series[i]->time);
        QCOMPARE(shared.series[i]->values.size(), legacy.series[i]->values.size());
        for (int j = 0; j < shared.series[i]->values.size(); ++j) {
            const double a = shared.series[i]->values[j], b = legacy.series[i]->values[j]; QVERIFY(a == b || (std::isnan(a) && std::isnan(b)));
        }
    }
    QVERIFY(shared.series[0]->time.constData() == shared.series[1]->time.constData());
    auto invalid = std::make_shared<PlotSeriesData>(*snapshot.series[0]); invalid->time[0] = qQNaN();
    auto invalidSnapshot = snapshot; invalidSnapshot.series[0] = invalid;
    QCOMPARE(evaluateObjects(objects, invalidSnapshot, cancel).errors, evaluateObjects(objects, invalidSnapshot, cancel, 0, nullptr, false).errors);
    cancel.store(true); QVERIFY(evaluateObjects(objects, snapshot, cancel).series.isEmpty());
}
void ObjectDataTest::mergeSamples()
{
    PlotSeriesStore store;
    store.replaceSeries({{0, {2, 0}, {20, 0}}, {1, {1, 3}, {10, 30}},
        {2, {2, 4}, {20, qQNaN()}}, {3, {4}, {40}}});
    DataObject object{"object", "merged", {{"a", "A", "scalar", 0}, {"b", "B", "scalar", 1},
        {"c", "C", "scalar", 2}, {"d", "D", "scalar", 3}}, {}};
    ObjectRule rule; rule.id = "merge"; rule.name = "merged"; rule.operation = "merge";
    rule.inputs = {"a", "b", "c", "d"}; rule.outputs = {{"out", "merged", 4}}; object.rules = {rule};
    QString error; QVERIFY2(validateObjects({object}, 5, &error), qPrintable(error));
    QVector<DataObject> restored;
    QVERIFY2(objectsFromJson(objectsToJson({object}), 5, &restored, &error, 11), qPrintable(error));
    QCOMPARE(restored.first().rules.first().inputs, rule.inputs);
    std::atomic_bool cancel{false};
    auto result = evaluateObjects(restored, store.snapshot({0, 1, 2, 3}), cancel);
    QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series.size(), 1);
    QCOMPARE(result.series.first()->time, QVector<double>({0, 1, 2, 3, 4}));
    QCOMPARE(result.series.first()->values, QVector<double>({0, 10, 20, 30, 40}));
    auto snapshot = store.snapshot({0, 1, 2, 3});
    auto shifted = std::make_shared<PlotSeriesData>(*snapshot.series[3]);
    shifted->timeOffset = -2; snapshot.series[3] = shifted;
    result = evaluateObjects({object}, snapshot, cancel);
    QVERIFY(result.errors.contains("merge")); QVERIFY(result.series.first()->values.isEmpty());
    shifted->timeOffset = 1;
    result = evaluateObjects({object}, snapshot, cancel);
    QVERIFY(result.errors.isEmpty()); QCOMPARE(result.series.first()->time.last(), 5.0);
    object.rules[0].inputs = {"a", "a"}; QVERIFY(!validateObjects({object}, 5, &error));
    cancel = true; QVERIFY(evaluateObjects(restored, snapshot, cancel).series.isEmpty());
    QTemporaryDir dir;
    for (int i = 0; i < 2; ++i) {
        QFile file(dir.filePath(QString::number(i) + ".csv")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(i == 0 ? "time,X\n0,0\n2,20\n" : "time,X\n1,10\n3,30\n");
    }
    AppController controller;
    for (int i = 0; i < 2; ++i) {
        QVERIFY(controller.loadCsv(dir.filePath(QString::number(i) + ".csv"))); QTRY_VERIFY(!controller.loading() && !controller.deriving());
    }
    const auto owner = controller.createDataObject("merged", "processing");
    const auto a = controller.bindObjectField(owner, {}, "A", "scalar", 0);
    const auto b = controller.bindObjectField(owner, {}, "B", "scalar", 1);
    QVERIFY(!controller.addObjectRule(owner, {{"name", "merged"}, {"operation", "merge"}, {"inputs", QVariantList{a, b}}}).isEmpty());
    QTRY_VERIFY(!controller.deriving());
    const auto session = dir.filePath("merge.disession"); QVERIFY(controller.saveSession(session));
    AppController loaded; QVERIFY(loaded.restoreSession(session)); QTRY_VERIFY(!loaded.loading() && !loaded.deriving());
    const auto loadedRule = loaded.dataObjects().first().toMap()["rules"].toList().first().toMap();
    QVERIFY(loadedRule["error"].toString().isEmpty());
    QCOMPARE(loadedRule["operation"].toString(), QString("merge"));
    QCOMPARE(loadedRule["outputs"].toList().first().toMap()["preview"].toString(), QString("0 / 10 / 20 / 30"));
}

void ObjectDataTest::dependencyPlanPreservesIndependentBranches()
{
    PlotSeriesStore store;
    store.replaceSeries({{0, {0, 1}, {2, 3}}, {1, {0, 1}, {20, 30}},
        {2, {}, {}}, {3, {}, {}}, {4, {}, {}}, {5, {}, {}}});
    DataObject object{"object", "device", {{"x", "X", "scalar", 0}, {"y", "Y", "scalar", 1}}, {}};
    auto rule = [](QString id, QString input, QString output, int row, double factor) {
        ObjectRule r; r.id = id; r.name = id; r.inputs = {input}; r.factor = factor; r.outputs = {{output, output, row}}; return r;
    };
    object.rules = {rule("a", "x", "a-out", 2, 2), rule("a-child", "a-out", "a-child-out", 3, 10),
        rule("b", "y", "b-out", 4, 3), rule("b-child", "b-out", "b-child-out", 5, 10)};
    const QVector<int> ids{0, 1, 2, 3, 4, 5}; std::atomic_bool cancel{false};
    auto initial = evaluateObjects({object}, store.snapshot(ids), cancel);
    store.publishComputed(initial.series);
    auto states = initial.states;
    QVERIFY(invalidatedObjectRules({object}, store.snapshot(ids), states).isEmpty());
    store.updateSeriesPen(0, Qt::red, 4, Qt::DashLine);
    object.name = "renamed"; object.rules[0].name = "renamed rule";
    QVERIFY(invalidatedObjectRules({object}, store.snapshot(ids), states).isEmpty());
    const auto preserved = store.snapshot({4, 5}).series;
    object.fields[0].series = 1;
    auto dirty = invalidatedObjectRules({object}, store.snapshot(ids), states);
    QCOMPARE(dirty, (QSet<QString>{"a", "a-child"}));
    const auto partial = evaluateObjects({object}, store.snapshot(ids), cancel, 32, &dirty);
    QCOMPARE(partial.series.size(), 2);
    QCOMPARE(partial.series[0]->values, QVector<double>({40, 60}));
    QCOMPARE(partial.series[1]->values, QVector<double>({400, 600}));
    store.publishComputed(partial.series);
    for (auto it = partial.states.cbegin(); it != partial.states.cend(); ++it) states.insert(it.key(), it.value());
    QCOMPARE(store.snapshot({4, 5}).series, preserved);
    QVERIFY(invalidatedObjectRules({object}, store.snapshot(ids), states).isEmpty());
    object.rules[1].factor = 5;
    dirty = invalidatedObjectRules({object}, store.snapshot(ids), states);
    QCOMPARE(dirty, (QSet<QString>{"a-child"}));
    cancel = true;
    const auto cancelled = evaluateObjects({object}, store.snapshot(ids), cancel, 0, &dirty);
    QVERIFY(cancelled.series.isEmpty()); QVERIFY(cancelled.states.isEmpty());
    cancel = false;
    const auto edited = evaluateObjects({object}, store.snapshot(ids), cancel, 0, &dirty);
    QCOMPARE(edited.series[0]->values, QVector<double>({200, 300}));

    QTemporaryDir dir; QFile file(dir.filePath("branches.csv"));
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("time,X,Y\n0,2,20\n1,3,30\n"); file.close();
    AppController controller; QVERIFY(controller.loadCsv(file.fileName())); QTRY_VERIFY(!controller.loading() && !controller.deriving());
    const auto owner = controller.createDataObject("branches", "processing");
    const auto x = controller.bindObjectField(owner, {}, "X", "scalar", 0);
    const auto y = controller.bindObjectField(owner, {}, "Y", "scalar", 1);
    QVERIFY(!controller.addObjectRule(owner, {{"name", "left"}, {"operation", "scale"}, {"inputs", QVariantList{x}}, {"factor", 2}}).isEmpty());
    QVERIFY(!controller.addObjectRule(owner, {{"name", "right"}, {"operation", "scale"}, {"inputs", QVariantList{y}}, {"factor", 3}}).isEmpty());
    QTRY_VERIFY(!controller.deriving());
    auto rules = controller.dataObjects()[0].toMap()["rules"].toList();
    const auto leftOutput = rules[0].toMap()["outputs"].toList()[0].toMap()["id"].toString();
    QVERIFY(!controller.addObjectRule(owner, {{"name", "downstream"}, {"operation", "scale"}, {"inputs", QVariantList{leftOutput}}, {"factor", 10}}).isEmpty());
    QTRY_VERIFY(!controller.deriving());
    QVERIFY(!controller.bindObjectField(owner, x, "X", "scalar", 1).isEmpty());
    rules = controller.dataObjects()[0].toMap()["rules"].toList();
    QCOMPARE(rules[1].toMap()["outputs"].toList()[0].toMap()["preview"].toString(), QString("60 / 90"));
    QTRY_VERIFY(!controller.deriving());
    rules = controller.dataObjects()[0].toMap()["rules"].toList();
    QCOMPARE(rules[0].toMap()["outputs"].toList()[0].toMap()["preview"].toString(), QString("40 / 60"));
    QCOMPARE(rules[2].toMap()["outputs"].toList()[0].toMap()["preview"].toString(), QString("400 / 600"));
}
void ObjectDataTest::bitsAndSignedFields()
{
    const auto snapshot = rawSnapshot({0, 1, 5, 128, 255, 256, -1, 1.5, qQNaN()});
    DataObject object{"object", "device", {{"source", "flags", "scalar", 0}}, {bitRule(8, 0, 8)}};
    std::atomic_bool cancel{false};
    const auto result = evaluateObjects({object}, snapshot, cancel);
    QCOMPARE(result.series.size(), 8);
    for (int k = 0; k < 8; ++k) {
        const auto data = result.series[k]; QVERIFY(data->step);
        QCOMPARE(data->pointAt(2).y(), double((5 >> k) & 1));
        QCOMPARE(data->pointAt(3).y(), k == 7 ? 1.0 : 0.0);
        QCOMPARE(data->pointAt(4).y(), 1.0);
        for (int i = 5; i < 9; ++i) QVERIFY(std::isnan(data->pointAt(i).y()));
    }
    QVERIFY(result.errors["rule"].contains("4"));
    object.rules = {bitRule(8, 4, 4, false, true)};
    const auto signedResult = evaluateObjects({object}, snapshot, cancel);
    QCOMPARE(signedResult.series.first()->pointAt(3).y(), -8.0);
    QCOMPARE(signedResult.series.first()->pointAt(4).y(), -1.0);
    object.rules = {bitRule(32, 31, 1)};
    QCOMPARE(evaluateObjects({object}, rawSnapshot({4294967295.0}), cancel).series.first()->pointAt(0).y(), 1.0);
    cancel = true; QVERIFY(evaluateObjects({object}, snapshot, cancel).series.isEmpty());
}
void ObjectDataTest::expressionsAndTimeBases()
{
    DataObject object{"object", "device", {{"source", "X", "scalar", 0}}, {}};
    ObjectRule rule; rule.id = "calc"; rule.name = "norm"; rule.operation = "expression";
    rule.inputs = {"source"}; rule.expression = "max(abs(-x), 2) + 3*4"; rule.outputs = {{"value", "value", 1}};
    object.rules = {rule}; std::atomic_bool cancel{false};
    auto result = evaluateObjects({object}, rawSnapshot({-5, 1, qQNaN()}), cancel);
    const auto limited = evaluateObjects({object}, rawSnapshot({3, 4, -2}), cancel, 512 * 1024 * 1024 - 8);
    QVERIFY(limited.budgetLimitedRules.contains(object.rules[0].id));
    QVERIFY(limited.series.first()->values.isEmpty());
    QCOMPARE(result.series.first()->pointAt(0).y(), 17.0);
    QCOMPARE(result.series.first()->pointAt(1).y(), 14.0);
    QVERIFY(std::isnan(result.series.first()->pointAt(2).y()));
    object.rules[0].expression = "x >= 3 && !(x == 4)";
    result = evaluateObjects({object}, rawSnapshot({2, 3, 4}), cancel);
    QCOMPARE(result.series.first()->values, QVector<double>({0, 1, 0}));
    QVERIFY(!validateObjectExpression("sqrt(x);anything", 1).isEmpty());
    QVERIFY(!validateObjectExpression("y", 1).isEmpty());
    object.rules[0].expression = "min(sqrt(-1), x)";
    QVERIFY(std::isnan(evaluateObjects({object}, rawSnapshot({3}), cancel).series.first()->pointAt(0).y()));
    auto other = std::make_shared<PlotSeriesData>(*rawSnapshot({4, 4}).series.first()); other->id = 2; other->timeOffset = .1;
    auto snapshot = rawSnapshot({3, 3}); snapshot.series.append(other);
    object.fields.append({"other", "Y", "scalar", 2});
    object.rules[0].inputs = {"source", "other"}; object.rules[0].expression = "x+y";
    result = evaluateObjects({object}, snapshot, cancel);
    QVERIFY(result.series.first()->values.isEmpty()); QVERIFY(result.errors["calc"].contains("时间基"));
}
void ObjectDataTest::ownershipAndSchema()
{
    SessionDocument state; state.files = {"data.csv"};
    SessionSignal raw; raw.file = 0; raw.table = raw.column = 0; raw.name = raw.originalName = "A"; raw.color = Qt::red;
    state.series = {raw};
    DataObject object{"obj", "device", {{"source", "word", "scalar", 0}}, {bitRule(8, 0, 1)}};
    state.objects = {object}; SessionSignal output = raw; output.file = -1; output.objectId = "obj"; output.outputId = "out0"; state.series.append(output);
    SessionDocument parsed; QString error;
    QVERIFY2(sessionFromJson(sessionToJson(state), &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.objects.size(), 2);
    QCOMPARE(parsed.objects[0].type, QStringLiteral("general"));
    QVERIFY(parsed.objects[0].rules.isEmpty());
    QCOMPARE(parsed.objects[1].type, QStringLiteral("processing"));
    auto old = sessionToJson(state); old["version"] = 7;
    old["objects"] = objectsToJson(state.objects); old.remove("processing");
    auto legacySignals = old["signals"].toArray();
    for (int i = 0; i < legacySignals.size(); ++i) {
        auto signal = legacySignals[i].toObject(); signal.remove("processingId");
        signal["objectId"] = state.series[i].objectId; legacySignals[i] = signal;
    }
    old["signals"] = legacySignals;
    auto oldObjects = old["objects"].toArray(); auto oldObject = oldObjects[0].toObject(); oldObject.remove("type"); oldObjects[0] = oldObject; old["objects"] = oldObjects;
    QVERIFY(sessionFromJson(old, &parsed, &error));
    old["version"] = 8; QVERIFY(!sessionFromJson(old, &parsed, &error));
    state.objects[0].type = "invalid"; QVERIFY(!sessionFromJson(sessionToJson(state), &parsed, &error)); state.objects[0].type = "general";
    state.series[1].objectId = "missing";
    QVERIFY(!sessionFromJson(sessionToJson(state), &parsed, &error));
    state.series[1].objectId = "obj"; state.objects[0].rules[0].inputs = {"out0"};
    QVERIFY(!sessionFromJson(sessionToJson(state), &parsed, &error));
    state.objects[0].rules[0].inputs = {"source"}; state.objects[0].fields[0].series = 1;
    QVERIFY(!sessionFromJson(sessionToJson(state), &parsed, &error));
}
void ObjectDataTest::controllerLifecycleAndRestore()
{
    QTemporaryDir dir; const auto path = dir.filePath("word.csv"); QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("time,word,x,y,z\n0,5,3,4,0\n1,128,0,0,5\n2,255,1,2,2\n"); file.close();
    AppController controller; PlotItem plot; plot.setWidth(600); plot.setHeight(300); controller.attachPlot(&plot);
    QVERIFY(controller.loadCsv(path)); QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && !controller.deriving(), 5000);
    QVERIFY(controller.addObjectRule("missing", {{"name", "bits"}, {"operation", "bits"}, {"inputs", QVariantList{"source"}}}).isEmpty());
    const auto objectId = controller.createDataObject("device", "processing"); QVERIFY(!objectId.isEmpty());
    const auto field = controller.bindObjectField(objectId, {}, "状态", "scalar", 0); QVERIFY(!field.isEmpty());
    const auto rule = controller.addObjectRule(objectId, {{"name", "flags"}, {"operation", "bits"}, {"inputs", QVariantList{field}}, {"bitCount", 8}});
    QVERIFY(!rule.isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(!controller.deriving(), 5000);
    QCOMPARE(controller.signalCount(), 12); controller.selectSignal(4);
    plot.setCursorMode(1); plot.setCursorX(0, 1);
    QVERIFY(!plot.cursorReadouts().isEmpty()); QCOMPARE(plot.cursorReadouts().first().toMap()["y"].toDouble(), 1.0);
    const auto session = dir.filePath("saved.disession"); QVERIFY(controller.saveSession(session));
    const auto xlsx = dir.filePath("derived.xlsx"); QVERIFY(controller.exportXlsx(xlsx, AppController::PlottedSignals));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);
    const auto workbook = readXlsxWorkbook(xlsx, {}, {}); QVERIFY2(workbook.error.isEmpty(), qPrintable(workbook.error));
    QCOMPARE(workbook.tables.size(), 1); QCOMPARE(workbook.tables.first().signalNames, QStringList{"flags.bit0"});
    QCOMPARE(workbook.tables.first().values.first(), QVector<double>({1, 0, 1}));
    QCOMPARE(workbook.tables.first().time, QVector<double>({0, 1, 2}));
    AppController restored; PlotItem target; target.setWidth(600); target.setHeight(300); restored.attachPlot(&target);
    QVERIFY(restored.restoreSession(session)); QTRY_VERIFY_WITH_TIMEOUT(!restored.loading() && !restored.deriving(), 5000);
    QCOMPARE(restored.signalCount(), 12); QCOMPARE(restored.dataObjects().size(), 1);
    QVERIFY(!restored.sessionModified()); // Recomputing results must not dirty a restored document.
    target.setCursorMode(1); target.setCursorX(1, 1);
    QCOMPARE(target.cursorReadouts().first().toMap()["y"].toDouble(), 0.0);
    const auto copy = restored.duplicateDataObject(objectId); QVERIFY(!copy.isEmpty()); QTRY_VERIFY(!restored.deriving());
    QCOMPARE(restored.signalCount(), 20);
    QVERIFY(restored.removeDataObject(copy)); QTRY_VERIFY(!restored.deriving()); QCOMPARE(restored.signalCount(), 12);
    QVERIFY(!restored.removeObjectField(objectId, field));
    QVERIFY(restored.bindObjectField(objectId, field, "状态", "scalar", -1).size() > 0); QTRY_VERIFY(!restored.deriving());
    QVERIFY(target.cursorReadouts().isEmpty());
    QVERIFY(restored.bindObjectField(objectId, field, "状态", "scalar", 0).size() > 0); QTRY_VERIFY(!restored.deriving());
    QVERIFY(!target.cursorReadouts().isEmpty());
    QVERIFY(restored.removeFile("word.csv")); QTRY_VERIFY(!restored.deriving()); QCOMPARE(restored.signalCount(), 0);
    const auto obj = restored.dataObjects().first().toMap(); QCOMPARE(obj["fields"].toList().first().toMap()["series"].toInt(), -1);
    QVERIFY(restored.saveSession(dir.filePath("without-source.disession")));
    QVERIFY(restored.removeDataObject(objectId)); QCOMPARE(restored.signalCount(), 0);
}
void ObjectDataTest::removingFilesCleansDerivedSignalsAndLegends()
{
    QTemporaryDir dir; QVERIFY(QDir(dir.path()).mkpath("left")); QVERIFY(QDir(dir.path()).mkpath("right"));
    const auto left = dir.filePath("left/same.csv"), right = dir.filePath("right/same.csv");
    for (const auto &path : {left, right}) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("time,A\n0,10\n1,20\n3,40\n");
    }
    AppController controller; controller.setLayout(1, 2);
    PlotItem first, second; first.setWidth(600); first.setHeight(300); second.setWidth(600); second.setHeight(300);
    controller.attachPlot(&first, 0); controller.attachPlot(&second, 1);
    QCOMPARE(controller.loadFiles(QVariantList{left, right}), 2); QTRY_VERIFY(!controller.loading());
    const auto leftGroup = controller.signalModel()->groupAt(0), rightGroup = controller.signalModel()->groupAt(1);
    QVERIFY(leftGroup != rightGroup);
    const auto merged = controller.mergeSignals({QVariantMap{{"name", "combined"}, {"signals", QVariantList{0, 1}}}});
    QVERIFY(!merged.contains("error")); QTRY_VERIFY(!controller.deriving());
    const auto created = merged["outputs"].toList()[0].toMap(); const auto objectId = created["object"].toString();
    const auto definition = [&](const QString &object) {
        for (const auto &value : controller.dataObjects()) if (value.toMap()["id"].toString() == object) return value.toMap();
        return QVariantMap{};
    };
    const auto rule = [&](const QString &object, const QString &id) {
        for (const auto &value : definition(object)["rules"].toList()) if (value.toMap()["id"].toString() == id) return value.toMap();
        return QVariantMap{};
    };
    const auto mergeId = created["rule"].toString();
    const auto combined = rule(objectId, mergeId)["outputs"].toList()[0].toMap();
    const auto downId = controller.addObjectRule(objectId, {{"name", "downstream"}, {"operation", "scale"}, {"inputs", QVariantList{combined["id"]}}, {"factor", 2}});
    const auto survivingField = definition(objectId)["fields"].toList()[1].toMap()["id"].toString();
    const auto keepId = controller.addObjectRule(objectId, {{"name", "independent"}, {"operation", "scale"}, {"inputs", QVariantList{survivingField}}, {"factor", 2}});
    const auto warningId = controller.addObjectRule(objectId, {{"name", "existing diagnostic"}, {"operation", "expression"}, {"inputs", QVariantList{survivingField}}, {"expression", "1/0"}});
    QVERIFY(!downId.isEmpty() && !keepId.isEmpty() && !warningId.isEmpty()); QTRY_VERIFY(!controller.deriving());
    const auto detector = controller.createDataObject("detection", "packetloss");
    QVERIFY(controller.identifyPacketLoss(detector)); QTRY_VERIFY(!controller.deriving());
    const auto detected = definition(detector)["rules"].toList(); QCOMPARE(detected.size(), 2);
    const auto lostOutput = detected[0].toMap()["outputs"].toList()[0].toMap();
    const auto packetDown = controller.addObjectRule(detector, {{"name", "packet downstream"}, {"operation", "scale"}, {"inputs", QVariantList{lostOutput["id"]}}});
    QVERIFY(!packetDown.isEmpty()); QTRY_VERIFY(!controller.deriving());
    const auto warning = rule(objectId, warningId)["error"].toString(); QVERIFY(!warning.isEmpty());
    const auto keepOutput = rule(objectId, keepId)["outputs"].toList()[0].toMap();
    controller.addSignalToPlot(0, combined["series"].toInt());
    controller.addSignalToPlot(0, rule(objectId, downId)["outputs"].toList()[0].toMap()["series"].toInt());
    controller.addSignalToPlot(0, keepOutput["series"].toInt());
    for (const auto &value : definition(detector)["rules"].toList()) for (const auto &output : value.toMap()["outputs"].toList())
        controller.addSignalToPlot(1, output.toMap()["series"].toInt());
    // Use the real legend component: this catches a stale Repeater as well as stale C++ bindings.
    QQmlEngine engine; QStringList warnings;
    connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) { for (const auto &error : errors) warnings.append(error.toString()); });
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
import "."
Item {
    id: fixture
    required property var controller
    required property var renderer
    property int plotIndex: 0
    property bool legendDragging: false
    width: 600; height: 300
    PlotLegend { objectName: "legendUnderTest"; width: parent.width; plot: fixture; controller: fixture.controller; renderer: fixture.renderer; dragPreview: fixture }
})", QUrl::fromLocalFile(QStringLiteral(DI_SOURCE_DIR "/qml/removal-fixture.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> view0(component.createWithInitialProperties({{"controller", QVariant::fromValue(&controller)}, {"renderer", QVariant::fromValue(&first)}}));
    std::unique_ptr<QObject> view1(component.createWithInitialProperties({{"controller", QVariant::fromValue(&controller)}, {"renderer", QVariant::fromValue(&second)}, {"plotIndex", 1}}));
    QVERIFY(view0 && view1); auto *legend0 = view0->findChild<QObject *>("legendUnderTest"), *legend1 = view1->findChild<QObject *>("legendUnderTest");
    QVERIFY(legend0 && legend1); QTRY_COMPARE(legend0->property("entryCount").toInt(), 3); QTRY_COMPARE(legend1->property("entryCount").toInt(), 3);
    QVERIFY(controller.removeFile(leftGroup)); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.signalCount(), 4); QCOMPARE(controller.loadedFileCount(), 1);
    QVERIFY(rule(objectId, mergeId).isEmpty()); QVERIFY(rule(objectId, downId).isEmpty()); QVERIFY(rule(detector, packetDown).isEmpty());
    QCOMPARE(definition(objectId)["rules"].toList().size(), 2); QCOMPARE(definition(detector)["rules"].toList().size(), 1);
    QCOMPARE(rule(objectId, keepId)["outputs"].toList()[0].toMap()["id"], keepOutput["id"]);
    QCOMPARE(rule(objectId, keepId)["outputs"].toList()[0].toMap()["preview"].toString(), QString("20 / 40 / 80"));
    QCOMPARE(rule(objectId, warningId)["error"].toString(), warning);
    QCOMPARE(controller.signalName(0), QString("A")); QCOMPARE(controller.signalName(1), QString("independent"));
    QCOMPARE(controller.plotSignalRows(0), QVariantList{1}); QCOMPARE(controller.plotSignalRows(1), QVariantList{3});
    QTRY_COMPARE(legend0->property("entryCount").toInt(), 1); QTRY_COMPARE(legend1->property("entryCount").toInt(), 1);
    QCOMPARE(first.visibleSeriesIds(), QVector<int>{1}); QCOMPARE(second.visibleSeriesIds(), QVector<int>{3});
    const auto session = dir.filePath("after-removal.disession"); QVERIFY(controller.saveSession(session));
    AppController restored; QVERIFY(restored.restoreSession(session)); QTRY_VERIFY(!restored.loading() && !restored.deriving());
    QCOMPARE(restored.signalCount(), 4); QCOMPARE(restored.plotSignalRows(0), QVariantList{1}); QCOMPARE(restored.plotSignalRows(1), QVariantList{3});
    // Removing the last source while an evaluation is in flight cannot resurrect its outputs.
    QVERIFY(controller.editObjectRule(objectId, keepId, {{"name", "independent"}, {"operation", "scale"}, {"inputs", QVariantList{survivingField}}, {"factor", 3}}));
    QVERIFY(controller.deriving()); QVERIFY(controller.removeFile(rightGroup));
    QTRY_VERIFY(!controller.deriving()); QTest::qWait(100);
    QCOMPARE(controller.signalCount(), 0); QCOMPARE(controller.signalModel()->rowCount(), 0);
    QVERIFY(controller.plotSignalRows(0).isEmpty() && controller.plotSignalRows(1).isEmpty());
    QVERIFY(first.visibleSeriesIds().isEmpty() && second.visibleSeriesIds().isEmpty());
    QTRY_COMPARE(legend0->property("entryCount").toInt(), 0); QTRY_COMPARE(legend1->property("entryCount").toInt(), 0);
    QVERIFY(!legend0->property("visible").toBool() && !legend1->property("visible").toBool());
    for (const auto &value : controller.dataObjects()) {
        QVERIFY(value.toMap()["rules"].toList().isEmpty());
        for (const auto &field : value.toMap()["fields"].toList()) QCOMPARE(field.toMap()["series"].toInt(), -1);
    }
    QVERIFY(controller.saveSession(session));
    QVERIFY(restored.restoreSession(session)); QTRY_VERIFY(!restored.loading() && !restored.deriving()); QCOMPARE(restored.signalCount(), 0);
    QCoreApplication::processEvents(); QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}
void ObjectDataTest::editsPreserveUnrelatedResultsAndMergePendingObjects()
{
    QTemporaryDir dir; QFile file(dir.filePath("objects.csv"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("time,A,B\n0,2,20\n1,3,30\n"); file.close();
    AppController controller; PlotItem plot; plot.setWidth(600); plot.setHeight(300);
    controller.attachPlot(&plot); QVERIFY(controller.loadCsv(file.fileName())); QTRY_VERIFY(!controller.loading() && !controller.deriving());
    const auto a = controller.createDataObject("A", "processing"), b = controller.createDataObject("B", "processing");
    const auto fa = controller.bindObjectField(a, {}, "input", "scalar", 0);
    const auto fb = controller.bindObjectField(b, {}, "input", "scalar", 1);
    const auto ruleA = controller.addObjectRule(a, {{"name", "A output"}, {"operation", "scale"}, {"inputs", QVariantList{fa}}, {"factor", 2}});
    const auto ruleB = controller.addObjectRule(b, {{"name", "B output"}, {"operation", "scale"}, {"inputs", QVariantList{fb}}, {"factor", 3}});
    QVERIFY(!ruleA.isEmpty() && !ruleB.isEmpty()); QTRY_VERIFY(!controller.deriving());
    const auto output = [&](int object) {
        return controller.dataObjects()[object].toMap()["rules"].toList()[0].toMap()["outputs"].toList()[0].toMap();
    };
    const auto previewB = output(1)["preview"].toString();
    QCOMPARE(previewB, QString("60 / 90"));
    controller.selectSignal(output(1)["series"].toInt()); plot.setCursorMode(1); plot.setCursorX(0, 1);
    QVERIFY(!controller.bindObjectField(a, fa, "input", "scalar", 1).isEmpty());
    QCOMPARE(output(1)["preview"].toString(), previewB);
    QVERIFY(!plot.cursorReadouts().isEmpty());
    QCOMPARE(plot.cursorReadouts()[0].toMap()["y"].toDouble(), 60.0);
    QVERIFY(!controller.bindObjectField(b, fb, "input", "scalar", 0).isEmpty());
    // The second edit cancels the first task; both dirty objects must be retried.
    QTRY_VERIFY(!controller.deriving());
    QCOMPARE(output(0)["preview"].toString(), QString("40 / 60"));
    QCOMPARE(output(1)["preview"].toString(), QString("6 / 9"));
    QVERIFY(!controller.bindObjectField(a, fa, "input", "scalar", -1).isEmpty());
    QTRY_VERIFY(!controller.deriving());
    const auto errorA = controller.dataObjects()[0].toMap()["rules"].toList()[0].toMap()["error"].toString();
    QVERIFY(!errorA.isEmpty());
    QVERIFY(controller.editObjectRule(b, ruleB, {{"name", "B output"}, {"operation", "scale"}, {"inputs", QVariantList{fb}}, {"factor", 4}}));
    QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.dataObjects()[0].toMap()["rules"].toList()[0].toMap()["error"].toString(), errorA);
    QCOMPARE(output(1)["preview"].toString(), QString("8 / 12"));
}
void ObjectDataTest::linkedTrajectoriesAndRuleEdits()
{
    QTemporaryDir dir; const auto path = dir.filePath("flight.csv"); QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("time,lat,lon,height,roll,pitch,yaw,lat2\n0,30,120,100,0,0,0,31\n1,30.1,120.1,200,1,2,3,31.1\n"); file.close();
    AppController controller; PlotItem plot; controller.attachPlot(&plot); QVERIFY(controller.loadCsv(path)); QTRY_VERIFY(!controller.loading() && !controller.deriving());
    const auto object = controller.createDataObject("plane", "aircraft");
    QVERIFY(controller.configureDataObjectAircraft(object, {{"geographic", true}, {"attitudeMode", 1}, {"radians", false}, {"order", 0}, {"scalarLast", false}, {"navigationToBody", false}}));
    const auto fields = controller.dataObjects().first().toMap()["fields"].toList();
    QCOMPARE(fields.size(), 6);
    const auto latitude = controller.bindObjectField(object, fields[0].toMap()["id"].toString(), "lat", "latitude", 0);
    QVERIFY(!latitude.isEmpty());
    QVERIFY(!controller.bindObjectField(object, fields[1].toMap()["id"].toString(), "lon", "longitude", 1).isEmpty());
    for (int i = 2; i < 6; ++i) QVERIFY(!controller.bindObjectField(object, fields[i].toMap()["id"].toString(), QString::number(i), QStringList{"height", "roll", "pitch", "yaw"}[i - 2], i).isEmpty());
    QVERIFY(controller.showObjectTrajectory(object, 0));
    QVERIFY(controller.styleTrajectory(0, "plane", QColor("#d95319"), 5, false));
    QVERIFY(controller.showObjectTrajectory(object, 0));
    QCOMPARE(controller.trajectoryState(0)["color"].value<QColor>(), QColor("#d95319"));
    QCOMPARE(controller.trajectoryState(0)["width"].toDouble(), 5.0);
    QVERIFY(controller.trajectoryState(0)["visible"].toBool());
    QVERIFY(controller.configureDataObjectAircraft(object, {{"geographic", true}, {"attitudeMode", 1}, {"radians", true}, {"order", 1}, {"scalarLast", false}, {"navigationToBody", true}}));
    QVERIFY(!controller.setDataObjectType(object, "general"));
    QVERIFY(controller.bindObjectField(object, latitude, "lat", "latitude", 6).size() > 0);
    QVERIFY(controller.renameDataObject(object, "plane updated"));
    const auto processing = controller.createDataObject("flight processing", "processing");
    const auto rule = controller.addObjectRule(processing, {{"name", "scaled"}, {"operation", "scale"}, {"inputs", QVariantList{"raw:6"}}, {"factor", 2}});
    QVERIFY(!rule.isEmpty()); QTRY_VERIFY(!controller.deriving());
    QVERIFY(controller.editObjectRule(processing, rule, {{"name", "modified"}, {"operation", "scale"}, {"inputs", QVariantList{"raw:6"}}, {"factor", 3}}));
    QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.processingConfigurations().first().toMap()["rules"].toList().first().toMap()["factor"].toDouble(), 3.0);
    const auto outputId = controller.processingConfigurations().first().toMap()["rules"].toList().first().toMap()["outputs"].toList().first().toMap()["id"].toString();
    QVERIFY(!controller.editObjectRule(processing, rule, {{"name", "cycle"}, {"operation", "scale"}, {"inputs", QVariantList{outputId}}}));
    const auto session = dir.filePath("plane.disession"); QVERIFY(controller.saveSession(session));
    SessionDocument saved; QString error; QVERIFY(readSessionDocument(session, &saved, &error));
    QCOMPARE(saved.plots[0].trajectory.activeEntry().objectId, object); QCOMPARE(saved.plots[0].trajectory.activeEntry().axes[0], 6);
    QCOMPARE(saved.plots[0].trajectory.activeEntry().name, QString("plane updated")); QCOMPARE(saved.plots[0].trajectory.activeEntry().attitude.mode, 1);
    QVERIFY(saved.plots[0].trajectory.activeEntry().attitude.radians); QCOMPARE(saved.plots[0].trajectory.activeEntry().attitude.order, 1); QVERIFY(saved.plots[0].trajectory.activeEntry().attitude.navigationToBody);
    AppController restored; QVERIFY(restored.restoreSession(session)); QTRY_VERIFY(!restored.loading() && !restored.deriving());
    QVERIFY(restored.dataObjects().first().toMap()["radians"].toBool());
    QVERIFY(restored.configureDataObjectAircraft(object, {{"geographic", false}, {"attitudeMode", 2}, {"radians", false}, {"order", 0}, {"scalarLast", true}, {"navigationToBody", false}}));
    const auto restoredFields = restored.dataObjects().first().toMap()["fields"].toList();
    const QStringList spatialRoles{"x", "y", "z", "qw", "qx", "qy", "qz"};
    for (const auto &value : restoredFields) {
        const auto field = value.toMap(); const int source = spatialRoles.indexOf(field["role"].toString());
        if (source >= 0) QVERIFY(!restored.bindObjectField(object, field["id"].toString(), field["name"].toString(), field["role"].toString(), source).isEmpty());
    }
    QVERIFY(restored.configureDataObjectAircraft(object, {{"geographic", false}, {"attitudeMode", 2}, {"radians", false}, {"order", 0}, {"scalarLast", true}, {"navigationToBody", false}}));
    QTRY_VERIFY(!restored.deriving());
    const auto quaternionSession = dir.filePath("quaternion.disession"); QVERIFY(restored.saveSession(quaternionSession));
    QVERIFY(readSessionDocument(quaternionSession, &saved, &error));
    QVERIFY(!saved.plots[0].trajectory.activeEntry().geographic); QCOMPARE(saved.plots[0].trajectory.activeEntry().attitude.mode, 2);
    QCOMPARE(saved.plots[0].trajectory.activeEntry().axes, (std::array<int, 3>{{0, 1, 2}}));
    QCOMPARE(saved.plots[0].trajectory.activeEntry().attitude.sources, (std::array<int, 4>{{4, 5, 6, 3}}));
    QVERIFY(restored.removeDataObject(object)); QCOMPARE(restored.signalCount(), 8);
    const auto removed = dir.filePath("removed.disession"); QVERIFY(restored.saveSession(removed));
    QVERIFY(readSessionDocument(removed, &saved, &error)); QVERIFY(saved.plots[0].trajectory.activeEntry().objectId.isEmpty());
    QCOMPARE(saved.plots[0].trajectory.activeEntry().axes[0], -1);
}

void ObjectDataTest::staircaseKeepsPulse()
{
    PlotSeriesInput input; input.id = 0; input.step = true;
    for (int i = 0; i < 1000; ++i) { input.time.append(i); input.values.append(i == 500 ? 1 : 0); }
    PlotSeriesStore store; store.appendSeries({input}); const auto snapshot = store.snapshot({0});
    LodRequestKey key{snapshot.generation, {0}, 0, 999, 10}; const auto lod = PlotLodBuilder::build(snapshot, key);
    QVERIFY(!lod.segments.isEmpty()); QVERIFY(lod.segments.first().step);
    bool pulse = false; for (const auto &point : lod.segments.first().points) if (point.y() == 1) pulse = true;
    QVERIFY(pulse);
    QVERIFY(lod.segments.first().points.contains(QPointF(501, 0))); // Do not widen the pulse to the end of its bucket.
    GeometryRequest geometry; geometry.transform = {0, 999, 0, 1, 600, 200}; geometry.lineWidth = 2;
    QVERIFY(!PlotGeometryBuilder::build(lod, geometry).segments.isEmpty());
}
void ObjectDataTest::objectManagerQml()
{
    qmlRegisterTypesAndRevisions<PlotItemQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<AppControllerQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<SignalModelQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
    QTemporaryDir dir; QFile file(dir.filePath("qml.csv")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("time,A,B\n0,5,8\n1,7,9\n"); file.close();
    AppController controller; QVERIFY(controller.loadCsv(file.fileName())); QTRY_VERIFY(!controller.loading() && !controller.deriving());
    QQmlEngine engine; QStringList warnings;
    connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) { for (const auto &error : errors) warnings.append(error.toString()); });
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(DI_SOURCE_DIR "/qml/Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"appController", QVariant::fromValue(&controller)}}));
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto *manager = root->findChild<QObject *>("objectManager"); QVERIFY(manager);
    controller.selectSignal(0);
    QVERIFY(controller.plotSignalEnabled(0, 0));
    QVERIFY(QMetaObject::invokeMethod(manager, "open"));
    QVERIFY(!root->findChild<QObject *>("plotGrid")->property("visible").toBool());
    const auto id = controller.createDataObject("设备 A"); QVERIFY(!id.isEmpty()); manager->setProperty("selectedId", id);
    QCoreApplication::processEvents(); QVERIFY(manager->property("selected").isValid());
    QVERIFY(root->findChild<QObject *>("createObjectButton"));
    auto *sidebar = root->findChild<QObject *>("sidebarObjectList"); QVERIFY(sidebar);
    auto *tabs = root->findChild<QObject *>("signalBrowserTabs"); QVERIFY(tabs);
    QCOMPARE(tabs->property("currentIndex").toInt(), 2);
    QTRY_COMPARE(sidebar->property("count").toInt(), 1);
    QVERIFY(qobject_cast<QQuickItem *>(sidebar)->isVisible());
    auto *objectSearch = root->findChild<QObject *>("objectSearch"); QVERIFY(objectSearch);
    objectSearch->setProperty("text", QStringLiteral("不存在"));
    QTRY_COMPARE(sidebar->property("count").toInt(), 0);
    objectSearch->setProperty("text", QStringLiteral("设备"));
    QTRY_COMPARE(sidebar->property("count").toInt(), 1);
    objectSearch->setProperty("text", QString());
    QVERIFY(QMetaObject::invokeMethod(manager->findChild<QObject *>("browseObjectSourcesButton"), "clicked"));
    QCOMPARE(tabs->property("currentIndex").toInt(), 0);
    QVERIFY(!qobject_cast<QQuickItem *>(sidebar)->isVisible());
    QVERIFY(manager->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(manager, "bindSource", Q_ARG(int, 0)));
    QCOMPARE(controller.dataObjects().first().toMap()["fields"].toList().size(), 1);
    QObject *treeCheck = nullptr;
    auto findTreeCheck = [&] {
        treeCheck = nullptr;
        QList<QQuickItem *> pending{qobject_cast<QQuickWindow *>(root.get())->contentItem()};
        while (!pending.isEmpty()) {
            auto *item = pending.takeLast(); pending.append(item->childItems());
            if (item->objectName() == "signalTreeCheck" && item->property("sourceRow").toInt() == 0) treeCheck = item;
        }
        return treeCheck != nullptr;
    };
    QTRY_VERIFY(findTreeCheck());
    QVERIFY(QMetaObject::invokeMethod(treeCheck, "clicked"));
    QCOMPARE(controller.dataObjects().first().toMap()["fields"].toList().first().toMap()["series"].toInt(), -1);
    root->setProperty("signalDragDestination", QVariant::fromValue(manager)); root->setProperty("signalDragRow", 0);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "finishSignalTreeDrag"));
    QCOMPARE(controller.dataObjects().first().toMap()["fields"].toList().first().toMap()["series"].toInt(), 0);
    QVERIFY(controller.plotSignalEnabled(0, 0));
    auto *processor = root->findChild<QObject *>("processingManager"); QVERIFY(processor);
    auto *button = processor->findChild<QObject *>("addObjectRuleButton"); QVERIFY(button);
    QVERIFY(!qobject_cast<QQuickItem *>(manager->findChild<QObject *>("addObjectRuleButton"))->isVisible());
    QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>("addObjectFieldButton"), "clicked"));
    QCOMPARE(controller.dataObjects().first().toMap()["fields"].toList().last().toMap()["name"].toString(), QStringLiteral("字段 2"));
    auto *fieldDialog = root->findChild<QObject *>("objectFieldDialog"); QVERIFY(fieldDialog);
    QVERIFY(!fieldDialog->property("visible").toBool());
    auto *quickWindow = qobject_cast<QQuickWindow *>(root.get()); QVERIFY(quickWindow);
    auto visualItem = [&](const QString &name, const QString &text = QString()) -> QQuickItem * {
        QList<QQuickItem *> pending{quickWindow->contentItem()};
        while (!pending.isEmpty()) {
            auto *item = pending.takeLast(); pending.append(item->childItems());
            if (item->objectName() == name && (text.isEmpty() || item->property("text").toString() == text)) return item;
        }
        return nullptr;
    };
    QQuickItem *fieldLabel = nullptr;
    QTRY_VERIFY((fieldLabel = visualItem("objectFieldRowName", QStringLiteral("字段 2"))));
    tabs->setProperty("currentIndex", 2);
    root->setProperty("signalTreeVisible", false);
    QCoreApplication::processEvents();
    QQuickItem *unboundSource = nullptr;
    QTRY_VERIFY((unboundSource = visualItem("objectFieldRowSource", QStringLiteral("未绑定"))));
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
        unboundSource->mapToScene(QPointF(20, unboundSource->height() / 2)).toPoint());
    QTRY_COMPARE(tabs->property("currentIndex").toInt(), 0);
    QVERIFY(root->property("signalTreeVisible").toBool());
    QCOMPARE(manager->property("selectedFieldId").toString(), controller.objectConfigurations().first().toMap()["fields"].toList().last().toMap()["id"].toString());
    auto *signalSearch = root->findChild<QObject *>("signalSearch"); QVERIFY(signalSearch);
    signalSearch->setProperty("text", "no matching signal");
    controller.filterSignals("no matching signal");
    controller.signalModel()->toggleGroup("qml.csv");
    tabs->setProperty("currentIndex", 2);
    QCoreApplication::processEvents();
    QQuickItem *boundSource = nullptr;
    QTRY_VERIFY((boundSource = visualItem("objectFieldRowSource", QStringLiteral("qml.csv / A"))));
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
        boundSource->mapToScene(QPointF(20, boundSource->height() / 2)).toPoint());
    QTRY_COMPARE(tabs->property("currentIndex").toInt(), 0);
    QCOMPARE(signalSearch->property("text").toString(), QString());
    auto *signalList = root->findChild<QObject *>("signalList"); QVERIFY(signalList);
    const int revealedRow = signalList->property("currentIndex").toInt(); QVERIFY(revealedRow >= 0);
    QCOMPARE(controller.signalModel()->data(controller.signalModel()->index(revealedRow), SignalModel::IndexRole).toInt(), 0);
    const auto navigationFields = controller.objectConfigurations().first().toMap()["fields"].toList();
    QCOMPARE(navigationFields.first().toMap()["series"].toInt(), 0);
    QCOMPARE(navigationFields.last().toMap()["series"].toInt(), -1);
    QVERIFY(manager->property("visible").toBool());
    QTest::mouseDClick(quickWindow, Qt::LeftButton, Qt::NoModifier, fieldLabel->mapToScene(QPointF(fieldLabel->width() / 2, fieldLabel->height() / 2)).toPoint());
    QTRY_VERIFY(fieldDialog->property("visible").toBool());
    root->findChild<QObject *>("objectFieldName")->setProperty("text", QStringLiteral("压力"));
    QVERIFY(QMetaObject::invokeMethod(fieldDialog, "accept"));
    QCOMPARE(controller.dataObjects().first().toMap()["fields"].toList().last().toMap()["name"].toString(), QStringLiteral("压力"));
    auto capture = [&](const QString &name) {
        const auto directory = qEnvironmentVariable("DI_OBJECT_UI_CAPTURE_DIR");
        if (directory.isEmpty()) return;
        QVERIFY(QDir().mkpath(directory)); QTest::qWait(150);
        const auto image = quickWindow->grabWindow(); QVERIFY(!image.isNull());
        QVERIFY(image.save(QDir(directory).filePath(name + ".png")));
    };
    capture("object-editor-light");
    auto *selectedText = visualItem("dataObjectRowText", QStringLiteral("设备 A · 常规")); QVERIFY(selectedText);
    QCOMPARE(selectedText->property("color").value<QColor>().name(), QStringLiteral("#202830"));
    QVERIFY(selectedText->isVisible());
    root->setProperty("darkTheme", true); capture("object-editor-dark");
    QCOMPARE(selectedText->property("color").value<QColor>().name(), QStringLiteral("#e6edf3"));
    root->setProperty("darkTheme", false);
    const auto processing = controller.createDataObject("设备 A 处理", "processing");
    processor->setProperty("selectedId", processing);
    QVERIFY(QMetaObject::invokeMethod(processor, "open"));
    QTRY_VERIFY(qobject_cast<QQuickItem *>(button)->isVisible());
    QCOMPARE(controller.signalBrowserSection(), 1);
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    auto *ruleFooter = processor->findChild<QObject *>("objectRuleDialog")->property("footer").value<QObject *>();
    QVERIFY(ruleFooter); QCOMPARE(ruleFooter->property("count").toInt(), 2);
    processor->findChild<QObject *>("objectRuleName")->setProperty("text", "flags");
    QVERIFY(QMetaObject::invokeMethod(processor, "selectRuleSource", Q_ARG(int, 0)));
    QVERIFY(QMetaObject::invokeMethod(processor->findChild<QObject *>("objectRuleDialog"), "submit"));
    QTRY_VERIFY(!controller.deriving()); QCOMPARE(controller.signalCount(), 10);
    // Editing can select any raw signal, not only sources previously used by the configuration.
    QQmlExpression editFirstRule(QQmlEngine::contextForObject(processor), processor, "editRule(selected.rules[0])");
    editFirstRule.evaluate(); QVERIFY(!editFirstRule.hasError());
    auto *inputSelector = processor->findChild<QObject *>("objectRuleDialog"); QVERIFY(inputSelector);
    QCOMPARE(inputSelector->property("inputOptions").toList().size(), 2);
    QCOMPARE(inputSelector->property("inputXId").toString(), QString("raw:0"));
    QVERIFY(QMetaObject::invokeMethod(processor, "selectRuleSource", Q_ARG(int, 2)));
    QCOMPARE(inputSelector->property("inputXId").toString(), QString("raw:0"));
    QVERIFY(!processor->findChild<QObject *>("objectRuleDialog")->property("errorText").toString().isEmpty());
    auto *inlineEditor = qobject_cast<QQuickItem *>(processor->findChild<QObject *>("objectRuleDialog"));
    QVERIFY(inlineEditor); QVERIFY(inlineEditor->isVisible());
    QCOMPARE(inlineEditor->metaObject()->indexOfProperty("modal"), -1); // A Pane has no popup modality.
    QCOMPARE(tabs->property("currentIndex").toInt(), 0);
    QQuickItem *inputCheck = nullptr;
    auto findInputCheck = [&](int row) {
        QList<QQuickItem *> pending{quickWindow->contentItem()};
        while (!pending.isEmpty()) {
            auto *item = pending.takeLast(); pending.append(item->childItems());
            if (item->objectName() == "signalTreeCheck" && item->property("sourceRow").toInt() == row && item->isVisible()) {
                inputCheck = item; return true;
            }
        }
        return false;
    };
    QTRY_VERIFY(findInputCheck(1));
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
        inputCheck->mapToScene(QPointF(inputCheck->width() / 2, inputCheck->height() / 2)).toPoint());
    QTRY_COMPARE(inputSelector->property("inputXId").toString(), QString("raw:1"));
    QCOMPARE(controller.processingConfigurations()[0].toMap()["rules"].toList()[0].toMap()["inputIds"].toStringList(), QStringList{"raw:0"});
    QVERIFY(controller.plotSignalEnabled(0, 0));
    QVERIFY(!controller.plotSignalEnabled(0, 1));
    capture("processing-inline-editor-light");
    root->setProperty("darkTheme", true); capture("processing-inline-editor-dark");
    root->setProperty("darkTheme", false);
    // Navigation retains the draft; applying is the only configuration mutation.
    QVERIFY(QMetaObject::invokeMethod(processor, "close"));
    QVERIFY(QMetaObject::invokeMethod(processor, "open"));
    QCOMPARE(inputSelector->property("inputXId").toString(), QString("raw:1"));
    QVERIFY(QMetaObject::invokeMethod(processor->findChild<QObject *>("objectRuleDialog"), "submit"));
    QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.processingConfigurations()[0].toMap()["rules"].toList()[0].toMap()["inputIds"].toStringList(), QStringList{"raw:1"});
    editFirstRule.evaluate(); QVERIFY(!editFirstRule.hasError());
    QCOMPARE(inputSelector->property("inputXId").toString(), QString("raw:1"));
    auto *inputName = visualItem("signalTreeName", QStringLiteral("A")); QVERIFY(inputName);
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
        inputName->mapToScene(QPointF(10, inputName->height() / 2)).toPoint());
    QTRY_COMPARE(inputSelector->property("inputXId").toString(), QString("raw:0"));
    QVERIFY(QMetaObject::invokeMethod(processor->findChild<QObject *>("objectRuleDialog"), "submit"));
    QTRY_VERIFY(!controller.deriving());
    QVERIFY(controller.configureTrajectory(0, true, 0, -1, -1, false));
    QVERIFY(!manager->property("timePlot").toBool());
    QVERIFY(QMetaObject::invokeMethod(processor->findChild<QObject *>("objectSwitchTimePlot"), "clicked"));
    QVERIFY(manager->property("timePlot").toBool());
    auto *nameField = root->findChild<QObject *>("objectNameField"); QVERIFY(nameField);
    QQmlExpression textColor(qmlContext(nameField), nameField, "palette.text");
    QCOMPARE(textColor.evaluate().value<QColor>().name(), QStringLiteral("#202830"));
    root->setProperty("darkTheme", true);
    QCOMPARE(textColor.evaluate().value<QColor>().name(), QStringLiteral("#e6edf3"));
    root->setProperty("darkTheme", false);
    QVERIFY(QMetaObject::invokeMethod(manager, "open"));
    QCOMPARE(controller.signalBrowserSection(), 0);
    QCOMPARE(tabs->property("currentIndex").toInt(), 2);
    QTRY_COMPARE(sidebar->property("count").toInt(), 1); // Processing configuration is excluded.
    manager->setProperty("selectedId", QString());
    QQuickItem *sidebarRow = nullptr;
    QTRY_VERIFY((sidebarRow = visualItem("sidebarObjectRow")));
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
        sidebarRow->mapToScene(QPointF(sidebarRow->width() / 2, sidebarRow->height() / 2)).toPoint());
    QTRY_COMPARE(manager->property("selectedId").toString(), id);
    capture("object-sidebar-light");
    root->setProperty("darkTheme", true); capture("object-sidebar-dark"); root->setProperty("darkTheme", false);
    QVERIFY(QMetaObject::invokeMethod(manager->findChild<QObject *>("browseObjectSourcesButton"), "clicked"));
    QVERIFY(controller.setDataObjectType(id, "aircraft"));
    QCOMPARE(controller.dataObjects().first().toMap()["fields"].toList().size(), 5);
    QCOMPARE(controller.objectConfigurations().first().toMap()["rules"].toList().size(), 0);
    QCOMPARE(controller.processingConfigurations().first().toMap()["rules"].toList().size(), 1);
    const auto planeFields = controller.objectConfigurations().first().toMap()["fields"].toList();
    for (const auto &value : planeFields) {
        const auto field = value.toMap(); const auto role = field["role"].toString();
        if (role == "latitude" || role == "longitude")
            QVERIFY(!controller.bindObjectField(id, field["id"].toString(), field["name"].toString(), role, role == "latitude" ? 0 : 1).isEmpty());
    }
    tabs->setProperty("currentIndex", 2);
    QQuickItem *trajectoryCheck = nullptr;
    QTRY_VERIFY((trajectoryCheck = visualItem("objectTrajectoryCheck")) && trajectoryCheck->isVisible());
    auto clickItem = [&](QQuickItem *item) {
        QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    };
    QVERIFY(controller.removeTrajectory(0, 0)); // Remove the earlier unlinked Cartesian trajectory fixture.
    QTRY_VERIFY(trajectoryCheck->width() > 0);
    clickItem(trajectoryCheck);
    QTRY_COMPARE(controller.trajectoryState(0)["objectId"].toString(), id);
    QVERIFY(controller.trajectoryState(0)["enabled"].toBool());
    QVERIFY(!manager->property("visible").toBool());
    QQuickItem *trajectoryLine = nullptr;
    QTRY_VERIFY((trajectoryLine = visualItem("objectTrajectoryStyleButton")) && trajectoryLine->isVisible());
    clickItem(trajectoryLine);
    auto *styleDialog = root->findChild<QObject *>("objectTrajectoryStyleDialog"); QVERIFY(styleDialog);
    QTRY_VERIFY(styleDialog->property("visible").toBool());
    styleDialog->setProperty("lineColor", QColor("#d95319"));
    root->findChild<QObject *>("objectTrajectoryWidth")->setProperty("value", 5);
    QVERIFY(QMetaObject::invokeMethod(styleDialog, "submit"));
    QVERIFY(!styleDialog->property("visible").toBool());
    QCOMPARE(controller.trajectoryState(0)["color"].value<QColor>(), QColor("#d95319"));
    QCOMPARE(controller.trajectoryState(0)["width"].toDouble(), 5.0);
    clickItem(trajectoryCheck); QTRY_VERIFY(!controller.trajectoryState(0)["visible"].toBool());
    clickItem(trajectoryCheck); QTRY_VERIFY(controller.trajectoryState(0)["visible"].toBool());
    QCOMPARE(controller.trajectoryState(0)["color"].value<QColor>(), QColor("#d95319"));
    QCOMPARE(controller.trajectoryState(0)["width"].toDouble(), 5.0);
    capture("aircraft-trajectory-sidebar");
    QVERIFY(controller.removeTrajectory(0, controller.trajectoryState(0)["active"].toInt()));
    QVERIFY(controller.configureTrajectory(0, false, 0, -1, -1, false));
    QVERIFY(QMetaObject::invokeMethod(manager, "open"));
    QVERIFY(QMetaObject::invokeMethod(manager->findChild<QObject *>("browseObjectSourcesButton"), "clicked"));
    const auto copied = controller.duplicateDataObject(id); QVERIFY(!copied.isEmpty());
    QCOMPARE(controller.dataObjects().last().toMap()["type"].toString(), QStringLiteral("aircraft"));
    manager->setProperty("selectedId", copied);
    QCoreApplication::processEvents(); QTRY_VERIFY(findTreeCheck()); QVERIFY(treeCheck->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(treeCheck, "clicked"));
    QCOMPARE(controller.dataObjects().last().toMap()["fields"].toList().first().toMap()["series"].toInt(), -1);
    manager->setProperty("selectedId", id);
    QCoreApplication::processEvents(); QVERIFY(treeCheck->property("checked").toBool());
    auto *mode = root->findChild<QObject *>("objectAircraftAttitude"); QVERIFY(mode); mode->setProperty("currentIndex", 2);
    QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 2)));
    QCOMPARE(controller.dataObjects().first().toMap()["attitudeMode"].toInt(), 2);
    QCOMPARE(manager->property("visibleFields").toList().size(), 9);
    capture("object-editor-aircraft-light");
    auto *popup = mode->property("popup").value<QObject *>(); QVERIFY(popup);
    QVERIFY(QMetaObject::invokeMethod(popup, "open")); QTRY_VERIFY(popup->property("visible").toBool());
    QQuickItem *popupOption = nullptr;
    QTRY_VERIFY((popupOption = visualItem("themedComboOption", QStringLiteral("四元数"))));
    QVERIFY(popupOption->isVisible());
    capture("object-editor-options-light");
    root->setProperty("darkTheme", true); capture("object-editor-options-dark"); root->setProperty("darkTheme", false);
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QVERIFY(controller.setDataObjectType(id, "general"));
    QCOMPARE(controller.dataObjects().first().toMap()["fields"].toList().size(), 9);
    QVERIFY(!controller.showObjectTrajectory(id, 0));
    QTRY_VERIFY(!controller.deriving());
    QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>("createObjectButton"), "clicked"));
    const auto automatic = controller.dataObjects().last().toMap();
    QCOMPARE(automatic["name"].toString(), QStringLiteral("飞机 1"));
    QCOMPARE(automatic["type"].toString(), QStringLiteral("aircraft"));
    QCOMPARE(manager->findChild<QObject *>("objectTypeSelector")->property("currentText").toString(), QStringLiteral("飞机"));
    auto *nameDialog = root->findChild<QObject *>("objectNameDialog"); QVERIFY(nameDialog); QVERIFY(!nameDialog->property("visible").toBool());
    QQuickItem *objectNameLabel = nullptr;
    QTRY_VERIFY((objectNameLabel = visualItem("dataObjectRowText", QStringLiteral("飞机 1 · 飞机"))));
    QTest::mouseDClick(quickWindow, Qt::LeftButton, Qt::NoModifier, objectNameLabel->mapToScene(QPointF(50, objectNameLabel->height() / 2)).toPoint());
    QTRY_VERIFY(nameDialog->property("visible").toBool());
    nameField->setProperty("text", QStringLiteral("液压对象")); QVERIFY(QMetaObject::invokeMethod(nameDialog, "accept"));
    QCOMPARE(controller.dataObjects().last().toMap()["name"].toString(), QStringLiteral("液压对象"));
    // Continue the generic field interaction fixture after verifying the aircraft default.
    QVERIFY(controller.setDataObjectType(automatic["id"].toString(), "general"));
    for (const auto &field : automatic["fields"].toList())
        QVERIFY(controller.removeObjectField(automatic["id"].toString(), field.toMap()["id"].toString()));
    QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>("addObjectFieldButton"), "clicked"));
    QQuickItem *targetLabel = nullptr;
    QTRY_VERIFY((targetLabel = visualItem("objectFieldRowName", QStringLiteral("字段 1"))));
    const auto targetPoint = targetLabel->mapToItem(qobject_cast<QQuickItem *>(manager), QPointF(30, targetLabel->height() / 2));
    QString fieldAt; QVERIFY(QMetaObject::invokeMethod(manager, "fieldAt", Q_RETURN_ARG(QString, fieldAt), Q_ARG(double, targetPoint.x()), Q_ARG(double, targetPoint.y())));
    QCOMPARE(fieldAt, controller.dataObjects().last().toMap()["fields"].toList().first().toMap()["id"].toString());
    manager->setProperty("selectedFieldId", QString());
    auto *sourceLabel = visualItem("signalTreeName", QStringLiteral("A")); QVERIFY(sourceLabel);
    const auto sourcePoint = sourceLabel->mapToScene(QPointF(20, sourceLabel->height() / 2)).toPoint();
    const auto dropPoint = targetLabel->mapToScene(QPointF(30, targetLabel->height() / 2)).toPoint();
    QTest::mousePress(quickWindow, Qt::LeftButton, Qt::NoModifier, sourcePoint);
    QTest::mouseMove(quickWindow, sourcePoint + QPoint(12, 0), 20);
    QTest::mouseMove(quickWindow, dropPoint, 20);
    QTRY_COMPARE(manager->property("dropFieldId").toString(), fieldAt);
    QTest::mouseRelease(quickWindow, Qt::LeftButton, Qt::NoModifier, dropPoint);
    QCOMPARE(controller.dataObjects().last().toMap()["fields"].toList().first().toMap()["series"].toInt(), 0);
    // Keep the same visible row and viewport when fields are appended or removed.
    auto *addField = root->findChild<QObject *>("addObjectFieldButton"); QVERIFY(addField);
    for (int i = 0; i < 35; ++i) QVERIFY(QMetaObject::invokeMethod(addField, "clicked"));
    auto *fieldList = root->findChild<QQuickItem *>("objectFieldList"); QVERIFY(fieldList);
    QTRY_COMPARE(fieldList->property("count").toInt(), 36);
    QTRY_VERIFY(!controller.deriving());
    QVERIFY(QMetaObject::invokeMethod(fieldList, "forceLayout"));
    const auto displayedFields = manager->property("visibleFields").toList();
    const auto storedFields = controller.dataObjects().last().toMap()["fields"].toList();
    QCOMPARE(displayedFields.size(), storedFields.size());
    for (int i = 0; i < storedFields.size(); ++i)
        QCOMPARE(displayedFields[i].toMap()["id"], storedFields[i].toMap()["id"]);
    fieldList->setProperty("contentY", 480.0);
    QPointer<QQuickItem> anchor;
    QTRY_VERIFY((anchor = visualItem("objectFieldRowName", QStringLiteral("字段 11"))));
    QTest::qWait(50);
    const auto anchorY = anchor->mapToScene(QPointF()).y();
    QVERIFY(QMetaObject::invokeMethod(addField, "clicked"));
    QTRY_COMPARE(fieldList->property("count").toInt(), 37);
    QTest::qWait(50);
    QVERIFY2(anchor, "Adding a field must not recreate existing delegates");
    QVERIFY(qAbs(anchor->mapToScene(QPointF()).y() - anchorY) < 1);
    auto fields = controller.dataObjects().last().toMap()["fields"].toList();
    const auto owner = controller.dataObjects().last().toMap()["id"].toString();
    QVERIFY(controller.removeObjectField(owner, fields.last().toMap()["id"].toString()));
    QTRY_COMPARE(fieldList->property("count").toInt(), 36);
    QTest::qWait(50);
    QVERIFY(anchor); QVERIFY(qAbs(anchor->mapToScene(QPointF()).y() - anchorY) < 1);
    QVERIFY(controller.removeObjectField(owner, fields.first().toMap()["id"].toString()));
    QTRY_COMPARE(fieldList->property("count").toInt(), 35);
    QTest::qWait(50);
    QVERIFY(anchor); QVERIFY(qAbs(anchor->mapToScene(QPointF()).y() - anchorY) < 1);
    const auto pagingObject = controller.createDataObject("pagination", "processing");
    const auto pagingField = controller.addDataObjectField(pagingObject);
    QVERIFY(!controller.bindObjectField(pagingObject, pagingField, "source", "scalar", 0).isEmpty());
    for (int i = 0; i < 25; ++i)
        QVERIFY(!controller.addObjectRule(pagingObject, {{"name", QString("page_%1").arg(i, 2, 10, QChar('0'))},
            {"operation", "scale"}, {"inputs", QVariantList{pagingField}}, {"factor", 2}}).isEmpty());
    QTRY_VERIFY(!controller.deriving());
    processor->setProperty("selectedId", pagingObject);
    QVERIFY(QMetaObject::invokeMethod(processor, "open"));
    const auto countRules = [&](const QString &property) {
        return QQmlExpression(QQmlEngine::contextForObject(processor), processor, property + ".length").evaluate().toInt();
    };
    QTRY_COMPARE(countRules("matchingRules"), 25);
    QCOMPARE(countRules("pageRules"), 20);
    processor->setProperty("rulePage", 1);
    QTRY_COMPARE(countRules("pageRules"), 5);
    auto *ruleSearch = processor->findChild<QObject *>("ruleSearch"); QVERIFY(ruleSearch);
    ruleSearch->setProperty("text", "page_2");
    QTRY_COMPARE(countRules("matchingRules"), 5);
    QCOMPARE(processor->property("rulePage").toInt(), 0);
    auto *ruleState = processor->findChild<QObject *>("ruleStateFilter"); QVERIFY(ruleState);
    ruleState->setProperty("currentIndex", 2);
    QTRY_COMPARE(countRules("matchingRules"), 0);
    ruleSearch->setProperty("text", ""); ruleState->setProperty("currentIndex", 0);
    const auto detector = controller.createDataObject("识别配置", "processing");
    processor->setProperty("selectedId", detector);
    auto *typeSelector = processor->findChild<QObject *>("objectTypeSelector"); QVERIFY(typeSelector);
    QVERIFY(!qobject_cast<QQuickItem *>(typeSelector)->isVisible());
    QCOMPARE(controller.dataObjects().last().toMap()["type"].toString(), QString("processing"));
    QQmlExpression autoLoss(QQmlEngine::contextForObject(processor), processor, "editRule(null)");
    autoLoss.evaluate(); QVERIFY(!autoLoss.hasError());
    auto *autoOperation = processor->findChild<QObject *>("objectRuleOperation"); QVERIFY(autoOperation);
    auto *autoDialog = processor->findChild<QObject *>("objectRuleDialog"); QVERIFY(autoDialog);
    auto *identify = processor->findChild<QObject *>("identifyPacketLossButton"); QVERIFY(identify);
    QVERIFY(!identify->property("visible").toBool());
    autoOperation->setProperty("currentIndex", 6);
    QVERIFY(identify->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(identify, "clicked")); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.dataObjects().last().toMap()["rules"].toList().size(), 1);
    const auto discovered = controller.dataObjects().last().toMap()["rules"].toList()[0].toMap();
    QCOMPARE(discovered["samplePeriod"].toDouble(), 1.0);
    QVERIFY(!autoDialog->property("visible").toBool());
    QCOMPARE(controller.dataObjects().last().toMap()["type"].toString(), QString("processing"));
    autoLoss.evaluate(); QVERIFY(!autoLoss.hasError()); autoOperation->setProperty("currentIndex", 6);
    QVERIFY(QMetaObject::invokeMethod(identify, "clicked")); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.dataObjects().last().toMap()["rules"].toList().size(), 1);
    // Exercise the actual dialog: estimated numeric prefill and editable positive period.
    QQmlExpression createLoss(QQmlEngine::contextForObject(processor), processor, "editRule(null)");
    createLoss.evaluate(); QVERIFY(!createLoss.hasError());
    QVERIFY(QMetaObject::invokeMethod(processor, "selectRuleSource", Q_ARG(int, 0)));
    auto *operation = processor->findChild<QObject *>("objectRuleOperation"); QVERIFY(operation);
    operation->setProperty("currentIndex", 6);
    processor->findChild<QObject *>("objectRuleName")->setProperty("text", "packet loss");
    auto *dialog = processor->findChild<QObject *>("objectRuleDialog"); QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "submit")); QTRY_VERIFY(!controller.deriving());
    auto loss = controller.dataObjects().last().toMap()["rules"].toList().last().toMap();
    QCOMPARE(loss["operation"].toString(), QString("packetloss"));
    QVERIFY(loss["effectiveSamplePeriod"].toDouble() > 0);
    QQmlExpression editLoss(QQmlEngine::contextForObject(processor), processor, "editRule(selected.rules[selected.rules.length - 1])");
    editLoss.evaluate(); QVERIFY(!editLoss.hasError());
    auto *period = processor->findChild<QObject *>("objectRuleSamplePeriod"); QVERIFY(period);
    QCOMPARE(period->property("text").toString().toDouble(), loss["effectiveSamplePeriod"].toDouble());
    QVERIFY(!processor->findChild<QObject *>("objectRuleAutoPeriod"));
    period->setProperty("text", "-1"); QVERIFY(QMetaObject::invokeMethod(dialog, "submit"));
    QVERIFY(!dialog->property("errorText").toString().isEmpty());
    period->setProperty("text", "0.5"); QVERIFY(QMetaObject::invokeMethod(dialog, "submit")); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.dataObjects().last().toMap()["rules"].toList().last().toMap()["samplePeriod"].toDouble(), .5);
    editLoss.evaluate(); QVERIFY(!editLoss.hasError());
    auto *rateModeControl = processor->findChild<QObject *>("objectRuleRateMode"); QVERIFY(rateModeControl);
    auto *rateWindowControl = processor->findChild<QObject *>("objectRuleRateWindow"); QVERIFY(rateWindowControl);
    QCOMPARE(rateModeControl->property("currentIndex").toInt(), 2);
    rateModeControl->setProperty("currentIndex", 2); rateWindowControl->setProperty("text", "0");
    QVERIFY(QMetaObject::invokeMethod(dialog, "submit")); QVERIFY(dialog->property("visible").toBool());
    QVERIFY(dialog->property("errorText").toString().contains(QStringLiteral("窗口时长")));
    rateWindowControl->setProperty("text", "2");
    QVERIFY(QMetaObject::invokeMethod(dialog, "submit")); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.dataObjects().last().toMap()["rules"].toList().last().toMap()["rateMode"].toString(), QString("window"));
    editLoss.evaluate(); QVERIFY(!editLoss.hasError());
    QCOMPARE(rateModeControl->property("currentIndex").toInt(), 2);
    QCOMPARE(rateWindowControl->property("text").toString(), QString("2"));
    rateModeControl->setProperty("currentIndex", 0);
    QVERIFY(QMetaObject::invokeMethod(dialog, "submit")); QTRY_VERIFY(!controller.deriving());
    const auto countResult = controller.dataObjects().last().toMap()["rules"].toList().last().toMap()["outputs"].toList();
    QCOMPARE(countResult.size(), 1);
    QVERIFY(countResult[0].toMap()["name"].toString().contains(QStringLiteral("累计丢包数")));
    editLoss.evaluate(); QVERIFY(!editLoss.hasError());
    rateModeControl->setProperty("currentIndex", 1);
    QVERIFY(QMetaObject::invokeMethod(dialog, "submit")); QTRY_VERIFY(!controller.deriving());
    editLoss.evaluate(); QVERIFY(!editLoss.hasError());
    auto *lossModeControl = processor->findChild<QObject *>("objectRuleLossMode"); QVERIFY(lossModeControl);
    lossModeControl->setProperty("currentIndex", 2);
    processor->findChild<QObject *>("objectRuleExpectedStride")->setProperty("text", "10");
    processor->findChild<QObject *>("objectRuleCounterModulus")->setProperty("text", "256");
    QVERIFY(QMetaObject::invokeMethod(dialog, "submit")); QTRY_VERIFY(!controller.deriving());
    loss = controller.dataObjects().last().toMap()["rules"].toList().last().toMap();
    QCOMPARE(loss["lossMode"].toString(), QString("stride")); QCOMPARE(loss["expectedStride"].toDouble(), 10.0);
    QCOMPARE(loss["counterModulus"].toDouble(), 256.0);
    const auto copy = controller.duplicateDataObject(detector); QVERIFY(!copy.isEmpty()); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.dataObjects().last().toMap()["rules"].toList().last().toMap()["lossMode"].toString(), QString("stride"));
    QVERIFY(QMetaObject::invokeMethod(processor, "close"));
    QVERIFY(root->findChild<QObject *>("plotGrid")->property("visible").toBool());
    QVERIFY(controller.plotSignalEnabled(0, 0));
    QFile other(dir.filePath("other.csv")); QVERIFY(other.open(QIODevice::WriteOnly));
    other.write("time,C\n0,2\n1,4\n"); other.close();
    QVERIFY(controller.loadCsv(other.fileName())); QTRY_VERIFY(!controller.loading() && !controller.deriving());
    QVERIFY(QMetaObject::invokeMethod(processor, "open"));
    QQmlExpression newRule(QQmlEngine::contextForObject(processor), processor, "editRule(null)");
    newRule.evaluate(); QVERIFY(!newRule.hasError());
    processor->findChild<QObject *>("objectRuleName")->setProperty("text", "stale input");
    QVERIFY(QMetaObject::invokeMethod(processor, "close"));
    QVERIFY(controller.removeFile("qml.csv")); QTRY_VERIFY(!controller.deriving());
    QVERIFY(QMetaObject::invokeMethod(processor, "open"));
    QCOMPARE(controller.signalCount(), 1); // Remaining C has shifted into former A's row.
    QVERIFY(QMetaObject::invokeMethod(dialog, "submit"));
    QVERIFY(dialog->property("visible").toBool());
    QVERIFY(dialog->property("errorText").toString().contains(QStringLiteral("来源已变更")));
    QCOMPARE(controller.signalCount(), 1);
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    const auto objectCount = controller.objectConfigurations().size();
    auto *createSidebar = root->findChild<QObject *>("createSidebarObjectButton"); QVERIFY(createSidebar);
    QCOMPARE(createSidebar->property("text").toString(), QStringLiteral("新建对象"));
    tabs->setProperty("currentIndex", 2);
    QVERIFY(QMetaObject::invokeMethod(createSidebar, "clicked"));
    QTRY_COMPARE(controller.objectConfigurations().size(), objectCount + 1);
    QCOMPARE(controller.objectConfigurations().last().toMap()["type"].toString(), QString("aircraft"));
    QVERIFY(controller.objectConfigurations().last().toMap()["name"].toString().startsWith(QStringLiteral("飞机 ")));
    QCOMPARE(manager->property("selectedId").toString(), controller.objectConfigurations().last().toMap()["id"].toString());
    QVERIFY(manager->property("visible").toBool());
    QVERIFY(!processor->property("visible").toBool());
    QCoreApplication::processEvents(); QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}
void ObjectDataTest::ruleInputsQml_data()
{
    QTest::addColumn<QUrl>("mainUrl");
    QTest::newRow("source") << QUrl::fromLocalFile(QStringLiteral(DI_SOURCE_DIR "/qml/Main.qml"));
    QTest::newRow("resource") << QUrl("qrc:/qt/qml/DataInspector/Main.qml");
}
void ObjectDataTest::ruleInputsQml()
{
    QFETCH(QUrl, mainUrl);
    qmlRegisterTypesAndRevisions<PlotItemQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<AppControllerQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<SignalModelQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
    QTemporaryDir dir; QFile file(dir.filePath("inputs.csv")); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("time,A,B,C\n0,3,4,12\n1,3,4,12\n"); file.close();
    AppController controller; QVERIFY(controller.loadCsv(file.fileName())); QTRY_VERIFY(!controller.loading());
    const auto config = controller.createDataObject("input binding", "processing"); QVERIFY(!config.isEmpty());
    QQmlEngine engine; QStringList warnings;
    connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) { for (const auto &error : errors) warnings.append(error.toString()); });
    QQmlComponent component(&engine, mainUrl); QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"appController", QVariant::fromValue(&controller)}}));
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get()); QVERIFY(window);
    auto *processor = root->findChild<QObject *>("processingManager"); QVERIFY(processor);
    processor->setProperty("selectedId", config); QVERIFY(QMetaObject::invokeMethod(processor, "open"));
    auto *editor = processor->findChild<QObject *>("objectRuleDialog"); QVERIFY(editor);
    auto *operation = processor->findChild<QObject *>("objectRuleOperation"); QVERIFY(operation);
    auto findItem = [&](const QString &name, const QString &text = QString()) -> QQuickItem * {
        QList<QQuickItem *> pending{window->contentItem()};
        while (!pending.isEmpty()) {
            auto *item = pending.takeLast(); pending.append(item->childItems());
            if (item->isVisible() && item->objectName() == name && (text.isEmpty() || item->property("text").toString() == text)) return item;
        }
        return nullptr;
    };
    auto start = [&](int op) {
        QQmlExpression expression(QQmlEngine::contextForObject(processor), processor, "editRule(null)");
        expression.evaluate(); if (expression.hasError()) return false;
        if (!editor->property("inputXId").toString().isEmpty() || !editor->property("inputYId").toString().isEmpty() || !editor->property("inputZId").toString().isEmpty()) return false;
        operation->setProperty("currentIndex", op);
        if (op != 5 && !QMetaObject::invokeMethod(processor, "selectRuleSource", Q_ARG(int, 0))) return false;
        processor->findChild<QObject *>("objectRuleName")->setProperty("text", "binding rule " + QString::number(op));
        QTest::qWait(50); return true;
    };
    auto drag = [&](const QString &sourceName, const QString &slot, bool cancel = false) {
        QTest::qWait(30); // Let newly added merge rows finish their layout before the next gesture.
        auto *source = findItem("signalTreeName", sourceName);
        auto *target = findItem(slot == "merge" ? "ruleMergeDropArea" : "ruleInputSlot_" + slot);
        if (!source || !target) return false;
        const auto from = source->mapToScene(QPointF(20, source->height() / 2)).toPoint();
        const auto to = target->mapToScene(QPointF(25, target->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(window, from + QPoint(12, 0), 20);
        QTest::mouseMove(window, to, 20);
        const bool hit = processor->property("dropRuleInput").toString() == slot;
        if (!hit) qWarning() << "Drag target not hit" << operation->property("currentIndex") << sourceName << slot << from << to << processor->property("dropRuleInput");
        if (cancel) QTest::keyClick(window, Qt::Key_Escape);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        return hit;
    };
    auto rules = [&] { return controller.processingConfigurations().first().toMap()["rules"].toList(); };
    auto preview = [&] { return rules().last().toMap()["outputs"].toList().first().toMap()["preview"].toString(); };
    QVERIFY(start(3)); // Magnitude: explicit X/Y/Z slots, exact drag target, no preselected target needed.
    QVERIFY(findItem("ruleInputSlot_x")); QVERIFY(findItem("ruleInputSlot_y")); QVERIFY(findItem("ruleInputSlot_z"));
    // The whole card is an input target. One click on padding/blank space must
    // select it immediately, without requiring a second click on the text.
    processor->setProperty("selectedRuleInput", "z");
    for (int region = 0; region < 5; ++region) for (const auto &slot : {QString("x"), QString("y"), QString("z")}) {
        auto *card = findItem("ruleInputSlot_" + slot); QVERIFY(card);
        const auto point = region == 0 ? QPointF(4, 4)
            : region == 1 ? QPointF(card->width() / 2, 4)
            : region == 2 ? QPointF(card->width() / 2, card->height() - 4)
            : region == 3 ? QPointF(4, card->height() / 2)
            : QPointF(card->width() - 4, card->height() / 2);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, card->mapToScene(point).toPoint());
        if (processor->property("selectedRuleInput").toString() != slot) {
            auto *hit = window->contentItem();
            const auto scene = card->mapToScene(point);
            while (auto *child = hit->childAt(hit->mapFromScene(scene).x(), hit->mapFromScene(scene).y())) hit = child;
            qInfo() << "Missed input card" << slot << region << card->position() << card->size() << scene << "hit" << hit->metaObject()->className() << hit->objectName();
        }
        QCOMPARE(processor->property("selectedRuleInput").toString(), slot);
    }
    QCOMPARE(editor->property("inputXId").toString(), QString("raw:0"));
    QVERIFY(editor->property("inputYId").toString().isEmpty()); QVERIFY(editor->property("inputZId").toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(editor, "submit")); QVERIFY(rules().isEmpty());
    QVERIFY(editor->property("errorText").toString().contains(QStringLiteral("三个输入")));
    QVERIFY(drag("C", "z")); QCOMPARE(editor->property("inputZId").toString(), QString("raw:2"));
    QVERIFY(drag("B", "y")); QCOMPARE(editor->property("inputYId").toString(), QString("raw:1"));
    QVERIFY(drag("B", "x", true)); QCOMPARE(editor->property("inputXId").toString(), QString("raw:0"));
    QVERIFY(rules().isEmpty()); QCOMPARE(controller.signalCount(), 3);
    QVERIFY(QMetaObject::invokeMethod(editor, "submit")); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(rules().size(), 1); QCOMPARE(preview(), QString("13 / 13"));
    const auto magnitude = rules().first().toMap();
    QCOMPARE(magnitude["inputIds"].toStringList(), (QStringList{"raw:0", "raw:1", "raw:2"}));
    // Every single-input operation uses the same drag slot and hides Y/Z.
    for (const int op : {0, 1, 2, 6}) {
        QVERIFY(start(op)); QVERIFY(findItem("ruleInputSlot_x"));
        QVERIFY(!findItem("ruleInputSlot_y")); QVERIFY(!findItem("ruleInputSlot_z"));
        QVERIFY(drag("B", "x")); QCOMPARE(editor->property("inputXId").toString(), QString("raw:1"));
        QVERIFY(QMetaObject::invokeMethod(editor, "close")); QCOMPARE(rules().size(), 1);
    }
    QVERIFY(start(4)); // Expressions must never silently shift Z into Y when Y is absent.
    QVERIFY(drag("C", "z"));
    processor->findChild<QObject *>("objectRuleExpression")->setProperty("text", "x+z");
    QVERIFY(QMetaObject::invokeMethod(editor, "submit")); QCOMPARE(rules().size(), 1);
    QVERIFY(editor->property("errorText").toString().contains(QStringLiteral("先绑定 Y")));
    // Select Y using the slot's real button, then bind it by clicking the tree name.
    auto *ySlot = findItem("ruleInputSlot_y"); QVERIFY(ySlot);
    QQuickItem *chooseY = nullptr;
    for (auto *item : ySlot->findChildren<QQuickItem *>())
        if (item->property("text").toString() == QStringLiteral("从信号树选择")) { chooseY = item; break; }
    QVERIFY(chooseY);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, chooseY->mapToScene(QPointF(chooseY->width() / 2, chooseY->height() / 2)).toPoint());
    QCOMPARE(processor->property("selectedRuleInput").toString(), QString("y"));
    auto *bName = findItem("signalTreeName", "B"); QVERIFY(bName);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, bName->mapToScene(QPointF(20, bName->height() / 2)).toPoint());
    QCOMPARE(editor->property("inputYId").toString(), QString("raw:1"));
    QVERIFY(QMetaObject::invokeMethod(editor, "submit")); QTRY_VERIFY(!controller.deriving());
    QCOMPARE(preview(), QString("15 / 15"));
    // Previously computed outputs can be bound from the processing tree.
    QVERIFY(start(2));
    auto *tabs = root->findChild<QObject *>("signalBrowserTabs"); QVERIFY(tabs); tabs->setProperty("currentIndex", 1);
    const auto output = magnitude["outputs"].toList().first().toMap();
    controller.signalModel()->revealSignal(output["series"].toInt()); QCoreApplication::processEvents();
    QVERIFY(drag(output["name"].toString(), "x")); QCOMPARE(editor->property("inputXId").toString(), output["id"].toString());
    QVERIFY(QMetaObject::invokeMethod(editor, "submit")); QTRY_VERIFY(!controller.deriving()); QCOMPARE(preview(), QString("13 / 13"));
    QQmlExpression editMagnitude(QQmlEngine::contextForObject(processor), processor, "editRule(selected.rules[0])");
    editMagnitude.evaluate(); QVERIFY(!editMagnitude.hasError());
    tabs->setProperty("currentIndex", 1); controller.signalModel()->revealSignal(output["series"].toInt());
    QVERIFY(drag(output["name"].toString(), "x")); // Own output is highlighted as invalid and rejected.
    QVERIFY(!processor->property("dropRuleAllowed").toBool());
    QCOMPARE(editor->property("inputXId").toString(), QString("raw:0"));
    QVERIFY(!editor->property("errorText").toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(processor, "clearRuleInput", Q_ARG(QString, QString("y"))));
    processor->setProperty("selectedRuleInput", "y");
    bool invalidChecked = true;
    QVERIFY(QMetaObject::invokeMethod(processor, "ruleSourceChecked", Q_RETURN_ARG(bool, invalidChecked), Q_ARG(int, output["series"].toInt())));
    QVERIFY(!invalidChecked); // An unbound slot must not check forbidden outputs with no indexed ID.
    // Merge adds without duplicates; a drop onto a numbered input replaces that entry.
    QVERIFY(start(5)); QVERIFY(drag("A", "merge")); QVERIFY(drag("A", "merge"));
    QCOMPARE(editor->property("mergeInputs").toList().size(), 1);
    auto *remainingMerge = findItem("ruleInputSlot_merge:0"); QVERIFY(remainingMerge);
    QQuickItem *removeInput = nullptr;
    for (auto *item : remainingMerge->findChildren<QQuickItem *>())
        if (item->property("text").toString() == QStringLiteral("移除")) { removeInput = item; break; }
    QVERIFY(removeInput);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, removeInput->mapToScene(QPointF(removeInput->width() / 2, removeInput->height() / 2)).toPoint());
    QCOMPARE(editor->property("mergeInputs").toList().size(), 0);
    QVERIFY(drag("A", "merge"));
    QVERIFY(drag("B", "merge")); QCOMPARE(editor->property("mergeInputs").toList().size(), 2);
    QVERIFY(drag("C", "merge:1")); QCOMPARE(editor->property("mergeInputs").toList().last().toString(), QString("raw:2"));
    QVERIFY(drag("A", "merge:1")); QCOMPARE(editor->property("mergeInputs").toList().last().toString(), QString("raw:2"));
    QVERIFY(editor->property("errorText").toString().contains(QStringLiteral("不能重复")));
    QVERIFY(QMetaObject::invokeMethod(processor, "selectRuleSource", Q_ARG(int, 0)));
    QCOMPARE(editor->property("mergeInputs").toList().size(), 1);
    QVERIFY(QMetaObject::invokeMethod(editor, "submit")); QCOMPARE(rules().size(), 3);
    QVERIFY(editor->property("errorText").toString().contains(QStringLiteral("两个输入")));
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    for (int row = 0; row < controller.signalCount(); ++row) QVERIFY(!controller.plotSignalEnabled(0, row));
    // New rules receive an unused default name; changing type never overwrites edits.
    QQmlExpression namedRule(QQmlEngine::contextForObject(processor), processor, "editRule(null)");
    auto *name = processor->findChild<QObject *>("objectRuleName"); QVERIFY(name);
    for (int number = 1; number <= 2; ++number) {
        namedRule.evaluate(); QVERIFY(!namedRule.hasError());
        QCOMPARE(name->property("text").toString(), QStringLiteral("规则%1").arg(number));
        operation->setProperty("currentIndex", 2);
        QVERIFY(QMetaObject::invokeMethod(processor, "selectRuleSource", Q_ARG(int, 0)));
        QCOMPARE(name->property("text").toString(), QStringLiteral("规则%1").arg(number));
        QVERIFY(QMetaObject::invokeMethod(editor, "submit")); QTRY_VERIFY(!controller.deriving());
        QCOMPARE(rules().last().toMap()["name"].toString(), QStringLiteral("规则%1").arg(number));
    }
    namedRule.evaluate(); QVERIFY(!namedRule.hasError()); QCOMPARE(name->property("text").toString(), QStringLiteral("规则3"));
    name->setProperty("text", QStringLiteral("手动命名")); operation->setProperty("currentIndex", 3);
    QCOMPARE(name->property("text").toString(), QStringLiteral("手动命名"));
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    namedRule.evaluate(); QVERIFY(!namedRule.hasError()); QCOMPARE(name->property("text").toString(), QStringLiteral("规则3"));
    QCoreApplication::processEvents(); QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}
void ObjectDataTest::ruleEditorLargeSignalNavigation()
{
    qmlRegisterTypesAndRevisions<PlotItemQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<AppControllerQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<SignalModelQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
    QTemporaryDir dir; QFile file(dir.filePath("large-inputs.csv")); QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray header("time"), values("0");
    for (int i = 0; i < 10000; ++i) { header += ",s" + QByteArray::number(i); values += ",1"; }
    file.write(header + '\n' + values + '\n'); file.close();
    AppController controller; QVERIFY(controller.loadCsv(file.fileName())); QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.signalCount(), 10000);
    const auto config = controller.createDataObject("large inputs", "processing"); QVERIFY(!config.isEmpty());
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(DI_SOURCE_DIR "/qml/Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"appController", QVariant::fromValue(&controller)}}));
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto *processor = root->findChild<QObject *>("processingManager"); QVERIFY(processor);
    processor->setProperty("selectedId", config); QVERIFY(QMetaObject::invokeMethod(processor, "open"));
    QQmlExpression open(QQmlEngine::contextForObject(processor), processor, "editRule(null)");
    open.evaluate(); QVERIFY(!open.hasError()); QTest::qWait(50);
    auto *editor = processor->findChild<QObject *>("objectRuleDialog"); QVERIFY(editor);
    QVERIFY(editor->property("inputXId").toString().isEmpty());
    QVERIFY(editor->property("inputYId").toString().isEmpty());
    QVERIFY(editor->property("inputZId").toString().isEmpty());
    QElapsedTimer elapsed; elapsed.start();
    auto *operation = processor->findChild<QObject *>("objectRuleOperation"); QVERIFY(operation);
    for (int i = 0; i < 10; ++i) { operation->setProperty("currentIndex", 3); QCoreApplication::processEvents(); operation->setProperty("currentIndex", 2); QCoreApplication::processEvents(); }
    const auto typesMs = elapsed.nsecsElapsed() / 1e6;
    QQmlExpression lookup(QQmlEngine::contextForObject(processor), processor,
        "for (let i = 0; i < 100; ++i) { ruleInputName(''); ruleInputName('raw:9999'); ruleSourceAllowed(9999); }");
    elapsed.restart(); lookup.evaluate(); QVERIFY(!lookup.hasError()); const auto lookupMs = elapsed.nsecsElapsed() / 1e6;
    QSignalSpy resets(controller.signalModel(), &QAbstractItemModel::modelReset);
    QVERIFY(QMetaObject::invokeMethod(processor, "selectRuleSource", Q_ARG(int, 0)));
    elapsed.restart();
    for (int i = 0; i < 10; ++i) QVERIFY(QMetaObject::invokeMethod(processor, "browseRuleSource"));
    const auto browseMs = elapsed.nsecsElapsed() / 1e6;
    operation->setProperty("currentIndex", 4); QTest::qWait(50);
    auto *window = qobject_cast<QQuickWindow *>(root.get()); QVERIFY(window);
    auto *list = root->findChild<QQuickItem *>("signalList"); QVERIFY(list);
    auto clickSlot = [&](const QString &slot, bool label = false) {
        QQuickItem *item = nullptr;
        QList<QQuickItem *> pending{window->contentItem()};
        while (!pending.isEmpty()) {
            auto *candidate = pending.takeLast(); pending.append(candidate->childItems());
            if (candidate->isVisible() && candidate->objectName() == (label ? "ruleInputLabel_" : "chooseRuleInput_") + slot) { item = candidate; break; }
        }
        if (!item) return false;
        // Click the actual input-selection button, not a property setter or navigation method.
        const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        QCoreApplication::processEvents();
        return processor->property("selectedRuleInput").toString() == slot;
    };
    QVERIFY(QMetaObject::invokeMethod(processor, "clearRuleInput", Q_ARG(QString, QString("x"))));
    QVERIFY(clickSlot("y")); QVERIFY(clickSlot("z"));
    QElapsedTimer clicks; clicks.start();
    for (int i = 0; i < 10; ++i) { QVERIFY(clickSlot("x")); QVERIFY(clickSlot("y")); QVERIFY(clickSlot("z")); }
    const auto clickMs = clicks.nsecsElapsed() / 1e6;
    QVERIFY(editor->property("inputXId").toString().isEmpty());
    QVERIFY(editor->property("inputYId").toString().isEmpty());
    QVERIFY(editor->property("inputZId").toString().isEmpty());
    // Bound, widely separated sources must not force re-navigation on every slot click.
    for (const auto &entry : {qMakePair(0, QString("x")), qMakePair(5000, QString("y")), qMakePair(9999, QString("z"))})
        QVERIFY(QMetaObject::invokeMethod(processor, "bindRuleSource", Q_ARG(int, entry.first), Q_ARG(QString, entry.second), Q_ARG(bool, false)));
    auto *search = root->findChild<QObject *>("signalSearch"); QVERIFY(search);
    search->setProperty("text", "s"); QTest::qWait(200);
    list->setProperty("contentY", 3000.0); QTest::qWait(50);
    const auto scrollY = list->property("contentY").toDouble(); resets.clear();
    clicks.restart();
    for (int i = 0; i < 10; ++i) { QVERIFY(clickSlot("x", true)); QVERIFY(clickSlot("y", true)); QVERIFY(clickSlot("z", true)); }
    const auto boundClickMs = clicks.nsecsElapsed() / 1e6;
    QCOMPARE(search->property("text").toString(), QString("s"));
    QCOMPARE(list->property("contentY").toDouble(), scrollY);
    QCOMPARE(editor->property("inputXId").toString(), QString("raw:0"));
    QCOMPARE(editor->property("inputYId").toString(), QString("raw:5000"));
    QCOMPARE(editor->property("inputZId").toString(), QString("raw:9999"));
    qInfo() << "10000 signals: Y/Z then 30 empty-slot clicks=" << clickMs << "ms; 30 bound-slot clicks=" << boundClickMs << "ms; tree resets=" << resets.count();
    qInfo().nospace() << "10000 signals: 20 type switches=" << typesMs << " ms; 100 input lookups=" << lookupMs << " ms; 10 source navigations=" << browseMs << " ms; tree resets=" << resets.count();
    QCOMPARE(resets.count(), 0);
}
void ObjectDataTest::objectFieldPolicy()
{
    AppController controller;
    const auto general = controller.addDataObject("general");
    QCOMPARE(controller.dataObjects().first().toMap()["name"].toString(), QStringLiteral("对象 1"));
    const auto first = controller.addDataObjectField(general); QVERIFY(!first.isEmpty());
    const auto second = controller.addDataObjectField(general); QVERIFY(!second.isEmpty());
    auto fields = controller.dataObjects().first().toMap()["fields"].toList();
    QCOMPARE(fields[0].toMap()["name"].toString(), QStringLiteral("字段 1"));
    QCOMPARE(fields[1].toMap()["name"].toString(), QStringLiteral("字段 2"));
    QVERIFY(controller.renameDataObjectField(general, first, "pressure"));
    QVERIFY(controller.bindObjectField(general, first, "pressure", "latitude", -1).isEmpty());
    const auto aircraft = controller.createDataObject("plane", "aircraft");
    fields = controller.dataObjects().last().toMap()["fields"].toList(); QCOMPARE(fields.size(), 3);
    for (const auto &field : fields) {
        const auto id = field.toMap()["id"].toString();
        QVERIFY(!controller.removeObjectField(aircraft, id));
        QVERIFY(controller.bindObjectField(aircraft, id, "renamed", "scalar", -1).isEmpty());
    }
    const auto extra = controller.addDataObjectField(aircraft); QVERIFY(!extra.isEmpty());
    QVERIFY(controller.removeObjectField(aircraft, extra));
    QVERIFY(controller.configureDataObjectAircraft(aircraft, {{"geographic", false}, {"attitudeMode", 2}, {"radians", false}, {"order", 1}, {"scalarLast", true}, {"navigationToBody", true}}));
    auto configured = controller.dataObjects().last().toMap(); QCOMPARE(configured["attitudeMode"].toInt(), 2); QVERIFY(!configured["geographic"].toBool());
    const auto namedLatitude = fields.first().toMap()["id"].toString();
    QVERIFY(controller.renameDataObjectField(aircraft, namedLatitude, "自定义纬度"));
    QVERIFY(controller.addDataObjectField(aircraft).size() > 0);
    const auto before = controller.dataObjects().last().toMap()["fields"].toList();
    QCOMPARE(before.size(), 11);
    QVERIFY(controller.setDataObjectType(aircraft, "general"));
    QCOMPARE(controller.dataObjects().last().toMap()["fields"].toList().size(), 11);
    const auto generalCopy = controller.duplicateDataObject(aircraft); QVERIFY(!generalCopy.isEmpty());
    QVERIFY(controller.setDataObjectType(generalCopy, "aircraft"));
    QCOMPARE(controller.dataObjects().last().toMap()["fields"].toList().size(), 11);
    QCOMPARE(controller.dataObjects().last().toMap()["fields"].toList().first().toMap()["name"].toString(), QStringLiteral("自定义纬度"));
    QVERIFY(controller.removeDataObject(generalCopy));
    QVERIFY(controller.setDataObjectType(aircraft, "aircraft"));
    QCOMPARE(controller.dataObjects().last().toMap()["fields"].toList(), before);
    QVERIFY(!controller.removeObjectField(aircraft, namedLatitude));
    for (int i = 0; i < 10; ++i) QVERIFY(before[i].toMap()["fixed"].toBool());
    QVERIFY(!before.last().toMap()["fixed"].toBool());
    QVERIFY(controller.configureDataObjectAircraft(aircraft, {{"geographic", true}, {"attitudeMode", 0}, {"radians", false}, {"order", 0}, {"scalarLast", false}, {"navigationToBody", false}}));
    QVERIFY(controller.configureDataObjectAircraft(aircraft, {{"geographic", false}, {"attitudeMode", 2}, {"radians", false}, {"order", 1}, {"scalarLast", true}, {"navigationToBody", true}}));
    QCOMPARE(controller.dataObjects().last().toMap()["fields"].toList(), before);
    QVERIFY(!controller.configureDataObjectAircraft(aircraft, {{"attitudeMode", 3}}));
    SessionDocument session; QString error;
    DataObject object; object.id = "plane"; object.name = "plane"; object.type = "aircraft";
    object.geographic = false; object.attitude = {2, {{-1, -1, -1, -1}}, false, true, true, 1}; ensureAircraftFields(object); session.objects = {object};
    SessionDocument parsed; QVERIFY2(sessionFromJson(sessionToJson(session), &parsed, &error), qPrintable(error));
    QVERIFY(!parsed.objects[0].geographic); QCOMPARE(parsed.objects[0].attitude.mode, 2); QVERIFY(parsed.objects[0].attitude.scalarLast);
    session.objects[0].type = "general";
    session.objects[0].fields[0].name = "renamed X";
    QVERIFY2(sessionFromJson(sessionToJson(session), &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.objects[0].fields[0].role, QStringLiteral("x"));
    QCOMPARE(parsed.objects[0].fields[0].name, QStringLiteral("renamed X"));
    parsed.objects[0].type = "aircraft"; QVERIFY(ensureAircraftFields(parsed.objects[0]));
    QCOMPARE(parsed.objects[0].fields.size(), 7);
    session.objects[0].type = "aircraft";
    auto legacy = sessionToJson(session); legacy["version"] = 8;
    auto objects = legacy["objects"].toArray(); auto plane = objects[0].toObject(); plane.remove("aircraft");
    auto legacyFields = plane["fields"].toArray(); while (legacyFields.size() > 6) legacyFields.removeLast(); plane["fields"] = legacyFields; objects[0] = plane; legacy["objects"] = objects;
    QVERIFY(sessionFromJson(legacy, &parsed, &error)); QCOMPARE(parsed.objects[0].fields.size(), 12);
    legacy["version"] = 9; QVERIFY(!sessionFromJson(legacy, &parsed, &error));
    session.objects[0].fields.removeFirst(); QVERIFY(!sessionFromJson(sessionToJson(session), &parsed, &error));
}
QTEST_MAIN(ObjectDataTest)
#include "objectdata_test.moc"
