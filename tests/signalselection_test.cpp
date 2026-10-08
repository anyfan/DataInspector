#include "signalmetadata.h"
#include "signalmodel.h"

#include <QtTest>
#include <QAbstractItemModelTester>

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
    void groupsCanExpandAndCollapseWithoutLosingSignalRows();
    void eachSignalKeepsIndependentPenProperties();
    void appendingSignalsPreservesBindingsAndPenProperties();
    void nestedFileAndTableGroupsExpandIndependently();
    void ancestorPathReportsVisibleGroupChain();
    void groupChangesKeepPersistentRowsWithoutReset();
    void removingFileGroupReindexesPlotBindings();
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
    QCOMPARE(model.signalWidth(1), 2.0);
    QCOMPARE(model.signalStyle(1), Qt::SolidLine);
}

void SignalSelectionTest::appendingSignalsPreservesBindingsAndPenProperties()
{
    SignalModel model;
    model.setNames({QStringLiteral("Pitch")},
                   {QStringLiteral("flight.csv")}, {QColor("red")});
    model.setPlotCount(1);
    model.setChecked(0, true);
    model.setSignalPen(0, QColor("green"), 4.0, Qt::DashLine);

    model.appendNames({QStringLiteral("Roll")},
                      {QStringLiteral("second.csv")}, {QColor("blue")});

    QCOMPARE(model.sourceCount(), 2);
    QCOMPARE(model.plotRows(0), QVector<int>({0}));
    QCOMPARE(model.signalColor(0), QColor("green"));
    QCOMPARE(model.signalWidth(0), 4.0);
    QCOMPARE(model.signalStyle(0), Qt::DashLine);
    QCOMPARE(model.nameAt(1), QStringLiteral("Roll"));
}

void SignalSelectionTest::nestedFileAndTableGroupsExpandIndependently()
{
    SignalModel model;
    model.setNames(QStringList{QStringLiteral("Pitch"), QStringLiteral("Roll")},
                   QStringList{QStringLiteral("flight.mat/p1"),
                               QStringLiteral("flight.mat/p2")});

    QCOMPARE(model.rowCount(), 5);
    QCOMPARE(model.data(model.index(0), SignalModel::NameRole).toString(),
             QStringLiteral("flight.mat"));
    QCOMPARE(model.data(model.index(0), SignalModel::DepthRole).toInt(), 0);
    QCOMPARE(model.data(model.index(1), SignalModel::NameRole).toString(),
             QStringLiteral("p1"));
    QCOMPARE(model.data(model.index(1), SignalModel::GroupRole).toString(),
             QStringLiteral("flight.mat/p1"));
    QCOMPARE(model.data(model.index(1), SignalModel::DepthRole).toInt(), 1);
    QCOMPARE(model.data(model.index(2), SignalModel::IndexRole).toInt(), 0);
    QCOMPARE(model.data(model.index(2), SignalModel::DepthRole).toInt(), 2);

    model.toggleGroup(QStringLiteral("flight.mat/p1"));
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.data(model.index(2), SignalModel::NameRole).toString(),
             QStringLiteral("p2"));

    model.toggleGroup(QStringLiteral("flight.mat"));
    QCOMPARE(model.rowCount(), 1);
}

void SignalSelectionTest::groupChangesKeepPersistentRowsWithoutReset()
{
    SignalModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setNames({"A", "B"}, QStringList{"file/p1/inner", "file/p2"});
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QPersistentModelIndex sibling(model.index(4));
    QCOMPARE(sibling.data(SignalModel::NameRole).toString(), QStringLiteral("p2"));
    model.toggleGroup("file/p1");
    QCOMPARE(model.rowCount(), 4); // All descendants, including still-expanded inner groups, disappear.
    QVERIFY(sibling.isValid());
    QCOMPARE(sibling.row(), 2);
    QCOMPARE(sibling.data(SignalModel::NameRole).toString(), QStringLiteral("p2"));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(reset.count(), 0);
    model.toggleGroup("file/p1");
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(sibling.row(), 4);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(reset.count(), 0);
    model.toggleGroup("file");
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(reset.count(), 0);
    model.toggleGroup("file");
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(model.ancestorPath(5).last().toMap().value("row").toInt(), 4);
}

void SignalSelectionTest::ancestorPathReportsVisibleGroupChain()
{
    SignalModel model;
    model.setNames(QStringList{QStringLiteral("Pitch"), QStringLiteral("Roll"),
                               QStringLiteral("Loose")},
                   QStringList{QStringLiteral("flight.mat/p1"),
                               QStringLiteral("flight.mat/p2"),
                               QString()});
    // Rows: 0 Loose, 1 flight.mat, 2 p1, 3 Pitch, 4 p2, 5 Roll.
    QCOMPARE(model.rowCount(), 6);
    QVERIFY(model.ancestorPath(0).isEmpty());
    QVERIFY(model.ancestorPath(1).isEmpty());
    QVERIFY(model.ancestorPath(-1).isEmpty());
    QVERIFY(model.ancestorPath(99).isEmpty());

    const QVariantList tablePath = model.ancestorPath(2);
    QCOMPARE(tablePath.size(), 1);
    QCOMPARE(tablePath.at(0).toMap().value("group").toString(), QStringLiteral("flight.mat"));
    QCOMPARE(tablePath.at(0).toMap().value("row").toInt(), 1);

    const QVariantList signalPath = model.ancestorPath(5);
    QCOMPARE(signalPath.size(), 2);
    QCOMPARE(signalPath.at(0).toMap().value("name").toString(), QStringLiteral("flight.mat"));
    QCOMPARE(signalPath.at(0).toMap().value("depth").toInt(), 0);
    QCOMPARE(signalPath.at(0).toMap().value("row").toInt(), 1);
    QCOMPARE(signalPath.at(1).toMap().value("name").toString(), QStringLiteral("p2"));
    QCOMPARE(signalPath.at(1).toMap().value("group").toString(), QStringLiteral("flight.mat/p2"));
    QCOMPARE(signalPath.at(1).toMap().value("depth").toInt(), 1);
    QCOMPARE(signalPath.at(1).toMap().value("row").toInt(), 4);
}

void SignalSelectionTest::removingFileGroupReindexesPlotBindings()
{
    SignalModel model;
    model.setNames(QStringList{QStringLiteral("Pitch"), QStringLiteral("Roll"),
                               QStringLiteral("Altitude")},
                   QStringList{QStringLiteral("first.csv"),
                               QStringLiteral("second.csv/p1"),
                               QStringLiteral("second.csv/p2")});
    model.setPlotCount(2);
    model.setPlotChecked(0, 0, true);
    model.setPlotChecked(0, 1, true);
    model.setPlotChecked(1, 2, true);

    QCOMPARE(model.removeFile(QStringLiteral("first.csv")), QVector<int>({0}));

    QCOMPARE(model.sourceCount(), 2);
    QCOMPARE(model.nameAt(0), QStringLiteral("Roll"));
    QCOMPARE(model.nameAt(1), QStringLiteral("Altitude"));
    QCOMPARE(model.plotRows(0), QVector<int>({0}));
    QCOMPARE(model.plotRows(1), QVector<int>({1}));
    QVERIFY(model.data(model.index(0), SignalModel::FileNodeRole).toBool());
}

QTEST_GUILESS_MAIN(SignalSelectionTest)
#include "signalselection_test.moc"
