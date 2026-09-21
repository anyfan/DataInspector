// Command-line file arguments: Explorer drop onto the executable, "Open with"
// and double-clicked file associations all arrive through argv.
#include "startupfiles.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
// Switches that consume the next argument as their value. A dropped path never
// starts with '-', so every other leading-dash token is simply skipped.
bool optionTakesValue(const QString &option)
{
    static const QStringList options{
        QStringLiteral("-platform"), QStringLiteral("-platformpluginpath"),
        QStringLiteral("-platformtheme"), QStringLiteral("-plugin"),
        QStringLiteral("-graphicssystem"), QStringLiteral("-style"),
        QStringLiteral("-stylesheet"), QStringLiteral("-session"),
        QStringLiteral("-display"), QStringLiteral("-geometry"),
        QStringLiteral("-title"), QStringLiteral("-qmljsdebugger"),
        QStringLiteral("-qwindowgeometry"), QStringLiteral("-qwindowtitle"),
        QStringLiteral("-qwindowicon")};
    return options.contains(option, Qt::CaseInsensitive);
}

bool hasSuffix(const QFileInfo &info, const QStringList &suffixes)
{
    return suffixes.contains(info.suffix().toLower());
}
}

const QStringList &startupDataSuffixes()
{
    static const QStringList suffixes{QStringLiteral("csv"), QStringLiteral("txt"),
                                      QStringLiteral("xlsx"), QStringLiteral("mat")};
    return suffixes;
}

const QStringList &startupSessionSuffixes()
{
    static const QStringList suffixes{QStringLiteral("disession"),
                                      QStringLiteral("json")};
    return suffixes;
}

bool runningElevated()
{
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool queried = GetTokenInformation(token, TokenElevation, &elevation,
                                             sizeof(elevation), &size);
    CloseHandle(token);
    return queried && elevation.TokenIsElevated != 0;
#else
    return false;
#endif
}

StartupFiles parseStartupFiles(const QStringList &arguments)
{
    StartupFiles startup;
    QSet<QString> seen;
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (argument.isEmpty()) continue;
        if (argument.startsWith(QLatin1Char('-'))) {
            if (optionTakesValue(argument) && !argument.contains(QLatin1Char('=')))
                ++index;
            continue;
        }
        const QFileInfo info(QDir::current().absoluteFilePath(argument));
        if (!info.isFile() || !info.isReadable()) continue;
        QString path = info.canonicalFilePath();
        if (path.isEmpty()) path = info.absoluteFilePath();
        if (seen.contains(path)) continue;
        seen.insert(path);
        if (hasSuffix(info, startupDataSuffixes())) {
            startup.dataFiles.append(path);
        } else if (hasSuffix(info, startupSessionSuffixes())) {
            if (startup.sessionFile.isEmpty()) startup.sessionFile = path;
        } else {
            startup.rejected.append(path);
        }
    }
    return startup;
}
