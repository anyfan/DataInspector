#pragma once
#include <QColor>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include "viewconfiguration.h"
#include "objectdefinition.h"

struct SessionSignal
{
    int file = -1, table = -1, column = -1;
    QString tableName, originalName, name;
    QString objectId, outputId; // Empty for a raw file column.
    QColor color;
    double width = 2.0, timeOffset = 0.0;
    int style = 1;
};
struct SessionDocument : ViewConfiguration
{
    static constexpr int version = 10;
    QStringList files; // Relative to the session document, or absolute.
    QVector<SessionSignal> series;
    QVector<DataObject> objects;
    SessionCursor cursor;
};

// Strict schema validation happens before a controller starts changing its state.
QJsonObject sessionToJson(const SessionDocument &session);
bool sessionFromJson(const QJsonObject &json, SessionDocument *session, QString *error);
bool readSessionDocument(const QString &path, SessionDocument *session, QString *error);
bool writeSessionDocument(const QString &path, const SessionDocument &session, QString *error);
