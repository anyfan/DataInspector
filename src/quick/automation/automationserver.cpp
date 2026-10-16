#include "automationserver.h"
#include "commandcatalog.h"
#include "controller/appcontroller.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRunnable>
#include <QSaveFile>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
constexpr qint64 MaxRequest = 65536;
QJsonObject failure(const QString &code, const QString &message)
{ return {{"ok", false}, {"error", QJsonObject{{"code", code}, {"message", message}}}}; }
QJsonObject success(const QJsonValue &value = QJsonObject{})
{ return {{"ok", true}, {"result", value}}; }
void require(bool condition, const char *message)
{ if (!condition) throw std::invalid_argument(message); }
void keys(const QJsonObject &p, const QStringList &allowed)
{ for (auto it = p.begin(); it != p.end(); ++it) require(allowed.contains(it.key()), "Unknown parameter"); }
int integer(const QJsonObject &p, const QString &key, int lo, int hi, int fallback = -1)
{
    if (!p.contains(key) && fallback >= 0) return fallback;
    const auto v = p.value(key);
    const double d = v.toDouble(std::numeric_limits<double>::quiet_NaN());
    require(v.isDouble() && std::isfinite(d) && d >= lo && d <= hi && std::floor(d) == d,
            "Missing or out-of-range integer parameter");
    return int(d);
}
double number(const QJsonObject &p, const QString &key)
{
    const auto v = p.value(key);
    require(v.isDouble() && std::isfinite(v.toDouble()), "Missing or non-finite numeric parameter");
    return v.toDouble();
}
QString textParam(const QJsonObject &p, const QString &key)
{
    require(p.value(key).isString() && !p.value(key).toString().trimmed().isEmpty(), "Missing or empty string parameter");
    return p.value(key).toString();
}
QJsonValue finite(double d) { return std::isfinite(d) ? QJsonValue(d) : QJsonValue(QJsonValue::Null); }
QJsonArray catalog()
{
    return automationCatalog();
}
QJsonObject analyze(const QString &command, const QJsonObject &p, const PlotSeriesSnapshot &snapshot,
                    const QString &generation, const std::shared_ptr<std::atomic_bool> &cancelled)
{
    const auto &series = *snapshot.series.first();
    QJsonObject result{{"generation", generation}, {"signal", series.id}};
    if (command == "samples") {
        const int offset = integer(p, "offset", 0, std::numeric_limits<int>::max(), 0);
        const int limit = integer(p, "limit", 1, 10000, 1000);
        QJsonArray samples;
        const qsizetype end = qMin(series.sampleCount(), qsizetype(offset) + limit);
        for (qsizetype i = offset; i < end; ++i) {
            const QPointF point = series.pointAt(i);
            samples.append(QJsonArray{finite(point.x()), finite(point.y())});
        }
        result["samples"] = samples;
        result["total"] = double(series.sampleCount());
        result["nextOffset"] = end < series.sampleCount() ? QJsonValue(double(end)) : QJsonValue(QJsonValue::Null);
    } else if (command == "nearest") {
        const auto samples = PlotSeriesStore::nearestSamples(snapshot, number(p, "time"));
        result["sample"] = samples.isEmpty() ? QJsonValue(QJsonValue::Null)
            : QJsonValue(QJsonArray{samples.first().x, samples.first().y});
    } else {
        const double start = p.contains("start") ? number(p, "start") : -std::numeric_limits<double>::infinity();
        const double end = p.contains("end") ? number(p, "end") : std::numeric_limits<double>::infinity();
        require(start <= end, "start must be <= end");
        qint64 count = 0, missing = 0;
        long double mean = 0, m2 = 0, squareMean = 0;
        double minimum = std::numeric_limits<double>::infinity(), maximum = -minimum;
        for (qsizetype i = 0; i < series.sampleCount(); ++i) {
            if ((i % 4096) == 0 && cancelled->load()) return failure("cancelled", "Analysis cancelled");
            const auto point = series.pointAt(i);
            if (!std::isfinite(point.x()) || point.x() < start || point.x() > end) continue;
            const double y = point.y();
            if (!std::isfinite(y)) { ++missing; continue; }
            ++count;
            const long double delta = static_cast<long double>(y) - mean;
            mean += delta / count;
            m2 += delta * (static_cast<long double>(y) - mean);
            const long double squared = static_cast<long double>(y) * y;
            squareMean += (squared - squareMean) / count;
            minimum = qMin(minimum, y); maximum = qMax(maximum, y);
        }
        result["count"] = double(count); result["missing"] = double(missing);
        result["min"] = count ? finite(minimum) : QJsonValue(QJsonValue::Null);
        result["max"] = count ? finite(maximum) : QJsonValue(QJsonValue::Null);
        result["mean"] = count ? finite(double(mean)) : QJsonValue(QJsonValue::Null);
        result["stddev"] = count ? finite(double(std::sqrt(qMax(0.0L, m2 / count)))) : QJsonValue(QJsonValue::Null);
        result["rms"] = count ? finite(double(std::sqrt(qMax(0.0L, squareMean)))) : QJsonValue(QJsonValue::Null);
    }
    return success(result);
}
}

AutomationServer::AutomationServer(AppController *controller, QObject *parent)
    : QObject(parent), m_controller(controller), m_cancelled(std::make_shared<std::atomic_bool>(false))
{
    m_workers.setMaxThreadCount(1);
    m_epoch = QUuid::createUuid().toString(QUuid::Id128);
    connect(controller, &AppController::sessionRestoreFinished, this, [this](bool success, const QString &) {
        // Restoring installs a new Store whose counter may equal the old one.
        if (success) m_epoch = QUuid::createUuid().toString(QUuid::Id128);
    });
    connect(&m_server, &QTcpServer::newConnection, this, &AutomationServer::acceptConnections);
    m_jobTimer.setInterval(25); connect(&m_jobTimer, &QTimer::timeout, this, &AutomationServer::pollJobs); m_jobTimer.start();
    connect(controller, &AppController::loadBatchFinished, this, [this](bool ok, const QString &error) {
        if (m_jobs.value(m_activeJob).watch == "load") completeJob(m_activeJob, {{"fileCount", m_controller->loadedFileCount()}, {"signalCount", m_controller->signalCount()}}, ok ? QString() : error);
    });
    connect(controller, &AppController::sessionRestoreFinished, this, [this](bool ok, const QString &error) {
        if (m_jobs.value(m_activeJob).watch != "restore_session") return;
        if (ok && m_controller->deriving()) m_jobs[m_activeJob].watch = "restore_derive";
        else completeJob(m_activeJob, {{"sessionPath", m_controller->sessionPath()}}, ok ? QString() : error);
    });
    connect(controller, &AppController::dataExportFinished, this, [this](bool ok, bool cancelled, const QString &error) {
        if (m_jobs.value(m_activeJob).watch == "export") completeJob(m_activeJob, m_jobs[m_activeJob].result, ok ? QString() : error, cancelled);
    });
    connect(controller, &AppController::imageExportFinished, this, [this](bool ok, const QString &error) {
        if (m_jobs.value(m_activeJob).watch == "export") completeJob(m_activeJob, m_jobs[m_activeJob].result, ok ? QString() : error);
    });
    connect(controller, &AppController::objectEvaluationAborted, this, [this] {
        if (m_jobs.value(m_activeJob).watch == "derive") completeJob(m_activeJob, {}, QStringLiteral("Evaluation superseded or cancelled"), true);
    });
}

AutomationServer::~AutomationServer()
{
    m_cancelled->store(true);
    for (auto &job : m_jobs) job.cancelled->store(true);
    m_workers.waitForDone();
    if (!m_connectionFile.isEmpty()) {
        QFile file(m_connectionFile);
        if (file.open(QIODevice::ReadOnly)
            && QJsonDocument::fromJson(file.readAll()).object().value("token").toString() == m_token) {
            file.close(); QFile::remove(m_connectionFile);
        }
    }
}

bool AutomationServer::start(const QString &connectionFile, QString *error)
{
    if (m_server.isListening()) { *error = "Automation already started"; return false; }
    if (!QFileInfo(connectionFile).isAbsolute() || QFileInfo::exists(connectionFile)) {
        *error = "Connection file must be an absolute, unused path"; return false;
    }
    if (!m_server.listen(QHostAddress::LocalHost, 0)) { *error = m_server.errorString(); return false; }
    m_token = QUuid::createUuid().toString(QUuid::Id128) + QUuid::createUuid().toString(QUuid::Id128);
    QSaveFile file(connectionFile);
    const QByteArray bytes = QJsonDocument(QJsonObject{{"protocol", 1}, {"host", "127.0.0.1"},
        {"port", m_server.serverPort()}, {"token", m_token}, {"pid", double(QCoreApplication::applicationPid())}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString(); m_server.close(); return false;
    }
    m_connectionFile = connectionFile;
    return true;
}

void AutomationServer::reply(QTcpSocket *socket, const QJsonObject &response)
{
    QJsonObject output = response;
    const auto id = socket->property("jobId").toString();
    if (!id.isEmpty() && m_jobs.contains(id)) {
        auto &job = m_jobs[id];
        job.result = response.value("result").toObject();
        if (job.watch.isEmpty() && m_controller->deriving()) { job.watch = "derive"; m_activeJob = id; job.data["cancelable"] = true; }
        if (!response.value("ok").toBool()) completeJob(id, {}, response.value("error").toObject().value("message").toString());
        else if (job.watch.isEmpty() || job.watch == "analysis") completeJob(id, job.result);
        output["task"] = jobValue(id); job.initial = output;
    }
    send(socket, output);
}

void AutomationServer::send(QTcpSocket *socket, const QJsonObject &response)
{
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
    socket->disconnectFromHost();
}

void AutomationServer::acceptConnections()
{
    while (auto *socket = m_server.nextPendingConnection()) {
        if (m_connections >= 8) { socket->abort(); socket->deleteLater(); continue; }
        ++m_connections;
        socket->setReadBufferSize(MaxRequest + 1);
        auto *timer = new QTimer(socket);
        timer->setSingleShot(true); timer->start(10000);
        connect(timer, &QTimer::timeout, socket, &QTcpSocket::abort);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { --m_connections; socket->deleteLater(); });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, timer]() {
            if (socket->property("dispatched").toBool()) return;
            if (socket->bytesAvailable() > MaxRequest) {
                socket->setProperty("dispatched", true);
                reply(socket, failure("request_too_large", "Request exceeds 64 KiB")); return;
            }
            if (!socket->canReadLine()) return;
            socket->setProperty("dispatched", true); timer->stop();
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(socket->readLine(), &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                reply(socket, failure("invalid_json", "Expected one JSON object per connection")); return;
            }
            dispatch(socket, doc.object());
        });
    }
}

void AutomationServer::dispatchOperation(QTcpSocket *socket, const QJsonObject &request)
{
    if (request.value("token").toString().isEmpty() || request.value("token").toString() != m_token) {
        reply(socket, failure("unauthorized", "Invalid connection token")); return;
    }
    try {
        keys(request, {"token", "command", "params"});
        const QString command = textParam(request, "command");
        require(!request.contains("params") || request.value("params").isObject(), "params must be an object");
        const auto p = request.value("params").toObject();
        auto *c = m_controller;
        const auto generation = m_epoch + ':' + QString::number(c->automationSnapshot().generation);
        if (dispatchExtended(socket, command, p)) return;
        if (command == "help") { keys(p, {}); reply(socket, success(catalog())); return; }
        if (command == "status") {
            keys(p, {});
            QJsonArray plots;
            bool rendering = false;
            for (int i = 0; i < c->plotRows() * c->plotColumns(); ++i) {
                const auto rows = c->signalModel()->plotRows(i);
                QJsonArray ids;
                for (int j = 0; j < qMin(rows.size(), qsizetype(1000)); ++j) ids.append(rows[j]);
                plots.append(QJsonObject{{"plot", i}, {"signals", ids}, {"signalCount", rows.size()}, {"truncated", rows.size() > 1000}});
                rendering |= c->trajectoryState(i).value("enabled").toBool() && c->trajectoryRenderState(i).value("pending").toBool();
            }
            reply(socket, success(QJsonObject{{"generation", generation}, {"loading", c->loading()},
                {"restoring", c->restoringSession()}, {"deriving", c->deriving()}, {"exporting", c->exporting()},
                {"analyzing", m_analyzing}, {"status", c->status()}, {"signalCount", c->signalCount()},
                {"fileCount", c->loadedFileCount()}, {"rows", c->plotRows()}, {"columns", c->plotColumns()},
                {"plots", plots}, {"rendering", rendering}, {"logicalView", c->logicalSignalView()}, {"activeTask", m_jobs.value(m_activeJob).data}, {"sessionPath", c->sessionPath()}, {"modified", c->sessionModified()}})); return;
        }
        const bool known = [&]() { for (const auto &v : catalog()) if (v.toObject().value("command") == command) return true; return false; }();
        if (!known) { reply(socket, failure("unknown_command", "Use help to list commands")); return; }
        if (c->loading() || c->restoringSession() || c->exporting() || c->deriving()) {
            reply(socket, failure("busy", "Wait for loading, restore, export and derived evaluation to finish")); return;
        }
        if (command == "objects") {
            const auto all = c->dataObjects();
            if (p.isEmpty()) reply(socket, success(QJsonArray::fromVariantList(all)));
            else { const int offset = integer(p, "offset", 0, INT_MAX, 0), limit = integer(p, "limit", 1, 100, 20); reply(socket, success(QJsonObject{{"objects", QJsonArray::fromVariantList(all.mid(offset, limit))}, {"total", all.size()}})); }
            return;
        }
        if (command == "clear") { keys(p, {}); c->clear(); reply(socket, success()); return; }
        if (command == "merge_signals") {
            keys(p, {"generation", "groups"});
            if (textParam(p, "generation") != generation) {
                reply(socket, failure("stale_generation", "Data changed; discover signal sources again")); return;
            }
            require(p.value("groups").isArray() && !p.value("groups").toArray().isEmpty()
                && p.value("groups").toArray().size() <= 256, "groups must contain 1-256 entries");
            for (const auto &value : p.value("groups").toArray()) {
                require(value.isObject(), "Each merge group must be an object");
                const auto group = value.toObject(); keys(group, {"name", "signals"}); textParam(group, "name");
                require(group.value("signals").isArray() && group.value("signals").toArray().size() >= 2
                    && group.value("signals").toArray().size() <= 256, "Each group requires 2-256 signals");
                for (const auto &source : group.value("signals").toArray())
                    integer(QJsonObject{{"signal", source}}, "signal", 0, c->signalCount() - 1);
            }
            const auto result = c->mergeSignals(p.value("groups").toArray().toVariantList());
            if (result.contains("error")) { reply(socket, failure("rejected", result.value("error").toString())); return; }
            reply(socket, success(QJsonObject::fromVariantMap(result))); return;
        }
        if (command == "object_create") {
            keys(p, {"name", "type"});
            const auto name = textParam(p, "name"), type = textParam(p, "type");
            require(type == "general" || type == "aircraft", "Invalid object type");
            const auto id = c->createDataObject(name, type);
            if (id.isEmpty()) { reply(socket, failure("rejected", "Object creation rejected")); return; }
            reply(socket, success(QJsonObject{{"object", id}})); return;
        }
        if (command == "aircraft_configure") {
            keys(p, {"object", "geographic", "attitudeMode", "radians", "order", "scalarLast", "navigationToBody"});
            const auto id = textParam(p, "object");
            for (const auto &key : {"geographic", "radians", "scalarLast", "navigationToBody"})
                require(p.value(key).isBool(), "Aircraft convention flags must be boolean");
            integer(p, "attitudeMode", 0, 2); integer(p, "order", 0, 1);
            auto config = p; config.remove("object");
            if (!c->configureDataObjectAircraft(id, config.toVariantMap())) {
                reply(socket, failure("rejected", "Aircraft configuration rejected")); return;
            }
            reply(socket, success()); return;
        }
        if (command == "object_bind_field") {
            keys(p, {"generation", "object", "field", "signal"});
            if (textParam(p, "generation") != generation) {
                reply(socket, failure("stale_generation", "Data changed; discover signal sources again")); return;
            }
            const auto id = textParam(p, "object"), fieldId = textParam(p, "field");
            const int source = integer(p, "signal", -1, c->signalCount() - 1);
            QVariantMap field;
            for (const auto &object : c->dataObjects()) {
                const auto map = object.toMap();
                if (map.value("id").toString() != id) continue;
                for (const auto &value : map.value("fields").toList())
                    if (value.toMap().value("id").toString() == fieldId) field = value.toMap();
            }
            require(!field.isEmpty(), "Unknown object field");
            if (c->bindObjectField(id, fieldId, field.value("name").toString(), field.value("role").toString(), source).isEmpty()) {
                reply(socket, failure("rejected", "Object binding rejected")); return;
            }
            reply(socket, success()); return;
        }
        if (command == "object_trajectory" || command == "trajectory_status" || command == "trajectory_view") {
            keys(p, command == "object_trajectory" ? QStringList{"object", "plot"}
                : command == "trajectory_view" ? QStringList{"plot", "preset"} : QStringList{"plot"});
            const int plot = integer(p, "plot", 0, c->plotRows() * c->plotColumns() - 1);
            if (command == "object_trajectory" && !c->showObjectTrajectory(textParam(p, "object"), plot)) {
                reply(socket, failure("rejected", c->status())); return;
            }
            if (command == "trajectory_view" && !c->setTrajectoryView(plot, integer(p, "preset", 0, 3))) {
                reply(socket, failure("rejected", "Trajectory view is not attached or enabled")); return;
            }
            reply(socket, success(QJsonObject::fromVariantMap(c->trajectoryRenderState(plot)))); return;
        }
        if (command == "capture_window") {
            keys(p, {"path"}); const auto path = textParam(p, "path");
            require(QFileInfo(path).isAbsolute(), "path must be absolute");
            if (!c->captureAutomationWindow(path)) { reply(socket, failure("rejected", "Window capture rejected")); return; }
            reply(socket, success(QJsonObject{{"accepted", true}, {"completed", false}})); return;
        }
        if (command == "signals") {
            keys(p, {"offset", "limit", "logical"});
            const int offset = integer(p, "offset", 0, std::numeric_limits<int>::max(), 0);
            const int limit = integer(p, "limit", 1, 1000, 100);
            QVector<int> selected;
            if (p.value("logical").toBool()) selected = c->logicalSignalRows(); else for (int i = 0; i < c->signalCount(); ++i) selected.append(i);
            const int end = int(qMin(qint64(selected.size()), qint64(offset) + limit));
            QJsonArray entries;
            for (int at = offset; at < end; ++at) {
                const int i = selected[at];
                const auto snap = c->automationSnapshot({i});
                QJsonObject item{{"signal", i}, {"name", c->signalName(i)}, {"group", c->signalModel()->groupAt(i)},
                    {"originalName", c->originalSignalName(i)},
                    {"color", c->signalColor(i).name(QColor::HexArgb)}, {"width", c->signalWidth(i)}, {"style", c->signalStyle(i)}};
                if (!snap.series.isEmpty()) {
                    item["samples"] = double(snap.series.first()->sampleCount());
                    item["sourceFile"] = snap.series.first()->sourceFile;
                    item["timeOffset"] = snap.series.first()->timeOffset;
                }
                entries.append(item);
            }
            reply(socket, success(QJsonObject{{"generation", generation}, {"signals", entries}, {"total", selected.size()},
                {"nextOffset", end < selected.size() ? QJsonValue(end) : QJsonValue(QJsonValue::Null)}})); return;
        }
        int row = -1;
        if (QStringList{"samples", "statistics", "nearest", "plot_signal", "style", "rename", "offset"}.contains(command)) {
            if (textParam(p, "generation") != generation) {
                reply(socket, failure("stale_generation", "Data changed; list signals again before using signal IDs")); return;
            }
            row = integer(p, "signal", 0, c->signalCount() - 1);
        }
        if (command == "samples" || command == "statistics" || command == "nearest") {
            if (command == "samples") {
                keys(p, {"generation", "signal", "offset", "limit"});
                integer(p, "offset", 0, std::numeric_limits<int>::max(), 0); integer(p, "limit", 1, 10000, 1000);
            } else if (command == "nearest") { keys(p, {"generation", "signal", "time"}); number(p, "time"); }
            else {
                keys(p, {"generation", "signal", "start", "end"});
                if (p.contains("start")) number(p, "start");
                if (p.contains("end")) number(p, "end");
                if (p.contains("start") && p.contains("end")) require(number(p, "start") <= number(p, "end"), "start must be <= end");
            }
            if (m_analyzing) { reply(socket, failure("busy", "One analysis is already running")); return; }
            const auto snapshot = c->automationSnapshot({row});
            require(snapshot.series.size() == 1, "Signal has no published samples");
            m_analyzing = true;
            const QPointer<QTcpSocket> target(socket);
            const auto jobId = socket->property("jobId").toString();
            const auto cancelled = m_jobs.contains(jobId) ? m_jobs[jobId].cancelled : m_cancelled;
            m_workers.start(QRunnable::create([this, target, command, p, snapshot, generation, cancelled, jobId]() {
                QJsonObject result;
                try { result = analyze(command, p, snapshot, generation, cancelled); }
                catch (const std::exception &) { result = failure("analysis_failed", "Analysis could not complete"); }
                QMetaObject::invokeMethod(this, [this, target, result, jobId]() {
                    m_analyzing = false;
                    if (target && target->state() == QAbstractSocket::ConnectedState) reply(target, result);
                    else completeJob(jobId, result.value("result").toObject(), result.value("error").toObject().value("message").toString());
                }, Qt::QueuedConnection);
            }));
            return;
        }
        if (command == "layout") {
            keys(p, {"rows", "columns"});
            const int rows = integer(p, "rows", 1, 8), columns = integer(p, "columns", 1, 8);
            c->setLayout(rows, columns);
        } else if (command == "plot_signal") {
            keys(p, {"generation", "signal", "plot", "enabled"});
            const int plot = integer(p, "plot", 0, c->plotRows() * c->plotColumns() - 1);
            require(p.value("enabled").isBool(), "enabled must be boolean");
            require(!c->trajectoryState(plot).value("enabled").toBool(), "Switch subplot to time mode first");
            if (p.value("enabled").toBool()) c->addSignalToPlot(plot, row); else c->removeLegendSignal(plot, row);
        } else if (command == "style") {
            keys(p, {"generation", "signal", "color", "width", "style"});
            const QColor color(textParam(p, "color")); const double width = number(p, "width");
            const int style = integer(p, "style", 1, 5);
            require(color.isValid() && width >= 1 && width <= 20, "Invalid color or width");
            c->setSignalPen(row, color, width, style);
        } else if (command == "rename") {
            keys(p, {"generation", "signal", "name"});
            require(c->renameSignal(row, textParam(p, "name")), "Signal rename rejected");
        } else if (command == "offset") {
            keys(p, {"generation", "signal", "seconds"});
            require(c->setTimeOffset(0, row, {}, number(p, "seconds")), "Time offset rejected");
        } else if (command == "cursor_mode") {
            keys(p, {"mode"}); c->setCursorMode(integer(p, "mode", 0, 2));
        } else if (command == "fit") { keys(p, {}); c->fitAllPlots(); }
        else {
            keys(p, {"path"});
            const QString path = textParam(p, "path");
            require(QFileInfo(path).isAbsolute(), "path must be absolute");
            if (command == "load") {
                require(QFileInfo(path).isFile() && QFileInfo(path).isReadable(), "Input file is not readable");
                const int accepted = c->loadFiles(path);
                if (!accepted) { reply(socket, failure("rejected", c->status())); return; }
                reply(socket, success(QJsonObject{{"accepted", accepted}, {"completed", false}})); return;
            }
            if (command == "save_session") {
                if (!c->saveSession(path)) { reply(socket, failure("save_failed", c->status())); return; }
            } else if (command == "restore_session") {
                if (!c->restoreSession(path)) { reply(socket, failure("restore_failed", c->status())); return; }
                reply(socket, success(QJsonObject{{"accepted", true}, {"completed", false}})); return;
            }
        }
        reply(socket, success(QJsonObject{{"generation", m_epoch + ':' + QString::number(c->automationSnapshot().generation)}}));
    } catch (const std::invalid_argument &error) {
        reply(socket, failure("invalid_params", QString::fromUtf8(error.what())));
    }
}
