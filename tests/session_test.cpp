#include "controller/appcontroller.h"
#include "items/plotitem.h"
#include "items/statetimelineitem.h"
#include "qmltypes.h"
#include "state/sessiondocument.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

class SessionTest final : public QObject
{
    Q_OBJECT
private slots:
    void stateTimelineIntervalsAndMissingValues();
    void stateTimelineViewChangesKeepFrame();
    void stateTimelineSubpixelPulseSurvivesAlignment();
    void stateTimelinePersistenceAndSourceRemoval();
    void stateTimelineQmlConfiguration();
    void signalEnumsInlineEditorAndSharedViews();
    void mixedStateAndCurveLayout();
    void trajectorySelectionKeepsSingleState();
    void roundTripWithDuplicateNamesAndLateDelegates();
    void restoredSessionsContinuePaletteSafely();
    void extremeImportedRangesRemainSaveable();
    void failuresPreserveCurrentSession();
    void relativePathsSurviveMovingTheBundle();
    void relocatedFilesAndModifiedState();
    void schemaValidationAndAtomicSave();
    void qmlRestoresToolbarAndPlotStates();
    void viewTemplatesMatchRemapAndUndo();
    void viewTemplateSchemaIsIndependent();
};

void SessionTest::trajectorySelectionKeepsSingleState()
{
    SessionDocument document;
    auto &state = document.plots[0].trajectory;
    state.tracks.append(SessionTrajectoryEntry{});
    state.tracks[1].id = "second";
    state.activeEntry().name = "first edit";
    state.select(1);
    state.activeEntry().name = "second edit";
    state.select(0);
    QCOMPARE(state.activeEntry().name, QString("first edit"));
    // Editing an inactive list item must survive selecting it and serialization.
    state.tracks[1].name = "inactive edit";
    state.select(1);
    QCOMPARE(state.activeEntry().name, QString("inactive edit"));
    SessionDocument restored; QString error;
    QVERIFY2(sessionFromJson(sessionToJson(document), &restored, &error), qPrintable(error));
    QCOMPARE(restored.plots[0].trajectory.active, 1);
    QCOMPARE(restored.plots[0].trajectory.tracks[0].name, QString("first edit"));
    QCOMPARE(restored.plots[0].trajectory.activeEntry().name, QString("inactive edit"));
    QHash<int, SessionTrajectory> stored; stored.insert(0, state);
    QStringList names;
    for (const auto &entry : stored.value(0).entries()) names.append(entry.name);
    QCOMPARE(names, (QStringList{"first edit", "inactive edit"}));
}

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
}
static SessionDocument documentFor(const QString &path)
{
    SessionDocument doc;
    doc.files = {path};
    SessionSignal signal;
    signal.file = 0; signal.table = 0; signal.column = 0;
    signal.tableName = QFileInfo(path).completeBaseName();
    signal.originalName = "A"; signal.name = "Restored A"; signal.color = QColor("red");
    doc.series = {signal}; doc.plots[0].seriesIds = {0};
    return doc;
}

void SessionTest::roundTripWithDuplicateNamesAndLateDelegates()
{
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkpath("left")); QVERIFY(QDir(dir.path()).mkpath("right"));
    const QString left = dir.filePath("left/same.csv"), right = dir.filePath("right/same.csv");
    const QString session = dir.filePath(QStringLiteral("实验会话.disession"));
    writeFile(left, "time,A,B\n0,1,2\n30,3,4\n");
    writeFile(right, "time,A\n100,5\n200,6\n");
    AppController source;
    source.setLayout(2, 2);
    PlotItem a, b, c, d;
    source.attachPlot(&a, 0); source.attachPlot(&b, 1); source.attachPlot(&c, 2); source.attachPlot(&d, 3);
    QCOMPARE(source.loadFiles(QVariantList{left, right}), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!source.loading(), 5000);
    source.setActivePlot(0); source.toggleSignal(0); source.toggleSignal(2);
    source.setActivePlot(1); source.toggleSignal(1);
    source.setActivePlot(3); source.toggleSignal(2);
    QVERIFY(source.renameSignal(0, QStringLiteral("俯仰角")));
    source.setSignalPen(0, QColor("#8033cc55"), 4.5, Qt::DashDotLine);
    QVERIFY(source.setTimeOffset(0, 1, {}, 4.5));
    a.setXRange(-10, 50);
    a.setYRange(-5, 5); b.setYRange(50, 60); c.setYRange(-9, 9);
    d.setNormalizeY(true); d.setYRange(-.2, 1.2); a.setLineWidth(3.0);
    a.setCursorMode(PlotItem::DoubleCursor);
    a.setCursorPosition(5, 1); a.setCursorPosition(25, 2);
    a.setXRange(6, 20); // Both cursor positions lie outside the saved viewport.
    source.setSoloPlot(3);
    QVERIFY(source.saveSession(QUrl::fromLocalFile(session)));
    SessionDocument saved;
    QString error;
    QVERIFY2(readSessionDocument(session, &saved, &error), qPrintable(error));
    QCOMPARE(saved.files.size(), 2); QVERIFY(QDir::isRelativePath(saved.files[0]));
    QCOMPARE(saved.series[0].originalName, QStringLiteral("A"));
    QCOMPARE(saved.cursor.x1, 5.0); QCOMPARE(saved.cursor.x2, 25.0);

    AppController restored;
    QSignalSpy finished(&restored, &AppController::sessionRestoreFinished);
    QVERIFY(restored.restoreSession(session));
    QVERIFY(restored.restoringSession());
    QVERIFY(!restored.saveSession(dir.filePath("busy.disession")));
    QVERIFY(!restored.loadCsv(left));
    restored.setLayout(8, 8); // Busy APIs must not override the pending layout.
    QTRY_VERIFY_WITH_TIMEOUT(!restored.restoringSession(), 5000);
    QCOMPARE(finished.count(), 1); QVERIFY(finished[0][0].toBool());
    QCOMPARE(restored.plotRows(), 2); QCOMPARE(restored.plotColumns(), 2);
    QCOMPARE(restored.activePlotIndex(), 3); QCOMPARE(restored.soloPlotIndex(), 3);
    QCOMPARE(restored.loadedFileCount(), 2); QCOMPARE(restored.signalCount(), 3);
    QVERIFY(restored.signalModel()->groupAt(0) != restored.signalModel()->groupAt(2));
    QCOMPARE(restored.signalName(0), QStringLiteral("俯仰角"));
    QCOMPARE(restored.originalSignalName(0), QStringLiteral("A"));
    QCOMPARE(restored.signalColor(0), QColor("#8033cc55"));
    QCOMPARE(restored.signalWidth(0), 4.5); QCOMPARE(restored.signalStyle(0), int(Qt::DashDotLine));
    QCOMPARE(restored.signalTimeOffset(1), 4.5);
    QCOMPARE(restored.plotSignalRows(0), QVariantList({QVariant(0), QVariant(2)}));
    QCOMPARE(restored.plotSignalRows(1), QVariantList({QVariant(1)}));
    QVERIFY(restored.plotSignalRows(2).isEmpty());
    QCOMPARE(restored.plotSignalRows(3), QVariantList({QVariant(2)}));
    // Saving before any QML delegates exist must still preserve restored views.
    const QString again = dir.filePath("again.disession");
    QVERIFY(restored.saveSession(again));
    SessionDocument beforeAttach;
    QVERIFY(readSessionDocument(again, &beforeAttach, &error));
    QCOMPARE(beforeAttach.plots[1].yMinimum, 50.0);
    QCOMPARE(beforeAttach.cursor.x2, 25.0);
    PlotItem ra, rb, rc, rd;
    restored.attachPlot(&ra, 0); restored.attachPlot(&rb, 1); restored.attachPlot(&rc, 2); restored.attachPlot(&rd, 3);
    QCoreApplication::processEvents();
    QCOMPARE(ra.xMinimum(), 6.0); QCOMPARE(ra.xMaximum(), 20.0);
    QCOMPARE(ra.yMinimum(), -5.0); QCOMPARE(rb.yMinimum(), 50.0); QCOMPARE(rc.yMaximum(), 9.0);
    QVERIFY(rd.normalizeY()); QCOMPARE(rd.yMinimum(), -.2); QCOMPARE(rd.yMaximum(), 1.2);
    QCOMPARE(ra.lineWidth(), 3.0);
    for (auto *plot : {&ra, &rb, &rc, &rd}) {
        QCOMPARE(plot->cursorMode(), int(PlotItem::DoubleCursor));
        QCOMPARE(plot->cursorX1(), 5.0); QCOMPARE(plot->cursorX2(), 25.0);
    }
    restored.detachPlot(&rb, 1);
    PlotItem replacement;
    restored.attachPlot(&replacement, 1);
    QCOMPARE(replacement.yMinimum(), 50.0); QCOMPARE(replacement.cursorX2(), 25.0);
    // Remove an earlier file: persisted source-column identity must survive ID remapping.
    QVERIFY(restored.removeFile(restored.signalModel()->groupAt(0)));
    QVERIFY(restored.saveSession(again));
    AppController afterRemoval;
    QVERIFY(afterRemoval.restoreSession(again));
    QTRY_VERIFY_WITH_TIMEOUT(!afterRemoval.restoringSession(), 5000);
    QCOMPARE(afterRemoval.signalCount(), 1);
    QCOMPARE(afterRemoval.plotSignalRows(3), QVariantList({QVariant(0)}));
}

void SessionTest::failuresPreserveCurrentSession()
{
    QTemporaryDir dir;
    const QString keep = dir.filePath("keep.csv"), incoming = dir.filePath("incoming.csv");
    const QString path = dir.filePath("test.disession");
    writeFile(keep, "time,Keep\n0,1\n1,2\n");
    AppController controller;
    PlotItem plot; controller.attachPlot(&plot);
    QVERIFY(controller.loadCsv(keep)); QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    controller.selectSignal(0); plot.setYRange(-7, 8);
    QString error;
    auto doc = documentFor("missing.csv");
    QVERIFY(writeSessionDocument(path, doc, &error));
    QVERIFY(!controller.restoreSession(path));
    QCOMPARE(controller.signalName(0), QStringLiteral("Keep")); QCOMPARE(plot.yMinimum(), -7.0);
    writeFile(path, "{bad json");
    QVERIFY(!controller.restoreSession(path)); QCOMPARE(controller.loadedFileCount(), 1);

    doc = documentFor("incoming.csv");
    QVERIFY(writeSessionDocument(path, doc, &error));
    for (const QByteArray &data : {QByteArray("time,Changed\n0,4\n"), QByteArray()}) {
        writeFile(incoming, data);
        QSignalSpy finished(&controller, &AppController::sessionRestoreFinished);
        QVERIFY(controller.restoreSession(path));
        // Staging keeps the current data and bindings until the entire restore succeeds.
        QCOMPARE(controller.signalName(0), QStringLiteral("Keep"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.restoringSession(), 5000);
        QCOMPARE(finished.count(), 1); QVERIFY(!finished[0][0].toBool());
        QCOMPARE(controller.signalCount(), 1); QCOMPARE(controller.signalName(0), QStringLiteral("Keep"));
        QCOMPARE(controller.plotSignalRows(0), QVariantList({QVariant(0)})); QCOMPARE(plot.yMinimum(), -7.0);
    }
}

void SessionTest::relativePathsSurviveMovingTheBundle()
{
    QTemporaryDir dir;
    QDir root(dir.path()); QVERIFY(root.mkpath("bundle/data"));
    writeFile(root.filePath("bundle/data/flight.csv"), "time,A\n0,1\n");
    {
        AppController controller;
        QVERIFY(controller.loadCsv(root.filePath("bundle/data/flight.csv")));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
        controller.selectSignal(0);
        QVERIFY(controller.saveSession(root.filePath("bundle/view.disession")));
    }
    QVERIFY(root.rename("bundle", "moved"));
    AppController restored;
    QVERIFY(restored.restoreSession(root.filePath("moved/view.disession")));
    QTRY_VERIFY_WITH_TIMEOUT(!restored.restoringSession(), 5000);
    QCOMPARE(restored.signalCount(), 1); QCOMPARE(restored.plotSignalRows(0), QVariantList({QVariant(0)}));
    // Empty sessions are also valid and replace the loaded data intentionally.
    SessionDocument empty;
    QString error;
    const QString emptyPath = root.filePath("empty.disession");
    QVERIFY(writeSessionDocument(emptyPath, empty, &error));
    QVERIFY(restored.restoreSession(emptyPath));
    QCOMPARE(restored.loadedFileCount(), 0); QVERIFY(!restored.restoringSession());
}

void SessionTest::relocatedFilesAndModifiedState()
{
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkpath("old"));
    QVERIFY(QDir(dir.path()).mkpath("new"));
    const QString original = dir.filePath("old/flight.csv");
    const QString relocated = dir.filePath("new/flight.csv");
    const QString session = dir.filePath("view.disession");
    writeFile(original, "time,A\n0,1\n1,2\n");
    AppController controller;
    QVERIFY(!controller.sessionModified());
    QVERIFY(controller.loadCsv(original));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(controller.sessionModified());
    controller.selectSignal(0);
    QVERIFY(controller.saveSession(session));
    QVERIFY(!controller.sessionModified());
    controller.filterSignals("A");
    controller.filterSignals("");
    controller.signalModel()->toggleGroup("flight.csv");
    QVERIFY(!controller.sessionModified());
    QVERIFY(controller.renameSignal(0, "Renamed"));
    QVERIFY(controller.sessionModified());
    QVERIFY(controller.saveSession(session));
    QVERIFY(QFile::rename(original, relocated));
    const auto missing = controller.missingSessionFiles(session);
    QCOMPARE(missing.size(), 1);
    QCOMPARE(missing[0].toMap()["index"].toInt(), 0);
    QCOMPARE(missing[0].toMap()["path"].toString(), original);
    QVERIFY(!controller.restoreSessionWithFiles(session, {{"0", dir.filePath("absent.csv")}}));
    QCOMPARE(controller.signalName(0), QStringLiteral("Renamed"));
    QVERIFY(!controller.sessionModified());
    QVERIFY(controller.restoreSessionWithFiles(session, {{"0", QUrl::fromLocalFile(relocated)}}));
    QTRY_VERIFY(!controller.restoringSession());
    QCOMPARE(controller.signalName(0), QStringLiteral("Renamed"));
    QCOMPARE(controller.plotSignalRows(0), QVariantList{0});
    QVERIFY(controller.sessionModified()); // New source paths need saving.
    QVERIFY(controller.saveSession(session));
    QVERIFY(controller.missingSessionFiles(session).isEmpty());
    QVERIFY(!controller.sessionModified());
    QVERIFY(controller.restoreSession(session));
    QTRY_VERIFY(!controller.restoringSession());
    QVERIFY(!controller.sessionModified());
    controller.clear();
    QVERIFY(controller.sessionModified());
}

void SessionTest::schemaValidationAndAtomicSave()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("schema.disession");
    QString error;
    auto doc = documentFor("flight.csv");
    const QJsonObject valid = sessionToJson(doc);
    QVector<QJsonObject> invalid;
    for (int version = 0; version < SessionDocument::version; ++version) {
        auto old = valid; old["version"] = version; invalid.append(old);
    }
    for (const auto &version : {QJsonValue(99), QJsonValue(18.5), QJsonValue("18"), QJsonValue()}) {
        auto wrong = valid; wrong["version"] = version; invalid.append(wrong);
    }
    auto object = valid; object["xRange"] = QJsonArray{2, 1}; invalid.append(object);
    object = valid; auto plots = object["plots"].toArray(); auto plot = plots[0].toObject();
    plot["signals"] = QJsonArray{99}; plots[0] = plot; object["plots"] = plots; invalid.append(object);
    object = valid; auto series = object["signals"].toArray(); series.append(series[0]); object["signals"] = series; invalid.append(object);
    for (const auto &json : invalid) {
        SessionDocument parsed = doc;
        QVERIFY(!sessionFromJson(json, &parsed, &error)); QVERIFY(!error.isEmpty());
        QCOMPARE(sessionToJson(parsed), valid);
    }
    writeFile(path, "original");
    doc.cursor.x1 = std::numeric_limits<double>::infinity();
    QVERIFY(!writeSessionDocument(path, doc, &error));
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("original")); file.close();
    QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(16 * 1024 * 1024 + 1)); file.close();
    SessionDocument parsed;
    QVERIFY(!readSessionDocument(path, &parsed, &error)); QVERIFY(error.contains("16 MiB"));
}

static QList<PlotItem *> visualPlots(QObject *root)
{
    QList<PlotItem *> result;
    const auto *window = qobject_cast<QQuickWindow *>(root);
    if (!window) return result;
    QList<QQuickItem *> pending{window->contentItem()};
    while (!pending.isEmpty()) {
        QQuickItem *item = pending.takeLast();
        if (auto *plot = qobject_cast<PlotItem *>(item)) result.append(plot);
        pending.append(item->childItems());
    }
    return result;
}

void SessionTest::qmlRestoresToolbarAndPlotStates()
{
    const bool nativeDialogsDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restoreDialogSetting = qScopeGuard([nativeDialogsDisabled] {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
    });
    qmlRegisterTypesAndRevisions<StateTimelineItemQmlRegistration, TrajectoryItemQmlRegistration>("DataInspector", 1);
    qmlRegisterType<PlotItem>("DataInspector", 1, 0, "PlotItem");
    qmlRegisterUncreatableType<AppController>("DataInspector", 1, 0, "AppController", "Owned by C++");
    qmlRegisterUncreatableType<SignalModel>("DataInspector", 1, 0, "SignalModel", "Owned by AppController");
    const QDir qmlDirectory(QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml"));
    qmlRegisterType(QUrl::fromLocalFile(qmlDirectory.filePath("QuickPlot.qml")), "DataInspector", 1, 0, "QuickPlot");
    AppController controller;
    QQmlEngine engine;
    QStringList qmlWarnings;
    connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors) qmlWarnings.append(error.toString());
    });
    QQmlComponent component(&engine, QUrl::fromLocalFile(qmlDirectory.filePath("Main.qml")));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"visible", false}, {"appController", QVariant::fromValue(&controller)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    QCOMPARE(root->property("appController").value<AppController *>(), &controller);
    QVERIFY(root->findChild<QObject *>("sessionMenuButton"));
    QVERIFY(root->findChild<QObject *>("saveSessionDialog"));
    QVERIFY(root->findChild<QObject *>("openSessionDialog"));
    QTemporaryDir dir;
    writeFile(dir.filePath("flight.csv"), "time,A\n0,1\n1,2\n");
    auto doc = documentFor("flight.csv");
    doc.rows = 2; doc.columns = 2; doc.active = 3; doc.solo = 3;
    doc.plots.resize(4);
    for (int i = 0; i < 4; ++i) { doc.plots[i].seriesIds = {0}; doc.plots[i].yMinimum = -10-i; doc.plots[i].yMaximum = 10+i; }
    doc.cursor = {2, -2, 3};
    QString error;
    const QString path = dir.filePath("qml.disession");
    QVERIFY(writeSessionDocument(path, doc, &error));
    QVERIFY(controller.restoreSession(path));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.restoringSession(), 5000);
    QTRY_COMPARE(visualPlots(root.get()).size(), 4);
    QCOMPARE(root->property("selectedCursorMode").toInt(), 2);
    QVERIFY(root->property("subplotMaximized").toBool());
    const auto plots = visualPlots(root.get());
    for (auto *plotItem : plots) {
        QQuickItem *owner = plotItem;
        while (owner && !owner->property("plotIndex").isValid()) owner = owner->parentItem();
        QVERIFY(owner);
        const int index = owner->property("plotIndex").toInt();
        QCOMPARE(plotItem->yMinimum(), double(-10-index));
        QCOMPARE(plotItem->cursorX1(), -2.0); QCOMPARE(plotItem->cursorX2(), 3.0);
    }
    QCoreApplication::processEvents();
    QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join('\n')));
    auto *host = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(host);
    host->show();
    QVERIFY(QTest::qWaitForWindowExposed(host));
    const QString view = dir.filePath("qml.diview");
    QVERIFY(controller.saveViewTemplate(view));
    auto *templateDialog = root->findChild<QObject *>("viewTemplateDialog");
    QVERIFY(templateDialog);
    templateDialog->setProperty("file", QUrl::fromLocalFile(view));
    templateDialog->setProperty("preview", controller.previewViewTemplate(view));
    templateDialog->setProperty("mapping", QVariantMap{{"0", -1}});
    QVERIFY(!templateDialog->property("complete").toBool());
    QVERIFY(QMetaObject::invokeMethod(templateDialog, "assign", Q_ARG(QVariant, 0), Q_ARG(QVariant, 0)));
    QTRY_VERIFY(templateDialog->property("complete").toBool());
    QVERIFY(QMetaObject::invokeMethod(templateDialog, "open"));
    QTRY_VERIFY(templateDialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(templateDialog, "apply"));
    QTRY_VERIFY(!templateDialog->property("visible").toBool());
    QVERIFY(controller.canUndoViewTemplate());
    QVERIFY(controller.undoViewTemplate());
    QCoreApplication::processEvents();
    QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join('\n')));
    QVERIFY(controller.renameSignal(0, "Unsaved rename"));
    QVERIFY(controller.sessionModified());
    auto *prompt = root->findChild<QObject *>("unsavedSessionDialog");
    QVERIFY(prompt);
    host->showMinimized();
    QTRY_COMPARE(host->windowState(), Qt::WindowMinimized);
    QWindow foregroundWindow;
    if (QGuiApplication::platformName() == "windows") {
        foregroundWindow.setTitle("DataInspector exit focus regression");
        foregroundWindow.show();
        foregroundWindow.requestActivate();
        QTRY_VERIFY(foregroundWindow.isActive());
    }
    host->close();
    QTRY_VERIFY(prompt->property("visible").toBool());
    QVERIFY(host->isVisible());
    QTRY_VERIFY(host->windowState() != Qt::WindowMinimized);
    if (QGuiApplication::platformName() == "windows") {
        QTRY_VERIFY(host->isActive());
        foregroundWindow.hide();
    }
    QVERIFY(prompt->property("focus").toBool());
    QTest::mouseClick(host, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTest::keyClick(host, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QVERIFY(prompt->property("visible").toBool());
    QCOMPARE(root->property("pendingUnsavedAction").toString(), QStringLiteral("close"));
    QVERIFY(QMetaObject::invokeMethod(prompt, "reject"));
    QCOMPARE(root->property("pendingUnsavedAction").toString(), QString());
    QVERIFY(host->isVisible());
    host->hide();
    host->close();
    QTRY_VERIFY(host->isVisible());
    QTRY_VERIFY(prompt->property("visible").toBool());
    host->showMaximized();
    QTRY_COMPARE(host->windowState(), Qt::WindowMaximized);
    host->showMinimized();
    host->close(); // Repeated close must bring the existing prompt back into view.
    QTRY_COMPARE(host->windowState(), Qt::WindowMaximized);
    QVERIFY(prompt->property("visible").toBool());
    auto *saveDialog = root->findChild<QObject *>("saveSessionDialog"); QVERIFY(saveDialog);
    QVERIFY(QMetaObject::invokeMethod(prompt, "accept"));
    QTRY_VERIFY(saveDialog->property("visible").toBool());
    QCOMPARE(saveDialog->property("parentWindow").value<QObject *>(), host);
    host->close();
    QVERIFY(!prompt->property("visible").toBool());
    QVERIFY(saveDialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(saveDialog, "reject"));
    QCOMPARE(root->property("pendingUnsavedAction").toString(), QString());
    QVERIFY(host->isVisible());
    host->close();
    QTRY_VERIFY(prompt->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(prompt, "discarded"));
    QCoreApplication::processEvents();
    QTRY_VERIFY(!host->isVisible()); // Queued activation must not reopen a discarded session.
    root->setProperty("allowClose", false);
    host->show();
    QVERIFY(controller.saveSession(path));
    QVERIFY(!controller.sessionModified());
    host->close();
    QTRY_VERIFY(!host->isVisible());
    QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join('\n')));
}

void SessionTest::restoredSessionsContinuePaletteSafely()
{
    const auto &palette = AppController::signalPalette();
    const int paletteSize = palette.size();
    for (int count : {paletteSize, paletteSize + 1, 2 * paletteSize + 3}) {
        QTemporaryDir dir;
        QByteArray header("time"), values("0");
        for (int i = 0; i < count; ++i) {
            header += ",S" + QByteArray::number(i); values += ",1";
        }
        writeFile(dir.filePath("many.csv"), header + "\n" + values + "\n");
        writeFile(dir.filePath("extra.csv"), "time,NewA,NewB\n0,2,3\n");
        const QString path = dir.filePath("many.disession");
        AppController source;
        QVERIFY(source.loadCsv(dir.filePath("many.csv")));
        QTRY_VERIFY_WITH_TIMEOUT(!source.loading(), 5000);
        source.setSignalPen(0, QColor("magenta"), 4.5, Qt::DashLine);
        QVERIFY(source.saveSession(path));
        AppController restored;
        QSignalSpy finished(&restored, &AppController::sessionRestoreFinished);
        QVERIFY(restored.restoreSession(path));
        QTRY_VERIFY_WITH_TIMEOUT(!restored.loading(), 5000);
        QCOMPARE(finished.size(), 1); QVERIFY(finished[0][0].toBool());
        QVERIFY(restored.loadCsv(dir.filePath("extra.csv")));
        QTRY_VERIFY_WITH_TIMEOUT(!restored.loading(), 5000);
        QCOMPARE(restored.signalCount(), count + 2);
        QCOMPARE(restored.signalColor(count), palette.at(0));
        QCOMPARE(restored.signalColor(count + 1), palette.at(1));
        QCOMPARE(restored.signalColor(0), QColor("magenta"));
        QCOMPARE(restored.signalWidth(0), 4.5);
    }
}

void SessionTest::extremeImportedRangesRemainSaveable()
{
    QTemporaryDir dir;
    const QString csv = dir.filePath("extreme.csv"), session = dir.filePath("view.disession");
    // The finite endpoints have an unrepresentable span: keep the previous view.
    writeFile(csv, "time,A\n-1e308,1\n1e308,2\n");
    AppController controller;
    QVERIFY(controller.loadCsv(csv));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    controller.selectSignal(0); controller.fitAllPlots();
    QVERIFY(controller.saveSession(session));
    SessionDocument document;
    QString error;
    QVERIFY(readSessionDocument(session, &document, &error));
    QCOMPARE(document.xMinimum, 0.0); QCOMPARE(document.xMaximum, 1.0);
    controller.clear();
    const double lo = 1e20, hi = lo + 1e6;
    writeFile(csv, "time,A\n" + QByteArray::number(lo, 'g', 17) + ",1\n"
              + QByteArray::number(hi, 'g', 17) + ",2\n");
    QVERIFY(controller.loadCsv(csv));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QVERIFY(controller.saveSession(session));
    QVERIFY(readSessionDocument(session, &document, &error));
    QVERIFY(document.xMinimum <= lo); QVERIFY(document.xMaximum >= hi);
    QVERIFY(qIsFinite(document.xMaximum - document.xMinimum));
}

void SessionTest::viewTemplatesMatchRemapAndUndo()
{
    QTemporaryDir dir;
    const QString oldFile = dir.filePath("old.csv"), newFile = dir.filePath("new.csv");
    const QString view = dir.filePath("analysis.diview");
    writeFile(oldFile, "time,A,B,Unused\n0,1,2,3\n10,4,5,6\n");
    writeFile(newFile, "time,B,A,Unused\n100,5,10,0\n140,6,20,0\n");
    AppController source;
    PlotItem a, b;
    source.setLayout(2, 1); source.attachPlot(&a, 0); source.attachPlot(&b, 1);
    QVERIFY(source.loadCsv(oldFile)); QTRY_VERIFY(!source.loading());
    source.selectSignal(0);
    source.setSignalPen(0, QColor("magenta"), 4, Qt::DashLine);
    QVERIFY(source.renameSignal(0, "Display A"));
    QVERIFY(source.configureTrajectory(1, true, 0, 1, -1, false));
    a.setXRange(2, 8); a.setYRange(-50, 50);
    QVERIFY(source.saveViewTemplate(view));
    QVERIFY(source.sessionPath().isEmpty());
    ViewTemplateDocument doc; QString error;
    QVERIFY2(readViewTemplate(view, &doc, &error), qPrintable(error));
    SessionDocument sessionDoc;
    QVERIFY(!readSessionDocument(view, &sessionDoc, &error));

    AppController target;
    PlotItem plot;
    target.attachPlot(&plot, 0);
    QVERIFY(target.loadCsv(newFile)); QTRY_VERIFY(!target.loading());
    target.selectSignal(0); plot.setCursorMode(PlotItem::DoubleCursor);
    target.setLayout(2, 1); target.setActivePlot(1); target.selectSignal(2);
    target.setLayout(1, 1); target.setActivePlot(0);
    plot.restoreCursorState(PlotItem::DoubleCursor, 110, 130);
    const auto beforeColor = target.signalColor(1);
    const auto preview = target.previewViewTemplate(view);
    QVERIFY(preview["error"].toString().isEmpty());
    const auto rows = preview["rows"].toList();
    QCOMPARE(rows.size(), 2); // Unused signals do not require assignments.
    QCOMPARE(rows[0].toMap()["match"].toInt(), 1);
    QCOMPARE(rows[1].toMap()["match"].toInt(), 0);
    QVERIFY(!target.applyViewTemplate(view, {{"0", 1}, {"1", 1}}));
    QVERIFY(!target.applyViewTemplate(view, {{"0", 1.5}, {"1", 0}}));
    QCOMPARE(target.plotRows(), 1); QCOMPARE(target.signalColor(1), beforeColor);
    QVERIFY(target.applyViewTemplate(view, {{"0", 1}, {"1", 0}}));
    QCOMPARE(target.loadedFileCount(), 1); QCOMPARE(target.signalCount(), 3);
    QCOMPARE(target.plotRows(), 2); QVERIFY(target.plotSignalEnabled(0, 1));
    QVERIFY(target.signalModel()->plotRows(1).isEmpty());
    QCOMPARE(target.signalName(1), QString("Display A"));
    QCOMPARE(target.signalColor(1), QColor("magenta"));
    const auto trajectory = target.trajectoryState(1);
    QVERIFY(trajectory["enabled"].toBool());
    QCOMPARE(trajectory["x"].toInt(), 1); QCOMPARE(trajectory["y"].toInt(), 0);
    QCOMPARE(plot.xMinimum(), 99.2); QCOMPARE(plot.xMaximum(), 140.8);
    QCOMPARE(plot.yMinimum(), 9.5); QCOMPARE(plot.yMaximum(), 20.5);
    QCOMPARE(plot.cursorX1(), 100.0); QCOMPARE(plot.cursorX2(), 140.0);
    QVERIFY(target.canUndoViewTemplate());
    QVERIFY(target.undoViewTemplate());
    QCOMPARE(target.plotRows(), 1); QVERIFY(target.plotSignalEnabled(0, 0));
    QVERIFY(!target.plotSignalEnabled(0, 1));
    QCOMPARE(target.signalColor(1), beforeColor);
    QCOMPARE(plot.cursorX1(), 110.0); QCOMPARE(plot.cursorX2(), 130.0);
    QVERIFY(!target.undoViewTemplate());
    QVERIFY(target.applyViewTemplate(view, {{"0", 1}, {"1", 0}}, true));
    QCOMPARE(plot.xMinimum(), 2.0); QCOMPARE(plot.xMaximum(), 8.0);
    QCOMPARE(plot.yMinimum(), -50.0); QCOMPARE(plot.yMaximum(), 50.0);
    QVERIFY(target.undoViewTemplate());
    QVERIFY(target.loadCsv(oldFile)); QTRY_VERIFY(!target.loading());
    const auto ambiguous = target.previewViewTemplate(view)["rows"].toList();
    // Different table names use the table ordinal fallback. Both files contain A/B.
    // Exact old table matches take precedence over fallback candidates.
    QCOMPARE(ambiguous[0].toMap()["match"].toInt(), 3);
    writeFile(dir.filePath("corrupt.diview"), "{\"format\":\"DataInspectorViewTemplate\",\"version\":99}");
    QVERIFY(!target.applyViewTemplate(dir.filePath("corrupt.diview"), {}));
    QCOMPARE(target.plotRows(), 1);
    QVERIFY(target.applyViewTemplate(view, {{"0", 3}, {"1", 4}}));
    QVERIFY(target.canUndoViewTemplate());
    QVERIFY(QDir(dir.path()).mkpath("duplicate"));
    const QString duplicate = dir.filePath("duplicate/old.csv");
    writeFile(duplicate, "time,A,B,Unused\n0,1,2,3\n10,4,5,6\n");
    QVERIFY(target.loadCsv(duplicate)); QTRY_VERIFY(!target.loading());
    QVERIFY(!target.canUndoViewTemplate()); QVERIFY(!target.undoViewTemplate());
    const auto duplicates = target.previewViewTemplate(view)["rows"].toList();
    QCOMPARE(duplicates[0].toMap()["match"].toInt(), -1);
    QCOMPARE(duplicates[1].toMap()["match"].toInt(), -1);
    QFile original(view); QVERIFY(original.open(QIODevice::ReadOnly));
    const auto bytes = original.readAll(); original.close();
    QVERIFY(readViewTemplate(view, &doc, &error));
    doc.series[0].color = QColor();
    QVERIFY(!writeViewTemplate(view, doc, &error));
    QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), bytes);
}
void SessionTest::viewTemplateSchemaIsIndependent()
{
    auto session = documentFor("D:/private/flight/old.csv");
    session.series[0].timeOffset = 123;
    auto unused = session.series[0]; unused.column = 1; unused.originalName = "Unused";
    session.series.append(unused);
    const auto view = viewTemplateFromSession(session);
    const auto json = viewTemplateToJson(view);
    const auto bytes = QJsonDocument(json).toJson();
    QCOMPARE(json["version"].toInt(), ViewTemplateDocument::version);
    QCOMPARE(json["signals"].toArray().size(), 1);
    const auto trajectory = json["view"].toObject()["plots"].toArray()[0].toObject()["trajectory"].toObject();
    QVERIFY(!trajectory.contains("axes")); QVERIFY(!trajectory.contains("geographic"));
    QVERIFY(!bytes.contains("D:/private"));
    QVERIFY(!bytes.contains("timeOffset")); QVERIFY(!bytes.contains("Unused"));
    QVERIFY(!json["view"].toObject().contains("files"));
    QVERIFY(!json["view"].toObject().contains("objects"));
    QVERIFY(!json["view"].toObject().contains("cursor"));
    ViewTemplateDocument restored; QString error;
    QVERIFY2(viewTemplateFromJson(json, &restored, &error), qPrintable(error));
    QCOMPARE(restored.series[0].sourceName, QString("old.csv"));
    const QJsonObject legacy{{"format", "DataInspectorViewTemplate"}, {"version", 1}, {"view", sessionToJson(session)}};
    QVERIFY(!viewTemplateFromJson(legacy, &restored, &error));
    QCOMPARE(viewTemplateToJson(restored), json);
    auto wrong = json; wrong["version"] = 2;
    QVERIFY(!viewTemplateFromJson(wrong, &restored, &error));
    wrong = json; wrong["files"] = QJsonArray{};
    QVERIFY(!viewTemplateFromJson(wrong, &restored, &error));
    wrong = json; auto signalRows = wrong["signals"].toArray(); signalRows.append(signalRows.first()); wrong["signals"] = signalRows;
    QVERIFY(!viewTemplateFromJson(wrong, &restored, &error));
    wrong = json; wrong["signals"] = QJsonArray{};
    QVERIFY(!viewTemplateFromJson(wrong, &restored, &error));
    wrong = json; signalRows = wrong["signals"].toArray(); auto signal = signalRows[0].toObject();
    signal["timeOffset"] = 1; signalRows[0] = signal; wrong["signals"] = signalRows;
    QVERIFY(!viewTemplateFromJson(wrong, &restored, &error));
    wrong = json; auto config = wrong["view"].toObject(); auto plots = config["plots"].toArray();
    auto plot = plots[0].toObject(); plot["signals"] = QJsonArray{1}; plots[0] = plot; config["plots"] = plots; wrong["view"] = config;
    QVERIFY(!viewTemplateFromJson(wrong, &restored, &error));
    QCOMPARE(viewTemplateToJson(restored), json); // Failed reads never replace the last valid template.
    QTemporaryDir dir;
    const auto path = dir.filePath("legacy.diview");
    writeFile(path, QJsonDocument(legacy).toJson());
    QVERIFY(!readViewTemplate(path, &restored, &error));
    QFile unchanged(path); QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(unchanged.readAll()).object(), legacy); unchanged.close();
    QVERIFY(writeViewTemplate(path, restored, &error));
    QFile updated(path); QVERIFY(updated.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(updated.readAll()).object()["version"].toInt(), ViewTemplateDocument::version);
}
void SessionTest::stateTimelineIntervalsAndMissingValues()
{
    PlotSeriesInput input; input.id = 0; input.timeOffset = 10;
    input.time = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    input.values = {0, 0, 1, 1, qQNaN(), 2, 7, 7, 3};
    PlotSeriesStore store; store.replaceSeries({input});
    StateLane lane; lane.source = 0; lane.name = QStringLiteral("模式");
    lane.values = {{0, {QStringLiteral("关"), QColor("gray")}}, {1, {QStringLiteral("开"), QColor("green")}},
        {2, {QStringLiteral("复位"), QColor("blue")}}, {3, {QStringLiteral("故障"), QColor("red")}}};
    StateTimelineConfig config{true, {lane}}; std::atomic_bool cancel{false};
    const auto snapshot = store.snapshot({0});
    const auto data = StateTimeline::build(config, snapshot, cancel);
    QVERIFY(data); QCOMPARE(data->lanes.size(), 1);
    const auto &row = data->lanes[0]; QCOMPARE(row.runs.size(), 6);
    QCOMPARE(row.runs[0].start, 10.); QCOMPARE(row.runs[0].end, 12.); QVERIFY(row.runs[0].leftOpen);
    QCOMPARE(StateTimeline::at(row, 12.)->value, 1.);
    QVERIFY(!StateTimeline::at(row, 14.)->known);
    QCOMPARE(StateTimeline::text(lane, *StateTimeline::at(row, 16.)), QStringLiteral("未定义(7)"));
    QVERIFY(!StateTimeline::at(row, 9.)); QVERIFY(!StateTimeline::at(row, 19.));
    QVERIFY(row.runs.last().rightOpen); QCOMPARE(row.runs.last().start, 18.);
    StateTimelineItem item; item.setWidth(400); item.setHeight(80); item.setMinimum(10); item.setMaximum(18);
    item.setSources(config, snapshot); QTRY_VERIFY_WITH_TIMEOUT(!item.pending(), 5000);
    QCOMPARE(item.transition(0, 10, 1).toDouble(), 12.);
    QCOMPARE(item.transition(0, 12, 1).toDouble(), 14.);
    QCOMPARE(item.transition(0, 14, -1).toDouble(), 12.);
    QVERIFY(!item.transition(0, 12, -1).isValid());
    QVERIFY(item.inspect(0, 14).contains(QStringLiteral("未知")));
    // A single-sample pulse survives exact indexing even when subpixel on screen.
    input.time = {0, 1, 1.000001, 2}; input.values = {0, 1, 0, 0}; store.replaceSeries({input});
    const auto pulse = StateTimeline::build(config, store.snapshot({0}), cancel);
    QCOMPARE(pulse->lanes[0].runs.size(), 3); QCOMPARE(pulse->lanes[0].runs[1].end, 11.000001);
    input.time = {0,1,2,3,4}; input.values = {6,256,-1,1.5,0}; store.replaceSeries({input});
    const auto extracted = StateTimeline::build(config, store.snapshot({0}), cancel);
    QCOMPARE(extracted->lanes[0].runs[0].value, 6.); QVERIFY(extracted->lanes[0].runs[1].known);
    QVERIFY(!extracted->lanes[0].runs[3].known);
    input.time = {0,1,1}; input.values = {0,1,2}; store.replaceSeries({input});
    const auto duplicate = StateTimeline::build(config, store.snapshot({0}), cancel);
    QVERIFY(!duplicate->lanes[0].error.isEmpty()); QVERIFY(duplicate->lanes[0].runs.isEmpty());
    cancel = true; QVERIFY(!StateTimeline::build(config, store.snapshot({0}), cancel));
}


void SessionTest::stateTimelineViewChangesKeepFrame()
{
    PlotSeriesInput input; input.id=0; input.time={0,5,10}; input.values={0,1,1};
    PlotSeriesStore store; store.replaceSeries({input});
    StateLane lane; lane.source=0; lane.name="mode";
    lane.values={{0,{"off",QColor("gray")}},{1,{"on",QColor("green")}}};
    StateTimelineConfig config{true,{lane}};
    StateTimelineItem item; item.setWidth(400); item.setHeight(80); item.setMaximum(10);
    item.setSources(config,store.snapshot({0}));
    QCOMPARE(item.cursorValues(4),QStringList{"1"}); // Nearest original sample, not held interval/LOD.
    QCOMPARE(item.cursorValues(0),QStringList{"0"});
    QCOMPARE(item.cursorValues(qQNaN()),QStringList{QStringLiteral("—")});
    QCOMPARE(StateTimeline::decode(5).value(),5.);
    QVERIFY(!StateTimeline::decode(1.5).has_value());
    QCOMPARE(StateTimeline::decode(-1).value(),-1.);
    QTRY_VERIFY_WITH_TIMEOUT(!item.pending(),5000);
    QVERIFY(item.hasFrame()); QCOMPARE(item.labels().size(),2);
    item.setMinimum(1); item.setMaximum(11);
    QVERIFY(item.pending()); QVERIFY(item.hasFrame()); QCOMPARE(item.labels().size(),2);
    QVERIFY(qAbs(item.labels()[0].toMap()["width"].toDouble()-160)<.1);
    item.setWidth(600);
    QVERIFY(item.hasFrame()); QVERIFY(qAbs(item.labels()[0].toMap()["width"].toDouble()-240)<.1);
    // Repeated requests keep a visible frame, and eventually publish the latest view.
    for(int i=0;i<30;++i) {
        item.setMinimum(1+i*.01); item.setMaximum(11+i*.01);
        QVERIFY(item.hasFrame()); QVERIFY(!item.labels().isEmpty());
    }
    QTRY_VERIFY_WITH_TIMEOUT(!item.pending(),5000);
    QVERIFY(item.hasFrame());
    const double expected=(5-item.minimum())/(item.maximum()-item.minimum())*item.width();
    QVERIFY(qAbs(item.labels()[0].toMap()["width"].toDouble()-expected)<.1);
    // Source/config changes must discard the previous frame and any queued source job.
    config.lanes[0].name="replacement";
    item.setSources(config,store.snapshot({0}));
    QVERIFY(!item.hasFrame()); QVERIFY(item.labels().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!item.pending(),5000);
    QCOMPARE(item.rowNames(),QStringList{"replacement"}); QVERIFY(item.hasFrame());
}

void SessionTest::stateTimelineSubpixelPulseSurvivesAlignment()
{
    class InspectableTimeline : public StateTimelineItem {
    public:
        using StateTimelineItem::updatePaintNode;
    };
    PlotSeriesInput input; input.id=0;
    input.time={9590,9594.99,9595.01,9600}; input.values={192,0,192,192};
    PlotSeriesStore store; store.replaceSeries({input});
    StateLane lane; lane.source=0; lane.name="short pulse";
    InspectableTimeline item; item.setWidth(100); item.setHeight(40);
    item.setMinimum(9590); item.setMaximum(9600);
    item.setSources({true,{lane}},store.snapshot({0}));
    const auto colorWidth=[&](const QColor &color) {
        std::unique_ptr<QSGNode> node(item.updatePaintNode(nullptr,nullptr));
        auto *geometryNode=dynamic_cast<QSGGeometryNode *>(node.get());
        if(!geometryNode) return 0.;
        auto *geometry=geometryNode->geometry();
        const auto *vertices=geometry->vertexDataAsColoredPoint2D();
        double width=0;
        for(int i=0;i<geometry->vertexCount();i+=6) {
            const auto &v=vertices[i];
            if(v.r!=color.red() || v.g!=color.green() || v.b!=color.blue()) continue;
            float left=v.x,right=v.x;
            for(int j=1;j<6;++j) { left=qMin(left,vertices[i+j].x); right=qMax(right,vertices[i+j].x); }
            width+=right-left;
        }
        return width;
    };
    // Both values are undefined and share one color, as in the reported case.
    // Move the pulse through bucket interiors, boundaries and midpoint ties.
    for(double span : {10.,8.,5.}) for(double shift : {0.,.01,.025,.049,.05,.075,.099}) {
        item.setMinimum(9595-span/2+shift); item.setMaximum(9595+span/2+shift);
        QTRY_VERIFY_WITH_TIMEOUT(!item.pending(),5000);
        QVERIFY2(colorWidth(QColor("#684180"))+colorWidth(QColor("#d6a0e8"))>=.9,
                 qPrintable(QString("Lost pulse: span=%1 shift=%2").arg(span).arg(shift)));
    }
    // When resolved, use the real enum color rather than a compression marker.
    lane.values={{0,{"pulse",QColor("#00ff00")}},{192,{"baseline",QColor("#a66d22")}}};
    item.setSources({true,{lane}},store.snapshot({0}));
    item.setMinimum(9594.9); item.setMaximum(9595.1);
    QTRY_VERIFY_WITH_TIMEOUT(!item.pending(),5000);
    QVERIFY(colorWidth(QColor("#00ff00"))>=9.9);
    QCOMPARE(colorWidth(QColor("#684180"))+colorWidth(QColor("#d6a0e8")),0.);
    // A single step remains an ordinary color boundary at every alignment.
    input.time={9590,9595,9600}; input.values={192,0,0}; store.replaceSeries({input});
    item.setSources({true,{lane}},store.snapshot({0}));
    for(double shift : {0.,.025,.05,.075}) {
        item.setMinimum(9590+shift); item.setMaximum(9600+shift);
        QTRY_VERIFY_WITH_TIMEOUT(!item.pending(),5000);
        QCOMPARE(colorWidth(QColor("#684180"))+colorWidth(QColor("#d6a0e8")),0.);
        QVERIFY(colorWidth(QColor("#00ff00"))>40);
    }
    const auto capture=qEnvironmentVariable("DI_STATE_PULSE_CAPTURE_DIR");
    if(!capture.isEmpty()) {
        QQuickWindow window; window.resize(400,80); window.setColor(Qt::white);
        StateTimelineItem raster; raster.setParentItem(window.contentItem());
        raster.setSize(QSizeF(400,80));
        input.time={9590,9594.99,9595.01,9600}; input.values={192,0,192,192};
        store.replaceSeries({input}); lane.values.clear();
        raster.setSources({true,{lane}},store.snapshot({0}));
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.rendererInterface()->graphicsApi()!=QSGRendererInterface::Software);
        int index=0;
        for(double shift : {0.,.0025,.005,.0075}) {
            raster.setMinimum(9590+shift); raster.setMaximum(9600+shift);
            QTRY_VERIFY_WITH_TIMEOUT(!raster.pending(),5000);
            window.requestUpdate(); QTest::qWait(100);
            const auto image=window.grabWindow(); QVERIFY(!image.isNull());
            int marked=0;
            for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
                const auto pixel=image.pixelColor(x,y);
                if(pixel==QColor("#684180") || pixel==QColor("#d6a0e8")) ++marked;
            }
            QVERIFY(marked>=10);
            QVERIFY(image.save(QDir(capture).filePath(QString("pulse-%1.png").arg(index++))));
        }
    }
}

void SessionTest::stateTimelinePersistenceAndSourceRemoval()
{
    QTemporaryDir dir;
    const auto a = dir.filePath("a.csv"), b = dir.filePath("b.csv");
    writeFile(a, "time,A\n0,0\n1,1\n2,2\n"); writeFile(b, "time,B\n0,3\n1,0\n2,1\n");
    AppController controller; QCOMPARE(controller.loadFiles(QVariantList{a,b}), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    StateLane lane; lane.source = 1; lane.name = QStringLiteral("工作状态"); lane.values = {{0, {QStringLiteral("关"), QColor("gray")}}, {1, {QStringLiteral("开"), QColor("green")}}};
    const auto map = stateTimelineToJson({true, {lane}})["lanes"].toArray()[0].toObject().toVariantMap();
    QVERIFY(controller.setSignalEnums(1,signalEnumsToJson(lane.values).toVariantList()));
    QVERIFY(controller.configureStateLane(0, -1, map)); QVERIFY(controller.sessionModified());
    PlotItem plot; controller.attachPlot(&plot, 0); plot.setYRange(-2,2); // Caching view changes must retain state definitions.
    QCOMPARE(controller.stateTimelineState(0)["lanes"].toList().size(), 1);
    QVERIFY(controller.plotSignalEnabled(0,1)); controller.toggleSignal(1); QVERIFY(!controller.plotSignalEnabled(0,1));
    controller.toggleSignal(1); QVERIFY(controller.plotSignalEnabled(0,1));
    auto bad = map; bad["source"] = -1; QVERIFY(!controller.configureStateLane(0,0,bad));
    auto mappings = controller.signalEnums(1); mappings.append(mappings.first());
    QVERIFY(!controller.setSignalEnums(1,mappings));
    const auto path = dir.filePath("states.disession"), view = dir.filePath("states.diview");
    QVERIFY(controller.saveSession(path)); QVERIFY(controller.saveViewTemplate(view));
    SessionDocument doc; QString error; QVERIFY2(readSessionDocument(path, &doc, &error), qPrintable(error));
    QCOMPARE(doc.plots[0].states.lanes[0].source, 1); QCOMPARE(doc.series[1].enums[1].text, QStringLiteral("开"));
    auto json = sessionToJson(doc); auto plots = json["plots"].toArray(); auto p = plots[0].toObject(); p.remove("states"); plots[0] = p; json["plots"] = plots;
    QVERIFY(!sessionFromJson(json, &doc, &error));
    AppController restored; QVERIFY(restored.restoreSession(path)); QTRY_VERIFY_WITH_TIMEOUT(!restored.restoringSession(), 5000);
    QCOMPARE(restored.stateTimelineState(0)["lanes"].toList().size(), 1);
    QCOMPARE(restored.signalEnums(1),controller.signalEnums(1));
    // Template source identity is remapped, and undo restores the previous configuration.
    QVERIFY(controller.applyViewTemplate(view, QVariantMap{{"1",0}}));
    QCOMPARE(controller.stateTimelineState(0)["lanes"].toList()[0].toMap()["source"].toInt(), 0);
    QCOMPARE(controller.signalEnums(0),controller.signalEnums(1));
    QVERIFY(controller.undoViewTemplate());
    QVERIFY(controller.signalEnums(0).isEmpty());
    QCOMPARE(controller.stateTimelineState(0)["lanes"].toList()[0].toMap()["source"].toInt(), 1);
    QVERIFY(controller.removeFile("a.csv"));
    QCOMPARE(controller.stateTimelineState(0)["lanes"].toList()[0].toMap()["source"].toInt(), 0);
    QVERIFY(!controller.signalEnums(0).isEmpty());
    QVERIFY(controller.removeFile("b.csv")); QVERIFY(controller.stateTimelineState(0)["lanes"].toList().isEmpty());
    QVERIFY(controller.signalEnums(0).isEmpty());
    restored.clear(); QVERIFY(restored.stateTimelineState(0)["lanes"].toList().isEmpty());
}

void SessionTest::signalEnumsInlineEditorAndSharedViews()
{
    qmlRegisterTypesAndRevisions<PlotItemQmlRegistration, StateTimelineItemQmlRegistration, TrajectoryItemQmlRegistration,
        AppControllerQmlRegistration, SignalModelQmlRegistration>("DataInspector", 1);
    QTemporaryDir dir; const auto path=dir.filePath("enum.csv");
    writeFile(path,"time,Mode,Other\n0,0,3\n1,1,4\n2,0,3\n");
    AppController controller; QVERIFY(controller.loadCsv(path)); QTRY_VERIFY(!controller.loading());
    controller.setLayout(2,1); QVERIFY(controller.addStateSignal(0,0)); QVERIFY(controller.addStateSignal(1,0));
    const QVariantList values{QVariantMap{{"value",0},{"text","off"},{"color","#ff687887"}},QVariantMap{{"value",1},{"text","on"},{"color","#ff278451"}}};
    QVERIFY(controller.setSignalEnums(0,values));
    StateTimelineItem a,b; a.setWidth(400); b.setWidth(400); a.setHeight(80); b.setHeight(80); a.setMaximum(2); b.setMaximum(2);
    controller.attachStateTimeline(&a,0); controller.attachStateTimeline(&b,1);
    QTRY_VERIFY(!a.pending() && !b.pending()); QVERIFY(a.inspect(0,1).contains("on")); QVERIFY(b.inspect(0,1).contains("on"));
    auto invalid=values; invalid.append(values.first()); QVERIFY(!controller.setSignalEnums(0,invalid)); QCOMPARE(controller.signalEnums(0),values);
    QQmlEngine engine; QStringList warnings;
    connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError>& errors){for(const auto&e:errors) warnings.append(e.toString());});
    const QDir qmlDir(QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml"));
    QQmlComponent component(&engine,QUrl::fromLocalFile(qmlDir.filePath("SignalPropertiesPane.qml")));
    QQuickWindow host; host.resize(340,760);
    std::unique_ptr<QObject> pane(component.createWithInitialProperties({{"controller",QVariant::fromValue(&controller)},{"darkTheme",false},{"width",340},{"height",760}}));
    QVERIFY2(pane,qPrintable(component.errorString())); qobject_cast<QQuickItem*>(pane.get())->setParentItem(host.contentItem()); host.show();
    QVERIFY(QMetaObject::invokeMethod(pane.get(),"editSignal",Q_ARG(QVariant,0),Q_ARG(QVariant,-1)));
    auto *name=pane->findChild<QObject*>("signalPropertyName"); QVERIFY(name); name->setProperty("text","mode draft");
    QVERIFY(QMetaObject::invokeMethod(pane.get(),"close")); QCOMPARE(controller.signalName(0),QString("Mode"));
    QVERIFY(QMetaObject::invokeMethod(pane.get(),"editSignal",Q_ARG(QVariant,1),Q_ARG(QVariant,-1)));
    QVERIFY(QMetaObject::invokeMethod(pane.get(),"editSignal",Q_ARG(QVariant,0),Q_ARG(QVariant,-1)));
    QCOMPARE(name->property("text").toString(),QString("mode draft"));
    QQmlExpression change(QQmlEngine::contextForObject(pane.get()),pane.get(),"enumRows.setProperty(1, 'title', 'RUNNING')");
    change.evaluate(); QVERIFY2(!change.hasError(),qPrintable(change.error().toString()));
    QVERIFY(QMetaObject::invokeMethod(pane.get(),"apply")); QCOMPARE(controller.signalName(0),QString("mode draft"));
    QTRY_VERIFY(!a.pending() && !b.pending()); QVERIFY(a.inspect(0,1).contains("RUNNING")); QVERIFY(b.inspect(0,1).contains("RUNNING"));
    QVERIFY(QMetaObject::invokeMethod(pane.get(),"showStates",Q_ARG(QVariant,0))); QCOMPARE(pane->property("mode").toString(),QString("states"));
    const auto session=dir.filePath("enum.disession"); QVERIFY(controller.saveSession(session));
    AppController restored; QVERIFY(restored.restoreSession(session)); QTRY_VERIFY(!restored.restoringSession());
    QCOMPARE(restored.signalEnums(0),controller.signalEnums(0));
    const auto capture=qEnvironmentVariable("DI_ENUM_EDITOR_CAPTURE_PATH");
    if(!capture.isEmpty()) {
        QVERIFY(QMetaObject::invokeMethod(pane.get(),"editSignal",Q_ARG(QVariant,0),Q_ARG(QVariant,-1)));
        host.requestUpdate(); QTest::qWait(100); QVERIFY(host.grabWindow().save(capture));
    }
    QCOMPARE(warnings,QStringList{});
}

void SessionTest::stateTimelineQmlConfiguration()
{
    qmlRegisterTypesAndRevisions<PlotItemQmlRegistration, StateTimelineItemQmlRegistration, TrajectoryItemQmlRegistration,
        AppControllerQmlRegistration, SignalModelQmlRegistration>("DataInspector", 1);
    QTemporaryDir dir; const auto path = dir.filePath("states.csv");
    writeFile(path, "time,Mode\n0,0\n1,2\n2,1\n3,3\n4,0\n");
    AppController controller; QVERIFY(controller.loadCsv(path)); QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QQmlEngine engine; QStringList warnings;
    connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) { for (const auto &e : errors) warnings.append(e.toString()); });
    const QDir qmlDir(QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml"));
    QQmlComponent component(&engine, QUrl::fromLocalFile(qmlDir.filePath("QuickPlot.qml")));
    QQuickWindow stateWindow;
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"plotIndex",0}, {"controller", QVariant::fromValue(&controller)}, {"width",800}, {"height",300}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    if (auto *itemRoot = qobject_cast<QQuickItem *>(root.get())) itemRoot->setParentItem(stateWindow.contentItem());
    stateWindow.resize(800,300); stateWindow.show();
    QVERIFY(QMetaObject::invokeMethod(root.get(), "configureStateSource", Q_ARG(QVariant,0)));
    QVERIFY(controller.renameSignal(0,QStringLiteral("设备状态")));
    QVERIFY(controller.setSignalEnums(0,{{QVariantMap{{"value",0},{"text",QStringLiteral("关")},{"color","#687887"}}},
        {QVariantMap{{"value",1},{"text",QStringLiteral("开")},{"color","#278451"}}},
        {QVariantMap{{"value",2},{"text",QStringLiteral("复位")},{"color","#2563b0"}}},
        {QVariantMap{{"value",3},{"text",QStringLiteral("故障")},{"color","#bc3545"}}}}));
    QTRY_VERIFY(root->property("stateMode").toBool());
    auto *item = root->findChild<StateTimelineItem *>("stateTimelineItem"); QVERIFY(item);
    QTRY_VERIFY_WITH_TIMEOUT(!item->pending(), 5000);
    QCOMPARE(item->rowNames(), QStringList{QStringLiteral("设备状态")}); QVERIFY(!item->labels().isEmpty());
    QVERIFY(item->inspect(0,1.5).contains(QStringLiteral("复位")));
    auto *hoverPlot = root->findChild<PlotItem *>(); QVERIFY(hoverPlot);
    auto *numericFrame = root->findChild<QQuickItem *>("numericAxisFrame"); QVERIFY(numericFrame);
    QVERIFY(!numericFrame->isVisible());
    auto *stateXAxis = root->findChild<QQuickItem *>("stateXAxisLine"); QVERIFY(stateXAxis);
    QVERIFY(stateXAxis->isVisible());
    QTest::mouseMove(&stateWindow, hoverPlot->mapToScene(QPointF(hoverPlot->width()*.3,item->headerHeight()+16)).toPoint());
    QTRY_VERIFY(hoverPlot->property("stateInspectionVisible").toBool());
    QTest::mouseMove(&stateWindow, hoverPlot->mapToScene(QPointF(hoverPlot->width()*.3,100)).toPoint());
    QTRY_VERIFY(!hoverPlot->property("stateInspectionVisible").toBool());
    QVERIFY(hoverPlot->property("hoveredStateDetails").toString().isEmpty());
    controller.setCursorMode(1);
    auto *plot = root->findChild<PlotItem *>(); QVERIFY(plot); plot->setCursorPosition(0);
    auto *nextChange=root->findChild<QObject *>("nextStateChangeMenuItem"); QVERIFY(nextChange);
    auto *stateMenu=root->findChild<QObject *>("plotContextMenu"); QVERIFY(stateMenu);
    QVERIFY(QMetaObject::invokeMethod(stateMenu,"open"));
    QTRY_VERIFY(nextChange->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(nextChange, "triggered"));
    QCOMPARE(plot->cursorX1(), 1.);
    QVERIFY(QMetaObject::invokeMethod(stateMenu,"close"));
    QTRY_COMPARE(root->property("stateCursorValues").toStringList(),QStringList{"2"});
    plot->editCursorTime(2.,1);
    QTRY_COMPARE(root->property("stateCursorValues").toStringList(),QStringList{"1"});
    plot->setCursorMode(PlotItem::DoubleCursor);
    plot->editCursorTime(0.,2); plot->setActiveCursorIndex(2);
    QTRY_COMPARE(root->property("stateCursorValues").toStringList(),QStringList{"0"});
    plot->setActiveCursorIndex(1);
    QTRY_COMPARE(root->property("stateCursorValues").toStringList(),QStringList{"1"});
    plot->setCursorMode(PlotItem::SingleCursor); plot->editCursorTime(1.,1);

    const auto capture = qEnvironmentVariable("DI_STATE_GPU_CAPTURE_PATH");
    if (!capture.isEmpty()) {
        stateWindow.requestUpdate(); QTest::qWait(250);
        const auto image = stateWindow.grabWindow(); QVERIFY(!image.isNull());
        QVERIFY(image.save(capture));
        const auto position = item->mapToScene(QPointF(item->width()*.375, item->headerHeight()+5));
        const auto pixel = image.pixelColor(qRound(position.x()*image.devicePixelRatio()), qRound(position.y()*image.devicePixelRatio()));
        QCOMPARE(pixel, QColor("#2563b0"));
        const double initialMinimum=plot->xMinimum(), initialMaximum=plot->xMaximum();
        auto *statusLabel=root->findChild<QQuickItem *>("stateTimelineStatusMessage"); QVERIFY(statusLabel);
        for(int i=1;i<=6;++i) {
            plot->setXRange(initialMinimum+i*.05,initialMaximum+i*.05);
            QVERIFY(item->hasFrame()); QVERIFY(!statusLabel->isVisible());
            stateWindow.requestUpdate();
            const auto duringDrag=stateWindow.grabWindow(); QVERIFY(!duringDrag.isNull());
            const double px=(1.5-plot->xMinimum())/(plot->xMaximum()-plot->xMinimum())*item->width();
            const auto sample=item->mapToScene(QPointF(px,5));
            QCOMPARE(duringDrag.pixelColor(qRound(sample.x()*duringDrag.devicePixelRatio()),qRound(sample.y()*duringDrag.devicePixelRatio())),QColor("#2563b0"));
        }
        // Off-grid ordinary joins must contain only the two adjacent state colors.
        plot->setCursorMode(PlotItem::NoCursor);
        plot->setXRange(.013,4.013);
        QTRY_VERIFY_WITH_TIMEOUT(!item->pending(),5000);
        stateWindow.requestUpdate(); QTest::qWait(100);
        const auto joins=stateWindow.grabWindow(); QVERIFY(!joins.isNull());
        const QColor colors[]{QColor("#687887"),QColor("#2563b0"),QColor("#278451"),QColor("#bc3545")};
        for(int edge=1;edge<=3;++edge) {
            const double x=(edge-plot->xMinimum())/(plot->xMaximum()-plot->xMinimum())*item->width();
            const auto point=item->mapToScene(QPointF(x,item->headerHeight()+5));
            for(int dx=-2;dx<=2;++dx) {
                const auto color=joins.pixelColor(qRound(point.x()*joins.devicePixelRatio())+dx,
                                                  qRound(point.y()*joins.devicePixelRatio()));
                QVERIFY2(color==colors[edge-1] || color==colors[edge],qPrintable(color.name()));
            }
        }
        plot->setXRange(initialMinimum,initialMaximum);
        plot->setCursorMode(PlotItem::SingleCursor);
        QTRY_VERIFY_WITH_TIMEOUT(!item->pending(),5000);
    }
    // A replacement source/config must invalidate an in-flight frame.
    controller.toggleSignal(0); QTRY_VERIFY_WITH_TIMEOUT(!item->pending(),5000); QVERIFY(item->rowNames().isEmpty());
    controller.toggleSignal(0); QTRY_VERIFY_WITH_TIMEOUT(!item->pending(),5000); QCOMPARE(item->rowNames().size(),1);
    const auto baseLane = controller.stateTimelineState(0)["lanes"].toList()[0].toMap();
    for (int i=1; i<10; ++i) {
        auto lane = baseLane; lane["name"] = QStringLiteral("状态行 %1").arg(i+1);
        QVERIFY(controller.configureStateLane(0,-1,lane));
    }
    QTRY_VERIFY_WITH_TIMEOUT(!item->pending(),5000);
    auto *scroll = root->findChild<QObject *>("stateTimelineScrollBar"); QVERIFY(scroll);
    QVERIFY(scroll->property("visible").toBool());
    scroll->setProperty("position", 1-scroll->property("size").toDouble());
    QQuickItem *lastLabel = nullptr;
    const auto findLastLabel = [&] {
        QList<QQuickItem *> pending{qobject_cast<QQuickItem *>(root.get())};
        while (!pending.isEmpty()) {
            auto *child = pending.takeLast();
            if (child->objectName() == "stateTimelineRow9") return child;
            pending.append(child->childItems());
        }
        return static_cast<QQuickItem *>(nullptr);
    };
    QTRY_VERIFY((lastLabel = findLastLabel()));
    QTest::mouseClick(&stateWindow, Qt::LeftButton, Qt::NoModifier, lastLabel->mapToScene(QPointF(lastLabel->width()/2,16)).toPoint());
    QCOMPARE(root->property("selectedStateRow").toInt(),9);
    QCOMPARE(item->rowHeight(),32); QCOMPARE(item->headerHeight(),0);
    QQuickItem *legendEntry = nullptr;
    QList<QQuickItem *> pendingLegend{qobject_cast<QQuickItem *>(root.get())};
    while (!pendingLegend.isEmpty()) {
        auto *child = pendingLegend.takeLast();
        if (child->objectName() == "stateLegendEntry1") { legendEntry = child; break; }
        pendingLegend.append(child->childItems());
    }
    QVERIFY(legendEntry);
    QTest::mouseClick(&stateWindow, Qt::LeftButton, Qt::NoModifier, legendEntry->mapToScene(QPointF(10,10)).toPoint());
    QCOMPARE(root->property("selectedStateRow").toInt(),1);
    QVERIFY(scroll->property("position").toDouble()<.5);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

void SessionTest::mixedStateAndCurveLayout()
{
    qmlRegisterTypesAndRevisions<PlotItemQmlRegistration, StateTimelineItemQmlRegistration, TrajectoryItemQmlRegistration,
        AppControllerQmlRegistration, SignalModelQmlRegistration>("DataInspector", 1);
    const QDir qmlDir(QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml"));
    qmlRegisterType(QUrl::fromLocalFile(qmlDir.filePath("QuickPlot.qml")), "DataInspector", 1, 0, "QuickPlot");
    AppController controller;
    controller.setLayout(2,1);
    controller.setStateTimelineEnabled(0,true);
    QQmlEngine engine; QStringList warnings;
    connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors) warnings.append(error.toString());
    });
    QQmlComponent component(&engine, QUrl::fromLocalFile(qmlDir.filePath("Main.qml")));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"visible",true}, {"width",1100}, {"height",800}, {"appController",QVariant::fromValue(&controller)}}));
    QVERIFY2(root,qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get()); QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto plotOwner = [&](int index) -> QQuickItem * {
        for (auto *renderer : visualPlots(root.get())) {
            auto *owner = renderer->parentItem();
            while (owner && !owner->property("plotIndex").isValid()) owner = owner->parentItem();
            if (owner && owner->property("plotIndex").toInt() == index) return owner;
        }
        return nullptr;
    };
    QTRY_VERIFY(plotOwner(0) && plotOwner(1));
    auto *state = plotOwner(0), *curve = plotOwner(1);
    auto *closedProperties=root->findChild<QQuickItem *>("signalPropertiesPane"); QVERIFY(closedProperties);
    QVERIFY(!closedProperties->isVisible());
    controller.setStateTimelineEnabled(0,false);
    auto *switchAction=state->findChild<QObject *>("switchToStateTimeline"); QVERIFY(switchAction);
    QVERIFY(QMetaObject::invokeMethod(switchAction,"triggered"));
    QTRY_VERIFY(state->property("stateMode").toBool());
    QVERIFY(!closedProperties->isVisible());
    auto *viewMenu=state->findChild<QObject *>("switchViewMenu"); QVERIFY(viewMenu);
    QCOMPARE(viewMenu->property("title").toString(),QStringLiteral("切换视图"));
    QCOMPARE(viewMenu->property("count").toInt(),3);
    auto *curveAction=viewMenu->findChild<QObject *>("curveModeMenuItem"); QVERIFY(curveAction);
    QVERIFY(QMetaObject::invokeMethod(curveAction,"triggered"));
    QTRY_VERIFY(!state->property("stateMode").toBool());
    QVERIFY(QMetaObject::invokeMethod(switchAction,"triggered"));
    QTRY_VERIFY(state->property("stateMode").toBool());

    QTRY_VERIFY(qAbs(state->height()-curve->height())<=1);
    QCOMPARE(state->property("measuredAxisLeft").toDouble(),60.);
    QVERIFY(curve->property("axisLeft").toDouble() < 80);
    QCOMPARE(state->property("axisLeft"),curve->property("axisLeft"));
    QTemporaryDir dir; const auto path = dir.filePath("mixed.csv");
    writeFile(path,"time,Mode,Speed\n0,0,10\n1,2,20\n2,1,15\n3,3,30\n4,0,25\n");
    QVERIFY(controller.loadCsv(path)); QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(),5000);
    controller.setActivePlot(1); controller.selectSignal(1);
    StateLane lane; lane.source=0; lane.name=QStringLiteral("模式");
    lane.values={{0,{QStringLiteral("关"),QColor("gray")}},{1,{QStringLiteral("开"),QColor("green")}},
        {2,{QStringLiteral("复位"),QColor("blue")}},{3,{QStringLiteral("故障"),QColor("red")}}};
    auto mapping=stateTimelineToJson({true,{lane}})["lanes"].toArray()[0].toObject().toVariantMap();
    QVERIFY(controller.configureStateLane(0,-1,mapping));
    QTRY_VERIFY(qAbs(state->height()-curve->height())<=1);
    QTRY_COMPARE(state->property("axisLeft"),curve->property("axisLeft"));
    QVERIFY(curve->property("axisLeft").toDouble() < 80);
    const double oneRowHeight=state->height();
    const double shortNameMargin=curve->property("axisLeft").toDouble();
    mapping["name"]=QStringLiteral("这是一个用于验证长状态行名称和滚动布局的状态");
    for(int i=0;i<10;++i) QVERIFY(controller.configureStateLane(0,-1,mapping));
    QTRY_VERIFY(qAbs(state->height()-oneRowHeight)<=1);
    QTRY_VERIFY(qAbs(state->height()-curve->height())<=1);
    QTRY_COMPARE(state->property("axisLeft"),curve->property("axisLeft"));
    QCOMPARE(state->property("measuredAxisLeft").toDouble(),60.);
    QTRY_COMPARE(curve->property("axisLeft").toDouble(),shortNameMargin);
    auto *stateRenderer=state->findChild<PlotItem *>();
    auto *curveRenderer=curve->findChild<PlotItem *>();
    QVERIFY(stateRenderer && curveRenderer);
    QTRY_VERIFY(qAbs(stateRenderer->mapToScene(QPointF()).x()-curveRenderer->mapToScene(QPointF()).x())<.01);
    QTRY_VERIFY(qAbs(stateRenderer->width()-curveRenderer->width())<.01);
    controller.setCursorMode(2);
    const auto renderers=visualPlots(root.get()); QVERIFY(renderers.size()==2);
    renderers[0]->setXRange(1,3);
    QTRY_COMPARE(renderers[1]->xMinimum(),1.);
    renderers[0]->setCursorPosition(2);
    QTRY_COMPARE(renderers[1]->cursorX1(),2.);
    controller.setStateTimelineEnabled(0,false);
    QTRY_VERIFY(qAbs(state->height()-curve->height())<=1);
    controller.setStateTimelineEnabled(0,true);
    controller.setActivePlot(0); root->setProperty("subplotMaximized",true);
    QTRY_VERIFY(!curve->isVisible());
    QTRY_VERIFY(state->height()>oneRowHeight+100);
    root->setProperty("subplotMaximized",false);
    QTRY_VERIFY(curve->isVisible());
    QTRY_VERIFY(qAbs(state->height()-curve->height())<=1);
    controller.setLayout(1,2);
    QTRY_VERIFY(plotOwner(0) && plotOwner(1));
    QTRY_VERIFY(qAbs(plotOwner(0)->height()-plotOwner(1)->height())<=1);
    controller.setLayout(2,1);
    QTRY_VERIFY(plotOwner(0) && plotOwner(1));
    QTRY_VERIFY(plotOwner(0)->height()<=plotOwner(1)->height()+2);
    for(int i=10;i>0;--i) QVERIFY(controller.removeStateLane(0,i));
    QTRY_VERIFY(qAbs(plotOwner(0)->height()-plotOwner(1)->height())<=1);
    controller.setActivePlot(1);
    QVERIFY(controller.setSignalEnums(0,signalEnumsToJson(lane.values).toVariantList()));
    QVERIFY(QMetaObject::invokeMethod(root.get(),"editSignalPen",Q_ARG(QVariant,0)));
    auto *properties=root->findChild<QQuickItem *>("signalPropertiesPane"); QVERIFY(properties);
    QTRY_VERIFY(properties->isVisible()); QVERIFY(plotOwner(0)->isVisible()); QVERIFY(plotOwner(1)->isVisible());
    QVERIFY(!root->findChild<QObject *>("stateDisplayDialog"));
    auto *browser=root->findChild<QQuickItem *>("signalBrowser"); QVERIFY(browser);
    QVERIFY(browser->isVisible());
    QTRY_VERIFY(properties->mapToScene(QPointF()).x() >= plotOwner(0)->mapToScene(QPointF(plotOwner(0)->width(),0)).x()-1);
    QVERIFY(properties->width()>=260);
    const auto sidebarCapture=qEnvironmentVariable("DI_RIGHT_PROPERTIES_CAPTURE_PATH");
    if(!sidebarCapture.isEmpty()) {
        window->requestUpdate(); QTest::qWait(200); QVERIFY(window->grabWindow().save(sidebarCapture));
    }
    auto *stateConfiguration=plotOwner(0)->findChild<QObject *>("stateConfigurationMenuItem"); QVERIFY(stateConfiguration);
    auto *stateMenu=plotOwner(0)->findChild<QObject *>("plotContextMenu"); QVERIFY(stateMenu);
    const auto verifyMenuLayout=[](QObject *menu) {
        qreal bottom=0;
        for(int i=0;i<menu->property("count").toInt();++i) {
            QQuickItem *entry=nullptr;
            QVERIFY(QMetaObject::invokeMethod(menu,"itemAt",Q_RETURN_ARG(QQuickItem *,entry),Q_ARG(int,i)));
            QVERIFY(entry);
            if(!entry->isVisible()) { QCOMPARE(entry->height(),0.); continue; }
            QTRY_VERIFY(qAbs(entry->y()-bottom)<=menu->property("spacing").toDouble()+1);
            bottom=entry->y()+entry->height();
        }
        auto *content=qvariant_cast<QQuickItem *>(menu->property("contentItem")); QVERIFY(content);
        QTRY_VERIFY(qAbs(content->property("contentHeight").toDouble()-bottom)<=1);
    };
    QVERIFY(QMetaObject::invokeMethod(stateMenu,"open"));
    QTRY_VERIFY(stateConfiguration->property("visible").toBool());
    auto *fitY=plotOwner(0)->findChild<QObject *>("fitPlotYMenuItem"); QVERIFY(fitY);
    QVERIFY(!fitY->property("visible").toBool());
    verifyMenuLayout(stateMenu);
    const auto menuCapture=qEnvironmentVariable("DI_SUBPLOT_MENU_CAPTURE_DIR");
    if(!menuCapture.isEmpty()) {
        QTest::qWait(100); QVERIFY(window->grabWindow().save(QDir(menuCapture).filePath("state-menu.png")));
    }
    auto *curveFitY=plotOwner(1)->findChild<QObject *>("fitPlotYMenuItem"); QVERIFY(curveFitY);
    auto *curveMenu=plotOwner(1)->findChild<QObject *>("plotContextMenu"); QVERIFY(curveMenu);
    QVERIFY(QMetaObject::invokeMethod(curveMenu,"open"));
    QTRY_VERIFY(curveFitY->property("visible").toBool());
    verifyMenuLayout(curveMenu);
    if(!menuCapture.isEmpty()) {
        QTest::qWait(100); QVERIFY(window->grabWindow().save(QDir(menuCapture).filePath("curve-menu.png")));
    }
    QVERIFY(!plotOwner(1)->findChild<QObject *>("stateConfigurationMenuItem")->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(curveMenu,"close"));
    QVERIFY(QMetaObject::invokeMethod(stateMenu,"close"));
    QVERIFY(QMetaObject::invokeMethod(stateConfiguration,"triggered"));
    QCOMPARE(properties->property("mode").toString(),QString("states"));
    QVERIFY(QMetaObject::invokeMethod(properties,"chooseStateSource",Q_ARG(int,0)));
    QCOMPARE(browser->property("statePickTarget").toInt(),0);
    QVERIFY(QMetaObject::invokeMethod(browser,"addPickedState",Q_ARG(QVariant,1)));
    QCOMPARE(controller.stateTimelineState(0)["lanes"].toList().size(),2);
    QVERIFY(properties->isVisible());
    QVERIFY(controller.removeStateLane(0,1));
    QVERIFY(QMetaObject::invokeMethod(properties,"close"));
    const auto capture=qEnvironmentVariable("DI_MIXED_STATE_CAPTURE_PATH");
    if(!capture.isEmpty()) {
        for(auto *timeline : root->findChildren<StateTimelineItem *>()) QTRY_VERIFY_WITH_TIMEOUT(!timeline->pending(),5000);
        window->requestUpdate(); QTest::qWait(250);
        QVERIFY(window->grabWindow().save(capture));
    }
    controller.enterTrajectoryMode(1);
    auto *trajectoryPane=root->findChild<QQuickItem *>("trajectoryPropertiesPane"); QVERIFY(trajectoryPane);
    QVERIFY(!trajectoryPane->isVisible());
    QObject *configurationButton=nullptr;
    QTRY_VERIFY((configurationButton=plotOwner(1)->findChild<QObject *>("trajectoryConfigurationMenuItem")));
    QVERIFY(QMetaObject::invokeMethod(configurationButton,"triggered"));
    QTRY_VERIFY(trajectoryPane->isVisible()); QVERIFY(!properties->isVisible());
    QTRY_VERIFY(trajectoryPane->mapToScene(QPointF()).x() >= plotOwner(1)->mapToScene(QPointF(plotOwner(1)->width(),0)).x()-1);
    QVERIFY(trajectoryPane->width()>=260); QVERIFY(browser->isVisible());
    QVERIFY(!trajectoryPane->findChild<QObject *>("trajectoryAxisX"));
    QVERIFY(!trajectoryPane->findChild<QObject *>("trajectoryAxisY"));
    const auto plane=controller.addDataObject("aircraft"); QVERIFY(!plane.isEmpty());
    const auto fields=controller.dataObjects().last().toMap()["fields"].toList();
    for(int axis=0;axis<2;++axis) {
        const auto field=fields[axis].toMap();
        QVERIFY(!controller.bindObjectField(plane,field["id"].toString(),field["name"].toString(),field["role"].toString(),axis).isEmpty());
    }
    auto *objects=trajectoryPane->findChild<QObject *>("trajectoryObjectSelector"); QVERIFY(objects);
    objects->setProperty("currentIndex",0);
    QVERIFY(QMetaObject::invokeMethod(objects,"activated",Q_ARG(int,0)));
    QCOMPARE(controller.trajectoryState(1)["x"].toInt(),0);
    QCOMPARE(controller.trajectoryState(1)["y"].toInt(),1);
    QCOMPARE(controller.trajectoryState(1)["objectId"].toString(),plane);
    trajectoryPane->findChild<QObject *>("trajectoryName")->setProperty("text",QStringLiteral("对象航迹"));
    QVERIFY(QMetaObject::invokeMethod(trajectoryPane,"apply"));
    QCOMPARE(controller.trajectoryState(1)["objectId"].toString(),plane);
    QCOMPARE(controller.trajectoryState(1)["name"].toString(),QStringLiteral("对象航迹"));
    const auto trajectoryCapture=qEnvironmentVariable("DI_TRAJECTORY_SIDEBAR_CAPTURE_PATH");
    if(!trajectoryCapture.isEmpty()) { window->requestUpdate(); QTest::qWait(200); QVERIFY(window->grabWindow().save(trajectoryCapture)); }
    QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
}

QTEST_MAIN(SessionTest)
#include "session_test.moc"
