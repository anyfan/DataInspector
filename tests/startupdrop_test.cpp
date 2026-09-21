// Reproduces: after files are imported from the command line at startup,
// dragging further files onto the window must still import them.
#include "appcontroller.h"
#include "plotitem.h"
#include "signalmodel.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QMimeData>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <memory>

class StartupDropTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void windowAcceptsDropsAfterStartupImport();

private:
    QString m_qmlDirectory;
};

static void writeCsvFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
}

// Sends the drag-enter / drop pair a real window manager would deliver.
static void dropFileOnWindow(QQuickWindow *window, const QString &path)
{
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    const QPoint position(window->width() / 2, window->height() / 2);
    QDragEnterEvent enter(position, Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &enter);
    QDropEvent drop(QPointF(position), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &drop);
}

void StartupDropTest::initTestCase()
{
    qmlRegisterType<PlotItem>("DataInspector", 1, 0, "PlotItem");
    qmlRegisterUncreatableType<AppController>("DataInspector", 1, 0,
                                              "AppController", "Owned by C++");
    qmlRegisterUncreatableType<SignalModel>("DataInspector", 1, 0,
                                            "SignalModel", "Owned by AppController");
    m_qmlDirectory = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
                         .filePath(QStringLiteral("../qml"));
    qmlRegisterType(QUrl::fromLocalFile(QDir(m_qmlDirectory).filePath(QStringLiteral("QuickPlot.qml"))),
                    "DataInspector", 1, 0, "QuickPlot");
}

void StartupDropTest::windowAcceptsDropsAfterStartupImport()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir root(directory.path());
    const QString startupFile = root.filePath(QStringLiteral("startup.csv"));
    const QString droppedFile = root.filePath(QStringLiteral("dropped.csv"));
    writeCsvFile(startupFile, "time,A\n0,1\n1,2\n");
    writeCsvFile(droppedFile, "time,B\n0,3\n1,4\n");

    AppController controller;
    QQmlEngine engine;
    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(QDir(m_qmlDirectory).filePath(QStringLiteral("Main.qml"))));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("appController"), QVariant::fromValue(&controller)}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));

    // Import exactly the way a drop onto the executable does.
    QCOMPARE(controller.openStartupFiles(QStringList{
                 QStringLiteral("DataInspector.exe"), startupFile}), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QCOMPARE(controller.loadedFileCount(), 1);

    // The window must still accept dragged files afterwards.
    dropFileOnWindow(window, droppedFile);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    QCOMPARE(controller.loadedFileCount(), 2);
    QCOMPARE(controller.signalCount(), 2);
}

QTEST_MAIN(StartupDropTest)
#include "startupdrop_test.moc"
