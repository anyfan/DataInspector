#include "appcontroller.h"
#include "plotitem.h"
#include "qmltypes.h"
#include "xlsxreader.h"
#include "xlsxwriter.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QWheelEvent>
#include <QTemporaryDir>
#include <QUrl>
#include <QtCore/private/qzipwriter_p.h>
#include <QtTest>

#include <algorithm>
#include <functional>
#include <limits>
#ifdef ENABLE_MAT
#include "matio.h"
#endif

class AppControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void axisTicksHandleLargeAndTinyRanges();
    void unchangedPensDoNotInvalidatePlots();
    void cursorReadoutsFollowVisibleXRange();
    void cursorsInitializeFromSelectedPlot();
    void viewUndoRestoresSharedAndLocalRanges();
    void curveClicksSelectWithoutPanning();
    void normalizeYKeepsRawCursorValues();
    void cursorKeyboardStepsAcrossRawSamples();
    void draggingCursorPastTheOtherSwapsDrag();
    void unicodeMatFileNameLoads();
    void sameNamedSourcesRemainIndependent();
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
    void axisZoomAnchorsOnPointerPosition();
    void axisSelectionAppliesRange();
    void fittingUsesUnionOfSubplotTimeRanges();
    void asynchronousLodKeepsLatestRequest();
    void quickPlotLoadsWithLegendAndCursors();
    void cursorBadgesInitializeCenteredWithDelta();
    void cursorBadgeDragsSelectAndMoveOneOrBoth();
    void interactingWithSubplotSelectsIt();
    void toolbarModesToggleAndRememberSelection();
    void signalSectionsKeepNestedPathsAndAlignedNames();
    void signalTreeDragAddsToTargetPlot();
    void editDialogsRememberAndResetValues();
    void realMatImportPerformanceWhenRequested();
    void realCsvImportPerformanceWhenRequested();
    void addingSignalPreservesCurrentXRange();
    void attachingNewPlotPreservesSharedXRange();
    void removingFileKeepsRemainingSignalsAndBindings();
    void timeOffsetsApplyToSignalsGroupsAndFiles();
    void timeOffsetSnapshotsRemainImmutable();
    void exportsAllLoadedSignals();
    void exportsUnionOfSignalsDrawnAcrossPlots();
    void exportCompressionOptionReachesWriter();
    void renamingSignalUpdatesModelLegendsAndExport();
    void matExportMirrorsImportLayout();
    void matExportKeepsSourceTableNumbers();
};

static void writeCsvFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(contents), qint64(contents.size()));
}

void AppControllerTest::sameNamedSourcesRemainIndependent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath("left")); QVERIFY(root.mkpath("right"));
    const QString first = root.filePath("left/same.csv");
    const QString second = root.filePath("right/same.csv");
    writeCsvFile(first, "time,A\n0,1\n1,2\n");
    writeCsvFile(second, "time,B\n100,3\n200,4\n");
    AppController controller;
    QCOMPARE(controller.loadFiles(QVariantList{first, second}), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QCOMPARE(controller.loadedFileCount(), 2);
    QCOMPARE(controller.signalCount(), 2);
    const QString firstGroup = controller.signalModel()->groupAt(0);
    const QString secondGroup = controller.signalModel()->groupAt(1);
    QVERIFY(firstGroup != secondGroup);
    QVERIFY(controller.setTimeOffset(2, -1, secondGroup, 7));
    QCOMPARE(controller.signalTimeOffset(0), 0.0);
    QCOMPARE(controller.signalTimeOffset(1), 7.0);
    const QString xlsx = root.filePath("separate.xlsx");
    QVERIFY(controller.exportXlsx(xlsx, AppController::AllLoadedData));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);
    const auto read = readXlsxWorkbook(xlsx, {}, {});
    QVERIFY2(read.error.isEmpty(), qPrintable(read.error));
    QCOMPARE(read.tables.size(), 2);
    QCOMPARE(read.tables[0].time, QVector<double>({0, 1}));
    QCOMPARE(read.tables[1].time, QVector<double>({107, 207}));
    QCOMPARE(read.tables[0].signalNames, QStringList({"A"}));
    QCOMPARE(read.tables[1].signalNames, QStringList({"B"}));
#ifdef ENABLE_MAT
    const QString mat = root.filePath("separate.mat");
    QVERIFY(controller.exportMat(mat, AppController::AllLoadedData));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);
    mat_t *file = Mat_Open(mat.toUtf8().constData(), MAT_ACC_RDONLY);
    QVERIFY(file);
    matvar_t *p1 = Mat_VarRead(file, "p1"), *p2 = Mat_VarRead(file, "p2");
    QVERIFY(p1); QVERIFY(p2);
    QCOMPARE(p1->dims[1], size_t(2)); QCOMPARE(p2->dims[1], size_t(2));
    QCOMPARE(static_cast<double *>(p1->data)[0], 0.0);
    QCOMPARE(static_cast<double *>(p2->data)[0], 107.0);
    Mat_VarFree(p1); Mat_VarFree(p2); Mat_Close(file);
#endif
    QVERIFY(controller.removeFile(firstGroup));
    QCOMPARE(controller.loadedFileCount(), 1);
    QCOMPARE(controller.signalCount(), 1);
    QCOMPARE(controller.signalName(0), QStringLiteral("B"));
    QCOMPARE(controller.loadFiles(QVariantList{second}), 0);
    QVERIFY(controller.loadCsv(first));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QCOMPARE(controller.loadedFileCount(), 2);
    QCOMPARE(controller.signalCount(), 2);
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

void AppControllerTest::viewUndoRestoresSharedAndLocalRanges()
{
    AppController controller;
    controller.setLayout(1, 2);
    PlotItem first, second;
    controller.attachPlot(&first, 0); controller.attachPlot(&second, 1);
    QVERIFY(!controller.canUndoView());
    const double initialY = first.yMinimum(), initialMaxY = first.yMaximum();
    first.setXRange(2, 4);
    QCOMPARE(second.xMinimum(), 2.0);
    controller.undoView();
    QCOMPARE(first.xMinimum(), 0.0); QCOMPARE(second.xMaximum(), 1.0);
    QVERIFY(!controller.canUndoView());
    controller.beginViewChange();
    first.setXRange(10, 20);
    first.setYRange(-10, 10);
    first.setXRange(12, 22);
    second.setYRange(100, 200);
    controller.endViewChange();
    controller.fitPlots(true, true, true);
    controller.undoView();
    QCOMPARE(first.xMinimum(), 12.0); QCOMPARE(second.xMaximum(), 22.0);
    QCOMPARE(first.yMinimum(), -10.0); QCOMPARE(second.yMaximum(), 200.0);
    controller.undoView();
    QCOMPARE(first.xMinimum(), 0.0); QCOMPARE(second.xMaximum(), 1.0);
    QCOMPARE(first.yMinimum(), initialY); QCOMPARE(second.yMaximum(), initialMaxY);
    QVERIFY(!controller.canUndoView());
    first.setNormalizeY(true);
    controller.undoView();
    QVERIFY(!first.normalizeY());
    QCOMPARE(first.yMinimum(), initialY);
    QVERIFY(!controller.canUndoView());
    first.setXRange(0, 1);
    QVERIFY(!controller.canUndoView());
}

void AppControllerTest::curveClicksSelectWithoutPanning()
{
    AppController controller;
    QQuickWindow window;
    window.resize(400, 240);
    PlotItem plot;
    plot.setParentItem(window.contentItem());
    plot.setWidth(400); plot.setHeight(240);
    controller.attachPlot(&plot);
    auto store = std::make_shared<PlotSeriesStore>();
    store->replaceSeries({{1, {0, 10}, {0, 10}, QColor("red")},
                          {2, {0, 10}, {8, 8}, QColor("blue")},
                          {3, {0, 4, 5, 6, 10}, {2, 2, qQNaN(), 2, 2}, QColor("green")}});
    plot.setSeriesStore(store); plot.setVisibleSeries({1, 2, 3});
    plot.setXRange(0, 10); plot.setYRange(0, 10);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(!plot.lodPending());
    QSignalSpy selected(&plot, &PlotItem::seriesClicked);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(200, 120));
    QCOMPARE(plot.highlightedSeries(), 1); QCOMPARE(selected.count(), 1);
    QCOMPARE(plot.xMinimum(), 0.0); QCOMPARE(plot.yMaximum(), 10.0);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(200, 48));
    QCOMPARE(plot.highlightedSeries(), 2); QCOMPARE(selected.count(), 2);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(200, 192));
    QCOMPARE(selected.count(), 2); // Do not bridge a NaN gap.
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(200, 120));
    QTest::mouseMove(&window, QPoint(220, 140));
    QTest::mouseMove(&window, QPoint(240, 160));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(240, 160));
    QCOMPARE(selected.count(), 2);
    QVERIFY(plot.xMinimum() != 0.0);
    controller.undoView();
    QCOMPARE(plot.xMinimum(), 0.0); QCOMPARE(plot.yMinimum(), 0.0);
    plot.setNormalizeY(true);
    plot.setYRange(0, 1);
    plot.selectSeriesAt(200, 120);
    QVERIFY(plot.highlightedSeries() >= 0);
}

void AppControllerTest::cursorsInitializeFromSelectedPlot()
{
    AppController controller;
    controller.setLayout(1, 2);
    PlotItem first, second;
    controller.attachPlot(&first, 0);
    controller.attachPlot(&second, 1);
    auto store = std::make_shared<PlotSeriesStore>();
    store->replaceSeries({{1, {0, 2, 8, 10}, {1, 2, 3, 4}, QColor("red")},
                          {2, {0, 3, 7, 10}, {1, 2, 3, 4}, QColor("blue")}});
    first.setSeriesStore(store); first.setVisibleSeries({1});
    second.setSeriesStore(store); second.setVisibleSeries({2});
    first.setXRange(0, 10);
    controller.setActivePlot(1);
    controller.setCursorMode(PlotItem::DoubleCursor);
    QCOMPARE(second.cursorX1(), 3.0); QCOMPARE(second.cursorX2(), 7.0);
    QCOMPARE(first.cursorX1(), 3.0); QCOMPARE(first.cursorX2(), 7.0);
    controller.setCursorMode(PlotItem::NoCursor);
    controller.setActivePlot(0);
    first.setXRange(0, 4);
    controller.setCursorMode(PlotItem::DoubleCursor);
    QCOMPARE(first.cursorX2(), 2.0);
    QCOMPARE(second.cursorX2(), first.cursorX2());

    controller.setCursorMode(PlotItem::SingleCursor);
    first.setCursorPosition(2);
    first.setXRange(6, 10);
    controller.setCursorMode(PlotItem::DoubleCursor);
    QCOMPARE(first.cursorX1(), 2.0);
    QCOMPARE(first.cursorX2(), 8.0);
    QCOMPARE(second.cursorX2(), 8.0);
    QCOMPARE(first.activeCursorIndex(), 2);
    // A view between sparse raw samples still gets a visible second cursor.
    controller.setCursorMode(PlotItem::SingleCursor);
    first.setXRange(4, 6);
    controller.setCursorMode(PlotItem::DoubleCursor);
    QVERIFY(first.cursorX2() >= 4 && first.cursorX2() <= 6);
    QCOMPARE(second.cursorX2(), first.cursorX2());
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

void AppControllerTest::normalizeYKeepsRawCursorValues()
{
    PlotItem plot;
    auto store = std::make_shared<PlotSeriesStore>();
    store->replaceSeries({{1, {0, 5, 10}, {10, 20, 30}, QColor("red")},
                          {2, {0, 5, 10}, {7, 7, 7}, QColor("blue")}});
    plot.setSeriesStore(store); plot.setVisibleSeries({1, 2}); plot.setXRange(0, 10);
    plot.setCursorMode(PlotItem::SingleCursor);
    plot.setCursorPosition(5);
    QVERIFY(!plot.normalizeY());
    plot.setNormalizeY(true);
    QVERIFY(plot.normalizeY());
    const QVariantList readouts = plot.cursorReadouts();
    QCOMPARE(readouts.size(), 2);
    QCOMPARE(readouts.at(0).toMap().value("y").toDouble(), 20.0);
    QCOMPARE(readouts.at(0).toMap().value("displayY").toDouble(), 0.5);
    QCOMPARE(readouts.at(1).toMap().value("y").toDouble(), 7.0);
    QCOMPARE(readouts.at(1).toMap().value("displayY").toDouble(), 0.5);
    QVERIFY(plot.yMinimum() < 0.0 && plot.yMaximum() > 1.0 && plot.yMaximum() < 2.0);
    plot.setNormalizeY(false);
    QCOMPARE(plot.cursorReadouts().at(0).toMap().value("displayY").toDouble(), 20.0);
}

void AppControllerTest::draggingCursorPastTheOtherSwapsDrag()
{
    QQuickWindow window;
    window.resize(400, 200);
    PlotItem plot;
    plot.setParentItem(window.contentItem());
    plot.setSize(QSizeF(400, 200));
    auto store = std::make_shared<PlotSeriesStore>();
    QVector<double> xs, ys;
    for (int i = 0; i <= 100; ++i) { xs.append(i); ys.append(i); }
    store->replaceSeries({{1, xs, ys, QColor("red")}});
    plot.setSeriesStore(store); plot.setVisibleSeries({1}); plot.setXRange(0, 100);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    plot.setCursorMode(PlotItem::DoubleCursor);
    QCOMPARE(plot.cursorX1(), 25.0);
    QCOMPARE(plot.cursorX2(), 75.0);
    // Drag cursor 1 (100 px) rightwards past cursor 2 (300 px): the drag hands over.
    plot.setCursorMode(PlotItem::NoCursor);
    plot.setXRange(0.4, 100);
    plot.setCursorMode(PlotItem::DoubleCursor);
    QCOMPARE(plot.cursorX1(), 25.0); // 25.3 snapped to raw sample
    QCOMPARE(plot.cursorX2(), 75.0); // 75.1 snapped to raw sample
    plot.setCursorMode(PlotItem::NoCursor);
    plot.setXRange(0, 100);
    plot.setCursorMode(PlotItem::DoubleCursor);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseMove(&window, QPoint(200, 100), 20);
    QCOMPARE(plot.cursorX1(), 50.0);
    QCOMPARE(plot.cursorX2(), 75.0);
    QTest::mouseMove(&window, QPoint(360, 100), 20);
    QCOMPARE(plot.cursorX1(), 75.0);
    QCOMPARE(plot.cursorX2(), 90.0);
    QTest::mouseMove(&window, QPoint(320, 100), 20);
    QCOMPARE(plot.cursorX1(), 75.0);
    QCOMPARE(plot.cursorX2(), 80.0);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(320, 100));
}

void AppControllerTest::cursorKeyboardStepsAcrossRawSamples()
{
    PlotItem plot;
    auto store = std::make_shared<PlotSeriesStore>();
    store->replaceSeries({
        {1, {0, 2, 5, 9, 10}, {10, 20, 50, 90, 100}, QColor("red")},
        {2, {0, 3, 4, 5, 12}, {1, 2, 3, 4, 5}, QColor("blue")},
    });
    plot.setSeriesStore(store);
    plot.setVisibleSeries({1, 2});
    plot.setXRange(0, 12);
    plot.setCursorMode(PlotItem::DoubleCursor);

    plot.setCursorX(3.4, 1);
    QCOMPARE(plot.cursorX1(), 3.0);
    plot.setCursorX(5.4, 2);
    QCOMPARE(plot.cursorX2(), 5.0);

    plot.stepCursor(1, 1);
    QCOMPARE(plot.cursorX1(), 4.0);
    plot.stepCursor(1, 2);
    QCOMPARE(plot.cursorX2(), 9.0);
    plot.stepCursor(-1, 2);
    QCOMPARE(plot.cursorX2(), 5.0);
    plot.stepCursor(0, 1);
    QCOMPARE(plot.cursorX1(), 4.0);

    plot.setActiveCursorIndex(2);
    plot.stepCursor(1);
    QCOMPARE(plot.cursorX1(), 4.0);
    QCOMPARE(plot.cursorX2(), 9.0);
    plot.stepCursor(-1);
    QCOMPARE(plot.cursorX1(), 4.0);
    QCOMPARE(plot.cursorX2(), 5.0);
    plot.setActiveCursorIndex(1);
    plot.stepCursor(1);
    QCOMPARE(plot.cursorX1(), 5.0);
    QCOMPARE(plot.cursorX2(), 5.0);
    plot.stepCursor(-1);
    plot.stepCursor(1, 0);
    QCOMPARE(plot.cursorX1(), 5.0);
    QCOMPARE(plot.cursorX2(), 9.0);
    plot.stepCursor(-1, 0);
    QCOMPARE(plot.cursorX1(), 4.0);
    QCOMPARE(plot.cursorX2(), 5.0);

    PlotItem duplicates;
    auto duplicateStore = std::make_shared<PlotSeriesStore>();
    duplicateStore->replaceSeries({{3, {0, 1, 4, 4, 7}, {0, 1, 2, 3, 4}, QColor("green")}});
    duplicates.setSeriesStore(duplicateStore);
    duplicates.setVisibleSeries({3});
    duplicates.setXRange(0, 7);
    duplicates.setCursorMode(PlotItem::SingleCursor);
    duplicates.setCursorX(3.9);
    QCOMPARE(duplicates.cursorX1(), 4.0);
    duplicates.stepCursor(1);
    QCOMPARE(duplicates.cursorX1(), 7.0);
    duplicates.stepCursor(-1);
    QCOMPARE(duplicates.cursorX1(), 4.0);
    duplicates.setCursorX(0.0);
    duplicates.stepCursor(-1);
    QCOMPARE(duplicates.cursorX1(), 0.0);
    duplicates.setCursorX(7.0);
    duplicates.stepCursor(1);
    QCOMPARE(duplicates.cursorX1(), 7.0);
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
    controller.signalModel()->setPlotChecked(0, 0, true);
    controller.signalModel()->setPlotChecked(0, 1, true);
    controller.clearPlotSignals(0);
    QVERIFY(!controller.plotSignalEnabled(0, 0));
    QVERIFY(!controller.plotSignalEnabled(0, 1));
    QCOMPARE(controller.signalCount(), 2);
    controller.setLayout(1, 2);
    controller.signalModel()->setPlotChecked(0, 0, true);
    controller.signalModel()->setPlotChecked(1, 1, true);
    controller.clearAllPlotSignals();
    QVERIFY(!controller.plotSignalEnabled(0, 0));
    QVERIFY(!controller.plotSignalEnabled(1, 1));
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
    QCOMPARE(second.yMinimum(), 1.9); QCOMPARE(second.yMaximum(), 2.1);

    // "当前时间轴" fits only the active subplot's signals; the shared X axis
    // still applies the result to every subplot.
    controller.setActivePlot(0);
    controller.fitPlots(true, false, false);
    QCOMPARE(first.xMinimum(), -.2); QCOMPARE(first.xMaximum(), 10.2);
    QCOMPARE(second.xMinimum(), -.2); QCOMPARE(second.xMaximum(), 10.2);
    controller.setActivePlot(1);
    controller.fitPlots(true, false, false);
    QCOMPARE(first.xMinimum(), 98.0); QCOMPARE(first.xMaximum(), 202.0);
    QCOMPARE(second.xMinimum(), 98.0); QCOMPARE(second.xMaximum(), 202.0);
    // "全部时间轴" goes back to the union of every subplot.
    controller.fitPlots(true, false, true);
    QCOMPARE(first.xMinimum(), -4.0); QCOMPARE(first.xMaximum(), 204.0);

    // A maximized subplot fits only the time span of its own signals.
    controller.setSoloPlot(1);
    QCOMPARE(controller.soloPlotIndex(), 1);
    controller.fitPlots(true, false, true);
    QCOMPARE(second.xMinimum(), 98.0); QCOMPARE(second.xMaximum(), 202.0);
    controller.setSoloPlot(0);
    controller.fitPlots(true, false, true);
    QCOMPARE(first.xMinimum(), -.2); QCOMPARE(first.xMaximum(), 10.2);
    // Restoring the tiled layout fits the union again.
    controller.setSoloPlot(-1);
    controller.fitPlots(true, false, true);
    QCOMPARE(first.xMinimum(), -4.0); QCOMPARE(first.xMaximum(), 204.0);
    QCOMPARE(second.xMinimum(), -4.0); QCOMPARE(second.xMaximum(), 204.0);
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
    plot.setXRange(0, 3);
    for (double value : {4294967295.0, -4294967295.0, 375.0, 1.0, 1e-8, 0.0}) {
        store->replaceSeries({{1, {0, 1, 2, 3}, {value, value, value, value}, QColor("red")}});
        plot.setSeriesStore(store);
        plot.setVisibleSeries({1});
        plot.fitY();
        const double padding = value == 0 ? .5 : std::abs(value) * .05;
        QCOMPARE(plot.yMinimum(), value - padding);
        QCOMPARE(plot.yMaximum(), value + padding);
    }

}

void AppControllerTest::axisZoomAnchorsOnPointerPosition()
{
    PlotItem plot;
    plot.setXRange(2.0, 10.0);

    // X: fraction is measured left to right, so the data value under the
    // pointer must stay put while the range shrinks around it.
    const double xAnchor = plot.xMinimum() + .1 * (plot.xMaximum() - plot.xMinimum());
    plot.zoomAxis(0, .1, 1.0);
    QVERIFY(plot.xMaximum() - plot.xMinimum() < 8.0);
    QVERIFY(qAbs(plot.xMinimum() + .1 * (plot.xMaximum() - plot.xMinimum()) - xAnchor) < 1e-12);

    // Y: fraction is measured top to bottom, matching the axis gutter.
    plot.setYRange(-3.0, 7.0);
    const double yAnchor = plot.yMaximum() - .9 * (plot.yMaximum() - plot.yMinimum());
    plot.zoomAxis(1, .9, -1.0);
    QVERIFY(plot.yMaximum() - plot.yMinimum() > 10.0);
    QVERIFY(qAbs(plot.yMaximum() - .9 * (plot.yMaximum() - plot.yMinimum()) - yAnchor) < 1e-12);

    // A centred pointer still behaves like the previous midpoint zoom.
    plot.setXRange(2.0, 10.0);
    plot.zoomAxis(0, .5, 1.0);
    QVERIFY(qAbs((plot.xMinimum() + plot.xMaximum()) / 2.0 - 6.0) < 1e-12);
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

void AppControllerTest::cursorBadgesInitializeCenteredWithDelta()
{
    qmlRegisterTypesAndRevisions<PlotItemQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<AppControllerQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<SignalModelQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
    AppController controller;
    QQmlEngine engine;
    QStringList warnings;
    connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors) warnings.append(error.toString());
    });
    const QString path = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml/QuickPlot.qml");
    QQmlComponent component(&engine, QUrl::fromLocalFile(path));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({
        {"plotIndex", 0}, {"controller", QVariant::fromValue<QObject *>(&controller)},
        {"width", 1400}, {"height", 400}, {"graphCursorMode", 2}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *root = qobject_cast<QQuickItem *>(object.get());
    auto *plot = object->findChild<PlotItem *>();
    auto *delta = object->findChild<QQuickItem *>("cursorDeltaBadge");
    QVERIFY(root && plot && delta);
    QList<QQuickItem *> times;
    for (auto *child : root->childItems())
        if (child->objectName() == "cursorTimeLabel") times.append(child);
    QCOMPARE(times.size(), 2);
    // Check initial creation before any cursor movement can refresh a stale binding.
    QTRY_VERIFY(delta->isVisible());
    const auto verifyCenters = [&]() {
        const double origin = plot->mapToItem(root, QPointF()).x();
        const double values[] = {plot->cursorX1(), plot->cursorX2()};
        for (int i = 0; i < 2; ++i) {
            const double line = origin + (values[i] - plot->xMinimum())
                / (plot->xMaximum() - plot->xMinimum()) * plot->width();
            QVERIFY2(qAbs(times[i]->x() + times[i]->width() / 2 - line) < .01,
                qPrintable(QString("cursor %1: center %2 line %3; range %4..%5")
                    .arg(i).arg(times[i]->x() + times[i]->width() / 2).arg(line)
                    .arg(plot->xMinimum()).arg(plot->xMaximum())));
            QCOMPARE(times[i]->y(), plot->mapToItem(root, QPointF(0, plot->height())).y());
        }
        QCOMPARE(root->property("axisBottom").toDouble(), 22.0);
        QVERIFY(delta->isVisible());
    };
    verifyCenters();
    // Reproduce the reported numeric range and rebuild the delegates on re-enable.
    plot->setXRange(375, 900);
    plot->setCursorPosition(468.96, 1);
    plot->setCursorPosition(831.56, 2);
    QCoreApplication::processEvents();
    verifyCenters();
    root->setProperty("graphCursorMode", 0);
    root->setProperty("graphCursorMode", 2);
    QTRY_VERIFY(delta->isVisible());
    times.clear();
    for (auto *child : root->childItems())
        if (child->objectName() == "cursorTimeLabel") times.append(child);
    QCOMPARE(times.size(), 2);
    verifyCenters();
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

void AppControllerTest::cursorBadgeDragsSelectAndMoveOneOrBoth()
{
    qmlRegisterTypesAndRevisions<PlotItemQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<AppControllerQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<SignalModelQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
    AppController controller;
    QQmlEngine engine;
    const QString path = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml/QuickPlot.qml");
    QQmlComponent component(&engine, QUrl::fromLocalFile(path));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({
        {"plotIndex", 0}, {"controller", QVariant::fromValue<QObject *>(&controller)},
        {"width", 800}, {"height", 400}, {"graphCursorMode", 2}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *root = qobject_cast<QQuickItem *>(object.get());
    auto *plot = object->findChild<PlotItem *>();
    auto *delta = object->findChild<QQuickItem *>("cursorDeltaBadge");
    QVERIFY(root && plot && delta);
    auto store = std::make_shared<PlotSeriesStore>();
    QVector<double> xs, ys;
    for (int i = 0; i <= 100; ++i) { xs.append(i); ys.append(i); }
    store->replaceSeries({{1, xs, ys, QColor("red")}});
    plot->setSeriesStore(store); plot->setVisibleSeries({1});
    plot->setXRange(0, 100);
    plot->setCursorPosition(25, 1); plot->setCursorPosition(75, 2);
    QList<QQuickItem *> times;
    for (auto *child : root->childItems())
        if (child->objectName() == "cursorTimeLabel") times.append(child);
    QCOMPARE(times.size(), 2);
    QQuickWindow window;
    window.resize(800, 400);
    root->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(30);
    const auto center = [](QQuickItem *item) {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    };
    QTest::mouseMove(&window, center(times[0]), 30);
    auto *axisCursor = object->findChild<QQuickItem *>("axisZoomCursor");
    QVERIFY(axisCursor && !axisCursor->isVisible());
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(times[1]));
    QCOMPARE(plot->activeCursorIndex(), 2);
    QTest::keyClick(&window, Qt::Key_Right);
    QCOMPARE(plot->cursorX1(), 25.0); QCOMPARE(plot->cursorX2(), 76.0);
    const auto drag = [&](QQuickItem *badge, double fraction) {
        const QPoint start = center(badge);
        const QPoint end = start + QPoint(qRound(plot->width() * fraction), 0);
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&window, end, 30);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, end);
    };
    drag(times[0], .2);
    QCOMPARE(plot->activeCursorIndex(), 1);
    QCOMPARE(plot->cursorX1(), 45.0); QCOMPARE(plot->cursorX2(), 76.0);
    drag(delta, .1);
    QCOMPARE(plot->activeCursorIndex(), 0);
    QCOMPARE(plot->cursorX1(), 55.0); QCOMPARE(plot->cursorX2(), 86.0);
    QCOMPARE(plot->cursorDeltaT(), 31.0);
    QTest::keyClick(&window, Qt::Key_Right);
    QCOMPARE(plot->cursorX1(), 56.0); QCOMPARE(plot->cursorX2(), 87.0);
    QTest::keyClick(&window, Qt::Key_Left);
    QCOMPARE(plot->cursorX1(), 55.0); QCOMPARE(plot->cursorX2(), 86.0);
    drag(delta, .15);
    QCOMPARE(plot->cursorX1(), 69.0); QCOMPARE(plot->cursorX2(), 100.0);
    QTest::keyClick(&window, Qt::Key_Right);
    QCOMPARE(plot->cursorX1(), 69.0); QCOMPARE(plot->cursorX2(), 100.0);
    QCOMPARE(plot->cursorDeltaT(), 31.0);
    QVERIFY(!root->property("axisSelecting").toBool());
    QCOMPARE(plot->xMinimum(), 0.0); QCOMPARE(plot->xMaximum(), 100.0);
    plot->setCursorPosition(50, 1); plot->setCursorPosition(50, 2);
    QCoreApplication::processEvents();
    QVERIFY(delta->isVisible());
    QVERIFY(times[0]->x() + times[0]->width() + 4 <= delta->x());
    QVERIFY(delta->x() + delta->width() + 4 <= times[1]->x());
    plot->setXRange(80, 90);
    QVERIFY(!delta->isVisible());
    plot->setXRange(0, 100);
    plot->setCursorPosition(25, 1); plot->setCursorPosition(75, 2);
    plot->setXRange(50, 100);
    QVERIFY(!delta->isVisible()); // Only one visible line has no visible difference badge.
    root->setProperty("graphCursorMode", 1);
    QVERIFY(!delta->isVisible());
    QCOMPARE(root->property("axisBottom").toDouble(), 22.0);
}

void AppControllerTest::interactingWithSubplotSelectsIt()
{
    qmlRegisterTypesAndRevisions<PlotItemQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<AppControllerQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<SignalModelQmlRegistration>("DataInspector", 1);
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
    AppController controller;
    controller.setLayout(1, 2);
    QQmlEngine engine;
    const QString path = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml/QuickPlot.qml");
    QQmlComponent component(&engine, QUrl::fromLocalFile(path));
    QQuickWindow window;
    window.resize(1000, 400);
    std::unique_ptr<QObject> objects[2];
    QQuickItem *roots[2];
    PlotItem *plots[2];
    for (int i = 0; i < 2; ++i) {
        objects[i].reset(component.createWithInitialProperties({
            {"plotIndex", i}, {"controller", QVariant::fromValue<QObject *>(&controller)},
            {"x", i * 500}, {"width", 500}, {"height", 400}}));
        QVERIFY2(objects[i], qPrintable(component.errorString()));
        roots[i] = qobject_cast<QQuickItem *>(objects[i].get());
        plots[i] = objects[i]->findChild<PlotItem *>();
        QVERIFY(roots[i] && plots[i]);
        roots[i]->setParentItem(window.contentItem());
        plots[i]->setYRange(-10, 10);
    }
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(30);
    const auto point = [](QQuickItem *item) { return item->mapToScene(QPointF(item->width() * .37, item->height() * .37)); };
    for (int i = 0; i < 2; ++i) {
        auto *xArea = objects[i]->findChild<QQuickItem *>("xAxisArea");
        auto *yArea = objects[i]->findChild<QQuickItem *>("yAxisArea");
        QVERIFY(xArea && yArea);
        // Wheel interactions select before changing the range, on graph or either axis.
        for (auto *target : {static_cast<QQuickItem *>(plots[i]), xArea, yArea}) {
            controller.setActivePlot(1 - i);
            const QPointF scene = point(target);
            QWheelEvent wheel(scene, window.mapToGlobal(scene.toPoint()), QPoint(), QPoint(0, 120),
                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&window, &wheel);
            QCOMPARE(controller.activePlotIndex(), i);
        }
        // Axis selection and graph panning/box zoom also select on press.
        for (auto *target : {xArea, yArea, static_cast<QQuickItem *>(plots[i])}) {
            controller.setActivePlot(1 - i);
            const QPoint scene = point(target).toPoint();
            QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, scene);
            QCOMPARE(controller.activePlotIndex(), i);
            QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, scene);
        }
        roots[i]->setProperty("graphZoomMode", 1);
        controller.setActivePlot(1 - i);
        const QPoint graph = point(plots[i]).toPoint();
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, graph);
        QCOMPARE(controller.activePlotIndex(), i);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, graph);
        roots[i]->setProperty("graphZoomMode", 0);
        controller.setActivePlot(1 - i);
        QTest::mouseMove(&window, graph, 30);
        QCOMPARE(controller.activePlotIndex(), 1 - i); // Hover alone does not change the active subplot.
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(controller.activePlotIndex(), i);
    }
}

void AppControllerTest::quickPlotLoadsWithLegendAndCursors()
{
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
    qmlRegisterType<PlotItem>("DataInspector", 1, 0, "PlotItem");
    qmlRegisterUncreatableType<AppController>("DataInspector", 1, 0, "AppController", "Owned by C++");
    qmlRegisterUncreatableType<SignalModel>("DataInspector", 1, 0, "SignalModel", "Owned by AppController");
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
    auto legendNames = [&]() {
        QStringList names;
        std::function<void(QQuickItem *)> visit = [&](QQuickItem *parent) {
            for (auto *child : parent->childItems()) {
                if (child->objectName() == "legendSignalName") names.append(child->property("text").toString());
                visit(child);
            }
        };
        visit(qobject_cast<QQuickItem *>(object.get()));
        return names;
    };
    QVERIFY(legendNames().contains("A"));
    QVERIFY(controller.renameSignal(0, QStringLiteral("Renamed")));
    QTRY_VERIFY(legendNames().contains("Renamed"));
    QVERIFY(!legendNames().contains("A"));
    QVERIFY(controller.resetSignalName(0));
    QTRY_VERIFY(legendNames().contains("A"));

    const double plotBottom = plot->parentItem()->y() + plot->height();
    // Fixed gutter, badges flush to the axis, and close readouts never overlap.
    for (const auto *label : visualTimes) QCOMPARE(label->y(), plotBottom);
    QCOMPARE(object->property("axisBottom").toDouble(), 22.0);
    QVERIFY(visualTimes[0]->x() + visualTimes[0]->width() + 4 <= visualTimes[1]->x());
    QQuickWindow window;
    window.resize(800, 400);
    auto *item = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(item);
    item->setParentItem(window.contentItem());
    window.show();
    QTest::qWait(50);
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    plot->setXRange(0, 1);
    plot->setCursorPosition(.25, 1);
    plot->setCursorPosition(.75, 2);
    QSignalSpy captured(&controller, &AppController::imageExportFinished);
    const QString pngPath = directory.filePath("view.png");
    QVERIFY(!controller.exportPlotImage(nullptr, pngPath));
    QVERIFY(controller.exportPlotImage(item, pngPath, 2));
    QTRY_COMPARE_WITH_TIMEOUT(captured.size(), 1, 10000);
    QVERIFY2(captured[0][0].toBool(), qPrintable(captured[0][1].toString()));
    const QImage image(pngPath);
    QCOMPARE(image.size(), QSize(1600, 800));
    QVERIFY(!image.isNull());
    if (window.rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
        const QPointF origin = plot->mapToItem(item, QPointF());
        const QRect region = QRect(qCeil(origin.x() * 2), qCeil(origin.y() * 2),
            qFloor(plot->width() * 2), qFloor(plot->height() * 2)).intersected(image.rect());
        const QColor stroke = controller.signalColor(1);
        int coloredPixels = 0;
        for (int y = region.top(); y <= region.bottom(); ++y) {
            for (int x = region.left(); x <= region.right(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (qAbs(pixel.red() - stroke.red()) < 12
                    && qAbs(pixel.green() - stroke.green()) < 12
                    && qAbs(pixel.blue() - stroke.blue()) < 12) ++coloredPixels;
            }
        }
        QVERIFY2(coloredPixels > 10, "Exported PNG must include the rendered curve");
    }
    QFile png(pngPath); QVERIFY(png.open(QIODevice::ReadOnly));
    const auto beforeCancel = png.readAll(); png.close();
    captured.clear();
    QVERIFY(controller.exportPlotImage(item, pngPath, 2));
    controller.cancelExport();
    QTRY_COMPARE_WITH_TIMEOUT(captured.size(), 1, 10000);
    QVERIFY(!captured[0][0].toBool());
    QVERIFY(png.open(QIODevice::ReadOnly));
    QCOMPARE(png.readAll(), beforeCancel); png.close();
    // Clicking a cursor line selects the keyboard target without dragging it.
    const QPoint secondLine = plot->mapToScene(QPointF(plot->width() * .75, 40)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, secondLine);
    QCOMPARE(plot->activeCursorIndex(), 2);
    QTest::keyClick(&window, Qt::Key_Right);
    QCOMPARE(plot->cursorX1(), .25);
    QCOMPARE(plot->cursorX2(), 1.0);
    QTest::keyClick(&window, Qt::Key_Left);
    QCOMPARE(plot->cursorX1(), .25);
    QCOMPARE(plot->cursorX2(), 0.0);
    // Time badges select their cursor and consume the press instead of zooming.
    plot->setCursorPosition(.25, 1);
    plot->setCursorPosition(.75, 2);
    QCoreApplication::processEvents();
    const QPoint badgeStart = visualTimes[0]->mapToScene(QPointF(visualTimes[0]->width() / 2, 8)).toPoint();
    const QPoint badgeEnd = visualTimes[1]->mapToScene(QPointF(visualTimes[1]->width() / 2, 8)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, badgeStart);
    QCOMPARE(plot->activeCursorIndex(), 1);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, badgeEnd);
    QCOMPARE(plot->activeCursorIndex(), 2);
    QVERIFY(!item->property("axisSelecting").toBool());
    QCOMPARE(plot->xMinimum(), 0.0);
    QCOMPARE(plot->xMaximum(), 1.0);
    plot->setXRange(0, 1);
    plot->setCursorPosition(.5, 1);
    plot->setCursorPosition(.50001, 2);
    QTRY_VERIFY(visualTimes[0]->x() + visualTimes[0]->width() + 4 <= visualTimes[1]->x());
    auto *delta = object->findChild<QQuickItem *>("cursorDeltaBadge");
    QVERIFY(delta && delta->isVisible());
    item->setWidth(100);
    QTRY_VERIFY(visualTimes[0]->x() + visualTimes[0]->width() + 4 <= visualTimes[1]->x());
    QCOMPARE(visualTimes[0]->y(), visualTimes[1]->y());
    QCOMPARE(object->property("axisBottom").toDouble(), 22.0);
    for (auto *label : visualTimes) {
        QVERIFY(label->x() >= 0);
        QVERIFY(label->x() + label->width() <= item->width());
    }
    item->setWidth(800);
    plot->setCursorPosition(.25, 1);
    plot->setCursorPosition(.75, 2);
    // Cursor focus recovery must not steal focus from either text input type.
    for (const QByteArray &type : {QByteArray("TextInput"), QByteArray("TextEdit")}) {
        QQmlComponent editorComponent(&engine);
        editorComponent.setData("import QtQuick\n" + type + " { width: 100; height: 30 }", QUrl());
        std::unique_ptr<QObject> editor(editorComponent.create());
        QVERIFY2(editor, qPrintable(editorComponent.errorString()));
        auto *textItem = qobject_cast<QQuickItem *>(editor.get());
        QVERIFY(textItem);
        textItem->setParentItem(window.contentItem());
        textItem->forceActiveFocus();
        QCOMPARE(window.activeFocusItem(), textItem);
        QVERIFY(QMetaObject::invokeMethod(item, "ensureCursorFocus"));
        QCOMPARE(window.activeFocusItem(), textItem);
    }
    item->forceActiveFocus();
    plot->setActiveCursorIndex(1);
    QTest::keyClick(&window, Qt::Key_Right);
    QCOMPARE(plot->cursorX1(), 1.0);
    QCOMPARE(plot->cursorX2(), .75);
    auto *axisCursor = item->findChild<QQuickItem *>("axisZoomCursor");
    QVERIFY(axisCursor);

    QVERIFY(item->setProperty("hoveredAxis", -1));
    plot->setXRange(.2, .8);
    plot->setYRange(100.0, 200.0);
    QTest::keyClick(&window, Qt::Key_Space);
    QCOMPARE(plot->xMinimum(), .2);
    QCOMPARE(plot->xMaximum(), .8);
    QCOMPARE(plot->yMinimum(), 100.0);
    QCOMPARE(plot->yMaximum(), 200.0);

    QVERIFY(item->setProperty("hoveredAxis", 0));
    QTest::keyClick(&window, Qt::Key_Space);
    QCOMPARE(plot->xMinimum(), -.02);
    QCOMPARE(plot->xMaximum(), 1.02);
    QCOMPARE(plot->yMinimum(), .95);
    QCOMPARE(plot->yMaximum(), 2.05);

    plot->setXRange(0.0, 1.0);
    plot->setYRange(100.0, 200.0);
    QVERIFY(item->setProperty("hoveredAxis", 1));
    QTest::keyClick(&window, Qt::Key_Space);
    QCOMPARE(plot->xMinimum(), -.02);
    QCOMPARE(plot->xMaximum(), 1.02);
    QCOMPARE(plot->yMinimum(), .95);
    QCOMPARE(plot->yMaximum(), 2.05);
    item->setProperty("hoveredAxis", -1);

    const QPointF graphOrigin = plot->mapToItem(item, QPointF(0, 0));
    const auto graphPoint = [&](double xFraction, double yFraction) {
        return item->mapToItem(window.contentItem(),
                               QPointF(graphOrigin.x() + plot->width() * xFraction,
                                       graphOrigin.y() + plot->height() * yFraction)).toPoint();
    };

    QVERIFY(item->setProperty("graphZoomMode", 1));
    plot->setXRange(0.0, 10.0);
    plot->setYRange(-20.0, 20.0);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, graphPoint(.2, .25));
    QTest::mouseMove(&window, graphPoint(.8, .75), 30);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, graphPoint(.8, .75));
    QVERIFY(qAbs(plot->xMinimum() - 2.0) < .02);
    QVERIFY(qAbs(plot->xMaximum() - 8.0) < .02);
    QVERIFY(qAbs(plot->yMinimum() + 10.0) < .15);
    QVERIFY(qAbs(plot->yMaximum() - 10.0) < .15);

    QVERIFY(item->setProperty("graphZoomMode", 2));
    plot->setXRange(0.0, 10.0);
    plot->setYRange(-20.0, 20.0);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, graphPoint(.25, .1));
    QTest::mouseMove(&window, graphPoint(.75, .9), 30);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, graphPoint(.75, .9));
    QVERIFY(qAbs(plot->xMinimum() - 2.5) < .02);
    QVERIFY(qAbs(plot->xMaximum() - 7.5) < .02);
    QCOMPARE(plot->yMinimum(), -20.0);
    QCOMPARE(plot->yMaximum(), 20.0);

    QVERIFY(item->setProperty("graphZoomMode", 3));
    plot->setXRange(0.0, 10.0);
    plot->setYRange(-20.0, 20.0);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, graphPoint(.1, .25));
    QTest::mouseMove(&window, graphPoint(.9, .75), 30);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, graphPoint(.9, .75));
    QCOMPARE(plot->xMinimum(), 0.0);
    QCOMPARE(plot->xMaximum(), 10.0);
    QVERIFY(qAbs(plot->yMinimum() + 10.0) < .15);
    QVERIFY(qAbs(plot->yMaximum() - 10.0) < .15);
    item->setProperty("graphZoomMode", 0);

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
    plot->setXRange(.4, .6);
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
    // Match Main.qml's column alignment when tick labels need different widths.
    const double sharedLeft = qMax(item->property("measuredAxisLeft").toDouble(),
                                   targetItem->property("measuredAxisLeft").toDouble());
    item->setProperty("sharedAxisLeft", sharedLeft);
    targetItem->setProperty("sharedAxisLeft", sharedLeft);
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
    for (const auto &range : {qMakePair(4294967294.95, 4294967295.05),
                              qMakePair(4.28e9, 4.31e9),
                              qMakePair(-4294967295.05, -4294967294.95),
                              qMakePair(1e20, 1e20 + 1e6),
                              qMakePair(1e-8, 1e-8 + 1e-12)}) {
        plot->setYRange(range.first, range.second);
        QVERIFY(!item->findChild<QQuickItem *>("yAxisOffsetLabel"));
        QSet<QString> tickLabels;
        for (const auto &tick : plot->yTicks()) {
            QVariant label;
            QVERIFY(QMetaObject::invokeMethod(item, "yTickLabel", Q_RETURN_ARG(QVariant, label),
                                              Q_ARG(QVariant, tick)));
            QVERIFY(label.toString().contains('e'));
            const double value = tick.toMap().value("value").toDouble();
            QVERIFY(std::abs(label.toString().toDouble() - value)
                    <= (range.second - range.first) * 1e-3);
            QCOMPARE(label.toString().toDouble() > 0, value > 0);
            QVERIFY(!tickLabels.contains(label.toString()));
            tickLabels.insert(label.toString());
        }
    }
    plot->setYRange(4.0e9, 4.5e9);
    QVERIFY(!item->findChild<QQuickItem *>("yAxisOffsetLabel"));
    for (const auto &tick : plot->yTicks()) {
        QVariant label;
        QVERIFY(QMetaObject::invokeMethod(item, "yTickLabel", Q_RETURN_ARG(QVariant, label),
                                          Q_ARG(QVariant, tick)));
        QVERIFY(label.toString().contains('e'));
        QVERIFY(label.toString().size() <= 8);
    }
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
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

void AppControllerTest::signalTreeDragAddsToTargetPlot()
{
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
    qmlRegisterType<PlotItem>("DataInspector", 1, 0, "PlotItem");
    qmlRegisterUncreatableType<AppController>("DataInspector", 1, 0, "AppController", "Owned by C++");
    qmlRegisterUncreatableType<SignalModel>("DataInspector", 1, 0, "SignalModel", "Owned by AppController");
    QTemporaryDir temp;
    const QString csv = temp.filePath("drag.csv");
    QByteArray csvData = "time,A,B";
    for (int i = 0; i < 60; ++i) csvData += ",Extra" + QByteArray::number(i);
    csvData += "\n0,1,2";
    for (int i = 0; i < 60; ++i) csvData += ",3";
    csvData += "\n1,3,4";
    for (int i = 0; i < 60; ++i) csvData += ",5";
    csvData += '\n';
    writeCsvFile(csv, csvData);
    AppController controller;
    controller.setLayout(1, 2);
    QVERIFY(controller.loadCsv(csv));
    QTRY_VERIFY(!controller.loading());
    controller.addSignalToPlot(0, 1);
    QQmlEngine engine;
    QStringList warnings;
    connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors) warnings.append(error.toString());
    });
    const QDir directory = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml");
    qmlRegisterType(QUrl::fromLocalFile(directory.filePath("QuickPlot.qml")), "DataInspector", 1, 0, "QuickPlot");
    QQmlComponent component(&engine, QUrl::fromLocalFile(directory.filePath("Main.qml")));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{"visible", false}, {"appController", QVariant::fromValue(&controller)}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(host);
    host->show();
    QVERIFY(QTest::qWaitForWindowExposed(host));
    const auto visualItem = [&](const std::function<bool(QQuickItem *)> &matches) -> QQuickItem * {
        QList<QQuickItem *> pending{host->contentItem()};
        while (!pending.isEmpty()) {
            auto *item = pending.takeLast();
            if (matches(item)) return item;
            pending.append(item->childItems());
        }
        return nullptr;
    };
    auto *search = object->findChild<QQuickItem *>("signalSearch");
    auto *preview = object->findChild<QQuickItem *>("signalTreeDragPreview");
    QVERIFY(search && preview);
    search->setProperty("text", "B");
    QTRY_COMPARE(controller.signalModel()->rowCount(), 2); // Wait for the debounced filter.
    const auto signalLabel = [&]() {
        return visualItem([](QQuickItem *item) {
            return item->objectName() == "signalTreeName"
                && item->parentItem()->parentItem()->property("signalIndex").toInt() == 1;
        });
    };
    QTRY_VERIFY(signalLabel());
    auto *target = visualItem([](QQuickItem *item) {
        return item->property("dropHighlighted").isValid() && item->property("plotIndex").toInt() == 1;
    });
    QVERIFY(target);
    QTRY_VERIFY(target->width() > 100 && signalLabel()->width() > 20);
    const auto startDrag = [&]() {
        auto *label = signalLabel();
        const QPoint start = label->mapToScene(QPointF(label->width() / 2, label->height() / 2)).toPoint();
        QTest::mousePress(host, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(host, start + QPoint(20, 0), 40);
        return start;
    };
    const QPoint destination = target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
    auto *sourceRow = signalLabel()->parentItem()->parentItem();
    const QPointF nameOrigin = signalLabel()->mapToScene(QPointF());
    const QPoint pressPosition = startDrag();
    QCOMPARE(preview->property("signalName").toString(), QStringLiteral("B"));
    QVERIFY(sourceRow->isVisible());
    QCOMPARE(sourceRow->opacity(), 1.0);
    QVERIFY(qAbs(preview->mapToScene(QPointF()).x() - nameOrigin.x() - 20 + 22) < 1);
    QVERIFY(qAbs(preview->mapToScene(QPointF()).y() - nameOrigin.y() + 6) < 1);
    QTest::mouseMove(host, destination, 40);
    QVERIFY(preview->isVisible());
    const QPointF expectedOrigin = QPointF(destination) - (QPointF(pressPosition) - nameOrigin) - QPointF(22, 6);
    QVERIFY(qAbs(preview->mapToScene(QPointF()).x() - expectedOrigin.x()) < 1);
    QVERIFY(qAbs(preview->mapToScene(QPointF()).y() - expectedOrigin.y()) < 1);
    auto *cursor = object->findChild<QQuickItem *>("signalDragCursor");
    QVERIFY(cursor && cursor->isVisible());
    QCOMPARE(cursor->property("cursorShape").toInt(), int(Qt::BlankCursor));
    QVERIFY(target->property("dropHighlighted").toBool());
    QCOMPARE(controller.activePlotIndex(), 0);
    QTest::mouseRelease(host, Qt::LeftButton, Qt::NoModifier, destination);
    QTRY_VERIFY(controller.plotSignalEnabled(1, 1));
    QVERIFY(controller.plotSignalEnabled(0, 1)); // Adding preserves the source plot's binding.
    QVERIFY(!controller.plotSignalEnabled(1, 0)); // Filtered row maps to B, not A.
    QCOMPARE(controller.activePlotIndex(), 1);
    QVERIFY(!preview->isVisible());
    QVERIFY(!cursor->isVisible());
    QVERIFY(!target->property("dropHighlighted").toBool());
    controller.setActivePlot(0);
    startDrag();
    QTest::mouseMove(host, destination, 40);
    QTest::keyClick(host, Qt::Key_Escape);
    QVERIFY(!preview->isVisible());
    QVERIFY(!target->property("dropHighlighted").toBool());
    QTest::mouseMove(host, destination + QPoint(10, 0), 40);
    QVERIFY(!preview->isVisible()); // Canceled gestures cannot restart before release.
    QTest::mouseRelease(host, Qt::LeftButton, Qt::NoModifier, destination);
    QCOMPARE(controller.activePlotIndex(), 0);
    startDrag();
    QTest::mouseMove(host, destination, 40);
    QTest::mouseRelease(host, Qt::LeftButton, Qt::NoModifier, destination);
    QCOMPARE(controller.plotSignalRows(1).size(), 1); // Duplicate drops are idempotent.
    controller.setActivePlot(0);
    startDrag();
    const QPoint outside(host->width() / 2, 5);
    QTest::mouseMove(host, outside, 40);
    QTest::mouseRelease(host, Qt::LeftButton, Qt::NoModifier, outside);
    QCOMPARE(controller.activePlotIndex(), 0);
    QVERIFY(!preview->isVisible());
    // The row's vertical padding and color swatch also initiate a drag.
    for (const QPointF &offset : {QPointF(80, 2), QPointF(sourceRow->width() - 20, 15)}) {
        const QPoint start = sourceRow->mapToScene(offset).toPoint();
        QTest::mousePress(host, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(host, start + QPoint(20, 0), 40);
        QVERIFY(preview->isVisible());
        QTest::mouseMove(host, destination, 40);
        QTest::mouseRelease(host, Qt::LeftButton, Qt::NoModifier, destination);
        QCOMPARE(controller.activePlotIndex(), 1);
        QCOMPARE(controller.plotSignalRows(1).size(), 1);
        QCOMPARE(object->property("editingSignalIndex").toInt(), -1);
        controller.setActivePlot(0);
    }
    controller.addSignalToPlot(99, 1);
    controller.addSignalToPlot(1, 99);
    QCOMPARE(controller.activePlotIndex(), 0);
    search->setProperty("text", "");
    QTRY_VERIFY(signalLabel());
    auto *list = object->findChild<QQuickItem *>("signalList");
    QVERIFY(list);
    QTRY_VERIFY(list->property("contentHeight").toDouble() > list->height());
    const double beforeDrag = list->property("contentY").toDouble();
    const QPoint verticalStart = signalLabel()->mapToScene(
        QPointF(signalLabel()->width() / 2, signalLabel()->height() / 2)).toPoint();
    QTest::mousePress(host, Qt::LeftButton, Qt::NoModifier, verticalStart);
    QTest::mouseMove(host, verticalStart + QPoint(0, 4), 40);
    QTest::mouseMove(host, verticalStart + QPoint(0, 30), 40);
    QTest::mouseMove(host, verticalStart + QPoint(0, 60), 40);
    QVERIFY(preview->isVisible());
    QCOMPARE(list->property("contentY").toDouble(), beforeDrag);
    QTest::mouseRelease(host, Qt::LeftButton, Qt::NoModifier, verticalStart + QPoint(0, 60));
    QVERIFY(!preview->isVisible());
    QCOMPARE(list->property("contentY").toDouble(), beforeDrag);
    // Exporting the whole grid from solo mode restores the UI without changing saved state.
    object->setProperty("subplotMaximized", true);
    QVERIFY(controller.saveSession(temp.filePath("image.disession")));
    QVERIFY(!controller.sessionModified());
    object->setProperty("imageCaptureAll", true);
    QTRY_VERIFY(target->isVisible());
    auto *grid = object->findChild<QQuickItem *>("plotGrid");
    QVERIFY(grid);
    QSignalSpy imageFinished(&controller, &AppController::imageExportFinished);
    const QString gridImage = temp.filePath("all-plots.png");
    QVERIFY(controller.exportPlotImage(grid, gridImage, 2));
    QTRY_COMPARE_WITH_TIMEOUT(imageFinished.size(), 1, 10000);
    QVERIFY2(imageFinished[0][0].toBool(), qPrintable(imageFinished[0][1].toString()));
    QCOMPARE(QImage(gridImage).size(), QSize(qCeil(grid->width() * 2), qCeil(grid->height() * 2)));
    QVERIFY(object->property("subplotMaximized").toBool());
    QVERIFY(!object->property("imageCaptureAll").toBool());
    QCOMPARE(controller.soloPlotIndex(), 0);
    QVERIFY(!controller.sessionModified());
    QCOMPARE(warnings, QStringList());
}

void AppControllerTest::signalSectionsKeepNestedPathsAndAlignedNames()
{
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
    qmlRegisterType<PlotItem>("DataInspector", 1, 0, "PlotItem");
    qmlRegisterUncreatableType<AppController>("DataInspector", 1, 0, "AppController", "Owned by C++");
    qmlRegisterUncreatableType<SignalModel>("DataInspector", 1, 0, "SignalModel", "Owned by AppController");
    AppController controller;
    controller.signalModel()->setNames({"Pitch", "Latitude", "Longitude"},
        {"flight.mat/p1", "flight.mat/p2/navigation/position", "flight.mat/p2/navigation/position"},
        {QColor("red"), QColor("blue"), QColor("green")});
    QQmlEngine engine;
    QStringList warnings;
    connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors) warnings.append(error.toString());
    });
    const QDir directory = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml");
    qmlRegisterType(QUrl::fromLocalFile(directory.filePath("QuickPlot.qml")),
                    "DataInspector", 1, 0, "QuickPlot");
    QQmlComponent component(&engine, QUrl::fromLocalFile(directory.filePath("Main.qml")));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{"visible", false}, {"appController", QVariant::fromValue(&controller)}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(host);
    host->show();
    QVERIFY(QTest::qWaitForWindowExposed(host));
    const auto findRow = [&](const QString &name) -> QQuickItem * {
        QList<QQuickItem *> pending{host->contentItem()};
        while (!pending.isEmpty()) {
            auto *item = pending.takeLast();
            if (item->objectName() == "signalTreeRow"
                && item->property("signalName").toString() == name) return item;
            pending.append(item->childItems());
        }
        return nullptr;
    };
    QTRY_VERIFY(findRow("Pitch") && findRow("Latitude") && findRow("position"));
    auto *pitchName = findRow("Pitch")->findChild<QQuickItem *>("signalTreeName");
    auto *latitudeName = findRow("Latitude")->findChild<QQuickItem *>("signalTreeName");
    auto *positionName = findRow("position")->findChild<QQuickItem *>("signalTreeName");
    QVERIFY(pitchName && latitudeName && positionName);
    auto *fileName = findRow("flight.mat")->findChild<QQuickItem *>("signalTreeName");
    auto *tableName = findRow("p1")->findChild<QQuickItem *>("signalTreeName");
    QVERIFY(fileName && tableName);
    QCOMPARE(fileName->property("color"), tableName->property("color"));
    QCOMPARE(fileName->property("font").value<QFont>(), tableName->property("font").value<QFont>());
    QCOMPARE(findRow("flight.mat")->height(), findRow("p1")->height());
    QCOMPARE(fileName->mapToScene(QPointF()).x(), tableName->mapToScene(QPointF()).x());
    QTRY_VERIFY(pitchName->width() > 0 && latitudeName->width() > 0);
    QCOMPARE(pitchName->mapToScene(QPointF()).x(), latitudeName->mapToScene(QPointF()).x());
    QCOMPARE(positionName->property("text").toString(), QStringLiteral("p2 / navigation / position"));
    QVERIFY(findRow("position")->property("groupExpanded").toBool());
    QTest::mouseClick(host, Qt::LeftButton, Qt::NoModifier,
        positionName->mapToScene(QPointF(positionName->width() / 2, positionName->height() / 2)).toPoint());
    QTRY_VERIFY(!findRow("Latitude"));
    QVERIFY(!findRow("position")->property("groupExpanded").toBool());
    auto *search = object->findChild<QQuickItem *>("signalSearch");
    QVERIFY(search);
    search->setProperty("text", "Latitude");
    QTRY_VERIFY(findRow("Latitude")); // Search reveals a collapsed group's matching child.
    QVERIFY(!findRow("Pitch"));
    QCOMPARE(findRow("position")->property("displayName").toString(), QStringLiteral("p2 / navigation / position"));
    object->setProperty("darkTheme", true);
    QCoreApplication::processEvents();
    search->setProperty("text", "");
    QTRY_COMPARE(controller.signalModel()->rowCount(), 6); // Wait for the debounced clear before changing groups.
    QTRY_VERIFY(findRow("Pitch"));
    controller.signalModel()->toggleGroup("flight.mat/p2/navigation/position");
    QStringList extraNames, extraGroups;
    for (int i = 0; i < 60; ++i) {
        extraNames.append(QStringLiteral("Sample%1").arg(i));
        extraGroups.append("flight.mat/p2/navigation/position");
    }
    for (int i = 0; i < 60; ++i) {
        extraNames.append(QStringLiteral("Other%1").arg(i));
        extraGroups.append("flight.mat/p3");
    }
    controller.signalModel()->appendNames(extraNames, extraGroups, {});
    QCOMPARE(controller.signalModel()->rowCount(), 129);
    auto *list = object->findChild<QQuickItem *>("signalList");
    auto *header = object->findChild<QQuickItem *>("signalStickyHeader");
    auto *breadcrumb = object->findChild<QQuickItem *>("signalBreadcrumb");
    QVERIFY(list && header && breadcrumb);
    auto *bar = object->findChild<QQuickItem *>("signalScrollBar");
    QVERIFY(bar);
    list->setProperty("contentY", 100.0);
    QTest::qWait(80); // Let the path timer and layout settle before measuring the clicked header.
    const double previousY = list->property("contentY").toDouble();
    auto *positionRow = findRow("position");
    QVERIFY(positionRow);
    const double headerY = positionRow->mapToScene(QPointF()).y();
    positionName = positionRow->findChild<QQuickItem *>("signalTreeName");
    QTest::mouseClick(host, Qt::LeftButton, Qt::NoModifier,
        positionName->mapToScene(QPointF(positionName->width() / 2, positionName->height() / 2)).toPoint());
    QTRY_VERIFY(!findRow("position")->property("groupExpanded").toBool());
    QCOMPARE(list->property("contentY").toDouble(), previousY);
    QCOMPARE(findRow("position")->mapToScene(QPointF()).y(), headerY);
    QVERIFY(QMetaObject::invokeMethod(list, "toggleGroup",
        Q_ARG(QVariant, "flight.mat/p2/navigation/position"), Q_ARG(QVariant, 5)));
    QTRY_VERIFY(findRow("position")->property("groupExpanded").toBool());
    QCOMPARE(list->property("contentY").toDouble(), previousY);
    QCOMPARE(findRow("position")->mapToScene(QPointF()).y(), headerY);
    const double viewportHeight = list->height(), scrollHeight = bar->height(), scrollY = bar->y();
    QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, 6), Q_ARG(int, 0)));
    QTRY_COMPARE(header->property("pathText").toString(),
        QStringLiteral("flight.mat › p2 › navigation › position"));
    QCOMPARE(header->height(), 28.0); // Deep paths still occupy exactly one row.
    QCOMPARE(list->y(), 0.0); // Overlay navigation never resizes the scroll viewport.
    const auto pathSegment = [&](const QString &name) -> QQuickItem * {
        QList<QQuickItem *> pending{breadcrumb};
        while (!pending.isEmpty()) {
            auto *item = pending.takeLast();
            if (item->objectName() == "signalBreadcrumbSegment"
                && item->property("text").toString() == name) return item;
            pending.append(item->childItems());
        }
        return nullptr;
    };
    QTRY_VERIFY(pathSegment("position"));
    auto *positionSegment = pathSegment("position");
    QTest::mouseClick(host, Qt::LeftButton, Qt::NoModifier,
        positionSegment->mapToScene(QPointF(positionSegment->width() / 2, positionSegment->height() / 2)).toPoint());
    QTRY_COMPARE(list->property("currentIndex").toInt(), 5);
    QTRY_COMPARE(header->property("pathText").toString(),
        QStringLiteral("flight.mat › p2 › navigation › position"));
    QCOMPARE(object->property("editingSignalIndex").toInt(), -1);
    QTest::mouseClick(host, Qt::LeftButton, Qt::NoModifier,
        header->mapToScene(QPointF(header->width() - 12, header->height() / 2)).toPoint());
    QCOMPARE(object->property("editingSignalIndex").toInt(), -1); // Blank navigation space consumes clicks too.
    const auto firstSegment = [&]() -> QQuickItem * {
        QList<QQuickItem *> pending{breadcrumb};
        while (!pending.isEmpty()) {
            auto *item = pending.takeLast();
            if (item->objectName() == "signalBreadcrumbSegment"
                && item->property("text").toString() == "flight.mat") return item;
            pending.append(item->childItems());
        }
        return nullptr;
    };
    QTRY_VERIFY(firstSegment());
    auto *segment = firstSegment();
    QCOMPARE(segment->property("color"), object->property("treeHeaderTextColor"));
    QVERIFY(!segment->property("font").value<QFont>().underline());
    QVERIFY(!segment->property("text").toString().contains("<a "));
    QTRY_VERIFY(segment->width() > 0 && segment->height() > 0);
    QTest::mouseClick(host, Qt::LeftButton, Qt::NoModifier,
        segment->mapToScene(QPointF(segment->width() / 2, segment->height() / 2)).toPoint());
    QTRY_COMPARE(list->property("currentIndex").toInt(), 0);
    QTRY_VERIFY(!header->isVisible());
    QCOMPARE(list->height(), viewportHeight);
    QCOMPARE(bar->height(), scrollHeight);
    QCOMPARE(bar->y(), scrollY);
    const QPoint thumb = bar->mapToScene(QPointF(bar->width() / 2,
        bar->height() * bar->property("size").toDouble() / 2)).toPoint();
    QTest::mousePress(host, Qt::LeftButton, Qt::NoModifier, thumb);
    double lastScroll = list->property("contentY").toDouble();
    for (int offset : {6, 12, 24, 40, 70}) {
        QTest::mouseMove(host, thumb + QPoint(0, offset), 60);
        QVERIFY(list->property("contentY").toDouble() >= lastScroll - 1);
        lastScroll = list->property("contentY").toDouble();
        QCOMPARE(bar->height(), scrollHeight);
        QCOMPARE(bar->y(), scrollY);
    }
    QTest::mouseRelease(host, Qt::LeftButton, Qt::NoModifier, thumb + QPoint(0, 70));
    QVERIFY(lastScroll > 0);
    const double beforeSearch = list->property("contentY").toDouble();
    search->setProperty("text", QStringLiteral("Other"));
    QTRY_VERIFY(controller.signalModel()->rowCount() < 129);
    search->setProperty("text", QString());
    QTRY_COMPARE(controller.signalModel()->rowCount(), 129);
    QTRY_VERIFY(qAbs(list->property("contentY").toDouble() - beforeSearch) < 1);
    const QString longFile = QStringLiteral("2026-09-22_11-38-58_") + QString(100, QLatin1Char('a')) + ".mat";
    controller.signalModel()->setNames(extraNames, QStringList(extraNames.size(), longFile + "/p19"));
    QTRY_VERIFY(findRow("p19"));
    QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, 2), Q_ARG(int, 0)));
    QTRY_COMPARE(header->property("pathText").toString(), longFile + QStringLiteral(" › p19"));
    QTRY_VERIFY(pathSegment("p19"));
    auto *shortSegment = pathSegment("p19");
    QTRY_VERIFY(shortSegment->width() >= shortSegment->implicitWidth() - 1);
    QVERIFY(shortSegment->mapToScene(QPointF(shortSegment->width(), 0)).x()
        <= header->mapToScene(QPointF(header->width(), 0)).x());
    QTest::mouseDClick(host, Qt::LeftButton, Qt::NoModifier,
        shortSegment->mapToScene(QPointF(shortSegment->width() / 2, shortSegment->height() / 2)).toPoint());
    QTRY_COMPARE(controller.signalModel()->rowCount(), 2);
    QVERIFY(!findRow("p19")->property("groupExpanded").toBool());
    QCOMPARE(object->property("editingSignalIndex").toInt(), -1);
    QCOMPARE(warnings, QStringList());
}

void AppControllerTest::toolbarModesToggleAndRememberSelection()
{
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
    qmlRegisterType<PlotItem>("DataInspector", 1, 0, "PlotItem");
    qmlRegisterUncreatableType<AppController>("DataInspector", 1, 0, "AppController", "Owned by C++");
    qmlRegisterUncreatableType<SignalModel>("DataInspector", 1, 0, "SignalModel", "Owned by AppController");
    AppController controller;
    QQmlEngine engine;
    QStringList qmlWarnings;
    connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors) qmlWarnings.append(error.toString());
    });
    const QDir qmlDirectory = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml");
    qmlRegisterType(QUrl::fromLocalFile(qmlDirectory.filePath("QuickPlot.qml")),
                    "DataInspector", 1, 0, "QuickPlot");
    const QString path = qmlDirectory.filePath("Main.qml");
    QQmlComponent component(&engine, QUrl::fromLocalFile(path));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"visible", false}, {"appController", QVariant::fromValue(&controller)}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("appController").value<AppController *>(), &controller);

    auto *layoutTool = object->findChild<QObject *>("layoutTool");
    auto *cursorTool = object->findChild<QObject *>("cursorSplitTool");
    auto *zoomTool = object->findChild<QObject *>("zoomSplitTool");
    QVERIFY(layoutTool);
    QVERIFY(cursorTool);
    QVERIFY(zoomTool);
    QCOMPARE(layoutTool->property("menuArrow").toBool(), false);

    QVERIFY(QMetaObject::invokeMethod(object.get(), "toggleCursorTool"));
    QCOMPARE(object->property("selectedCursorMode").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "selectCursorMode", Q_ARG(QVariant, 2)));
    QCOMPARE(object->property("selectedCursorMode").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "toggleCursorTool"));
    QCOMPARE(object->property("selectedCursorMode").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "selectCursorMode", Q_ARG(QVariant, 1)));
    QCOMPARE(object->property("selectedCursorMode").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "toggleCursorTool"));
    QCOMPARE(object->property("selectedCursorMode").toInt(), 1);

    auto *singleOption = object->findChild<QQuickItem *>("singleCursorOption");
    auto *doubleOption = object->findChild<QQuickItem *>("doubleCursorOption");
    auto *mainWindow = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(singleOption && doubleOption && mainWindow);
    mainWindow->show();
    QVERIFY(QTest::qWaitForWindowExposed(mainWindow));
    const auto chooseCursor = [&](QQuickItem *option) {
        QMetaObject::invokeMethod(cursorTool, "arrowClicked");
        QTest::qWait(50);
        QTest::mouseClick(mainWindow, Qt::LeftButton, Qt::NoModifier,
            option->mapToScene(QPointF(option->width() / 2, option->height() / 2)).toPoint());
        QCoreApplication::processEvents();
    };
    chooseCursor(singleOption);
    QVERIFY(singleOption->property("checked").toBool());
    QVERIFY(!doubleOption->property("checked").toBool());
    chooseCursor(doubleOption);
    QVERIFY(doubleOption->property("checked").toBool());
    QVERIFY(!singleOption->property("checked").toBool());
    chooseCursor(doubleOption);
    QVERIFY(doubleOption->property("checked").toBool());
    QCOMPARE(object->property("preferredCursorMode").toInt(), 2);
    mainWindow->hide();

    QCOMPARE(object->property("activeZoomMode").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "toggleZoomTool"));
    QCOMPARE(object->property("activeZoomMode").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "selectZoomMode", Q_ARG(QVariant, 3)));
    QCOMPARE(object->property("activeZoomMode").toInt(), 3);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "toggleZoomTool"));
    QCOMPARE(object->property("activeZoomMode").toInt(), 0);
    QCOMPARE(object->property("selectedZoomMode").toInt(), 3);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "selectZoomMode", Q_ARG(QVariant, 2)));
    QCOMPARE(object->property("activeZoomMode").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "toggleZoomTool"));
    QCOMPARE(object->property("activeZoomMode").toInt(), 2);
    auto *search = object->findChild<QQuickItem *>("signalSearch");
    auto *list = object->findChild<QQuickItem *>("signalList");
    auto *bar = object->findChild<QQuickItem *>("signalScrollBar");
    auto *sticky = object->findChild<QQuickItem *>("signalStickyHeader");
    auto *progress = object->findChild<QObject *>("transferProgress");
    QVERIFY(search); QVERIFY(list); QVERIFY(bar); QVERIFY(sticky); QVERIFY(progress);
    QVERIFY(search->setProperty("text", "pitch"));
    QVariant highlighted;
    QVERIFY(QMetaObject::invokeMethod(object.get(), "highlightedSearchText",
        Q_RETURN_ARG(QVariant, highlighted), Q_ARG(QVariant, "Pitch&PITCH<")));
    QCOMPARE(highlighted.toString().count("<b>"), 2);
    QVERIFY(highlighted.toString().contains("&amp;"));
    QVERIFY(highlighted.toString().endsWith("&lt;"));
    search->setProperty("text", "");
    QTemporaryDir directory;
    const QString csv = directory.filePath("tree.csv");
    QByteArray data = "time";
    for (int i = 0; i < 100; ++i) data += ",Pitch" + QByteArray::number(i);
    data += "\n0";
    for (int i = 0; i < 100; ++i) data += ",1";
    data += "\n1";
    for (int i = 0; i < 100; ++i) data += ",2";
    data += '\n';
    writeCsvFile(csv, data);
    QVERIFY(controller.loadCsv(csv));
    QTRY_VERIFY(!controller.loading());
    auto *host = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(host);
    host->show();
    QVERIFY(QTest::qWaitForWindowExposed(host));
    QTRY_VERIFY(bar->isVisible());
    list->setProperty("contentY", 150.0);
    QTRY_VERIFY(sticky->isVisible());
    QCOMPARE(sticky->property("pathText").toString(), QStringLiteral("tree.csv"));
    QCOMPARE(sticky->height(), 28.0);
    QVERIFY(sticky->x() + sticky->width() <= bar->x());
    QVERIFY(list->x() + list->width() <= bar->x());
    QCOMPARE(bar->width(), 5.0);
    QVERIFY(!bar->property("background").value<QObject *>());
    QCOMPARE(bar->x() + bar->width(), bar->parentItem()->width());
    PlotItem *view = nullptr;
    QList<QQuickItem *> pending{host->contentItem()};
    while (!pending.isEmpty() && !view) {
        auto *item = pending.takeLast();
        view = qobject_cast<PlotItem *>(item);
        pending.append(item->childItems());
    }
    QVERIFY(view);
    view->forceActiveFocus();
    const double oldMinimum = view->xMinimum(), oldMaximum = view->xMaximum();
    view->setXRange(2, 4);
    QTest::keyClick(host, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(view->xMinimum(), oldMinimum); QCOMPARE(view->xMaximum(), oldMaximum);
    view->setXRange(2, 4);
    search->forceActiveFocus();
    QTest::keyClick(host, Qt::Key_A);
    QTest::keyClick(host, Qt::Key_B);
    QTest::keyClick(host, Qt::Key_C);
    QTest::keyClick(host, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(view->xMinimum(), 2.0); // Text undo does not undo plot navigation.
    QCOMPARE(search->property("text").toString(), QString());
    QCOMPARE(progress->property("progressColor").value<QColor>(), QColor("#0078d4"));
    QVERIFY(controller.exportXlsx(directory.filePath("export.xlsx"), AppController::AllLoadedData));
    QCOMPARE(progress->property("progressColor").value<QColor>(), QColor("#e58a24"));
    QTRY_VERIFY(!controller.exporting());
    QCoreApplication::processEvents();
    QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join('\n')));
}

void AppControllerTest::xlsxWorkbookImportsAllWorksheets()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("worksheets.xlsx"));
    QVector<XlsxExportTable> tables;
    const QStringList names{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("17")};
    const int signalCounts[] = {25, 25, 30};
    int signalId = 0;
    for (int sheet = 0; sheet < names.size(); ++sheet) {
        XlsxExportTable table;
        table.name = names.at(sheet);
        for (int column = 0; column < signalCounts[sheet]; ++column) {
            auto data = std::make_shared<PlotSeriesData>();
            data->id = signalId++;
            for (int row = 0; row < sheet + 3; ++row) {
                data->time.append(sheet * 10.0 + row);
                data->values.append(column * 100.0 + row);
            }
            table.series.append({QStringLiteral("Signal%1").arg(column), data});
        }
        tables.append(table);
    }
    const auto written = writeXlsxWorkbook(path, tables);
    QVERIFY2(written.error.isEmpty(), qPrintable(written.error));
    QVERIFY(!written.cancelled);

    AppController controller;
    QVERIFY(controller.loadCsv(path));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 10000);
    QVERIFY2(controller.loadedFileCount() == 1, qPrintable(controller.status()));
    QCOMPARE(controller.signalCount(), 80);
    QVERIFY2(controller.status().contains(QStringLiteral("12 行")),
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
    const QByteArray csv("time,Pitch,Roll\n0,1,100\n10,2,200\n");
    QCOMPARE(file.write(csv), qint64(csv.size()));
    file.close();

    AppController controller;
    PlotItem plot;
    controller.attachPlot(&plot, 0);
    QSignalSpy loaded(&controller, &AppController::currentFileChanged);
    QVERIFY(controller.loadCsv(path));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000);

    controller.toggleSignal(0);
    QVERIFY(plot.xMinimum() < 0.0);
    QVERIFY(plot.xMaximum() > 10.0);
    QVERIFY(plot.yMinimum() < 1.0);
    QVERIFY(plot.yMaximum() > 2.0);
    controller.removeLegendSignal(0, 0);
    QCOMPARE(plot.yMinimum(), 0.0);
    QCOMPARE(plot.yMaximum(), 1.0);

    plot.setXRange(0.0, 1.0);
    controller.toggleSignal(0);

    QCOMPARE(plot.xMinimum(), 0.0);
    QCOMPARE(plot.xMaximum(), 1.0);
    const double smallYMaximum = plot.yMaximum();
    controller.toggleSignal(1);
    QVERIFY(plot.yMaximum() > 100.0);
    QCOMPARE(plot.xMinimum(), 0.0);
    QCOMPARE(plot.xMaximum(), 1.0);
    controller.removeLegendSignal(0, 1);
    QCOMPARE(plot.yMaximum(), smallYMaximum);
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

void AppControllerTest::renamingSignalUpdatesModelLegendsAndExport()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("flight.csv"));
    const QString output = directory.filePath(QStringLiteral("renamed.xlsx"));
    writeCsvFile(input, "time,Pitch,Roll\n0,1,2\n1,3,4\n");

    AppController controller;
    QVERIFY(controller.loadCsv(input));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QSignalSpy revisions(&controller, &AppController::plotBindingsChanged);

    QVERIFY(!controller.renameSignal(0, QStringLiteral("   ")));
    QVERIFY(!controller.renameSignal(7, QStringLiteral("Nope")));
    QCOMPARE(controller.signalName(0), QStringLiteral("Pitch"));
    QCOMPARE(revisions.count(), 0);

    QVERIFY(controller.renameSignal(0, QStringLiteral("  俯仰角  ")));
    QCOMPARE(controller.signalName(0), QStringLiteral("俯仰角"));
    // Legends rebuild from the plot state revision, like pen edits.
    QCOMPARE(revisions.count(), 1);
    QVERIFY(controller.status().contains(QStringLiteral("俯仰角")));
    // Renaming to the same name is accepted without another rebuild.
    QVERIFY(controller.renameSignal(0, QStringLiteral("俯仰角")));
    QCOMPARE(revisions.count(), 1);

    SignalModel *model = controller.signalModel();
    const int modelRow = model->revealSignal(0);
    QVERIFY(modelRow >= 0);
    QCOMPARE(model->data(model->index(modelRow), SignalModel::NameRole).toString(),
             QStringLiteral("俯仰角"));
    QCOMPARE(model->data(model->index(modelRow), SignalModel::IndexRole).toInt(), 0);

    // The search filter follows the new name.
    controller.filterSignals(QStringLiteral("Pitch"));
    QCOMPARE(model->revealSignal(0) >= 0, true);
    controller.filterSignals(QStringLiteral("俯仰"));
    QCOMPARE(model->rowCount(), 2);
    controller.filterSignals(QString());

    QVERIFY(controller.exportXlsx(output, AppController::AllLoadedData));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);
    const XlsxReadResult read = readXlsxWorkbook(output, {}, {});
    QVERIFY2(read.error.isEmpty(), qPrintable(read.error));
    QCOMPARE(read.tables.first().signalNames,
             QStringList({QStringLiteral("俯仰角"), QStringLiteral("Roll")}));
}

void AppControllerTest::matExportMirrorsImportLayout()
{
#ifdef ENABLE_MAT
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath(QStringLiteral("first.csv"));
    const QString second = directory.filePath(QStringLiteral("second.csv"));
    const QString output = directory.filePath(QStringLiteral("导出结果.mat"));
    writeCsvFile(first, "time,A,B\n0,1,2\n1,abc,4\n");
    writeCsvFile(second, "time,C\n10,5\n11,6\n");

    AppController controller;
    QVERIFY(controller.matExportSupported());
    QCOMPARE(controller.loadFiles(QVariantList{first, second}), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QVERIFY(controller.renameSignal(1, QStringLiteral("B 俯仰角")));
    QVERIFY(controller.exportMat(output, AppController::AllLoadedData));
    QVERIFY(controller.exporting());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);
    QCOMPARE(controller.exportProgress(), 100);
    QVERIFY2(controller.status().startsWith(QStringLiteral("已导出 MAT")),
             qPrintable(controller.status()));

    // Raw layout: uncompressed double pN with time first, UTF-8 pN_title
    // with one row per column, exactly what the loader expects.
    mat_t *file = Mat_Open(output.toUtf8().constData(), MAT_ACC_RDONLY);
    QVERIFY(file);
    matvar_t *p1 = Mat_VarRead(file, "p1");
    QVERIFY(p1);
    QCOMPARE(int(p1->class_type), int(MAT_C_DOUBLE));
    QCOMPARE(int(p1->data_type), int(MAT_T_DOUBLE));
    QCOMPARE(p1->rank, 2);
    QCOMPARE(int(p1->dims[0]), 2);
    QCOMPARE(int(p1->dims[1]), 3);
    const auto *values = static_cast<const double *>(p1->data);
    QCOMPARE(values[0], 0.0);
    QCOMPARE(values[1], 1.0);
    QCOMPARE(values[2], 1.0);
    QVERIFY(qIsNaN(values[3]));
    QCOMPARE(values[4], 2.0);
    QCOMPARE(values[5], 4.0);
    Mat_VarFree(p1);
    matvar_t *title = Mat_VarRead(file, "p1_title");
    QVERIFY(title);
    QCOMPARE(int(title->class_type), int(MAT_C_CHAR));
    QCOMPARE(int(title->data_type), int(MAT_T_UTF8));
    QCOMPARE(int(title->dims[0]), 3);
    QCOMPARE(int(title->dims[1]), int(QStringLiteral("B 俯仰角").size()));
    Mat_VarFree(title);
    matvar_t *p2 = Mat_VarReadInfo(file, "p2");
    QVERIFY(p2);
    QCOMPARE(int(p2->dims[0]), 2);
    QCOMPARE(int(p2->dims[1]), 2);
    Mat_VarFree(p2);
    QVERIFY(!Mat_VarReadInfo(file, "p3"));
    Mat_Close(file);

    // Round trip through the application loader.
    AppController reader;
    QVERIFY(reader.loadCsv(output));
    QTRY_VERIFY_WITH_TIMEOUT(!reader.loading(), 5000);
    QVERIFY2(reader.loadedFileCount() == 1, qPrintable(reader.status()));
    QCOMPARE(reader.signalCount(), 3);
    QCOMPARE(reader.signalName(0), QStringLiteral("A"));
    QCOMPARE(reader.signalName(1), QStringLiteral("B 俯仰角"));
    QCOMPARE(reader.signalName(2), QStringLiteral("C"));
    QCOMPARE(reader.signalModel()->groupAt(0), QStringLiteral("导出结果.mat/p1"));
    QCOMPARE(reader.signalModel()->groupAt(2), QStringLiteral("导出结果.mat/p2"));
#else
    QSKIP("MAT support disabled");
#endif
}

void AppControllerTest::matExportKeepsSourceTableNumbers()
{
#ifdef ENABLE_MAT
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("source.mat"));
    const QString csv = directory.filePath(QStringLiteral("extra.csv"));
    const QString output = directory.filePath(QStringLiteral("again"));
    writeCsvFile(csv, "time,X\n0,1\n");

    mat_t *file = Mat_CreateVer(input.toUtf8().constData(), nullptr, MAT_FT_MAT5);
    QVERIFY(file);
    size_t dimensions[] = {2, 2};
    double p3Values[] = {0, 1, 12, 34};
    double p1Values[] = {5, 6, 78, 90};
    for (const auto &entry : {std::make_pair("p3", p3Values), std::make_pair("p1", p1Values)}) {
        matvar_t *variable = Mat_VarCreate(entry.first, MAT_C_DOUBLE, MAT_T_DOUBLE, 2,
                                           dimensions, entry.second, 0);
        QVERIFY(variable);
        QCOMPARE(Mat_VarWrite(file, variable, MAT_COMPRESSION_ZLIB), 0);
        Mat_VarFree(variable);
    }
    Mat_Close(file);

    AppController controller;
    QCOMPARE(controller.loadFiles(QVariantList{input, csv}), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QCOMPARE(controller.signalCount(), 3);
    QVERIFY(controller.exportMat(output, AppController::AllLoadedData));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);

    // p1/p3 keep their numbers, the CSV table takes the first free slot (p2),
    // and a missing suffix is appended.
    mat_t *exported = Mat_Open((output + QStringLiteral(".mat")).toUtf8().constData(),
                               MAT_ACC_RDONLY);
    QVERIFY(exported);
    for (const char *name : {"p1", "p2", "p3", "p1_title", "p2_title", "p3_title"}) {
        matvar_t *variable = Mat_VarReadInfo(exported, name);
        QVERIFY2(variable, name);
        Mat_VarFree(variable);
    }
    QVERIFY(!Mat_VarReadInfo(exported, "p4"));
    matvar_t *p3 = Mat_VarRead(exported, "p3");
    QVERIFY(p3);
    QCOMPARE(static_cast<const double *>(p3->data)[2], 12.0);
    Mat_VarFree(p3);
    matvar_t *p2 = Mat_VarRead(exported, "p2");
    QVERIFY(p2);
    QCOMPARE(int(p2->dims[0]), 1);
    QCOMPARE(static_cast<const double *>(p2->data)[1], 1.0);
    Mat_VarFree(p2);
    Mat_Close(exported);
#else
    QSKIP("MAT support disabled");
#endif
}

void AppControllerTest::timeOffsetSnapshotsRemainImmutable()
{
    PlotSeriesStore store;
    PlotSeriesInput input;
    input.id = 0;
    input.time = {0, 1, 2};
    input.values = {4, 5, 6};
    store.replaceSeries({input});
    const auto original = store.snapshot({0});
    QVERIFY(store.addTimeOffset({0}, 2.5));
    const auto shifted = store.snapshot({0});
    QCOMPARE(original.series.first()->pointAt(0).x(), 0.0);
    QCOMPARE(shifted.series.first()->pointAt(0).x(), 2.5);
    QCOMPARE(PlotSeriesStore::nearestSamples(shifted, 3.4).first().x, 3.5);
    QCOMPARE(PlotSeriesStore::nearestSamples(shifted, 3.4).first().y, 5.0);
    QCOMPARE(PlotSeriesStore::timeBounds(shifted)->second, 4.5);
    QVERIFY(!store.addTimeOffset({0}, std::numeric_limits<double>::infinity()));
    QVERIFY(store.addTimeOffset({0}, -2.5));
    QCOMPARE(store.snapshot({0}).series.first()->pointAt(0).x(), 0.0);
    QCOMPARE(shifted.series.first()->pointAt(0).x(), 2.5);
}

void AppControllerTest::timeOffsetsApplyToSignalsGroupsAndFiles()
{
    QTemporaryDir directory;
    const QString input = directory.filePath(QStringLiteral("flight.csv"));
    const QString other = directory.filePath(QStringLiteral("other.csv"));
    writeCsvFile(input, "time,A,B\n0,1,2\n1,3,4\n");
    writeCsvFile(other, "time,C\n0,5\n1,6\n");
    AppController controller;
    QCOMPARE(controller.loadFiles(QVariantList{input, other}), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QVERIFY(!controller.addTimeOffset(0, -1, {}, 1));
    QVERIFY(!controller.addTimeOffset(3, 0, {}, 1));
    QVERIFY(!controller.addTimeOffset(1, -1, QStringLiteral("missing"), 1));
    QVERIFY(controller.addTimeOffset(0, 0, {}, 2.5));
    controller.filterSignals(QStringLiteral("A"));
    QVERIFY(controller.addTimeOffset(1, -1, controller.signalModel()->groupAt(0), -1));
    QCOMPARE(controller.signalTimeOffset(0), 1.5);
    QCOMPARE(controller.signalTimeOffset(1), -1.0);
    QVERIFY(controller.addTimeOffset(2, -1, QStringLiteral("flight.csv"), 3));
    QCOMPARE(controller.signalTimeOffset(0), 4.5);
    QCOMPARE(controller.signalTimeOffset(1), 2.0);
    QCOMPARE(controller.signalTimeOffset(2), 0.0);

    const QString output = directory.filePath(QStringLiteral("shifted.xlsx"));
    QVERIFY(controller.exportXlsx(output, AppController::AllLoadedData));
    QVERIFY(!controller.addTimeOffset(0, 0, {}, 1));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);
    const auto read = readXlsxWorkbook(output, {}, {});
    QVERIFY2(read.error.isEmpty(), qPrintable(read.error));
    QCOMPARE(read.tables.size(), 3);
    QHash<QString, double> expected{{"A", 4.5}, {"B", 2.0}, {"C", 0.0}};
    for (const auto &table : read.tables) {
        QCOMPARE(table.signalNames.size(), 1);
        QCOMPARE(table.time.first(), expected.value(table.signalNames.first()));
    }
#ifdef ENABLE_MAT
    const QString matPath = directory.filePath(QStringLiteral("shifted.mat"));
    QVERIFY(controller.exportMat(matPath, AppController::AllLoadedData));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.exporting(), 5000);
    AppController reloaded;
    QVERIFY(reloaded.loadCsv(matPath));
    QTRY_VERIFY_WITH_TIMEOUT(!reloaded.loading(), 5000);
    QCOMPARE(reloaded.signalCount(), 3);
    const QString roundTrip = directory.filePath(QStringLiteral("roundtrip.xlsx"));
    QVERIFY(reloaded.exportXlsx(roundTrip, AppController::AllLoadedData));
    QTRY_VERIFY_WITH_TIMEOUT(!reloaded.exporting(), 5000);
    const auto matRead = readXlsxWorkbook(roundTrip, {}, {});
    QVERIFY2(matRead.error.isEmpty(), qPrintable(matRead.error));
    QCOMPARE(matRead.tables.size(), 3);
    for (const auto &table : matRead.tables)
        QCOMPARE(table.time.first(), expected.value(table.signalNames.first()));
#endif
}

void AppControllerTest::editDialogsRememberAndResetValues()
{
    qmlRegisterTypesAndRevisions<TrajectoryItemQmlRegistration>("DataInspector", 1);
    qmlRegisterType<PlotItem>("DataInspector", 1, 0, "PlotItem");
    qmlRegisterUncreatableType<AppController>("DataInspector", 1, 0, "AppController", "Owned by C++");
    qmlRegisterUncreatableType<SignalModel>("DataInspector", 1, 0, "SignalModel", "Owned by AppController");
    QTemporaryDir directory;
    const QString input = directory.filePath("original.csv");
    writeCsvFile(input, "time,A,B\n0,1,2\n1,3,4\n");
    AppController controller;
    QVERIFY(controller.loadCsv(input));
    QTRY_VERIFY(!controller.loading());
    QQmlEngine engine;
    const QDir qmlDirectory = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../qml");
    qmlRegisterType(QUrl::fromLocalFile(qmlDirectory.filePath("QuickPlot.qml")), "DataInspector", 1, 0, "QuickPlot");
    QQmlComponent component(&engine, QUrl::fromLocalFile(qmlDirectory.filePath("Main.qml")));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"visible", false}, {"appController", QVariant::fromValue(&controller)}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *offsetDialog = object->findChild<QObject *>("timeOffsetDialog");
    auto *offsetField = object->findChild<QObject *>("timeOffsetField");
    auto *offsetReset = object->findChild<QObject *>("resetTimeOffsetButton");
    auto *nameDialog = object->findChild<QObject *>("renameSignalDialog");
    auto *nameField = object->findChild<QObject *>("renameSignalField");
    auto *nameReset = object->findChild<QObject *>("resetSignalNameButton");
    QVERIFY(offsetDialog && offsetField && offsetReset && nameDialog && nameField && nameReset);
    auto openOffset = [&](int scope, int row, const QString &group) {
        return QMetaObject::invokeMethod(object.get(), "requestTimeOffset",
            Q_ARG(QVariant, scope), Q_ARG(QVariant, row), Q_ARG(QVariant, group));
    };
    QVERIFY(controller.setTimeOffset(0, 0, {}, 2.5));
    QVERIFY(openOffset(0, 0, {}));
    QCOMPARE(offsetField->property("text").toString(), "2.5");
    QVERIFY(QMetaObject::invokeMethod(offsetDialog, "accept"));
    QCOMPARE(controller.signalTimeOffset(0), 2.5); // Reopening and confirming must not accumulate.
    QVERIFY(openOffset(0, 0, {}));
    QVERIFY(offsetField->setProperty("text", "-3.25"));
    QVERIFY(QMetaObject::invokeMethod(offsetDialog, "accept"));
    QCOMPARE(controller.signalTimeOffset(0), -3.25);
    QVERIFY(openOffset(2, -1, "original.csv"));
    QVERIFY(offsetDialog->property("mixedOffsets").toBool());
    QCOMPARE(offsetField->property("text").toString(), "");
    QVERIFY(QMetaObject::invokeMethod(offsetReset, "clicked"));
    QVERIFY(QMetaObject::invokeMethod(offsetDialog, "accept"));
    QCOMPARE(controller.signalTimeOffset(0), 0.0);
    QCOMPARE(controller.signalTimeOffset(1), 0.0);
    QVERIFY(controller.setTimeOffset(2, -1, "original.csv", 8));
    QVERIFY(openOffset(1, -1, controller.signalModel()->groupAt(0)));
    QCOMPARE(offsetField->property("text").toString(), "8");
    QVERIFY(QMetaObject::invokeMethod(offsetDialog, "reject"));
    QCOMPARE(controller.signalTimeOffset(0), 8.0);
    QVERIFY(controller.renameSignal(0, "Changed"));
    QVERIFY(QMetaObject::invokeMethod(object.get(), "requestRenameSignal", Q_ARG(QVariant, 0)));
    QCOMPARE(nameField->property("text").toString(), "Changed");
    QVERIFY(QMetaObject::invokeMethod(nameReset, "clicked"));
    QCOMPARE(nameField->property("text").toString(), "A");
    QVERIFY(QMetaObject::invokeMethod(nameDialog, "accept"));
    QCOMPARE(controller.signalName(0), "A");

    SignalModel model;
    model.setNames(QStringList{"First"}, QStringList{"one.csv"});
    model.appendNames({"Second"}, {"two.csv"});
    QVERIFY(model.renameSignal(1, "Edited"));
    model.removeFile("one.csv");
    QCOMPARE(model.originalNameAt(0), "Second");
    model.setNames({"Fresh"});
    QCOMPARE(model.originalNameAt(0), "Fresh");
}

void AppControllerTest::axisTicksHandleLargeAndTinyRanges()
{
    PlotItem plot;
    plot.setHeight(240);
    for (double scale : {1.0, 1e-18}) {
        plot.setYRange(-.31 * scale, .31 * scale);
        bool foundZero = false;
        for (const auto &entry : plot.yTicks()) {
            const auto tick = entry.toMap();
            const double value = tick.value("value").toDouble();
            if (qAbs(value) < .01 * scale) {
                QCOMPARE(value, 0.0);
                QCOMPARE(tick.value("label").toString(), QStringLiteral("0"));
                foundZero = true;
            }
        }
        QVERIFY(foundZero);
    }
    const double tiny = std::numeric_limits<double>::denorm_min();
    const QVector<QPair<double, double>> ranges{
        {1700000000.0, 1700000001.0},
        {1700000000.0, 1700000000.00002},
        {-1700000001.0, -1700000000.0},
        {1e20, 1e20 + 1e6}, {0.0, 8 * tiny}, {0.0, 1.0}
    };
    for (const auto &range : ranges) {
        plot.setXRange(range.first, range.second);
        plot.setYRange(range.first, range.second);
        for (const auto &ticks : {plot.xTicks(), plot.yTicks()}) {
            QVERIFY(ticks.size() >= 2 && ticks.size() <= 12);
            QSet<QString> labels;
            double previous = -std::numeric_limits<double>::infinity();
            for (const auto &entry : ticks) {
                const auto tick = entry.toMap();
                const double value = tick.value("value").toDouble();
                QVERIFY(qIsFinite(value));
                QVERIFY2(value >= range.first && value <= range.second,
                         qPrintable(QString::number(value, 'g', 17)));
                QVERIFY(value > previous);
                previous = value;
                const auto label = tick.value("label").toString();
                QVERIFY(!labels.contains(label)); labels.insert(label);
            }
        }
    }
}

void AppControllerTest::unchangedPensDoNotInvalidatePlots()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("pen.csv");
    writeCsvFile(path, "time,A\n0,1\n1,2\n");
    AppController controller;
    PlotItem plot; controller.attachPlot(&plot);
    QVERIFY(controller.loadCsv(path));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    controller.selectSignal(0);
    QSignalSpy changed(&controller, &AppController::plotBindingsChanged);
    const auto color = controller.signalColor(0);
    for (int i = 0; i < 10; ++i) controller.setSignalPen(0, color, 2, Qt::SolidLine);
    QCOMPARE(changed.size(), 0);
    controller.setSignalPen(0, color, 3, Qt::SolidLine);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(controller.signalWidth(0), 3.0);
}

QTEST_MAIN(AppControllerTest)
#include "appcontroller_test.moc"
