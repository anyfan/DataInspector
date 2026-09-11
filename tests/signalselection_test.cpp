#include "signalmetadata.h"
#include "signalmodel.h"

#include <QtTest>

class SignalSelectionTest final : public QObject
{
    Q_OBJECT

private slots:
    void matTitlesIgnoreLeadingTimeColumn();
    void tableGroupMatchesLegacyTreeStructure();
    void filteredRowsKeepDisplayNameGroupAndSourceIndex();
    void qmlRolesHaveUnambiguousNames();
    void checksFollowTheActivePlot();
    void resizingLayoutPreservesExistingPlotBindings();
    void hiddenCurveRemainsInThePlotLegend();
    void groupsCanExpandAndCollapseWithoutLosingSignalRows();
    void eachSignalKeepsIndependentPenProperties();
};

void SignalSelectionTest::matTitlesIgnoreLeadingTimeColumn()
{
    const QStringList names = composeMatSignalNames(
        {QStringLiteral("time"), QStringLiteral("Pitch"), QStringLiteral("Roll")},
        {QStringLiteral("unit"), QStringLiteral("deg"), QStringLiteral("deg/s")},
        2,
        QStringLiteral("p7"));

    QCOMPARE(names, QStringList({QStringLiteral("deg Pitch"),
                                 QStringLiteral("deg/s Roll")}));
}

void SignalSelectionTest::tableGroupMatchesLegacyTreeStructure()
{
    QCOMPARE(signalTableGroup(QStringLiteral("flight"), QStringLiteral("flight"), 1),
             QString());
    QCOMPARE(signalTableGroup(QStringLiteral("flight"), QStringLiteral("p1"), 1),
             QStringLiteral("p1"));
    QCOMPARE(signalTableGroup(QStringLiteral("flight"), QStringLiteral("p2"), 2),
             QStringLiteral("p2"));
}

void SignalSelectionTest::filteredRowsKeepDisplayNameGroupAndSourceIndex()
{
    SignalModel model;
    model.setNames({QStringLiteral("Pitch"), QStringLiteral("Roll")},
                   {QStringLiteral("p1"), QStringLiteral("p2")},
                   {QColor("red"), QColor("blue")});

    model.setFilter(QStringLiteral("Roll"));

    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.data(model.index(0), SignalModel::GroupNodeRole).toBool());
    QCOMPARE(model.data(model.index(1), SignalModel::NameRole).toString(),
             QStringLiteral("Roll"));
    QCOMPARE(model.data(model.index(1), SignalModel::GroupRole).toString(),
             QStringLiteral("p2"));
    QCOMPARE(model.data(model.index(1), SignalModel::IndexRole).toInt(), 1);
    QCOMPARE(model.data(model.index(1), SignalModel::ColorRole).value<QColor>(),
             QColor("blue"));
}

void SignalSelectionTest::qmlRolesHaveUnambiguousNames()
{
    const QHash<int, QByteArray> roles = SignalModel().roleNames();
    QCOMPARE(roles.value(SignalModel::NameRole), QByteArray("signalName"));
    QCOMPARE(roles.value(SignalModel::CheckedRole), QByteArray("signalChecked"));
    QCOMPARE(roles.value(SignalModel::ColorRole), QByteArray("signalColor"));
    QCOMPARE(roles.value(SignalModel::GroupRole), QByteArray("groupName"));
    QCOMPARE(roles.value(SignalModel::IndexRole), QByteArray("signalIndex"));
}

void SignalSelectionTest::checksFollowTheActivePlot()
{
    SignalModel model;
    model.setNames({QStringLiteral("Pitch"), QStringLiteral("Roll")});
    model.setPlotCount(2);

    model.setActivePlot(0);
    model.setChecked(0, true);
    QVERIFY(model.data(model.index(0), SignalModel::CheckedRole).toBool());
    QVERIFY(!model.data(model.index(1), SignalModel::CheckedRole).toBool());

    model.setActivePlot(1);
    QVERIFY(!model.data(model.index(0), SignalModel::CheckedRole).toBool());
    model.setChecked(1, true);

    QCOMPARE(model.plotRows(0), QVector<int>({0}));
    QCOMPARE(model.plotRows(1), QVector<int>({1}));

    model.setActivePlot(0);
    QVERIFY(model.data(model.index(0), SignalModel::CheckedRole).toBool());
    QVERIFY(!model.data(model.index(1), SignalModel::CheckedRole).toBool());
}

void SignalSelectionTest::resizingLayoutPreservesExistingPlotBindings()
{
    SignalModel model;
    model.setNames({QStringLiteral("A"), QStringLiteral("B")});
    model.setPlotCount(2);
    model.setActivePlot(0);
    model.setChecked(0, true);
    model.setActivePlot(1);
    model.setChecked(1, true);

    model.setPlotCount(1);
    model.setPlotCount(4);

    QCOMPARE(model.plotRows(0), QVector<int>({0}));
    QCOMPARE(model.plotRows(1), QVector<int>({1}));
    QVERIFY(model.plotRows(2).isEmpty());
    QVERIFY(model.plotRows(3).isEmpty());
}

void SignalSelectionTest::hiddenCurveRemainsInThePlotLegend()
{
    SignalModel model;
    model.setNames({QStringLiteral("Pitch")});
    model.setPlotCount(1);
    model.setChecked(0, true);

    model.setPlotSignalVisible(0, 0, false);

    QCOMPARE(model.plotRows(0), QVector<int>({0}));
    QVERIFY(model.visiblePlotRows(0).isEmpty());
    QVERIFY(!model.plotSignalVisible(0, 0));
}

void SignalSelectionTest::groupsCanExpandAndCollapseWithoutLosingSignalRows()
{
    SignalModel model;
    model.setNames(QStringList{QStringLiteral("Pitch"), QStringLiteral("Roll"),
                               QStringLiteral("Altitude")},
                   QStringList{QStringLiteral("p1"), QStringLiteral("p1"),
                               QStringLiteral("p2")});

    QCOMPARE(model.rowCount(), 5);
    QVERIFY(model.data(model.index(0), SignalModel::GroupNodeRole).toBool());
    QCOMPARE(model.data(model.index(0), SignalModel::GroupRole).toString(),
             QStringLiteral("p1"));
    QVERIFY(model.data(model.index(0), SignalModel::ExpandedRole).toBool());
    QVERIFY(!model.data(model.index(1), SignalModel::GroupNodeRole).toBool());
    QCOMPARE(model.data(model.index(1), SignalModel::IndexRole).toInt(), 0);
    QCOMPARE(model.data(model.index(1), SignalModel::DepthRole).toInt(), 1);

    model.toggleGroup(QStringLiteral("p1"));

    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.data(model.index(0), SignalModel::GroupNodeRole).toBool());
    QVERIFY(!model.data(model.index(0), SignalModel::ExpandedRole).toBool());
    QCOMPARE(model.data(model.index(1), SignalModel::GroupRole).toString(),
             QStringLiteral("p2"));
    QCOMPARE(model.data(model.index(2), SignalModel::IndexRole).toInt(), 2);

    model.toggleGroup(QStringLiteral("p1"));
    QCOMPARE(model.rowCount(), 5);
    QCOMPARE(model.data(model.index(2), SignalModel::IndexRole).toInt(), 1);
}

void SignalSelectionTest::eachSignalKeepsIndependentPenProperties()
{
    SignalModel model;
    model.setNames({QStringLiteral("Pitch"), QStringLiteral("Roll")},
                   {}, {QColor("red"), QColor("blue")});

    model.setSignalPen(0, QColor("green"), 6.0, Qt::DashLine);

    QCOMPARE(model.signalColor(0), QColor("green"));
    QCOMPARE(model.signalWidth(0), 6.0);
    QCOMPARE(model.signalStyle(0), Qt::DashLine);
    QCOMPARE(model.signalColor(1), QColor("blue"));
    QCOMPARE(model.signalWidth(1), 1.0);
    QCOMPARE(model.signalStyle(1), Qt::SolidLine);
}

QTEST_GUILESS_MAIN(SignalSelectionTest)
#include "signalselection_test.moc"
