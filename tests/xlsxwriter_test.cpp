#include "xlsxreader.h"
#include "xlsxwriter.h"

#include <QFile>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>
#include <QtMath>

#include <algorithm>

static PlotSeriesDataPtr series(int id, QVector<double> time,
                                QVector<double> values);

class XlsxWriterTest final : public QObject
{
    Q_OBJECT

private slots:
    void roundTripPreservesTablesAndBlankValues();
    void splitsRowsAndMakesSheetNamesUnique();
    void cancellationPreservesExistingFile();
    void progressIsMonotonicAcrossUnevenSheets();
    void zipCompressionIsOptIn();
    void largeExportPerformanceWhenRequested();
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

void XlsxWriterTest::progressIsMonotonicAcrossUnevenSheets()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVector<double> time(8192);
    QVector<double> values(8192);
    for (qsizetype row = 0; row < time.size(); ++row) {
        time[row] = double(row);
        values[row] = double(row) * 0.5;
    }
    const QVector<XlsxExportTable> tables = {
        {QStringLiteral("Small"), {{QStringLiteral("A"),
            series(0, {0.0}, {1.0})}}},
        {QStringLiteral("Large"), {{QStringLiteral("B"),
            series(1, time, values)}}}
    };
    for (const bool compressionEnabled : {false, true}) {
        QVector<int> progress;
        XlsxWriteOptions options;
        options.zipCompressionEnabled = compressionEnabled;
        const QString mode = compressionEnabled
            ? QStringLiteral("compressed") : QStringLiteral("stored");

        const XlsxWriteResult written = writeXlsxWorkbook(
            directory.filePath(QStringLiteral("progress-%1.xlsx").arg(mode)),
            tables, options,
            [&](int percentage) { progress.append(percentage); });

        QVERIFY2(written.error.isEmpty(), qPrintable(written.error));
        QVERIFY2(!progress.isEmpty(), qPrintable(mode));
        QCOMPARE(progress.last(), 100);
        QVERIFY2(std::is_sorted(progress.cbegin(), progress.cend()),
                 qPrintable(QStringLiteral("%1 模式进度发生回退：%2")
                                .arg(mode, QVariant::fromValue(progress).toString())));
        for (qsizetype index = 1; index < progress.size(); ++index) {
            QVERIFY2(progress.at(index) - progress.at(index - 1) <= 10,
                     qPrintable(QStringLiteral("%1 模式导出进度出现不合理跳变")
                                    .arg(mode)));
        }
    }
}

void XlsxWriterTest::zipCompressionIsOptIn()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVector<double> time(4096);
    QVector<double> values(4096, 1.25);
    for (qsizetype row = 0; row < time.size(); ++row)
        time[row] = double(row);
    const QVector<XlsxExportTable> tables = {{QStringLiteral("Data"), {
        {QStringLiteral("A"), series(0, time, values)},
        {QStringLiteral("B"), series(1, time, values)}
    }}};
    const QString storedPath = directory.filePath(QStringLiteral("stored.xlsx"));
    const QString compressedPath = directory.filePath(
        QStringLiteral("compressed.xlsx"));

    const XlsxWriteResult stored = writeXlsxWorkbook(storedPath, tables);
    XlsxWriteOptions compressedOptions;
    compressedOptions.zipCompressionEnabled = true;
    const XlsxWriteResult compressed = writeXlsxWorkbook(
        compressedPath, tables, compressedOptions);

    QVERIFY2(stored.error.isEmpty(), qPrintable(stored.error));
    QVERIFY2(compressed.error.isEmpty(), qPrintable(compressed.error));
    const XlsxReadResult storedRead = readXlsxWorkbook(storedPath, {}, {});
    const XlsxReadResult compressedRead = readXlsxWorkbook(compressedPath, {}, {});
    QVERIFY2(storedRead.error.isEmpty(), qPrintable(storedRead.error));
    QVERIFY2(compressedRead.error.isEmpty(), qPrintable(compressedRead.error));
    QCOMPARE(storedRead.tables.first().time, compressedRead.tables.first().time);
    QCOMPARE(storedRead.tables.first().values,
             compressedRead.tables.first().values);
    QVERIFY(QFileInfo(storedPath).size()
            > QFileInfo(compressedPath).size() * 2);
}

void XlsxWriterTest::largeExportPerformanceWhenRequested()
{
    if (qEnvironmentVariableIsEmpty("DATAINSPECTOR_PERF_XLSX"))
        QSKIP("Set DATAINSPECTOR_PERF_XLSX to run the XLSX export benchmark");
    const int rowCount = qMax(1, qEnvironmentVariableIntValue(
                                      "DATAINSPECTOR_PERF_XLSX_ROWS"));
    const int signalCount = qMax(1, qEnvironmentVariableIntValue(
                                         "DATAINSPECTOR_PERF_XLSX_SIGNALS"));
    QVector<double> time(rowCount);
    for (int row = 0; row < rowCount; ++row) time[row] = row * 0.01;
    QVector<XlsxExportSeries> exportSeries;
    exportSeries.reserve(signalCount);
    for (int signalIndex = 0; signalIndex < signalCount; ++signalIndex) {
        QVector<double> values(rowCount);
        for (int row = 0; row < rowCount; ++row)
            values[row] = row * 0.25 + signalIndex;
        exportSeries.append({QStringLiteral("Signal %1").arg(signalIndex + 1),
                             series(signalIndex, time, std::move(values))});
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("benchmark.xlsx"));
    QElapsedTimer timer;
    timer.start();
    XlsxWriteOptions options;
    options.zipCompressionEnabled = qEnvironmentVariableIntValue(
        "DATAINSPECTOR_PERF_XLSX_COMPRESS") != 0;
    const XlsxWriteResult result = writeXlsxWorkbook(
        path, {{QStringLiteral("Data"), std::move(exportSeries)}}, options);
    const qint64 elapsed = timer.elapsed();
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    qInfo() << "XLSX_EXPORT_MS" << elapsed << "ROWS" << rowCount
            << "SIGNALS" << signalCount << "COMPRESSED"
            << options.zipCompressionEnabled << "BYTES" << QFileInfo(path).size();
}

QTEST_GUILESS_MAIN(XlsxWriterTest)
#include "xlsxwriter_test.moc"
