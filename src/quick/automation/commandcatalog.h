#pragma once
#include <QJsonArray>
#include <QJsonObject>

QJsonArray automationCatalog();
QJsonObject automationCapabilities();
QJsonObject commandSpec(const QString &name);
QString validateCommandValue(const QJsonValue &value, const QJsonObject &schema, const QString &path = QStringLiteral("params"));
