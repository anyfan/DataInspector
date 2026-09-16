#include "xlsxreader.h"
#include "xlsxwriter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <QtMath>

static PlotSeriesDataPtr series(int id, QVector<double> time,
                                QVector<double> values);

class XlsxWriterTest final : public QObject
{
    Q_OBJECT

private slots:
    void roundTripPreservesTablesAndBlankValues();
    void splitsRowsAndMakesSheetNamesUnique();
    void cancellationPreservesExistingFile();
};

static PlotSeriesDataPtr series(int id, QVector<double> time,
                                QVector<double> values)
{
    auto data = std::make_shared<PlotSeriesData>();
    data->id = id;
    data->time = std::move(time);
    data->values = std::move(values);
    return data;
}

void XlsxWriterTest::roundTripPreservesTablesAndBlankValues()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("export.xlsx"));
    const QVector<XlsxExportTable> tables = {
        {QStringLiteral("flight/[one]"), {
            {QStringLiteral("Pitch"), series(0, {0.0, 1.0}, {2.5, qQNaN()})},
            {QStringLiteral("Roll"), series(1, {0.0, 1.0}, {-3.0, 4.0})}
        }},
        {QStringLiteral("flight/[one]"), {
            {QStringLiteral("Yaw"), series(2, {10.0}, {8.0})}
        }}
    };

    const XlsxWriteResult written = writeXlsxWorkbook(path, tables);
    QVERIFY2(written.error.isEmpty(), qPrintable(written.error));
    QVERIFY(!written.cancelled);

    const XlsxReadResult read = readXlsxWorkbook(path, {}, {});
    QVERIFY2(read.error.isEmpty(), qPrintable(read.error));
    QCOMPARE(read.tables.size(), 2);
    QCOMPARE(read.tables.at(0).name, QStringLiteral("flight__one_"));
    QCOMPARE(read.tables.at(1).name, QStringLiteral("flight__one_ (2)"));
    QCOMPARE(read.tables.at(0).signalNames,
             QStringList({QStringLiteral("Pitch"), QStringLiteral("Roll")}));
    QCOMPARE(read.tables.at(0).time, QVector<double>({0.0, 1.0}));
    QCOMPARE(read.tables.at(0).values.at(0).at(0), 2.5);
    QVERIFY(qIsNaN(read.tables.at(0).values.at(0).at(1)));
    QCOMPARE(read.tables.at(0).values.at(1), QVector<double>({-3.0, 4.0}));
    QCOMPARE(read.tables.at(1).time, QVector<double>({10.0}));
    QCOMPARE(read.tables.at(1).values.at(0), QVector<double>({8.0}));
}

void XlsxWriterTest::splitsRowsAndMakesSheetNamesUnique()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("split.xlsx"));
    XlsxWriteOptions options;
    options.maxDataRowsPerSheet = 2;
    const QVector<XlsxExportTable> tables = {{
        QStringLiteral("Data"), {{QStringLiteral("A"),
            series(0, {0, 1, 2, 3, 4}, {10, 11, 12, 13, 14})}}
    }};

    const XlsxWriteResult written = writeXlsxWorkbook(path, tables, options);
    QVERIFY2(written.error.isEmpty(), qPrintable(written.error));
    const XlsxReadResult read = readXlsxWorkbook(path, {}, {});
    QVERIFY2(read.error.isEmpty(), qPrintable(read.error));
    QCOMPARE(read.tables.size(), 3);
    QCOMPARE(read.tables.at(0).name, QStringLiteral("Data"));
    QCOMPARE(read.tables.at(1).name, QStringLiteral("Data (2)"));
    QCOMPARE(read.tables.at(2).name, QStringLiteral("Data (3)"));
    QCOMPARE(read.tables.at(0).time, QVector<double>({0, 1}));
    QCOMPARE(read.tables.at(1).time, QVector<double>({2, 3}));
    QCOMPARE(read.tables.at(2).time, QVector<double>({4}));
}

void XlsxWriterTest::cancellationPreservesExistingFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("existing.xlsx"));
    QFile original(path);
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("original"), qint64(8));
    original.close();

    int cancellationChecks = 0;
    const QVector<XlsxExportTable> tables = {{
        QStringLiteral("Data"), {{QStringLiteral("A"),
            series(0, {0, 1, 2}, {3, 4, 5})}}
    }};
    const XlsxWriteResult written = writeXlsxWorkbook(
        path, tables, {}, {}, [&]() { return ++cancellationChecks >= 2; });
    QVERIFY(written.cancelled);
    QVERIFY(written.error.isEmpty());

    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("original"));
}

QTEST_GUILESS_MAIN(XlsxWriterTest)
#include "xlsxwriter_test.moc"
