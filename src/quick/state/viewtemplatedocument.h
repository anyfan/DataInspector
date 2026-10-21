#pragma once
#include "viewconfiguration.h"
#include "models/objectdefinition.h"
#include <QMap>

struct SessionDocument;
struct ViewTemplateSignal {
    QString sourceName, tableName, originalName, name;
    int table = 0, style = 1;
    QColor color;
    double width = 2.0;
    SignalEnums enums;
    QString processingId, outputId;
};
// Independent of data paths, time offsets and session version.
struct ViewTemplateDocument : ViewConfiguration {
    static constexpr int version = 6;
    QMap<int, ViewTemplateSignal> series;
    QVector<DataObject> objects;
};
QSet<int> referencedTemplateSignals(const ViewTemplateDocument &view);
QSet<int> templateRawSignals(const ViewTemplateDocument &view);
ViewTemplateDocument viewTemplateFromSession(const SessionDocument &session);
QJsonObject viewTemplateToJson(const ViewTemplateDocument &view);
bool viewTemplateFromJson(const QJsonObject &json, ViewTemplateDocument *view, QString *error);
bool readViewTemplate(const QString &path, ViewTemplateDocument *view, QString *error);
bool writeViewTemplate(const QString &path, const ViewTemplateDocument &view, QString *error);
