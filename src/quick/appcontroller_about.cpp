#include "appcontroller.h"
#include "buildinfo.h"
#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QSysInfo>
#include <QTimeZone>

QVariantMap AppController::aboutInfo() const
{
    const auto parsed = QDateTime::fromString(QString::fromUtf8(BuildInfo::time),
                                             QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'"));
    const auto buildTime = QDateTime(parsed.date(), parsed.time(), QTimeZone(0))
                               .toOffsetFromUtc(8 * 60 * 60)
                               .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss '北京时间 (UTC+8)'"));
    return {{QStringLiteral("version"), QString::fromUtf8(BuildInfo::version)},
            {QStringLiteral("buildTime"), buildTime},
            {QStringLiteral("gitHash"), QString::fromUtf8(BuildInfo::hash)},
            {QStringLiteral("gitBranch"), QString::fromUtf8(BuildInfo::branch)},
            {QStringLiteral("gitState"), QString::fromUtf8(BuildInfo::state)},
            {QStringLiteral("buildType"), QString::fromUtf8(BuildInfo::type)},
            {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
            {QStringLiteral("architecture"), QSysInfo::buildCpuArchitecture()},
            {QStringLiteral("matSupport"), matExportSupported() ? QStringLiteral("启用") : QStringLiteral("禁用")}};
}

QString AppController::releaseNotes() const
{
    return QString::fromUtf8(BuildInfo::notes);
}

void AppController::copyAboutInfo() const
{
    const auto info = aboutInfo();
    QGuiApplication::clipboard()->setText(
        QStringLiteral("DataInspector %1\n构建时间：%2\nGit hash：%3\nGit 分支：%4\n工作区：%5\n构建类型：%6\nQt：%7\n架构：%8\nMAT 支持：%9")
            .arg(info.value("version").toString(), info.value("buildTime").toString(),
                 info.value("gitHash").toString(), info.value("gitBranch").toString(),
                 info.value("gitState").toString(), info.value("buildType").toString(),
                 info.value("qtVersion").toString(), info.value("architecture").toString(),
                 info.value("matSupport").toString()));
}
