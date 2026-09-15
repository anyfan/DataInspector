#include "appcontroller.h"
#include "plotitem.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <algorithm>
#ifdef ENABLE_MAT
#include "matio.h"
#endif

class AppControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void cursorReadoutsFollowVisibleXRange();
    void unicodeMatFileNameLoads();
    void detachedPlotsDoNotSynchronize();
    void loadedSignalsBindOnlyToTheActivePlot();
    void expandingLayoutKeepsExistingPlotAttached();
    void multipleFilesAppendAndPreserveExistingBindings();
    void customLayoutSupportsLegacyEightByEightRange();
    void replacingAPlotDelegateRestoresItsCurves();
    void qmlUrlListImportsLocalFiles();
    void qmlJavaScriptArrayImportsLocalFiles();
    void legendNavigationAndRemoval();
    void quotedCsvFieldsAreImported();
    void failedLoadReportsReason();
    void legendDoesNotToggleSignalVisibility();
    void fitAndAxisZoomFollowLegacyRanges();
    void fittingUsesUnionOfSubplotTimeRanges();
    void asynchronousLodKeepsLatestRequest();
    void quickPlotLoadsWithLegendAndCursors();
    void realMatImportPerformanceWhenRequested();
    void addingSignalPreservesCurrentXRange();
    void attachingNewPlotPreservesSharedXRange();
    void removingFileKeepsRemainingSignalsAndBindings();
};

void AppControllerTest::cursorReadoutsFollowVisibleXRange()
{
    PlotItem plot;
    auto store = std::make_shared<PlotSeriesStore>();
    store->replaceSeries({{1, {0, 2, 8, 10}, {1, 2, 3, 4}, QColor("red")}});
    plot.setSeriesStore(store); plot.setVisibleSeries({1}); plot.setXRange(0, 10);
    plot.setCursorMode(PlotItem::DoubleCursor);
    plot.setCursorPosition(2, 1); plot.setCursorPosition(8, 2);
    QCOMPARE(plot.cursorReadouts().size(), 2);
    plot.setXRange(3, 7);
    QVERIFY(plot.cursorReadouts().isEmpty());
    plot.setXRange(0, 5);
    QCOMPARE(plot.cursorReadouts().size(), 1);
    plot.setXRange(0, 10);
    QCOMPARE(plot.cursorReadouts().size(), 2);
    QCOMPARE(plot.cursorX2(), 8.0);
}

void AppControllerTest::unicodeMatFileNameLoads()
{
#ifdef ENABLE_MAT
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("11040237.DAT - 副本.mat"));
    mat_t *file = Mat_CreateVer(path.toUtf8().constData(), nullptr, MAT_FT_MAT5);
    QVERIFY(file);
    size_t dimensions[] = {2, 2};
    double values[] = {0, 1, 12, 34};
    matvar_t *variable = Mat_VarCreate("p1", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dimensions, values, 0);
    QVERIFY(variable);
    const int written = Mat_VarWrite(file, variable, MAT_COMPRESSION_ZLIB);
    Mat_VarFree(variable);
    Mat_Close(file);
    QCOMPARE(written, 0);
    AppController controller;
    QVERIFY(controller.loadCsv(path));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QVERIFY2(controller.loadedFileCount() == 1, qPrintable(controller.status()));
    QCOMPARE(controller.signalCount(), 1);
#else
    QSKIP("MAT support disabled");
#endif
}

void AppControllerTest::detachedPlotsDoNotSynchronize()
{
    AppController controller;
    controller.setLayout(1, 2);
    PlotItem first;
    PlotItem retired;
    PlotItem replacement;
    controller.attachPlot(&first, 0);
    controller.attachPlot(&retired, 1);
    controller.attachPlot(&replacement, 1);
    first.setXRange(10.0, 20.0);
    retired.setXRange(30.0, 40.0);
    QCOMPARE(first.xMinimum(), 10.0);
    QCOMPARE(replacement.xMinimum(), 10.0);
    retired.setCursorMode(PlotItem::DoubleCursor);
    QCOMPARE(first.cursorMode(), int(PlotItem::NoCursor));

    // Delayed destruction must not detach the replacement.
    controller.detachPlot(&retired, 1);
    replacement.setXRange(50.0, 60.0);
    QCOMPARE(first.xMinimum(), 50.0);
    controller.detachPlot(&replacement, 1);
    replacement.setXRange(70.0, 80.0);
    QCOMPARE(first.xMinimum(), 50.0);

    controller.attachPlot(&replacement, 1);
    controller.setLayout(1, 1);
    replacement.setXRange(90.0, 100.0);
    QCOMPARE(first.xMinimum(), 50.0);
}

void AppControllerTest::loadedSignalsBindOnlyToTheActivePlot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("flight.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(file.write("time,Pitch,Roll\n0,1,2\n1,3,4\n"), qint64(28));
    file.close();

    AppController controller;
    controller.setLayout(1, 2);
    QSignalSpy layoutChanged(&controller, &AppController::layoutChanged);
    controller.setLayout(1, 2);
    QCOMPARE(layoutChanged.count(), 0);
    PlotItem firstPlot;
    PlotItem secondPlot;
    controller.attachPlot(&firstPlot, 0);
    controller.attachPlot(&secondPlot, 1);
    QSignalSpy loaded(&controller, &AppController::currentFileChanged);

    QVERIFY(controller.loadCsv(path));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000);
    QCOMPARE(controller.signalName(0), QStringLiteral("Pitch"));
    QCOMPARE(controller.signalName(1), QStringLiteral("Roll"));
    controller.setSignalPen(0, QColor("green"), 5.0, Qt::DotLine);
    QCOMPARE(controller.signalColor(0), QColor("green"));
    QCOMPARE(controller.signalWidth(0), 5.0);
    QCOMPARE(controller.signalStyle(0), int(Qt::DotLine));
    QCOMPARE(controller.signalWidth(1), 2.0);
    QVERIFY(controller.plotSignalRows(0).isEmpty());
    QVERIFY(controller.plotSignalRows(1).isEmpty());

    controller.setActivePlot(0);
    controller.toggleSignal(0);
    QVariantList firstPlotRows;
    firstPlotRows.append(0);
    QCOMPARE(controller.plotSignalRows(0), firstPlotRows);
    QVERIFY(controller.plotSignalRows(1).isEmpty());

    controller.setActivePlot(1);
    controller.toggleSignal(1);
    QCOMPARE(controller.plotSignalRows(0), firstPlotRows);
    QVariantList secondPlotRows;
    secondPlotRows.append(1);
    QCOMPARE(controller.plotSignalRows(1), secondPlotRows);
    QVERIFY(controller.signalModel()->data(controller.signalModel()->index(2),
                                            SignalModel::CheckedRole).toBool());
    QVERIFY(!controller.signalModel()->data(controller.signalModel()->index(1),
                                             SignalModel::CheckedRole).toBool());

    const int revisionBeforeClear = controller.plotStateRevision();
    QSignalSpy bindingsChanged(&controller, &AppController::plotBindingsChanged);
    controller.clear();
    QVERIFY(controller.plotSignalRows(0).isEmpty());
    QVERIFY(controller.plotSignalRows(1).isEmpty());
    QVERIFY(controller.plotStateRevision() > revisionBeforeClear);
    QCOMPARE(bindingsChanged.count(), 1);
}

void AppControllerTest::expandingLayoutKeepsExistingPlotAttached()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("flight.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(file.write("time,Pitch,Roll\n0,1,2\n1,3,4\n"), qint64(28));
    file.close();

    AppController controller;
    PlotItem firstPlot;
    controller.attachPlot(&firstPlot, 0);
    QSignalSpy loaded(&controller, &AppController::currentFileChanged);
    QVERIFY(controller.loadCsv(path));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000);

    controller.toggleSignal(0);
    QCOMPARE(firstPlot.visibleSeriesIds(), QVector<PlotSeriesId>({0}));

    // A numeric QML Repeater can retain delegate 0 and only create delegate 1.
    controller.setLayout(1, 2);
    PlotItem secondPlot;
    controller.attachPlot(&secondPlot, 1);
    controller.toggleSignal(1);

    QCOMPARE(firstPlot.visibleSeriesIds(), QVector<PlotSeriesId>({0, 1}));
    QVERIFY(secondPlot.visibleSeriesIds().isEmpty());
}

void AppControllerTest::multipleFilesAppendAndPreserveExistingBindings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.filePath(QStringLiteral("first.csv"));
    const QString secondPath = directory.filePath(QStringLiteral("second.csv"));
    const QVector<QPair<QString, QByteArray>> contents = {
        {firstPath, QByteArray("time,Pitch\n0,1\n1,2\n")},
        {secondPath, QByteArray("time,Roll\n0,3\n1,4\n")}
    };
    for (const auto &entry : contents) {
        QFile file(entry.first);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QCOMPARE(file.write(entry.second), qint64(entry.second.size()));
    }

    AppController controller;
    PlotItem plot;
    controller.attachPlot(&plot, 0);
    QSignalSpy loaded(&controller, &AppController::currentFileChanged);
    QVector<int> progressValues;
    bool completedWhileLoading = false;
    connect(&controller, &AppController::loadingProgressChanged,
            &controller, [&]() {
                progressValues.append(controller.loadingProgress());
                if (controller.loading() && controller.loadingProgress() == 100)
                    completedWhileLoading = true;
            });
    QVariantList files{QUrl::fromLocalFile(firstPath), QUrl::fromLocalFile(secondPath)};

    QCOMPARE(controller.loadFiles(files), 2);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 5000);
    QCOMPARE(controller.loadedFileCount(), 2);
    QCOMPARE(controller.signalCount(), 2);
    QCOMPARE(controller.signalName(0), QStringLiteral("Pitch"));
    QCOMPARE(controller.signalName(1), QStringLiteral("Roll"));

    controller.toggleSignal(0);
    QCOMPARE(plot.visibleSeriesIds(), QVector<PlotSeriesId>({0}));
    controller.toggleSignal(1);
    QCOMPARE(plot.visibleSeriesIds(), QVector<PlotSeriesId>({0, 1}));
    QCOMPARE(controller.loadingProgress(), 100);
    QVERIFY(!controller.loading());
    QVERIFY(!progressValues.isEmpty());
    QVERIFY(std::is_sorted(progressValues.cbegin(), progressValues.cend()));
    QVERIFY(!completedWhileLoading);
}

void AppControllerTest::customLayoutSupportsLegacyEightByEightRange()
{
    AppController controller;
    controller.setLayout(8, 8);
    QCOMPARE(controller.plotRows(), 8);
    QCOMPARE(controller.plotColumns(), 8);

    controller.setLayout(9, 0);
    QCOMPARE(controller.plotRows(), 8);
    QCOMPARE(controller.plotColumns(), 1);
}

void AppControllerTest::replacingAPlotDelegateRestoresItsCurves()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("flight.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(file.write("time,Pitch\n10,1\n20,2\n"), qint64(21));
    file.close();

    AppController controller;
    auto *oldPlot = new PlotItem;
    controller.attachPlot(oldPlot, 0);
    QSignalSpy loaded(&controller, &AppController::currentFileChanged);
    QVERIFY(controller.loadCsv(path));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000);
    controller.toggleSignal(0);

    controller.setLayout(2, 2);
    PlotItem replacement;
    controller.attachPlot(&replacement, 0);
    controller.detachPlot(oldPlot, 0);
    delete oldPlot;

    QCOMPARE(replacement.visibleSeriesIds(), QVector<PlotSeriesId>({0}));
}

void AppControllerTest::qmlUrlListImportsLocalFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("typed-url-list.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(file.write("time,Pitch\n0,1\n"), qint64(15));
    file.close();

    AppController controller;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("testController"),
                                              &controller);
    QQmlComponent component(&engine);
    const QByteArray source = QByteArrayLiteral(
        "import QtQml\n"
        "QtObject {\n"
        "  property list<url> files: [\"")
        + QUrl::fromLocalFile(path).toString().toUtf8()
        + QByteArrayLiteral("\"]\n"
                            "  function submit() {\n"
                            "    return testController.loadFiles(files)\n"
                            "  }\n"
                            "}\n");
    component.setData(source, QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));

    QVariant accepted;
    QVERIFY(QMetaObject::invokeMethod(object.get(), "submit",
                                      Q_RETURN_ARG(QVariant, accepted)));
    QCOMPARE(accepted.toInt(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(controller.loadedFileCount(), 1, 5000);
}

void AppControllerTest::legendNavigationAndRemoval()
{
    AppController controller;
    controller.signalModel()->setNames({"A", "B"}, QStringList{"file/table", "file/table"});
    controller.signalModel()->setPlotChecked(0, 1, true);
    QSignalSpy revealed(&controller, &AppController::revealSignalRequested);
    controller.revealLegendSignal(0, 1);
    QCOMPARE(revealed.count(), 1);
    QCOMPARE(revealed.first().first().toInt(), 1);
    QVERIFY(controller.plotSignalEnabled(0, 1));
    controller.signalModel()->toggleGroup("file");
    controller.signalModel()->setFilter("missing");
    const int row = controller.signalModel()->revealSignal(1);
    QVERIFY(row >= 0);
    QCOMPARE(controller.signalModel()->data(controller.signalModel()->index(row), SignalModel::NameRole).toString(), "B");
    controller.removeLegendSignal(0, 1);
    QVERIFY(!controller.plotSignalEnabled(0, 1));
    QCOMPARE(controller.signalCount(), 2);
}

void AppControllerTest::fittingUsesUnionOfSubplotTimeRanges()
{
    QTemporaryDir directory;
    QFile firstFile(directory.filePath("first.csv")), secondFile(directory.filePath("second.csv"));
    QVERIFY(firstFile.open(QIODevice::WriteOnly));
    firstFile.write("time,A\n0,nan\n10,1\n"); firstFile.close();
    QVERIFY(secondFile.open(QIODevice::WriteOnly));
    secondFile.write("time,B\n100,2\n200,nan\n"); secondFile.close();
    AppController controller;
    controller.setLayout(1, 2);
    PlotItem first, second;
    controller.attachPlot(&first, 0); controller.attachPlot(&second, 1);
    controller.loadFiles(QVariantList{firstFile.fileName(), secondFile.fileName()});
    QTRY_VERIFY(!controller.loading());
    controller.setActivePlot(0); controller.toggleSignal(0);
    controller.setActivePlot(1); controller.toggleSignal(1);
    controller.fitPlots(true, true, true);
    QCOMPARE(first.xMinimum(), -4.0); QCOMPARE(first.xMaximum(), 204.0);
    QCOMPARE(second.xMinimum(), -4.0); QCOMPARE(second.xMaximum(), 204.0);
    QCOMPARE(first.yMinimum(), .95); QCOMPARE(first.yMaximum(), 1.05);
    QCOMPARE(second.yMinimum(), 1.95); QCOMPARE(second.yMaximum(), 2.05);
}

void AppControllerTest::fitAndAxisZoomFollowLegacyRanges()
{
    auto store = std::make_shared<PlotSeriesStore>();
    store->replaceSeries({{1, {0, 1, 2, 3}, {1000, 10, 20, -1000}, QColor("red")}});
    PlotItem plot;
    plot.setSeriesStore(store);
    plot.setVisibleSeries({1});
    plot.setXRange(1, 2);
    plot.fitY();
    QCOMPARE(plot.yMinimum(), 9.5);
    QCOMPARE(plot.yMaximum(), 20.5);
    plot.zoomAxis(0, .5, 1);
    QCOMPARE(plot.yMinimum(), 9.5);
    QVERIFY(plot.xMinimum() > 1);
    const double xmin = plot.xMinimum();
    plot.zoomAxis(1, .5, 1);
    QCOMPARE(plot.xMinimum(), xmin);
    QVERIFY(plot.yMinimum() > 9.5);
}

void AppControllerTest::quickPlotLoadsWithLegendAndCursors()
{
    qmlRegisterType<PlotItem>("DataInspector", 1, 0, "PlotItem");
    QTemporaryDir directory;
    QFile file(directory.filePath("ui.csv"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("time,A,B\n0,1,1\n1,2,2\n");
    file.close();
    AppController controller;
    QVERIFY(controller.loadCsv(file.fileName()));
    QTRY_VERIFY(!controller.loading());
    controller.toggleSignal(0);
    controller.toggleSignal(1);
    QQmlEngine engine;
    QStringList warnings;
    connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors) warnings.append(error.toString());
    });
    const QString path = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml/QuickPlot.qml");
    QQmlComponent component(&engine, QUrl::fromLocalFile(path));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({
        {"plotIndex", 0}, {"controller", QVariant::fromValue<QObject *>(&controller)},
        {"width", 800}, {"height", 400}, {"graphCursorMode", 2}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *plot = object->findChild<PlotItem *>();
    QVERIFY(plot);
    QTRY_VERIFY(!plot->lodPending());
    plot->setCursorPosition(.5);
    QCoreApplication::processEvents();
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    QCOMPARE(plot->cursorReadouts().size(), 4);
    QCOMPARE(plot->cursorReadouts().first().toMap().value("sampleX").toDouble(), 0.0);
    // QML Repeater delegates are visual children; their QObject parent is not guaranteed.
    QList<QQuickItem *> visualMarkers, visualTimes;
    std::function<void(QQuickItem *)> collect = [&](QQuickItem *parent) {
        for (auto *child : parent->childItems()) {
            if (child->objectName() == "cursorSampleMarker") visualMarkers.append(child);
            if (child->objectName() == "cursorTimeLabel") visualTimes.append(child);
            collect(child);
        }
    };
    collect(qobject_cast<QQuickItem *>(object.get()));
    QCOMPARE(visualMarkers.size(), 4);
    QCOMPARE(visualTimes.size(), 2);
    const double plotBottom = plot->parentItem()->y() + plot->height();
    for (const auto *label : visualTimes) QVERIFY(label->y() > plotBottom);
    QQuickWindow window;
    window.resize(800, 400);
    auto *item = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(item);
    item->setParentItem(window.contentItem());
    window.show();
    QTest::qWait(50);
    QSignalSpy revealed(&controller, &AppController::revealSignalRequested);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(40, 12));
    QTRY_COMPARE(revealed.count(), 1);
    QCOMPARE(plot->highlightedSeries(), 0);
    QVERIFY(controller.plotSignalEnabled(0, 0));
    QTest::mouseClick(&window, Qt::RightButton, Qt::NoModifier, QPoint(40, 12));
    QTest::qWait(30);
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    QVariant formatted;
    QVERIFY(QMetaObject::invokeMethod(object.get(), "formatCompact", Q_RETURN_ARG(QVariant, formatted),
                                     Q_ARG(QVariant, 1.25), Q_ARG(QVariant, 10)));
    QCOMPARE(formatted.toString(), "1.25");
    QVERIFY(QMetaObject::invokeMethod(object.get(), "formatCompact", Q_RETURN_ARG(QVariant, formatted),
                                     Q_ARG(QVariant, 100.0), Q_ARG(QVariant, 7)));
    QCOMPARE(formatted.toString(), "100");
    plot->setXRange(.6, .8);
    QCoreApplication::processEvents();
    for (const auto *label : visualTimes) QVERIFY(!label->isVisible());
    item->setParentItem(nullptr);
}

void AppControllerTest::asynchronousLodKeepsLatestRequest()
{
    PlotSeriesStore store;
    QVector<double> time(200000), values(200000);
    for (int i = 0; i < time.size(); ++i) { time[i] = i; values[i] = i % 100; }
    store.replaceSeries({{1, time, values, QColor("red")}});
    PlotLodScheduler scheduler;
    QSignalSpy ready(&scheduler, &PlotLodScheduler::ready);
    const auto snapshot = store.snapshot({1});
    for (int i = 0; i < 100; ++i)
        scheduler.request(snapshot, {snapshot.generation, {1}, double(i), double(i + 1000), 800, 1});
    QTRY_VERIFY_WITH_TIMEOUT(!scheduler.pending(), 10000);
    QVERIFY(scheduler.result());
    QCOMPARE(scheduler.result()->key.xMinimum, 99.0);
    QCOMPARE(ready.count(), 1);
    store.clear();
    const auto empty = store.snapshot({});
    scheduler.request(empty, {empty.generation, {}, 0, 1, 800, 1});
    QVERIFY(!scheduler.result());
    QTRY_VERIFY_WITH_TIMEOUT(!scheduler.pending(), 10000);
    QVERIFY(scheduler.result()->segments.isEmpty());
    // Destruction must not wait for a worker or leave a callback to a dead QObject.
    { PlotLodScheduler temporary; temporary.request(snapshot, {snapshot.generation, {1}, 0, 200000, 800, 1}); }
}

void AppControllerTest::qmlJavaScriptArrayImportsLocalFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("dropped.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(file.write("time,Pitch\n0,1\n"), qint64(15));
    file.close();

    AppController controller;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("testController"),
                                              &controller);
    QQmlComponent component(&engine);
    const QByteArray source = QByteArrayLiteral(
        "import QtQml\n"
        "QtObject {\n"
        "  function submit() { return testController.loadFiles([\"")
        + QUrl::fromLocalFile(path).toString().toUtf8()
        + QByteArrayLiteral("\"]) }\n"
                            "}\n");
    component.setData(source, QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));

    QVariant accepted;
    QVERIFY(QMetaObject::invokeMethod(object.get(), "submit",
                                      Q_RETURN_ARG(QVariant, accepted)));
    QCOMPARE(accepted.toInt(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(controller.loadedFileCount(), 1, 5000);
}

void AppControllerTest::quotedCsvFieldsAreImported()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("quoted.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray contents =
        "\"time\",\"Pitch,deg\"\n\"0\",\"1.5\"\n\"1\",\"2.5\"\n";
    QCOMPARE(file.write(contents), qint64(contents.size()));
    file.close();

    AppController controller;
    PlotItem plot;
    controller.attachPlot(&plot, 0);
    QSignalSpy loaded(&controller, &AppController::currentFileChanged);

    QVERIFY(controller.loadCsv(path));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000);
    QCOMPARE(controller.signalCount(), 1);
    QCOMPARE(controller.signalName(0), QStringLiteral("Pitch,deg"));
}

void AppControllerTest::failedLoadReportsReason()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("invalid.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray contents = "time,Pitch\nnot-a-number,1\n";
    QCOMPARE(file.write(contents), qint64(contents.size()));
    file.close();

    AppController controller;
    QVERIFY(controller.loadCsv(path));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QVERIFY(controller.status().contains(QStringLiteral("没有读取到有效数据")));
}

void AppControllerTest::legendDoesNotToggleSignalVisibility()
{
    const QString sourcePath = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
                                   .filePath(QStringLiteral("../qml/QuickPlot.qml"));
    QFile source(sourcePath);
    QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(source.errorString()));
    const QByteArray qml = source.readAll();
    QVERIFY(!qml.contains("togglePlotSignal"));
}

void AppControllerTest::realMatImportPerformanceWhenRequested()
{
    const QString path = QString::fromLocal8Bit(qgetenv("DATAINSPECTOR_PERF_MAT"));
    if (path.isEmpty()) QSKIP("Set DATAINSPECTOR_PERF_MAT to run the large MAT integration test");
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));

    AppController controller;
    QElapsedTimer timer;
    qint64 reached99Milliseconds = -1;
    connect(&controller, &AppController::loadingProgressChanged, &controller,
            [&]() {
                if (reached99Milliseconds < 0
                    && controller.loadingProgress() >= 99)
                    reached99Milliseconds = timer.elapsed();
            });

    timer.start();
    QVERIFY(controller.loadCsv(path));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 180000);
    const qint64 totalMilliseconds = timer.elapsed();
    const qint64 finishMilliseconds = reached99Milliseconds < 0
        ? -1 : totalMilliseconds - reached99Milliseconds;
    qInfo() << "MAT_IMPORT_MS" << totalMilliseconds
            << "MAT_FINISH_AFTER_99_MS" << finishMilliseconds
            << "SIGNALS" << controller.signalCount();
    QVERIFY2(controller.loadedFileCount() == 1, qPrintable(controller.status()));
    QVERIFY(controller.signalCount() > 0);
}

void AppControllerTest::addingSignalPreservesCurrentXRange()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("wide.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(file.write("time,Pitch\n0,1\n10,2\n"), qint64(20));
    file.close();

    AppController controller;
    PlotItem plot;
    controller.attachPlot(&plot, 0);
    QSignalSpy loaded(&controller, &AppController::currentFileChanged);
    QVERIFY(controller.loadCsv(path));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000);

    plot.setXRange(0.0, 1.0);
    controller.toggleSignal(0);

    QCOMPARE(plot.xMinimum(), 0.0);
    QCOMPARE(plot.xMaximum(), 1.0);
}

void AppControllerTest::attachingNewPlotPreservesSharedXRange()
{
    AppController controller;
    PlotItem firstPlot;
    controller.attachPlot(&firstPlot, 0);
    firstPlot.setXRange(12.0, 14.0);

    controller.setLayout(1, 2);
    PlotItem secondPlot;
    controller.attachPlot(&secondPlot, 1);

    QCOMPARE(firstPlot.xMinimum(), 12.0);
    QCOMPARE(firstPlot.xMaximum(), 14.0);
    QCOMPARE(secondPlot.xMinimum(), 12.0);
    QCOMPARE(secondPlot.xMaximum(), 14.0);
}

void AppControllerTest::removingFileKeepsRemainingSignalsAndBindings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.filePath(QStringLiteral("first.csv"));
    const QString secondPath = directory.filePath(QStringLiteral("second.csv"));
    const QVector<QPair<QString, QByteArray>> contents = {
        {firstPath, QByteArray("time,Pitch\n0,1\n1,2\n")},
        {secondPath, QByteArray("time,Roll\n0,3\n1,4\n")}
    };
    for (const auto &entry : contents) {
        QFile file(entry.first);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QCOMPARE(file.write(entry.second), qint64(entry.second.size()));
    }

    AppController controller;
    PlotItem plot;
    controller.attachPlot(&plot, 0);
    QSignalSpy loaded(&controller, &AppController::currentFileChanged);
    QCOMPARE(controller.loadFiles(QVariantList{firstPath, secondPath}), 2);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 5000);
    controller.toggleSignal(0);
    controller.toggleSignal(1);

    QVERIFY(controller.removeFile(QStringLiteral("first.csv")));

    QCOMPARE(controller.loadedFileCount(), 1);
    QCOMPARE(controller.signalCount(), 1);
    QCOMPARE(controller.signalName(0), QStringLiteral("Roll"));
    QVariantList remainingRows;
    remainingRows.append(0);
    QCOMPARE(controller.plotSignalRows(0), remainingRows);
    QCOMPARE(plot.visibleSeriesIds(), QVector<PlotSeriesId>({0}));
}

QTEST_MAIN(AppControllerTest)
#include "appcontroller_test.moc"
