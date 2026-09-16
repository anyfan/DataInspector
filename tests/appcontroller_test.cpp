#include "appcontroller.h"
#include "plotitem.h"
#include "xlsxreader.h"

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
#include <QtCore/private/qzipwriter_p.h>
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
    void xlsxWorkbookImportsAllWorksheets();
    void xlsxParserHandlesSparseAndCachedCells();
    void invalidSignalValuesDoNotDiscardRows();
    void failedLoadReportsReason();
    void legendDoesNotToggleSignalVisibility();
    void fitAndAxisZoomFollowLegacyRanges();
    void axisZoomKeepsCurrentMidpoint();
    void axisSelectionAppliesRange();
    void fittingUsesUnionOfSubplotTimeRanges();
    void asynchronousLodKeepsLatestRequest();
    void quickPlotLoadsWithLegendAndCursors();
    void realMatImportPerformanceWhenRequested();
    void realCsvImportPerformanceWhenRequested();
    void addingSignalPreservesCurrentXRange();
    void attachingNewPlotPreservesSharedXRange();
    void removingFileKeepsRemainingSignalsAndBindings();
    void exportsAllLoadedSignals();
    void exportsUnionOfSignalsDrawnAcrossPlots();
    void exportCompressionOptionReachesWriter();
};

static void writeCsvFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(contents), qint64(contents.size()));
}

void AppControllerTest::exportsAllLoadedSignals()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("flight.csv"));
    const QString output = directory.filePath(QStringLiteral("all.xlsx"));
    writeCsvFile(input, "time,Pitch,Roll\n0,1,2\n1,3,4\n");

    AppController controller;
    QVERIFY(controller.loadCsv(input));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QVERIFY(controller.exportXlsx(output, AppController::AllLoadedData));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);
    QCOMPARE(controller.exportProgress(), 100);

    const XlsxReadResult read = readXlsxWorkbook(output, {}, {});
    QVERIFY2(read.error.isEmpty(), qPrintable(read.error));
    QCOMPARE(read.tables.size(), 1);
    QCOMPARE(read.tables.first().signalNames,
             QStringList({QStringLiteral("Pitch"), QStringLiteral("Roll")}));
}

void AppControllerTest::exportsUnionOfSignalsDrawnAcrossPlots()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath(QStringLiteral("first.csv"));
    const QString second = directory.filePath(QStringLiteral("second.csv"));
    const QString output = directory.filePath(QStringLiteral("plotted.xlsx"));
    writeCsvFile(first, "time,A,B\n0,1,2\n1,3,4\n");
    writeCsvFile(second, "time,C\n10,5\n11,6\n");

    AppController controller;
    controller.setLayout(1, 2);
    QCOMPARE(controller.loadFiles(QVariantList{first, second}), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    controller.setActivePlot(0);
    controller.toggleSignal(1);
    controller.setActivePlot(1);
    controller.toggleSignal(1);
    controller.toggleSignal(2);

    QVERIFY(controller.exportXlsx(output, AppController::PlottedSignals));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);
    const XlsxReadResult read = readXlsxWorkbook(output, {}, {});
    QVERIFY2(read.error.isEmpty(), qPrintable(read.error));
    QCOMPARE(read.tables.size(), 2);
    QCOMPARE(read.tables.at(0).signalNames, QStringList({QStringLiteral("B")}));
    QCOMPARE(read.tables.at(1).signalNames, QStringList({QStringLiteral("C")}));
}

void AppControllerTest::exportCompressionOptionReachesWriter()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("repeated.csv"));
    const QString storedOutput = directory.filePath(QStringLiteral("stored.xlsx"));
    const QString compressedOutput = directory.filePath(
        QStringLiteral("compressed.xlsx"));
    QByteArray contents("time,A,B\n");
    for (int row = 0; row < 4096; ++row)
        contents += QByteArray::number(row) + ",1.25,1.25\n";
    writeCsvFile(input, contents);

    AppController controller;
    QVERIFY(controller.loadCsv(input));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QVERIFY(controller.exportXlsx(storedOutput, AppController::AllLoadedData));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);
    QVERIFY(controller.exportXlsx(compressedOutput,
                                  AppController::AllLoadedData, true));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);

    const XlsxReadResult stored = readXlsxWorkbook(storedOutput, {}, {});
    const XlsxReadResult compressed = readXlsxWorkbook(compressedOutput, {}, {});
    QVERIFY2(stored.error.isEmpty(), qPrintable(stored.error));
    QVERIFY2(compressed.error.isEmpty(), qPrintable(compressed.error));
    QCOMPARE(stored.tables.first().values, compressed.tables.first().values);
    QVERIFY(QFileInfo(storedOutput).size()
            > QFileInfo(compressedOutput).size() * 2);
}

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

void AppControllerTest::axisZoomKeepsCurrentMidpoint()
{
    PlotItem plot;
    plot.setXRange(2.0, 10.0);

    const double initialXMidpoint = (plot.xMinimum() + plot.xMaximum()) / 2.0;
    plot.zoomAxis(0, 0.1, 1.0);
    QVERIFY(qAbs((plot.xMinimum() + plot.xMaximum()) / 2.0 - initialXMidpoint) < 1e-12);

    plot.fitY();
    const double initialYMidpoint = (plot.yMinimum() + plot.yMaximum()) / 2.0;
    plot.zoomAxis(1, 0.9, -1.0);
    QVERIFY(qAbs((plot.yMinimum() + plot.yMaximum()) / 2.0 - initialYMidpoint) < 1e-12);
}

void AppControllerTest::axisSelectionAppliesRange()
{
    PlotItem plot;
    plot.setXRange(2.0, 10.0);
    plot.setYRange(-3.0, 7.0);

    QCOMPARE(plot.xMinimum(), 2.0);
    QCOMPARE(plot.xMaximum(), 10.0);
    QCOMPARE(plot.yMinimum(), -3.0);
    QCOMPARE(plot.yMaximum(), 7.0);
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
    auto *axisCursor = item->findChild<QQuickItem *>("axisZoomCursor");
    QVERIFY(axisCursor);
    plot->setXRange(0.0, 10.0);
    plot->setYRange(-20.0, 20.0);
    const QPointF plotOrigin = plot->mapToItem(item, QPointF(0, 0));
    const QPoint xStart = item->mapToItem(window.contentItem(),
                                          QPointF(plotOrigin.x() + plot->width() * .2,
                                                  plotOrigin.y() + plot->height() + 10)).toPoint();
    const QPoint xEnd = item->mapToItem(window.contentItem(),
                                                  QPointF(plotOrigin.x() + plot->width() * .8,
                                                  plotOrigin.y() + plot->height() + 10)).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, xStart);
    QVERIFY(axisCursor->property("visible").toBool());
    QCOMPARE(axisCursor->property("source").toUrl(), QUrl("qrc:/icons/zoom-x.svg"));
    QTest::mouseMove(&window, xEnd, 30);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, xEnd);
    QVERIFY(qAbs(plot->xMinimum() - 2.0) < .01);
    QVERIFY(qAbs(plot->xMaximum() - 8.0) < .01);

    const QPoint yStart = item->mapToItem(window.contentItem(),
                                          QPointF(10, plotOrigin.y() + plot->height() * .25)).toPoint();
    const QPoint yEnd = item->mapToItem(window.contentItem(),
                                        QPointF(10, plotOrigin.y() + plot->height() * .75)).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, yStart);
    QVERIFY(axisCursor->property("visible").toBool());
    QCOMPARE(axisCursor->property("source").toUrl(), QUrl("qrc:/icons/zoom-y.svg"));
    QTest::mouseMove(&window, yEnd, 30);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, yEnd);
    QVERIFY(qAbs(plot->yMinimum() + 10.0) < .1);
    QVERIFY(qAbs(plot->yMaximum() - 10.0) < .1);

    QSignalSpy revealed(&controller, &AppController::revealSignalRequested);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(40, 12));
    QTRY_COMPARE(revealed.count(), 1);
    QCOMPARE(plot->highlightedSeries(), 0);
    QVERIFY(controller.plotSignalEnabled(0, 0));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(400, 200));
    QCOMPARE(plot->highlightedSeries(), -1);
    QCOMPARE(revealed.count(), 1);
    QVERIFY(controller.plotSignalEnabled(0, 0));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(40, 12));
    QCOMPARE(plot->highlightedSeries(), 0);
    QCOMPARE(revealed.count(), 2);
    // Clicking outside the plot item must also clear the selection.
    window.resize(900, 500);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(850, 450));
    QCOMPARE(plot->highlightedSeries(), -1);
    QVERIFY(controller.plotSignalEnabled(0, 0));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(40, 12));
    QCOMPARE(plot->highlightedSeries(), 0);
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
    controller.setLayout(1, 2);
    std::unique_ptr<QObject> target(component.createWithInitialProperties({
        {"plotIndex", 1}, {"controller", QVariant::fromValue<QObject *>(&controller)},
        {"x", 800}, {"width", 800}, {"height", 400}}));
    QVERIFY2(target, qPrintable(component.errorString()));
    auto *targetItem = qobject_cast<QQuickItem *>(target.get());
    targetItem->setParentItem(window.contentItem());
    window.resize(1600, 400);
    QTest::qWait(30);
    QCOMPARE(item->property("axisLeft"), targetItem->property("axisLeft"));
    QCOMPARE(item->property("axisTop"), targetItem->property("axisTop"));
    QVERIFY(QMetaObject::invokeMethod(object.get(), "formatYTick", Q_RETURN_ARG(QVariant, formatted),
                                     Q_ARG(QVariant, 12000.0)));
    QCOMPARE(formatted.toString(), "1.20e+4");
    plot->setXRange(0.0, 10.0);
    plot->setYRange(-20.0, 20.0);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "applyAxisSelection",
                                      Q_ARG(QVariant, 0), Q_ARG(QVariant, .2), Q_ARG(QVariant, .8)));
    QCOMPARE(plot->xMinimum(), 2.0);
    QCOMPARE(plot->xMaximum(), 8.0);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "applyAxisSelection",
                                      Q_ARG(QVariant, 1), Q_ARG(QVariant, .25), Q_ARG(QVariant, .75)));
    QCOMPARE(plot->yMinimum(), -10.0);
    QCOMPARE(plot->yMaximum(), 10.0);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(40, 12));
    QTest::mouseMove(&window, QPoint(1000, 100), 30);
    QTRY_VERIFY(targetItem->property("dropHighlighted").toBool());
    QVERIFY(item->property("legendDragging").toBool());
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(1000, 100));
    QTRY_VERIFY(controller.plotSignalEnabled(1, 0));
    QVERIFY(!item->property("legendDragging").toBool());
    QVERIFY(!controller.plotSignalEnabled(0, 0));
    QVERIFY(controller.plotSignalEnabled(0, 1));
    QCOMPARE(controller.activePlotIndex(), 1);
    QVERIFY(!targetItem->property("dropHighlighted").toBool());
    // Dropping on itself or outside the layout must not remove the signal.
    controller.moveLegendSignal(1, 1, 0);
    controller.moveLegendSignal(1, 99, 0);
    QVERIFY(controller.plotSignalEnabled(1, 0));
    // Moving to a plot that already contains the signal must not duplicate it.
    controller.signalModel()->setPlotChecked(0, 0, true);
    controller.moveLegendSignal(1, 0, 0);
    QVERIFY(!controller.plotSignalEnabled(1, 0));
    QCOMPARE(controller.plotSignalRows(0).count(0), 1);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    targetItem->setParentItem(nullptr);
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

void AppControllerTest::xlsxWorkbookImportsAllWorksheets()
{
    const QString path = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
                             .filePath(QStringLiteral(
                                 "../test_file/2026-09-05_06-43-26.dat.xlsx"));
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));

    AppController controller;
    QVERIFY(controller.loadCsv(path));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 10000);
    QVERIFY2(controller.loadedFileCount() == 1, qPrintable(controller.status()));
    QCOMPARE(controller.signalCount(), 80);
    QVERIFY2(controller.status().contains(QStringLiteral("12105 行")),
             qPrintable(controller.status()));

    QStringList worksheetGroups;
    SignalModel *model = controller.signalModel();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row);
        if (model->data(index, SignalModel::GroupNodeRole).toBool()
            && model->data(index, SignalModel::DepthRole).toInt() == 1) {
            worksheetGroups.append(
                model->data(index, SignalModel::NameRole).toString());
        }
    }
    QCOMPARE(worksheetGroups, QStringList({QStringLiteral("1"),
                                           QStringLiteral("2"),
                                           QStringLiteral("17")}));
}

void AppControllerTest::xlsxParserHandlesSparseAndCachedCells()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("minimal.xlsx"));
    QZipWriter archive(path);
    QVERIFY(archive.isWritable());
    archive.addFile(QStringLiteral("[Content_Types].xml"), QByteArrayLiteral(
        "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>"
        "</Types>"));
    archive.addFile(QStringLiteral("_rels/.rels"), QByteArrayLiteral(
        "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>"));
    archive.addFile(QStringLiteral("xl/workbook.xml"), QByteArrayLiteral(
        "<?xml version=\"1.0\"?><workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"Data\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>"));
    archive.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"), QByteArrayLiteral(
        "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/>"
        "</Relationships>"));
    archive.addFile(QStringLiteral("xl/sharedStrings.xml"), QByteArrayLiteral(
        "<?xml version=\"1.0\"?><sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<si><t>Time</t></si><si><r><t>Ya</t></r><r><t>w</t></r></si></sst>"));
    archive.addFile(QStringLiteral("xl/worksheets/sheet1.xml"), QByteArrayLiteral(
        "<?xml version=\"1.0\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>"
        "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"inlineStr\"><is><t>Pitch</t></is></c><c r=\"D1\" t=\"s\"><v>1</v></c></row>"
        "<row r=\"2\"><c r=\"A2\"><v>0</v></c><c r=\"B2\"><v>1.5</v></c><c r=\"D2\"><f>1+2</f><v>3</v></c></row>"
        "<row r=\"3\"><c r=\"A3\"><v>1</v></c><c r=\"D3\"><v>4</v></c></row>"
        "</sheetData></worksheet>"));
    archive.close();
    QCOMPARE(archive.status(), QZipWriter::NoError);

    const XlsxReadResult result = readXlsxWorkbook(path, {}, {});
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.tables.size(), 1);
    const LoadedTable &table = result.tables.first();
    QCOMPARE(table.name, QStringLiteral("Data"));
    QCOMPARE(table.signalNames,
             QStringList({QStringLiteral("Pitch"), QStringLiteral("Signal 2"),
                          QStringLiteral("Yaw")}));
    QCOMPARE(table.time, QVector<double>({0.0, 1.0}));
    QCOMPARE(table.values.at(0).at(0), 1.5);
    QVERIFY(qIsNaN(table.values.at(0).at(1)));
    QVERIFY(qIsNaN(table.values.at(1).at(0)));
    QVERIFY(qIsNaN(table.values.at(1).at(1)));
    QCOMPARE(table.values.at(2), QVector<double>({3.0, 4.0}));
    QCOMPARE(result.skippedRows, 0);
}

void AppControllerTest::invalidSignalValuesDoNotDiscardRows()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("gaps.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray contents =
        "time,Pitch,Roll\r\n0,1,2\r\n1,,4\r\n2,5,not-a-number\r\n";
    QCOMPARE(file.write(contents), qint64(contents.size()));
    file.close();

    AppController controller;
    QVERIFY(controller.loadCsv(path));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QCOMPARE(controller.signalCount(), 2);
    QVERIFY2(controller.status().contains(QStringLiteral("3 行")),
             qPrintable(controller.status()));
    QVERIFY2(!controller.status().contains(QStringLiteral("跳过")),
             qPrintable(controller.status()));
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

void AppControllerTest::realCsvImportPerformanceWhenRequested()
{
    const QString path = QString::fromLocal8Bit(qgetenv("DATAINSPECTOR_PERF_CSV"));
    if (path.isEmpty())
        QSKIP("Set DATAINSPECTOR_PERF_CSV to run the large CSV integration test");
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
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 60000);
    const qint64 totalMilliseconds = timer.elapsed();
    const qint64 finishMilliseconds = reached99Milliseconds < 0
        ? -1 : totalMilliseconds - reached99Milliseconds;
    qInfo() << "CSV_IMPORT_MS" << totalMilliseconds
            << "CSV_FINISH_AFTER_99_MS" << finishMilliseconds
            << "SIGNALS" << controller.signalCount();
    QCOMPARE(controller.loadedFileCount(), 1);
    QCOMPARE(controller.signalCount(), 12);
    QVERIFY2(controller.status().contains(QStringLiteral("1280000 行")),
             qPrintable(controller.status()));
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
