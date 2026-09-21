#include <QGuiApplication>
#include <QSurfaceFormat>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QDebug>
#include <QTimer>
#include <QTextStream>
#include "appcontroller.h"
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
    engine.setInitialProperties({{QStringLiteral("appController"), QVariant::fromValue(&controller)}});
    engine.loadFromModule(QStringLiteral("DataInspector"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        const QString message = qmlErrors.isEmpty()
            ? QStringLiteral("QML root object could not be created. No detailed error was reported.")
            : qmlErrors.join(QLatin1Char('\n'));
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
    // Files dropped onto the executable arrive as arguments. Start the import
    // once the event loop runs, so the window and its subplots already exist.
    const QStringList startupArguments = app.arguments();
    QTimer::singleShot(0, &controller, [&controller, startupArguments]() {
        controller.openStartupFiles(startupArguments);
    });
    return app.exec();
}
