#include "appcontroller.h"
#include "plotitem.h"
#include "objectdefinition.h"
#include "qmltypes.h"
#include "xlsxreader.h"
#include "render/plotgeometrybuilder.h"
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
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
    void dependencyPlanPreservesIndependentBranches();
    void expressionsAndTimeBases();
    void ownershipAndSchema();
    void controllerLifecycleAndRestore();
    void editsPreserveUnrelatedResultsAndMergePendingObjects();
    void linkedTrajectoriesAndRuleEdits();
    void staircaseKeepsPulse();
    void objectManagerQml();
    void objectFieldPolicy();
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
    AppController controller; QVERIFY(controller.loadCsv(file.fileName())); QTRY_VERIFY(!controller.loading());
    const auto owner = controller.createDataObject("branches");
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
    QCOMPARE(parsed.objects.size(), 1);
    QCOMPARE(parsed.objects[0].type, QStringLiteral("general"));
    auto old = sessionToJson(state); old["version"] = 7;
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
    QVERIFY(controller.loadCsv(path)); QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QVERIFY(controller.addObjectRule("missing", {{"name", "bits"}, {"operation", "bits"}, {"inputs", QVariantList{"source"}}}).isEmpty());
    const auto objectId = controller.createDataObject("device"); QVERIFY(!objectId.isEmpty());
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
    QVERIFY(restored.removeFile("word.csv")); QTRY_VERIFY(!restored.deriving()); QCOMPARE(restored.signalCount(), 8);
    const auto obj = restored.dataObjects().first().toMap(); QCOMPARE(obj["fields"].toList().first().toMap()["series"].toInt(), -1);
    QVERIFY(restored.saveSession(dir.filePath("without-source.disession")));
    QVERIFY(restored.removeDataObject(objectId)); QCOMPARE(restored.signalCount(), 0);
}
void ObjectDataTest::editsPreserveUnrelatedResultsAndMergePendingObjects()
{
    QTemporaryDir dir; QFile file(dir.filePath("objects.csv"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("time,A,B\n0,2,20\n1,3,30\n"); file.close();
    AppController controller; PlotItem plot; plot.setWidth(600); plot.setHeight(300);
    controller.attachPlot(&plot); QVERIFY(controller.loadCsv(file.fileName())); QTRY_VERIFY(!controller.loading());
    const auto a = controller.createDataObject("A"), b = controller.createDataObject("B");
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
    AppController controller; PlotItem plot; controller.attachPlot(&plot); QVERIFY(controller.loadCsv(path)); QTRY_VERIFY(!controller.loading());
    const auto object = controller.createDataObject("plane", "aircraft");
    QVERIFY(controller.configureDataObjectAircraft(object, {{"geographic", true}, {"attitudeMode", 1}, {"radians", false}, {"order", 0}, {"scalarLast", false}, {"navigationToBody", false}}));
    const auto fields = controller.dataObjects().first().toMap()["fields"].toList();
    QCOMPARE(fields.size(), 6);
    const auto latitude = controller.bindObjectField(object, fields[0].toMap()["id"].toString(), "lat", "latitude", 0);
    QVERIFY(!latitude.isEmpty());
    QVERIFY(!controller.bindObjectField(object, fields[1].toMap()["id"].toString(), "lon", "longitude", 1).isEmpty());
    for (int i = 2; i < 6; ++i) QVERIFY(!controller.bindObjectField(object, fields[i].toMap()["id"].toString(), QString::number(i), QStringList{"height", "roll", "pitch", "yaw"}[i - 2], i).isEmpty());
    QVERIFY(controller.showObjectTrajectory(object, 0));
    QVERIFY(controller.configureDataObjectAircraft(object, {{"geographic", true}, {"attitudeMode", 1}, {"radians", true}, {"order", 1}, {"scalarLast", false}, {"navigationToBody", true}}));
    QVERIFY(!controller.setDataObjectType(object, "general"));
    QVERIFY(controller.bindObjectField(object, latitude, "lat", "latitude", 6).size() > 0);
    QVERIFY(controller.renameDataObject(object, "plane updated"));
    const auto rule = controller.addObjectRule(object, {{"name", "scaled"}, {"operation", "scale"}, {"inputs", QVariantList{latitude}}, {"factor", 2}});
    QVERIFY(!rule.isEmpty()); QTRY_VERIFY(!controller.deriving());
    QVERIFY(controller.editObjectRule(object, rule, {{"name", "modified"}, {"operation", "scale"}, {"inputs", QVariantList{latitude}}, {"factor", 3}}));
    QTRY_VERIFY(!controller.deriving());
    QCOMPARE(controller.dataObjects().first().toMap()["rules"].toList().first().toMap()["factor"].toDouble(), 3.0);
    const auto outputId = controller.dataObjects().first().toMap()["rules"].toList().first().toMap()["outputs"].toList().first().toMap()["id"].toString();
    QVERIFY(!controller.editObjectRule(object, rule, {{"name", "cycle"}, {"operation", "scale"}, {"inputs", QVariantList{outputId}}}));
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
    QVERIFY(restored.removeDataObject(object)); QCOMPARE(restored.signalCount(), 7);
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
    QTemporaryDir dir; QFile file(dir.filePath("qml.csv")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("time,A\n0,5\n1,7\n"); file.close();
    AppController controller; QVERIFY(controller.loadCsv(file.fileName())); QTRY_VERIFY(!controller.loading());
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
    auto *button = root->findChild<QObject *>("addObjectRuleButton"); QVERIFY(button);
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
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    auto *ruleFooter = root->findChild<QObject *>("objectRuleDialog")->property("footer").value<QObject *>();
    QVERIFY(ruleFooter); QCOMPARE(ruleFooter->property("count").toInt(), 2);
    root->findChild<QObject *>("objectRuleName")->setProperty("text", "flags");
    QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>("objectRuleDialog"), "submit"));
    QTRY_VERIFY(!controller.deriving()); QCOMPARE(controller.signalCount(), 9);
    QVERIFY(controller.configureTrajectory(0, true, 0, -1, -1, false));
    QVERIFY(!manager->property("timePlot").toBool());
    QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>("objectSwitchTimePlot"), "clicked"));
    QVERIFY(manager->property("timePlot").toBool());
    auto *nameField = root->findChild<QObject *>("objectNameField"); QVERIFY(nameField);
    QQmlExpression textColor(qmlContext(nameField), nameField, "palette.text");
    QCOMPARE(textColor.evaluate().value<QColor>().name(), QStringLiteral("#202830"));
    root->setProperty("darkTheme", true);
    QCOMPARE(textColor.evaluate().value<QColor>().name(), QStringLiteral("#e6edf3"));
    root->setProperty("darkTheme", false);
    QVERIFY(controller.setDataObjectType(id, "aircraft"));
    QCOMPARE(controller.dataObjects().first().toMap()["fields"].toList().size(), 5);
    QCOMPARE(controller.dataObjects().first().toMap()["rules"].toList().size(), 1);
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
    QCOMPARE(automatic["name"].toString(), QStringLiteral("对象 1"));
    auto *nameDialog = root->findChild<QObject *>("objectNameDialog"); QVERIFY(nameDialog); QVERIFY(!nameDialog->property("visible").toBool());
    QQuickItem *objectNameLabel = nullptr;
    QTRY_VERIFY((objectNameLabel = visualItem("dataObjectRowText", QStringLiteral("对象 1 · 常规"))));
    QTest::mouseDClick(quickWindow, Qt::LeftButton, Qt::NoModifier, objectNameLabel->mapToScene(QPointF(50, objectNameLabel->height() / 2)).toPoint());
    QTRY_VERIFY(nameDialog->property("visible").toBool());
    nameField->setProperty("text", QStringLiteral("液压对象")); QVERIFY(QMetaObject::invokeMethod(nameDialog, "accept"));
    QCOMPARE(controller.dataObjects().last().toMap()["name"].toString(), QStringLiteral("液压对象"));
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
    QVERIFY(QMetaObject::invokeMethod(manager, "close"));
    QVERIFY(root->findChild<QObject *>("plotGrid")->property("visible").toBool());
    QVERIFY(controller.plotSignalEnabled(0, 0));
    QCoreApplication::processEvents(); QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
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
