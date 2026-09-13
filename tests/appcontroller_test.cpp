#include "appcontroller.h"
#include "plotitem.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <algorithm>

class AppControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void detachedPlotsDoNotSynchronize();
    void loadedSignalsBindOnlyToTheActivePlot();
    void expandingLayoutKeepsExistingPlotAttached();
    void multipleFilesAppendAndPreserveExistingBindings();
    void customLayoutSupportsLegacyEightByEightRange();
    void replacingAPlotDelegateRestoresItsCurves();
    void qmlUrlListImportsLocalFiles();
    void qmlJavaScriptArrayImportsLocalFiles();
    void qmlCanCallLegendModeSetter();
    void quotedCsvFieldsAreImported();
    void failedLoadReportsReason();
    void legendDoesNotToggleSignalVisibility();
    void realMatImportPerformanceWhenRequested();
    void addingSignalPreservesCurrentXRange();
    void attachingNewPlotPreservesSharedXRange();
    void removingFileKeepsRemainingSignalsAndBindings();
};

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

void AppControllerTest::qmlCanCallLegendModeSetter()
{
    AppController controller;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("testController"),
                                              &controller);
    QQmlComponent component(&engine);
    component.setData(QByteArrayLiteral(
        "import QtQml\n"
        "QtObject {\n"
        "  function submit() { testController.setLegendMode(2) }\n"
        "}\n"), QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(object.get(), "submit"));
    QCOMPARE(controller.legendMode(), 2);
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
    QCOMPARE(controller.loadedFileCount(), 1);
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
