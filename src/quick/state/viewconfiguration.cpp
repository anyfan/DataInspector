#include "viewconfiguration.h"
#include "jsonvalidation.h"
namespace {
using namespace JsonValidation;
constexpr int maxSignals = 100000;
QJsonObject trackToJson(const SessionTrajectoryEntry &track)
{
    QJsonArray sources; for (int id : track.attitude.sources) sources.append(id);
    return {{"id", track.id}, {"objectId", track.objectId}, {"name", track.name}, {"visible", track.visible}, {"geographic", track.geographic},
        {"color", track.color.name(QColor::HexArgb)}, {"width", track.width},
        {"axes", QJsonArray{track.axes[0], track.axes[1], track.axes[2]}},
        {"attitude", QJsonObject{{"mode", track.attitude.mode}, {"sources", sources},
            {"radians", track.attitude.radians}, {"order", track.attitude.order},
            {"scalarLast", track.attitude.scalarLast}, {"navigationToBody", track.attitude.navigationToBody}}}};
}
bool readTrack(const QJsonValue &value, int signalCount, SessionTrajectoryEntry *track)
{
    if (!value.isObject()) return false;
    const auto obj = value.toObject(); QString color;
    if (obj.contains("objectId") && !text(obj.value("objectId"), &track->objectId, true)) return false;
    if (!text(obj.value("id"), &track->id) || track->id.size() > 128
        || !text(obj.value("name"), &track->name) || track->name.size() > 256
        || !obj.value("visible").isBool() || !obj.value("geographic").isBool()
        || !text(obj.value("color"), &color) || !(track->color = QColor(color)).isValid()
        || !number(obj.value("width"), &track->width) || track->width < 1 || track->width > 12) return false;
    track->visible = obj.value("visible").toBool(); track->geographic = obj.value("geographic").toBool();
    const auto axes = obj.value("axes").toArray(); QSet<int> used;
    if (axes.size() != 3) return false;
    for (int a = 0; a < 3; ++a) {
        if (!integer(axes[a], -1, signalCount - 1, &track->axes[a])) return false;
        if (track->axes[a] >= 0) { if (used.contains(track->axes[a])) return false; used.insert(track->axes[a]); }
    }
    if (!obj.value("attitude").isObject()) return false;
    const auto attitude = obj.value("attitude").toObject(); auto &config = track->attitude;
    if (!integer(attitude.value("mode"), 0, 2, &config.mode) || !integer(attitude.value("order"), 0, 1, &config.order)
        || !attitude.value("radians").isBool() || !attitude.value("scalarLast").isBool()
        || !attitude.value("navigationToBody").isBool()) return false;
    config.radians = attitude.value("radians").toBool(); config.scalarLast = attitude.value("scalarLast").toBool();
    config.navigationToBody = attitude.value("navigationToBody").toBool();
    const auto sources = attitude.value("sources").toArray(); if (sources.size() != 4) return false;
    for (int a = 0; a < 4; ++a) if (!integer(sources[a], -1, signalCount - 1, &config.sources[a])) return false;
    return true;
}


}
QJsonObject viewConfigurationToJson(const ViewConfiguration &s)
{
    QJsonArray plots;
    for (const auto &plot : s.plots) {
        QJsonArray bindings;
        QJsonArray rotation;
        QJsonArray available;
        QJsonArray tracks; for (const auto &track : plot.trajectory.entries()) tracks.append(trackToJson(track));
        for (int id : plot.trajectory.signalIds) available.append(id);
        if (plot.trajectory.camera.freeRotation)
            for (double component : plot.trajectory.camera.rotation) rotation.append(component);
        for (int signal : plot.seriesIds) bindings.append(signal);
        plots.append(QJsonObject{{"signals", bindings},
            {"yRange", QJsonArray{plot.yMinimum, plot.yMaximum}},
            {"normalizeY", plot.normalizeY}, {"lineWidth", plot.lineWidth},
            {"trajectory", QJsonObject{{"enabled", plot.trajectory.enabled},
                {"geographic", plot.trajectory.activeEntry().geographic},
                {"tracks", tracks}, {"active", plot.trajectory.active},
                {"signals", available},
                {"rotation", rotation}, {"viewScale", plot.trajectory.camera.viewScale},
                {"panDepth", plot.trajectory.camera.panDepth},
                {"axes", QJsonArray{plot.trajectory.activeEntry().axes[0], plot.trajectory.activeEntry().axes[1], plot.trajectory.activeEntry().axes[2]}},
                {"camera", QJsonArray{plot.trajectory.camera.azimuth, plot.trajectory.camera.elevation,
                    plot.trajectory.camera.zoom, plot.trajectory.camera.panX, plot.trajectory.camera.panY}}}}});
    }
    return {{"plots", plots},
        {"layout", QJsonObject{{"rows", s.rows}, {"columns", s.columns}, {"active", s.active}, {"solo", s.solo}}},
        {"xRange", QJsonArray{s.xMinimum, s.xMaximum}}};
}
bool viewConfigurationFromJson(const QJsonObject &root, int signalCount,
    ViewConfiguration *view, QString *error, int version, const QVector<QColor> &legacyColors)
{
    auto fail = [&](const QString &field) { *error = QStringLiteral("视图配置无效：%1").arg(field); return false; };
    ViewConfiguration parsed;
    const auto layout = root.value("layout").toObject();
    if (!integer(layout.value("rows"), 1, 8, &parsed.rows)
        || !integer(layout.value("columns"), 1, 8, &parsed.columns)
        || !integer(layout.value("active"), 0, parsed.rows * parsed.columns - 1, &parsed.active)
        || !integer(layout.value("solo"), -1, parsed.rows * parsed.columns - 1, &parsed.solo)
        || (parsed.solo >= 0 && parsed.solo != parsed.active)) return fail("layout");
    if (!range(root.value("xRange"), &parsed.xMinimum, &parsed.xMaximum)) return fail("xRange");
    if (!root.value("plots").isArray()
        || root.value("plots").toArray().size() != parsed.rows * parsed.columns) return fail("plots");
    parsed.plots.clear();
    for (const auto &entry : root.value("plots").toArray()) {
        const auto obj = entry.toObject();
        SessionPlot plot;
        if (!range(obj.value("yRange"), &plot.yMinimum, &plot.yMaximum)
            || !obj.value("normalizeY").isBool()
            || !number(obj.value("lineWidth"), &plot.lineWidth) || plot.lineWidth < 1 || plot.lineWidth > 12
            || !obj.value("signals").isArray()
            || obj.value("signals").toArray().size() > maxSignals) return fail("plots[]");
        plot.normalizeY = obj.value("normalizeY").toBool();
        QSet<int> used;
        for (const auto &binding : obj.value("signals").toArray()) {
            int signal = -1;
            if (!integer(binding, 0, signalCount - 1, &signal) || used.contains(signal))
                return fail("plots.signals");
            used.insert(signal); plot.seriesIds.append(signal);
        }
        if (version >= 2) {
            const auto trajectory = obj.value("trajectory").toObject();
            if (version >= 6) {
                const auto tracks = trajectory.value("tracks").toArray();
                if (tracks.isEmpty() || tracks.size() > 64 || !integer(trajectory.value("active"), 0, tracks.size() - 1, &plot.trajectory.active)) return fail("trajectory.tracks/active");
                plot.trajectory.tracks.clear(); QSet<QString> ids;
                for (const auto &entry : tracks) {
                    SessionTrajectoryEntry track;
                    if (!readTrack(entry, signalCount, &track) || ids.contains(track.id)) return fail("trajectory.tracks[]");
                    ids.insert(track.id); plot.trajectory.tracks.append(track);
                }

                const auto activeTrack = trackToJson(plot.trajectory.activeEntry());
                if (trajectory.value("axes") != activeTrack.value("axes") || trajectory.value("geographic") != activeTrack.value("geographic")) return fail("trajectory.active fields");
            }
            const auto axes = trajectory.value("axes").toArray();
            const auto camera = trajectory.value("camera").toArray();
            if (!trajectory.value("enabled").isBool() || axes.size() != 3 || camera.size() != 5)
                return fail("plots.trajectory");
            plot.trajectory.enabled = trajectory.value("enabled").toBool();
            if (version < 3) plot.trajectory.activeEntry().geographic = false;
            if (version >= 3) {
                if (!trajectory.value("geographic").isBool()) return fail("plots.trajectory.geographic");
                plot.trajectory.activeEntry().geographic = trajectory.value("geographic").toBool();
            }
            for (int axis = 0; axis < 3; ++axis)
                if (!integer(axes[axis], -1, signalCount - 1, &plot.trajectory.activeEntry().axes[axis]))
                    return fail("plots.trajectory.axes");
            if (version >= 5) {
                if (!trajectory.value("signals").isArray() || trajectory.value("signals").toArray().size() > maxSignals)
                    return fail("plots.trajectory.signals");
                QSet<int> available;
                for (const auto &entry : trajectory.value("signals").toArray()) {
                    int id = -1;
                    if (!integer(entry, 0, signalCount - 1, &id) || available.contains(id))
                        return fail("plots.trajectory.signals");
                    available.insert(id); plot.trajectory.signalIds.append(id);
                }
                for (int id : plot.trajectory.activeEntry().axes) if (id >= 0 && !available.contains(id))
                    return fail("plots.trajectory.axes");
                if (version >= 6) {
                    std::optional<bool> geographic;
                    for (const auto &track : plot.trajectory.entries()) {
                        for (int id : track.axes) if (id >= 0 && !available.contains(id)) return fail("trajectory.tracks.axes");
                        for (int id : track.attitude.sources) if (id >= 0 && !available.contains(id)) return fail("trajectory.attitude.sources");
                        if (std::any_of(track.axes.cbegin(), track.axes.cend(), [](int id) { return id >= 0; })) {
                            if (geographic && *geographic != track.geographic) return fail("trajectory.mixed coordinates");
                            geographic = track.geographic;
                        }
                    }
                }
            } else {
                for (int id : plot.trajectory.activeEntry().axes) if (id >= 0 && !plot.trajectory.signalIds.contains(id))
                    plot.trajectory.signalIds.append(id);
            }
            auto &view = plot.trajectory.camera;
            if (!number(camera[0], &view.azimuth) || !number(camera[1], &view.elevation)
                || !number(camera[2], &view.zoom) || !number(camera[3], &view.panX)
                || !number(camera[4], &view.panY) || !view.valid()) return fail("plots.trajectory.camera");
            if (version >= 4) {
                if (!trajectory.value("rotation").isArray()) return fail("plots.trajectory.rotation");
                const auto rotation = trajectory.value("rotation").toArray();
                if (!rotation.isEmpty() && rotation.size() != 4) return fail("plots.trajectory.rotation");
                view.freeRotation = !rotation.isEmpty();
                for (int i = 0; i < rotation.size(); ++i)
                    if (!number(rotation[i], &view.rotation[i])) return fail("plots.trajectory.rotation");
                if (!number(trajectory.value("viewScale"), &view.viewScale)
                    || !number(trajectory.value("panDepth"), &view.panDepth) || !view.valid())
                    return fail("plots.trajectory.camera");
            }
        }
        if (version < 6) {

            // Legacy trajectory colour came from its first time source.
            for (int id : plot.trajectory.activeEntry().axes) if (id >= 0) { plot.trajectory.activeEntry().color = legacyColors.value(id); break; }
        }
        parsed.plots.append(plot);
    }
    *view = parsed;
    return true;
}
QSet<int> referencedViewSignals(const ViewConfiguration &view)
{
    QSet<int> ids;
    for (const auto &plot : view.plots) {
        for (int id : plot.seriesIds) ids.insert(id);
        for (int id : plot.trajectory.signalIds) ids.insert(id);
        for (const auto &track : plot.trajectory.entries()) {
            for (int id : track.axes) if (id >= 0) ids.insert(id);
            for (int id : track.attitude.sources) if (id >= 0) ids.insert(id);
        }
    }
    return ids;
}