#include <QGuiApplication>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDebug>
#include <QTextStream>
#include <QtQml/qqml.h>
#include "appcontroller.h"
#include "plotitem.h"
int main(int argc, char *argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("DataInspector"));
    app.setOrganizationName(QStringLiteral("DataInspector"));
    qmlRegisterType<PlotItem>("DataInspector", 1, 0, "PlotItem");
    AppController controller;
    QQmlApplicationEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    QStringList qmlErrors;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                     [&qmlErrors](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qmlErrors.append(warning.toString());
    });
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
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
    return app.exec();
}
