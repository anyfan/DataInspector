#include "matwriter.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#ifdef ENABLE_MAT
#include "matio.h"
#endif

class MatWriterTest final : public QObject
{
    Q_OBJECT
private slots:
    void chunksRoundTripAndReportProgress();
    void cancellationPreservesDestination_data();
    void cancellationPreservesDestination();
    void rejectsMismatchedTimes();
};

static QVector<DataExportTable> makeTables(qsizetype rows)
{
    auto data = std::make_shared<PlotSeriesData>();
    data->time.resize(rows);
    data->values.resize(rows);
    data->timeOffset = 2.5;
    for (qsizetype row = 0; row < rows; ++row) {
        data->time[row] = row * 0.25;
        data->values[row] = row == 9000 ? qQNaN() : double(row * 3);
    }
    return {{QStringLiteral("file/p7"), {{QStringLiteral("俯仰角"), data},
                                         {QStringLiteral("B"), data}}}};
}

void MatWriterTest::chunksRoundTripAndReportProgress()
{
#ifndef ENABLE_MAT
    QSKIP("MAT disabled");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("stream.mat"));
    QVector<int> progress;
    const auto result = writeMatFile(path, makeTables(20123),
        [&](int value) { progress.append(value); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(!result.cancelled);
    QCOMPARE(progress.first(), 0);
    QCOMPARE(progress.last(), 100);
    QVERIFY(progress.size() > 5);
    for (qsizetype i = 1; i < progress.size(); ++i) QVERIFY(progress[i] > progress[i-1]);
    mat_t *file = Mat_Open(path.toUtf8().constData(), MAT_ACC_RDONLY);
    QVERIFY(file);
    matvar_t *matrix = Mat_VarRead(file, "p7");
    QVERIFY(matrix);
    QCOMPARE(matrix->dims[0], size_t(20123));
    QCOMPARE(matrix->dims[1], size_t(3));
    const auto *values = static_cast<const double *>(matrix->data);
    for (int row : {0, 8191, 8192, 16384, 20122}) {
        QCOMPARE(values[row], row * 0.25 + 2.5);
        QCOMPARE(values[20123 + row], double(row * 3));
        QCOMPARE(values[40246 + row], double(row * 3));
    }
    QVERIFY(qIsNaN(values[20123 + 9000]));
    Mat_VarFree(matrix);
    matvar_t *titles = Mat_VarRead(file, "p7_title");
    QVERIFY(titles);
    QCOMPARE(titles->class_type, MAT_C_CHAR);
    QCOMPARE(titles->dims[0], size_t(3));
    QCOMPARE(titles->dims[1], size_t(4));
    Mat_VarFree(titles);
    Mat_Close(file);
#endif
}

void MatWriterTest::cancellationPreservesDestination_data()
{
    QTest::addColumn<int>("threshold");
    QTest::newRow("before-writing") << 0;
    QTest::newRow("during-first-matrix") << 10;
    QTest::newRow("before-commit") << 99;
}

void MatWriterTest::cancellationPreservesDestination()
{
#ifndef ENABLE_MAT
    QSKIP("MAT disabled");
#else
    QFETCH(int, threshold);
    QTemporaryDir dir;
    const QString path = dir.filePath("existing.mat");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("original"), qint64(8));
    file.close();
    int progress = -1;
    const auto result = writeMatFile(path, makeTables(100000),
        [&](int value) { progress = value; }, [&] { return progress >= threshold; });
    QVERIFY(result.cancelled);
    QVERIFY(result.error.isEmpty());
    QVERIFY(progress < 100);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("original"));
    // QSaveFile's temporary artifact must not remain after cancellation.
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 1);
#endif
}

void MatWriterTest::rejectsMismatchedTimes()
{
#ifndef ENABLE_MAT
    QSKIP("MAT disabled");
#else
    QTemporaryDir dir;
    auto tables = makeTables(10);
    auto other = std::make_shared<PlotSeriesData>(*tables.first().series.first().data);
    other->timeOffset = 3.0;
    tables.first().series.last().data = other;
    const QString path = dir.filePath("invalid.mat");
    const auto result = writeMatFile(path, tables);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!QFile::exists(path));
#endif
}

QTEST_GUILESS_MAIN(MatWriterTest)
#include "matwriter_test.moc"
