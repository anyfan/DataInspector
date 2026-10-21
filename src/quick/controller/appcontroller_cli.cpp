#include "appcontroller.h"
#include "automation/automationserver.h"
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QUuid>

bool AppController::startAutomation(const QString &connectionFile, QString *error)
{
    if (automationEnabled()) {
        *error = QStringLiteral("CLI 已开启");
        return false;
    }
    auto server = std::make_unique<AutomationServer>(this);
    if (!server->start(connectionFile, error)) {
        m_automationError = *error;
        emit automationStateChanged();
        return false;
    }
    m_automationServer = std::move(server);
    m_automationConnectionFile = QDir::toNativeSeparators(connectionFile);
    m_automationError.clear();
    emit automationStateChanged();
    return true;
}

bool AppController::enableAutomation()
{
    if (automationEnabled()) return true;
    // Connection descriptors are per-run files, not persistent settings.
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/DataInspector/automation");
    if (!QDir().mkpath(directory)) {
        m_automationError = QStringLiteral("无法创建 CLI 连接目录：%1").arg(directory);
        emit automationStateChanged();
        return false;
    }
    const auto path = QDir(directory).filePath(QStringLiteral("connection-%1.json")
        .arg(QUuid::createUuid().toString(QUuid::Id128)));
    QString error;
    return startAutomation(path, &error);
}

void AppController::disableAutomation()
{
    m_automationServer.reset();
    m_automationConnectionFile.clear();
    m_automationError.clear();
    emit automationStateChanged();
}

QString AppController::automationCommand() const
{
    if (!automationEnabled()) return {};
    const auto quote = [](QString path) {
        path = QDir::toNativeSeparators(path);
        return QLatin1Char('\'') + path.replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\'');
    };
    return QStringLiteral("& %1 --connection %2 status --ascii")
        .arg(quote(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("datainspector_cli.ps1"))),
             quote(m_automationConnectionFile));
}

void AppController::copyAutomationConnectionFile() const
{
    if (automationEnabled()) QGuiApplication::clipboard()->setText(m_automationConnectionFile);
}

void AppController::copyAutomationCommand() const
{
    if (automationEnabled()) QGuiApplication::clipboard()->setText(automationCommand());
}
