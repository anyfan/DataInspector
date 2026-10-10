#pragma once
#include <QColor>
#include <QJsonObject>
#include <QSet>
#include <QVector>
#include "render/trajectorybuilder.h"
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
struct SessionTrajectory
{
    bool enabled = false;
    int active = 0;
    // Each entry has exactly one owner; changing selection never copies state.
    QVector<SessionTrajectoryEntry> tracks{SessionTrajectoryEntry{}};
    const QVector<SessionTrajectoryEntry> &entries() const & { return tracks; }
    // QHash::value() returns a temporary. Its entries must own their lifetime.
    QVector<SessionTrajectoryEntry> entries() const && { return tracks; }
    SessionTrajectoryEntry &activeEntry() { return tracks[active]; }
    const SessionTrajectoryEntry &activeEntry() const { return tracks.at(active); }
    void select(int index) {
        Q_ASSERT(index >= 0 && index < tracks.size());
        active = index;
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
struct ViewConfiguration
{
    QVector<SessionPlot> plots{SessionPlot{}};
    int rows = 1, columns = 1, active = 0, solo = -1;
    double xMinimum = 0.0, xMaximum = 1.0;
};
QJsonObject viewConfigurationToJson(const ViewConfiguration &view);
bool viewConfigurationFromJson(const QJsonObject &json, int signalCount,
    ViewConfiguration *view, QString *error, int legacyVersion = 6,
    const QVector<QColor> &legacyColors = {});
QSet<int> referencedViewSignals(const ViewConfiguration &view);