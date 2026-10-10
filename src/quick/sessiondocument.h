#pragma once
#include <QColor>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include "render/trajectorybuilder.h"
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
struct SessionCursor
{
    int mode = 0;
    double x1 = 0.0, x2 = 1.0;
};
struct SessionTrajectoryEntry
{
    QString id = QStringLiteral("trajectory-1"), name = QStringLiteral("航迹 1");
    QString objectId; // Linked object owns the measurement bindings.
    bool geographic = true, visible = true;
    QColor color = QColor("#0072bd");
    double width = 2;
    std::array<int, 3> axes{{-1, -1, -1}};
    TrajectoryAttitude attitude;
};
struct SessionTrajectory : SessionTrajectoryEntry
{
    bool enabled = false;
    int active = 0;
    // The inherited entry is the active editing state. entries() publishes a
    // complete value snapshot; selecting another entry flushes this state first.
    QVector<SessionTrajectoryEntry> tracks{SessionTrajectoryEntry{}};
    QVector<SessionTrajectoryEntry> entries() const {
        auto result = tracks;
        if (active >= 0 && active < result.size()) result[active] = static_cast<const SessionTrajectoryEntry &>(*this);
        return result;
    }
    void select(int index) {
        tracks = entries(); active = index; static_cast<SessionTrajectoryEntry &>(*this) = tracks.at(active);
    }
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
    static constexpr int version = 10;
    QStringList files; // Relative to the session document, or absolute.
    QVector<SessionSignal> series;
    QVector<DataObject> objects;
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
bool readViewTemplate(const QString &path, SessionDocument *session, QString *error);
bool writeViewTemplate(const QString &path, const SessionDocument &session, QString *error);
