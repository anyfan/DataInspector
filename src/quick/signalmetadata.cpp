#include "signalmetadata.h"

QStringList composeMatSignalNames(QStringList title1,
                                  QStringList title2,
                                  int signalCount,
                                  const QString &tableName)
{
    if (title1.size() == signalCount + 1)
        title1.removeFirst();
    if (title2.size() == signalCount + 1)
        title2.removeFirst();

    QStringList names;
    names.reserve(signalCount);
    for (int index = 0; index < signalCount; ++index) {
        QString name;
        if (title2.size() == signalCount)
            name = title2.at(index);
        if (title1.size() == signalCount) {
            if (!name.isEmpty())
                name += QLatin1Char(' ');
            name += title1.at(index);
        }
        name = name.trimmed();
        if (name.isEmpty()) {
            const bool titlesMatched = title1.size() == signalCount
                || title2.size() == signalCount;
            name = titlesMatched
                ? QStringLiteral("Signal %1").arg(index + 1)
                : QStringLiteral("%1_Sig%2").arg(tableName).arg(index + 1);
        }
        names.append(name);
    }
    return names;
}

QString signalTableGroup(const QString &fileBaseName,
                         const QString &tableName,
                         int tableCount)
{
    return tableCount == 1 && tableName == fileBaseName
        ? QString() : tableName;
}
