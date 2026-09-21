#include "appcontroller.h"
#include "startupfiles.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class StartupFilesTest final : public QObject
{
    Q_OBJECT

private slots:
    void parsingSkipsSwitchesAndKeepsExistingDataFiles();
    void droppedArgumentsLoadOnStartup();
    void sessionArgumentTakesPrecedenceOverDataFiles();
};

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
}

void StartupFilesTest::parsingSkipsSwitchesAndKeepsExistingDataFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir root(dir.path());
    const QString csv = root.filePath(QStringLiteral("数据.csv"));
    const QString mat = root.filePath(QStringLiteral("second.MAT"));
    const QString other = root.filePath(QStringLiteral("notes.pdf"));
    writeFile(csv, "time,A\n0,1\n1,2\n");
    writeFile(mat, "not really a mat file");
    writeFile(other, "ignored");

    const QString previous = QDir::currentPath();
    QVERIFY(QDir::setCurrent(dir.path()));
    const StartupFiles startup = parseStartupFiles(QStringList{
        QStringLiteral("DataInspector.exe"),
        QStringLiteral("-platform"), QStringLiteral("offscreen"),
        QStringLiteral("--some-flag"),
        QStringLiteral("数据.csv"),          // Relative to the working directory.
        csv,                                 // Duplicate of the same file.
        QStringLiteral("second.MAT"),        // Suffix matching is case-insensitive.
        other,
        root.filePath(QStringLiteral("missing.csv"))});
    QVERIFY(QDir::setCurrent(previous));

    QCOMPARE(startup.dataFiles.size(), 2);
    QCOMPARE(QFileInfo(startup.dataFiles.at(0)).fileName(), QStringLiteral("数据.csv"));
    QCOMPARE(QFileInfo(startup.dataFiles.at(1)).fileName(), QStringLiteral("second.MAT"));
    QVERIFY(startup.sessionFile.isEmpty());
    QCOMPARE(startup.rejected.size(), 1);
    QVERIFY(!startup.isEmpty());
    QVERIFY(parseStartupFiles(QStringList{QStringLiteral("DataInspector.exe")}).isEmpty());
}

void StartupFilesTest::droppedArgumentsLoadOnStartup()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir root(dir.path());
    const QString first = root.filePath(QStringLiteral("first.csv"));
    const QString second = root.filePath(QStringLiteral("second.txt"));
    writeFile(first, "time,A\n0,1\n1,2\n");
    writeFile(second, "time,B\n0,3\n1,4\n");

    AppController controller;
    QCOMPARE(controller.openStartupFiles(QStringList{
        QStringLiteral("DataInspector.exe"), QStringLiteral("-platform"),
        QStringLiteral("offscreen"), first, second}), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QCOMPARE(controller.loadedFileCount(), 2);
    QCOMPARE(controller.signalCount(), 2);

    AppController empty;
    QCOMPARE(empty.openStartupFiles(QStringList{QStringLiteral("DataInspector.exe")}), 0);
    QVERIFY(!empty.loading());
    QCOMPARE(empty.loadedFileCount(), 0);
}

void StartupFilesTest::sessionArgumentTakesPrecedenceOverDataFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir root(dir.path());
    const QString saved = root.filePath(QStringLiteral("saved.csv"));
    const QString extra = root.filePath(QStringLiteral("extra.csv"));
    const QString session = root.filePath(QStringLiteral("startup.disession"));
    writeFile(saved, "time,A\n0,1\n1,2\n");
    writeFile(extra, "time,B\n0,3\n1,4\n");

    AppController source;
    QVERIFY(source.loadCsv(saved));
    QTRY_VERIFY_WITH_TIMEOUT(!source.loading(), 5000);
    source.toggleSignal(0);
    QVERIFY(source.renameSignal(0, QStringLiteral("俯仰角")));
    QVERIFY(source.saveSession(session));

    AppController restored;
    QCOMPARE(restored.openStartupFiles(QStringList{
        QStringLiteral("DataInspector.exe"), extra, session}), 1);
    QVERIFY(restored.restoringSession());
    QTRY_VERIFY_WITH_TIMEOUT(!restored.restoringSession(), 5000);
    // Only the session sources are restored; the loose argument is ignored.
    QCOMPARE(restored.loadedFileCount(), 1);
    QCOMPARE(restored.signalName(0), QStringLiteral("俯仰角"));
}

QTEST_MAIN(StartupFilesTest)
#include "startupfiles_test.moc"
