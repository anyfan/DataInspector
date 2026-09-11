#include "appcontroller.h"
#include "plotitem.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class AppControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void loadedSignalsBindOnlyToTheActivePlot();
    void expandingLayoutKeepsExistingPlotAttached();
};

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
    QCOMPARE(controller.signalWidth(1), 1.0);
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
    QVERIFY(controller.signalModel()->data(controller.signalModel()->index(1),
                                            SignalModel::CheckedRole).toBool());
    QVERIFY(!controller.signalModel()->data(controller.signalModel()->index(0),
                                             SignalModel::CheckedRole).toBool());

    controller.togglePlotSignal(1, 1);
    QCOMPARE(controller.plotSignalRows(1), secondPlotRows);
    QVERIFY(!controller.plotSignalVisible(1, 1));

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

QTEST_MAIN(AppControllerTest)
#include "appcontroller_test.moc"
