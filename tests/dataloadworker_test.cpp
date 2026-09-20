#include "dataloadworker.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#ifdef ENABLE_MAT
#include "matio.h"
#endif

class DataLoadWorkerTest final : public QObject
{
    Q_OBJECT
private slots:
    void csvRejectsMalformedQuotes();
    void csvRejectsMalformedHeader();
    void csvNumbersDoNotDependOnQuoting();
    void matSkipsUnsupportedStorage_data();
    void matSkipsUnsupportedStorage();
};

static void writeText(const QString &path, const QByteArray &text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(text), qint64(text.size()));
}

void DataLoadWorkerTest::csvRejectsMalformedQuotes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("quoted.csv");
    writeText(path, "\xef\xbb\xbf\"time\",\"A,\"\"quoted\"\"\"\n"
                    "0,1\n1,\"2\n2,3\"4\"\n3,\"5\"6\n4,  \"7\"  \n");
    DataLoadWorker worker;
    QSignalSpy finished(&worker, &DataLoadWorker::finished);
    worker.loadFile(path);
    QCOMPARE(finished.size(), 1);
    const auto result = finished.takeFirst();
    QVERIFY2(result.at(3).toString().isEmpty(), qPrintable(result.at(3).toString()));
    QCOMPARE(result.at(2).toInt(), 3);
    const auto tables = qvariant_cast<QVector<LoadedTable>>(result.at(1));
    QCOMPARE(tables.size(), 1);
    QCOMPARE(tables.first().signalNames, QStringList({"A,\"quoted\""}));
    QCOMPARE(tables.first().time, QVector<double>({0, 4}));
    QCOMPARE(tables.first().values.first(), QVector<double>({1, 7}));
}

void DataLoadWorkerTest::csvRejectsMalformedHeader()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("header.csv");
    writeText(path, "time,\"A\n0,1\n");
    DataLoadWorker worker;
    QSignalSpy finished(&worker, &DataLoadWorker::finished);
    worker.loadFile(path);
    QCOMPARE(finished.size(), 1);
    const auto result = finished.takeFirst();
    QVERIFY(!result.at(3).toString().isEmpty());
    QVERIFY(qvariant_cast<QVector<LoadedTable>>(result.at(1)).isEmpty());
}

void DataLoadWorkerTest::csvNumbersDoNotDependOnQuoting()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("numbers.csv");
    writeText(path, "time;A;B\n0;1,234;5\n1;\"1,234\";5\n"
                    "2;1.25;3\n3;\"1.25\";\"3\"\n"
                    "1,234;9;9\n\"1,234\";9;9\n");
    DataLoadWorker worker;
    QSignalSpy finished(&worker, &DataLoadWorker::finished);
    worker.loadFile(path);
    QCOMPARE(finished.size(), 1);
    const auto result = finished.takeFirst();
    QVERIFY(result.at(3).toString().isEmpty());
    QCOMPARE(result.at(2).toInt(), 2);
    const auto tables = qvariant_cast<QVector<LoadedTable>>(result.at(1));
    QCOMPARE(tables.size(), 1);
    const auto &table = tables.first();
    QCOMPARE(table.time, QVector<double>({0, 1, 2, 3}));
    QVERIFY(qIsNaN(table.values.first().at(0)));
    QVERIFY(qIsNaN(table.values.first().at(1)));
    QCOMPARE(table.values.first().at(2), 1.25);
    QCOMPARE(table.values.first().at(3), 1.25);
}

void DataLoadWorkerTest::matSkipsUnsupportedStorage_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("complex") << 0;
    QTest::newRow("sparse") << 1;
    QTest::newRow("empty") << 2;
}

void DataLoadWorkerTest::matSkipsUnsupportedStorage()
{
#ifdef ENABLE_MAT
    QFETCH(int, kind);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("storage.mat");
    mat_t *file = Mat_CreateVer(path.toUtf8().constData(), nullptr, MAT_FT_MAT5);
    QVERIFY(file);
    size_t dimensions[] = {2, 2};
    double real[] = {0, 1, 12, 34};
    double imaginary[] = {0, 0, 1, 1};
    mat_complex_split_t complex{real, imaginary};
    mat_uint32_t rows[] = {0, 1};
    mat_uint32_t columns[] = {0, 1, 2};
    double sparseValues[] = {1, 2};
    mat_sparse_t sparse{};
    sparse.nzmax = 2;
    sparse.ir = rows;
    sparse.nir = 2;
    sparse.jc = columns;
    sparse.njc = 3;
    sparse.ndata = 2;
    sparse.data = sparseValues;
    matvar_t *unsupported = nullptr;
    if (kind == 0) {
        unsupported = Mat_VarCreate("p1", MAT_C_DOUBLE, MAT_T_DOUBLE, 2,
                                    dimensions, &complex, MAT_F_COMPLEX);
    } else if (kind == 1) {
        unsupported = Mat_VarCreate("p1", MAT_C_SPARSE, MAT_T_DOUBLE, 2,
                                    dimensions, &sparse, 0);
    } else {
        size_t empty[] = {0, 2};
        unsupported = Mat_VarCreate("p1", MAT_C_DOUBLE, MAT_T_DOUBLE, 2,
                                    empty, nullptr, 0);
    }
    QVERIFY(unsupported);
    const int unsupportedWritten = Mat_VarWrite(file, unsupported, MAT_COMPRESSION_NONE);
    Mat_VarFree(unsupported);
    matvar_t *valid = Mat_VarCreate("p2", MAT_C_DOUBLE, MAT_T_DOUBLE, 2,
                                    dimensions, real, 0);
    QVERIFY(valid);
    const int validWritten = Mat_VarWrite(file, valid, MAT_COMPRESSION_ZLIB);
    Mat_VarFree(valid);
    Mat_Close(file);
    QCOMPARE(unsupportedWritten, 0);
    QCOMPARE(validWritten, 0);

    DataLoadWorker worker;
    QSignalSpy finished(&worker, &DataLoadWorker::finished);
    worker.loadFile(path);
    QCOMPARE(finished.size(), 1);
    const auto result = finished.takeFirst();
    QVERIFY2(result.at(3).toString().isEmpty(), qPrintable(result.at(3).toString()));
    const auto tables = qvariant_cast<QVector<LoadedTable>>(result.at(1));
    QCOMPARE(tables.size(), 1);
    QCOMPARE(tables.first().name, QStringLiteral("p2"));
    QCOMPARE(tables.first().time, QVector<double>({0, 1}));
    QCOMPARE(tables.first().values.first(), QVector<double>({12, 34}));
#else
    QSKIP("MAT support disabled");
#endif
}

QTEST_GUILESS_MAIN(DataLoadWorkerTest)
#include "dataloadworker_test.moc"
