#include "appcontroller.h"
#include "plotitem.h"
#include "qmltypes.h"
#include "sessiondocument.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

class SessionTest final : public QObject
{
    Q_OBJECT
private slots:
    void roundTripWithDuplicateNamesAndLateDelegates();
    void restoredSessionsContinuePaletteSafely();
    void extremeImportedRangesRemainSaveable();
    void failuresPreserveCurrentSession();
    void relativePathsSurviveMovingTheBundle();
    void relocatedFilesAndModifiedState();
    void schemaValidationAndAtomicSave();
    void qmlRestoresToolbarAndPlotStates();
};

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
    auto object = valid; object["version"] = 99; invalid.append(object);
    object = valid; object["xRange"] = QJsonArray{2, 1}; invalid.append(object);
    object = valid; auto plots = object["plots"].toArray(); auto plot = plots[0].toObject();
    plot["signals"] = QJsonArray{99}; plots[0] = plot; object["plots"] = plots; invalid.append(object);
    object = valid; auto series = object["signals"].toArray(); series.append(series[0]); object["signals"] = series; invalid.append(object);
    for (const auto &json : invalid) {
        SessionDocument parsed;
        QVERIFY(!sessionFromJson(json, &parsed, &error)); QVERIFY(!error.isEmpty());
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
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
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
    QVERIFY(controller.renameSignal(0, "Unsaved rename"));
    QVERIFY(controller.sessionModified());
    auto *prompt = root->findChild<QObject *>("unsavedSessionDialog");
    QVERIFY(prompt);
    host->close();
    QTRY_VERIFY(prompt->property("visible").toBool());
    QVERIFY(host->isVisible());
    QVERIFY(QMetaObject::invokeMethod(prompt, "reject"));
    QCOMPARE(root->property("pendingUnsavedAction").toString(), QString());
    QVERIFY(host->isVisible());
    QVERIFY(controller.saveSession(path));
    QVERIFY(!controller.sessionModified());
    host->close();
    QTRY_VERIFY(!host->isVisible());
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
        QCOMPARE(restored.signalColor(count), palette.at(count % paletteSize));
        QCOMPARE(restored.signalColor(count + 1), palette.at((count + 1) % paletteSize));
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

QTEST_MAIN(SessionTest)
#include "session_test.moc"
