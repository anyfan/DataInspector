#include <QGuiApplication>
#include <QSurfaceFormat>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QDebug>
#include <QTimer>
#include <QTextStream>
#include <QQuickWindow>
#include "appcontroller.h"

static int writeStartupError(QQmlApplicationEngine &engine,
                             const QStringList &qmlErrors,
                             const QString &fallback)
{
    const QString message = qmlErrors.isEmpty() ? fallback : qmlErrors.join(QLatin1Char('\n'));
    QFile logFile(QCoreApplication::applicationDirPath() + QStringLiteral("/startup-error.log"));
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&logFile);
        stream << "Import paths:\n";
        for (const QString &path : engine.importPathList())
            stream << path << '\n';
        stream << message << Qt::endl;
    }
    qCritical().noquote() << message;
    return 1;
}

int main(int argc, char *argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    // Antialias triangle edges consistently at every slope on the GPU backend.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("DataInspector"));
    app.setOrganizationName(QStringLiteral("DataInspector"));
    AppController controller;
    QQmlApplicationEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    QStringList qmlErrors;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                     [&qmlErrors](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qmlErrors.append(warning.toString());
    });
    // Load a tiny QtQuick-only window first. Main.qml is deliberately delayed
    // until the splash has rendered at least one frame, so expensive Controls,
    // dialogs and plot components never present as a blank launch.
    engine.loadFromModule(QStringLiteral("DataInspector"), QStringLiteral("Splash"));
    if (engine.rootObjects().isEmpty()) {
        return writeStartupError(
            engine, qmlErrors,
            QStringLiteral("Splash root object could not be created. No detailed error was reported."));
    }

    auto *splash = qobject_cast<QQuickWindow *>(engine.rootObjects().constLast());
    if (!splash) {
        return writeStartupError(engine, qmlErrors,
                                 QStringLiteral("Splash window could not be created."));
    }

    const QStringList startupArguments = app.arguments();
    QTimer::singleShot(120, &app, [&]() {
        qmlErrors.clear();
        engine.setInitialProperties({{QStringLiteral("appController"), QVariant::fromValue(&controller)}});
        const int rootsBeforeMain = engine.rootObjects().size();
        engine.loadFromModule(QStringLiteral("DataInspector"), QStringLiteral("Main"));
        if (engine.rootObjects().size() <= rootsBeforeMain) {
            splash->close();
            app.exit(writeStartupError(
                engine, qmlErrors,
                QStringLiteral("QML root object could not be created. No detailed error was reported.")));
            return;
        }

        // Files dropped onto the executable arrive as arguments. Start the
        // import once the real window and its subplots already exist.
        QTimer::singleShot(0, &controller, [&controller, startupArguments]() {
            controller.openStartupFiles(startupArguments);
        });
        QMetaObject::invokeMethod(splash, "dismiss", Qt::QueuedConnection);
        QTimer::singleShot(360, splash, &QObject::deleteLater);
    });
    return app.exec();
}
