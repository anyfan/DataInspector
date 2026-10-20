#include "statetimelineitem.h"
#include <QThreadPool>
#include <QMutexLocker>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace {
QThreadPool &statePool() {
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(2); return true; }();
    Q_UNUSED(configured); return pool;
}
}
StateTimelineItem::StateTimelineItem(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true); setClip(true);
    m_poll.setInterval(16); m_debounce.setSingleShot(true); m_debounce.setInterval(25);
    connect(&m_poll, &QTimer::timeout, this, &StateTimelineItem::finish);
    connect(&m_debounce, &QTimer::timeout, this, &StateTimelineItem::start);
}
StateTimelineItem::~StateTimelineItem() { if (m_job) m_job->cancelled = true; }
void StateTimelineItem::setSources(const StateTimelineConfig &config, const PlotSeriesSnapshot &sources) {
    bool sameEnums = config.lanes.size() == m_config.lanes.size();
    for (int i = 0; sameEnums && i < config.lanes.size(); ++i) {
        const auto &a = config.lanes[i]; const auto &b = m_config.lanes[i];
        sameEnums = a.values == b.values;
    }
    if (sameEnums && stateTimelineToJson(config) == stateTimelineToJson(m_config) && sources.series == m_sources.series) return;
    if (m_job) { m_job->cancelled = true; m_job.reset(); m_poll.stop(); }
    m_config = config; m_sources = sources; m_data.reset();
    { QMutexLocker lock(&m_mutex); m_frame.reset(); }
    emit sourcesChanged();
    update(); emit frameChanged(); request();
}
void StateTimelineItem::setMinimum(double value) { if (!std::isfinite(value) || value == m_minimum) return; m_minimum = value; emit viewportChanged(); request(); }
void StateTimelineItem::setMaximum(double value) { if (!std::isfinite(value) || value == m_maximum) return; m_maximum = value; emit viewportChanged(); request(); }
void StateTimelineItem::geometryChange(const QRectF &now, const QRectF &old) { QQuickItem::geometryChange(now, old); if (now.size() != old.size()) request(); }
void StateTimelineItem::request() {
    // Keep the last frame and project it into the current view while refreshing.
    // View changes coalesce without starving a running job or restarting its timer.
    {
        QMutexLocker lock(&m_mutex);
        m_displayMinimum = m_minimum; m_displayMaximum = m_maximum; m_displayWidth = width();
    }
    update();
    if (!m_debounce.isActive()) m_debounce.start();
    emit frameChanged();
}
void StateTimelineItem::start() {
    if (m_job) return;
    if (width() <= 0 || height() <= 0 || !(m_maximum > m_minimum) || !std::isfinite(m_maximum-m_minimum)) return;
    auto job = std::make_shared<Job>(); job->config = m_config; job->sources = m_sources; job->data = m_data;
    job->minimum = m_minimum; job->maximum = m_maximum; job->size = size(); m_job = job;
    statePool().start([job] {
        if (!job->data) job->data = StateTimeline::build(job->config, job->sources, job->cancelled);
        if (!job->data || job->cancelled) { job->done.store(true, std::memory_order_release); return; }
        auto frame = std::make_shared<Frame>();
        frame->minimum = job->minimum; frame->maximum = job->maximum; frame->width = job->size.width();
        const int pixels = int(qBound(1., std::ceil(job->size.width()), 4096.));
        const double span = job->maximum - job->minimum;
        constexpr double rowHeight = RowHeight;
        for (int row = 0; row < job->data->lanes.size(); ++row) {
            const auto &lane = job->data->lanes[row]; frame->names.append(lane.config.name);
            if (!lane.error.isEmpty()) frame->error += lane.config.name + ": " + lane.error + "\n";
            for (int pixel = 0; pixel < pixels; ++pixel) {
                if ((pixel & 255) == 0 && job->cancelled) { job->done.store(true, std::memory_order_release); return; }
                const double a = job->minimum + span * (double(pixel) / pixels), b = job->minimum + span * (double(pixel + 1) / pixels);
                auto next = std::upper_bound(lane.runs.cbegin(), lane.runs.cend(), a, [](double t, const StateRun &run) { return t < run.start; });
                const auto *run = StateTimeline::at(lane, (a + b) / 2);
                // A sub-bucket pulse can straddle a bucket boundary and miss both
                // midpoint samples. Preserve it even when each bucket sees only
                // one transition. Record edges and single steps are not pulses.
                const auto compressedPulse = [&](const StateRun &candidate) {
                    return !candidate.leftOpen && !candidate.rightOpen
                        && candidate.end > candidate.start
                        && candidate.start < b && candidate.end > a
                        && candidate.end-candidate.start < span/pixels;
                };
                const bool mixed = (next != lane.runs.cend() && next != lane.runs.cbegin()
                    && next+1 != lane.runs.cend() && (next+1)->start < b)
                    || (next != lane.runs.cbegin() && compressedPulse(*std::prev(next)))
                    || (next != lane.runs.cend() && compressedPulse(*next));
                if (!run && !mixed) continue;
                const QColor color = mixed ? QColor(pixel % 2 ? "#d6a0e8" : "#684180") : (!run->known && pixel % 4 < 2 ? QColor("#93a0ad") : StateTimeline::color(lane.config, *run));
                const QRectF rect(double(pixel) / pixels * job->size.width(), row * rowHeight + HeaderHeight + 3, job->size.width() / pixels + .05, rowHeight - HeaderHeight - 6);
                if (!frame->cells.isEmpty() && frame->cells.last().color == color && frame->cells.last().rect.y() == rect.y() && std::abs(frame->cells.last().rect.right() - rect.left()) < .1)
                    frame->cells.last().rect.setRight(rect.right());
                else frame->cells.append({rect, color});
            }
            auto first = std::lower_bound(lane.runs.cbegin(), lane.runs.cend(), job->minimum, [](const StateRun &run, double t) { return run.end < t; });
            for (auto it = first; it != lane.runs.cend(); ++it) {
                if (job->cancelled) { job->done.store(true, std::memory_order_release); return; }
                const auto &run = *it;
                if (run.end < job->minimum) continue;
                if (run.start > job->maximum || frame->labels.size() >= 512) break;
                const double x = qMax(0., (run.start - job->minimum) / span * job->size.width());
                const double right = qMin(job->size.width(), (run.end - job->minimum) / span * job->size.width());
                const auto background = StateTimeline::color(lane.config, run);
                const QString foreground = background.redF()*.299 + background.greenF()*.587 + background.blueF()*.114 > .6 ? "#101820" : "#ffffff";
                if (right - x >= 54 && rowHeight >= 18) frame->labels.append(QVariantMap{{"x", x}, {"y", row * rowHeight + HeaderHeight}, {"width", right-x}, {"height", rowHeight - HeaderHeight}, {"text", StateTimeline::text(lane.config, run)}, {"color", foreground}});
            }
        }
        job->frame = frame; job->done.store(true, std::memory_order_release);
    });
    m_poll.start(); emit frameChanged();
}
void StateTimelineItem::finish() {
    if (!m_job || !m_job->done.load(std::memory_order_acquire)) return;
    auto job = std::move(m_job); m_poll.stop();
    if (!job->cancelled) { m_data = job->data; QMutexLocker lock(&m_mutex); m_frame = job->frame; }
    if (job->minimum != m_minimum || job->maximum != m_maximum || job->size != size())
        if (!m_debounce.isActive()) m_debounce.start();
    update(); emit frameChanged();
}
bool StateTimelineItem::hasFrame() const { QMutexLocker lock(&m_mutex); return bool(m_frame); }
QVariantList StateTimelineItem::labels() const {
    QMutexLocker lock(&m_mutex);
    if (!m_frame || !(m_displayMaximum > m_displayMinimum) || m_frame->width <= 0) return {};
    const double span = m_displayMaximum-m_displayMinimum;
    const double offset = (m_frame->minimum-m_displayMinimum)/span*m_displayWidth;
    const double scale = (m_frame->maximum-m_frame->minimum)/span*m_displayWidth/m_frame->width;
    QVariantList projected;
    for (const auto &value : m_frame->labels) {
        auto label = value.toMap();
        const double left = offset+label["x"].toDouble()*scale;
        const double right = left+label["width"].toDouble()*scale;
        const double clippedLeft = qMax(0.,left), clippedRight = qMin(m_displayWidth,right);
        if (!std::isfinite(clippedLeft) || !std::isfinite(clippedRight) || clippedRight-clippedLeft < 54) continue;
        label["x"] = clippedLeft; label["width"] = clippedRight-clippedLeft;
        projected.append(label);
    }
    return projected;
}
QStringList StateTimelineItem::rowNames() const {
    QStringList names; for (const auto &lane : m_config.lanes) if (lane.visible) names.append(lane.name); return names;
}
QString StateTimelineItem::error() const { QMutexLocker lock(&m_mutex); return m_frame ? m_frame->error : QString(); }
QStringList StateTimelineItem::cursorValues(double time) const {
    QHash<int, double> samples;
    if (std::isfinite(time))
        for (const auto &sample : PlotSeriesStore::nearestSamples(m_sources, time))
            samples.insert(sample.id, sample.y);
    QStringList values;
    for (const auto &lane : m_config.lanes) {
        if (!lane.visible) continue;
        const auto sample = samples.constFind(lane.source);
        const auto value = sample == samples.cend() ? std::optional<double>{} : StateTimeline::decode(*sample);
        values.append(value ? QString::number(*value, 'f', 0) : QStringLiteral("—"));
    }
    return values;
}
QString StateTimelineItem::inspect(int row, double time) const {
    if (!m_data || row < 0 || row >= m_data->lanes.size()) return {};
    const auto &lane = m_data->lanes[row]; const auto *run = StateTimeline::at(lane, time);
    if (!run) return lane.error.isEmpty() ? QStringLiteral("记录范围外／未知") : lane.error;
    return QStringLiteral("%1：%2\n区间起点原始值：%3\n首次观察：%4 s；结束观察：%5 s\n按前值保持持续：%6 s%7\n边沿为首次观察时间，采样间实际翻转时间未知")
        .arg(lane.config.name, StateTimeline::text(lane.config, *run), QString::number(run->raw, 'g', 16), QString::number(run->start, 'g', 16), QString::number(run->end, 'g', 16), QString::number(run->end-run->start, 'g', 12),
            run->leftOpen || run->rightOpen ? QStringLiteral("（记录边界不完整）") : QString());
}
QVariant StateTimelineItem::transition(int row, double time, int direction) const {
    if (!m_data || row < 0 || row >= m_data->lanes.size() || !std::isfinite(time)) return {};
    const auto &runs = m_data->lanes[row].runs;
    if (runs.size() < 2) return {};
    if (direction > 0) {
        auto it = std::upper_bound(runs.cbegin()+1, runs.cend(), time, [](double t, const StateRun &r) { return t < r.start; });
        if (it != runs.cend()) return it->start;
    } else {
        auto it = std::lower_bound(runs.cbegin()+1, runs.cend(), time, [](const StateRun &r, double t) { return r.start < t; });
        if (it != runs.cbegin()+1) return (--it)->start;
    }
    return {};
}
QSGNode *StateTimelineItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    delete old;
    std::shared_ptr<const Frame> frame; double minimum, maximum, viewportWidth;
    { QMutexLocker lock(&m_mutex); frame = m_frame; minimum = m_displayMinimum; maximum = m_displayMaximum; viewportWidth = m_displayWidth; }
    if (!frame || frame->cells.isEmpty() || !(maximum > minimum) || frame->width <= 0) return nullptr;
    const double offset = (frame->minimum-minimum)/(maximum-minimum)*viewportWidth;
    const double scale = (frame->maximum-frame->minimum)/(maximum-minimum)*viewportWidth/frame->width;
    QVector<Cell> projected;
    for (const auto &cell : frame->cells) {
        const double left = qMax(0.,offset+cell.rect.left()*scale);
        const double right = qMin(viewportWidth,offset+cell.rect.right()*scale);
        if (std::isfinite(left) && std::isfinite(right) && right > left)
            projected.append({QRectF(left,cell.rect.y(),right-left,cell.rect.height()),cell.color});
    }
    if (projected.isEmpty()) return nullptr;
    auto *node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), projected.size()*6);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto *v = geometry->vertexDataAsColoredPoint2D();
    for (const auto &cell : projected) {
        const auto &r = cell.rect; const auto c = QColor::fromRgba(qPremultiply(cell.color.rgba()));
        const QPointF points[]{r.topLeft(), r.topRight(), r.bottomRight(), r.topLeft(), r.bottomRight(), r.bottomLeft()};
        for (const auto &p : points) (v++)->set(float(p.x()), float(p.y()), c.red(), c.green(), c.blue(), c.alpha());
    }
    node->setGeometry(geometry); node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(new QSGVertexColorMaterial); node->setFlag(QSGNode::OwnsMaterial); return node;
}
