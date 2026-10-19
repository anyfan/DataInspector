#pragma once
#include <QSet>
#include "render/plotseriesstore.h"
#include "render/trajectorybuilder.h"
#include <QStringList>
#include <atomic>
#include <utility>
#include <QJsonArray>
#include <QJsonObject>

struct ObjectField {
    QString id, name, role = QStringLiteral("scalar");
    int series = -1;
};
struct ObjectOutput {
    QString id, name; int series = -1;
    ObjectOutput() = default;
    ObjectOutput(QString identity, QString title, int row) : id(std::move(identity)), name(std::move(title)), series(row) {}
    int startBit = 0, bitCount = 0; // zero inherits the rule's scalar defaults
    bool signedField = false;
    QString expression;
};
struct ObjectRule {
    QString id, name, operation = QStringLiteral("scale"), expression;
    QStringList inputs;
    int wordBits = 8, startBit = 0, bitCount = 1;
    bool signedField = false, autoWordBits = false;
    QString byteOrder = QStringLiteral("little");
    double factor = 1, bias = 0;
    double samplePeriod = 0; // Seconds; zero estimates the period from input timestamps.
    QString lossMode = QStringLiteral("time"); // time / sequence / stride / rate
    double counterStep = 1, expectedStride = 1, counterModulus = 0;
    double counterRate = 1, counterTolerance = 0; // Frames/second and frames.
    QString rateMode = QStringLiteral("cumulative"); // count / cumulative / window
    double rateWindowSeconds = 1;
    QVector<ObjectOutput> outputs;
};
inline int outputStartBit(const ObjectRule &rule, const ObjectOutput &output) { return output.bitCount ? output.startBit : rule.startBit; }
inline int outputBitCount(const ObjectRule &rule, const ObjectOutput &output) { return output.bitCount ? output.bitCount : rule.bitCount; }
inline bool outputSignedField(const ObjectRule &rule, const ObjectOutput &output) { return output.bitCount ? output.signedField : rule.signedField; }
inline const QString &outputExpression(const ObjectRule &rule, const ObjectOutput &output) { return output.expression.isEmpty() ? rule.expression : output.expression; }
struct DataObject {
    QString id, name;
    QVector<ObjectField> fields;
    QVector<ObjectRule> rules; // References may only point to fields or earlier outputs.
    QString type = QStringLiteral("general");
    bool geographic = true;
    TrajectoryAttitude attitude{0}; // Sources are resolved from fixed object fields when enabled.
};
// Processing configurations share the bounded evaluation representation, but
// have their own lifecycle and are stored outside the session's objects array.
inline bool isProcessingConfiguration(const DataObject &definition)
{
    return definition.type == "processing";
}
struct ObjectRuleState {
    QJsonObject definition;
    QVector<PlotSeriesDataPtr> inputs;
    double samplePeriod = 0;
    qsizetype duplicateSamples = 0;
    int effectiveWordBits = 0;
};
struct ObjectEvaluation {
    qsizetype mergeTimelineBuilds = 0, mergeTimelineHits = 0, mergeTimelineBytes = 0;
    QVector<PlotSeriesDataPtr> series;
    QHash<QString, QString> errors;
    QSet<QString> budgetLimitedRules;
    QHash<QString, ObjectRuleState> states;
};
QSet<QString> invalidatedObjectRules(const QVector<DataObject> &objects,
    const PlotSeriesSnapshot &snapshot, const QHash<QString, ObjectRuleState> &states);
QSet<int> removeRulesDependingOnSources(QVector<DataObject> &objects, const QSet<int> &removedSources);
bool validateObjects(const QVector<DataObject> &objects, int signalCount, QString *error);
QString validateObjectExpression(const QString &expression, int inputCount);
ObjectEvaluation evaluateObjects(const QVector<DataObject> &objects,
    const PlotSeriesSnapshot &snapshot, const std::atomic_bool &cancelled,
    qsizetype retainedValues = 0, const QSet<QString> *rulesToEvaluate = nullptr,
    std::atomic<int> *completedRules = nullptr);
QJsonArray objectsToJson(const QVector<DataObject> &objects);
QStringList aircraftFieldRoles();
QStringList activeAircraftFieldRoles(const DataObject &object);
bool ensureAircraftFields(DataObject &object);
bool objectsFromJson(const QJsonValue &value, int signalCount, QVector<DataObject> *objects, QString *error);

struct PacketLossGroup {
    QVector<int> sources;
    QString name;
    double samplePeriod = 0;
};
QVector<PacketLossGroup> identifyPacketLossGroups(const PlotSeriesSnapshot &snapshot,
    const std::atomic_bool &cancelled, std::atomic<int> *completed = nullptr);
