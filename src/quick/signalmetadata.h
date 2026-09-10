#pragma once

#include <QStringList>

QStringList composeMatSignalNames(QStringList title1,
                                  QStringList title2,
                                  int signalCount,
                                  const QString &tableName);

QString signalTableGroup(const QString &fileBaseName,
                         const QString &tableName,
                         int tableCount);
