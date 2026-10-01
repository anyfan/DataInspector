#pragma once
#include <QColor>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include "render/trajectorybuilder.h"

struct SessionSignal
{
    int file = -1, table = -1, column = -1;
    QString tableName, originalName, name;
    QColor color;
    double width = 2.0, timeOffset = 0.0;
    int style = 1;
};
struct SessionCursor
{
    int mode = 0;
    double x1 = 0.0, x2 = 1.0;
};
struct SessionTrajectory
{
    bool enabled = false;
    bool geographic = false;
    std::array<int, 3> axes{{-1, -1, -1}};
    QVector<int> signalIds; // available sources in this trajectory subplot, independent of axis assignment
    TrajectoryCamera camera;
};
struct SessionPlot
{
    SessionTrajectory trajectory;
    QVector<int> seriesIds;
    double yMinimum = -1.0, yMaximum = 1.0, lineWidth = 2.0;
    bool normalizeY = false;
};
struct SessionDocument
{
    static constexpr int version = 5;
    QStringList files; // Relative to the session document, or absolute.
    QVector<SessionSignal> series;
    QVector<SessionPlot> plots{SessionPlot{}};
    int rows = 1, columns = 1, active = 0, solo = -1;
    double xMinimum = 0.0, xMaximum = 1.0;
    SessionCursor cursor;
};

// Strict schema validation happens before a controller starts changing its state.
QJsonObject sessionToJson(const SessionDocument &session);
bool sessionFromJson(const QJsonObject &json, SessionDocument *session, QString *error);
bool readSessionDocument(const QString &path, SessionDocument *session, QString *error);
bool writeSessionDocument(const QString &path, const SessionDocument &session, QString *error);
