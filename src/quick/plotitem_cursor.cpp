// Raw-sample cursor navigation and readouts.
#include "plotitem.h"
#include <QMutexLocker>
#include <QtMath>
#include <optional>

namespace {

std::optional<double> adjacentRawX(const PlotSeriesSnapshot &snapshot,
                                   double currentX, int direction)
{
    if (!qIsFinite(currentX) || direction == 0)
        return std::nullopt;

    const bool forward = direction > 0;
    std::optional<double> best;
    const auto consider = [&](double x) {
        if (!qIsFinite(x) || (forward ? x <= currentX : x >= currentX))
            return;
        if (!best || (forward ? x < *best : x > *best))
            best = x;
    };

    for (const PlotSeriesDataPtr &series : snapshot.series) {
        if (!series) continue;
        const qsizetype count = series->sampleCount();
        if (series->monotonicTime) {
            qsizetype lo = 0;
            qsizetype hi = count;
            while (lo < hi) {
                const qsizetype mid = lo + (hi - lo) / 2;
                const double x = series->pointAt(mid).x();
                if (forward ? x <= currentX : x < currentX)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (forward) {
                for (qsizetype i = lo; i < count; ++i) {
                    const double x = series->pointAt(i).x();
                    if (qIsFinite(x)) { consider(x); break; }
                }
            } else {
                for (qsizetype i = lo - 1; i >= 0; --i) {
                    const double x = series->pointAt(i).x();
                    if (qIsFinite(x)) { consider(x); break; }
                }
            }
        } else {
            for (qsizetype i = 0; i < count; ++i)
                consider(series->pointAt(i).x());
        }
    }
    return best;
}

} // namespace

int PlotItem::activeCursorIndex() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_cursorMode == DoubleCursor ? m_activeCursorIndex : 1;
}

void PlotItem::setActiveCursorIndex(int index)
{
    {
        QMutexLocker lock(&m_dataMutex);
        if (index < (m_cursorMode == DoubleCursor ? 0 : 1) || index > (m_cursorMode == DoubleCursor ? 2 : 1)
            || m_activeCursorIndex == index) return;
        m_activeCursorIndex = index;
    }
    emit activeCursorChanged();
}

void PlotItem::setCursorMode(int mode)
{
    const int normalized = qBound(static_cast<int>(NoCursor), mode, static_cast<int>(DoubleCursor));
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_cursorMode == normalized) return;
        const bool wasDisabled = m_cursorMode == NoCursor;
        const bool addingSecond = m_cursorMode == SingleCursor && normalized == DoubleCursor;
        m_cursorMode = normalized;
        if (wasDisabled && normalized != NoCursor) {
            const double span = qMax(m_xMaximum - m_xMinimum, 1e-12);
            // Snap the initial positions to raw samples like an interactive drag would.
            m_cursorX1 = nearestRawX(m_xMinimum + span * .25);
            m_cursorX2 = nearestRawX(m_xMinimum + span * .75);
        }
        if (addingSecond) {
            const double span = qMax(m_xMaximum - m_xMinimum, 1e-12);
            // Preserve cursor 1, even off-screen; place the new cursor in this view.
            const double fraction = m_cursorX1 > m_xMinimum + span * .5 ? .25 : .75;
            const double target = m_xMinimum + span * fraction;
            const double snapped = nearestRawX(target);
            m_cursorX2 = snapped >= m_xMinimum && snapped <= m_xMaximum ? snapped : target;
        }
        m_activeCursorIndex = addingSecond ? 2 : 1;

        updateCursorValuesLocked();
        rebuildTicksLocked();
    }
    emit cursorChanged();
    emit activeCursorChanged();
    emit cursorDeltaTChanged();
    emit cursorValuesChanged();
    update();
}
void PlotItem::setCursorX(double x, int cursorIndex)
{
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_cursorMode == NoCursor) return;
        const double clamped = qBound(m_xMinimum, x, m_xMaximum);
        const double snapped = nearestRawX(clamped);
        if ((cursorIndex == 2 ? m_cursorX2 : m_cursorX1) == snapped) return;
        if (cursorIndex == 2) m_cursorX2 = snapped;
        else m_cursorX1 = snapped;

        updateCursorValuesLocked();
    }
    emit cursorChanged(); emit cursorDeltaTChanged(); emit cursorValuesChanged(); update();
}
void PlotItem::moveCursorPair(double startX1, double startX2, double offset)
{
    if (!qIsFinite(startX1) || !qIsFinite(startX2) || !qIsFinite(offset) || offset == 0) return;
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_cursorMode != DoubleCursor) return;
        const double lower = m_xMinimum - qMin(startX1, startX2);
        const double upper = m_xMaximum - qMax(startX1, startX2);
        const bool contained = lower <= 0 && upper >= 0;
        if (contained) offset = qBound(lower, offset, upper);
        offset = nearestRawX(startX1 + offset) - startX1;
        if (contained) offset = qBound(lower, offset, upper);
        const double x1 = startX1 + offset, x2 = startX2 + offset;
        if (x1 == m_cursorX1 && x2 == m_cursorX2) return;
        m_cursorX1 = x1;
        m_cursorX2 = x2;
        updateCursorValuesLocked();
    }
    emit cursorChanged(); emit cursorDeltaTChanged(); emit cursorValuesChanged(); update();
}

void PlotItem::setCursorPosition(double x, int cursorIndex)
{
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_cursorMode == NoCursor) return;
        const double clamped = qBound(m_xMinimum, x, m_xMaximum);
        if ((cursorIndex == 2 ? m_cursorX2 : m_cursorX1) == clamped) return;
        if (cursorIndex == 2) m_cursorX2 = clamped;
        else m_cursorX1 = clamped;

        updateCursorValuesLocked();
    }
    emit cursorChanged(); emit cursorDeltaTChanged(); emit cursorValuesChanged(); update();
}

void PlotItem::restoreCursorState(int mode, double x1, double x2)
{
    if (mode < NoCursor || mode > DoubleCursor || !qIsFinite(x1) || !qIsFinite(x2)) return;
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_cursorMode == mode && m_cursorX1 == x1 && m_cursorX2 == x2) return;
        m_cursorMode = mode;
        m_cursorX1 = x1;
        m_cursorX2 = x2;

        updateCursorValuesLocked();
        rebuildTicksLocked();
    }
    emit cursorChanged();
    emit activeCursorChanged();
    emit cursorDeltaTChanged();
    emit cursorValuesChanged();
    update();
}

void PlotItem::stepCursor(int direction, int cursorIndex)
{
    if (direction == 0 || cursorIndex < -1 || cursorIndex > 2)
        return;

    bool changed = false;
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_cursorMode == NoCursor)
            return;
        const bool selectedPair = cursorIndex == -1 && m_cursorMode == DoubleCursor && m_activeCursorIndex == 0;
        if (selectedPair) {
            const auto next = adjacentRawX(m_seriesSnapshot, m_cursorX1, direction);
            const double offset = next ? *next - m_cursorX1
                : (m_seriesSnapshot.series.isEmpty() ? (direction > 0 ? 1 : -1) * (m_xMaximum - m_xMinimum) / 100.0 : 0);
            const double x1 = m_cursorX1, x2 = m_cursorX2;
            lock.unlock();
            moveCursorPair(x1, x2, offset);
            return;
        }
        if (cursorIndex == -1)
            cursorIndex = m_cursorMode == DoubleCursor ? m_activeCursorIndex : 1;

        const int lastTarget = m_cursorMode == DoubleCursor ? 2 : 1;
        for (int target = 1; target <= lastTarget; ++target) {
            if (cursorIndex != 0 && target != cursorIndex)
                continue;
            const double current = target == 2 ? m_cursorX2 : m_cursorX1;
            const auto next = adjacentRawX(m_seriesSnapshot, current, direction);
            // Without raw samples (no signal bound, or the cursor already sits
            // on the outermost sample of an empty plot) fall back to a
            // view-relative step so the keyboard still drives the cursor.
            const double fallback = current
                    + (direction > 0 ? 1 : -1) * (m_xMaximum - m_xMinimum) / 100.0;
            const double candidate = next ? *next
                                          : (m_seriesSnapshot.series.isEmpty() ? fallback
                                                                               : current);
            const double clamped = qBound(m_xMinimum, candidate, m_xMaximum);
            if (clamped == current)
                continue;
            if (target == 2)
                m_cursorX2 = clamped;
            else
                m_cursorX1 = clamped;

            changed = true;
        }

        if (!changed)
            return;
        updateCursorValuesLocked();
    }

    emit cursorChanged();
    emit cursorDeltaTChanged();
    emit cursorValuesChanged();
    update();
}
double PlotItem::nearestRawX(double x) const
{
    const auto nearest = PlotSeriesStore::nearestX(m_seriesSnapshot, x);
    return nearest.has_value() ? *nearest : x;
}

void PlotItem::updateCursorValuesLocked()
{
    m_cursorReadouts.clear();
    if (m_cursorMode == NoCursor) return;
    const double keys[] = {m_cursorX1, m_cursorX2};
    const int cursorCount = m_cursorMode == DoubleCursor ? 2 : 1;
    QHash<PlotSeriesId, PlotSample> samplesByCursor[2];
    for (int c = 0; c < cursorCount; ++c)
        if (keys[c] >= m_xMinimum && keys[c] <= m_xMaximum)
        for (const auto &sample : PlotSeriesStore::nearestSamples(m_seriesSnapshot, keys[c]))
            samplesByCursor[c].insert(sample.id, sample);
    for (int s = 0; s < m_seriesSnapshot.series.size(); ++s) {
        for (int c = 0; c < cursorCount; ++c) {
            double value = qQNaN();
            QColor color;
            const auto sample = samplesByCursor[c].constFind(m_seriesSnapshot.series.at(s)->id);
            if (sample != samplesByCursor[c].cend()) {
                value = sample->y;
                color = sample->color;
            }
            if (qIsFinite(value)) {
                QString rawText = QString::number(value, 'f', 12);
                while (rawText.contains(QLatin1Char('.')) && rawText.endsWith(QLatin1Char('0')))
                    rawText.chop(1);
                if (rawText.endsWith(QLatin1Char('.'))) rawText.chop(1);
                m_cursorReadouts.append(QVariantMap{{QStringLiteral("x"), keys[c]},
                                                    {QStringLiteral("y"), value},
                                                    {QStringLiteral("displayY"), normalizedYLocked(m_seriesSnapshot.series.at(s)->id, value)},
                                                    {QStringLiteral("seriesId"), m_seriesSnapshot.series.at(s)->id},
                                                    {QStringLiteral("cursorIndex"), c + 1},
                                                    {QStringLiteral("sampleX"), sample->x},
                                                    {QStringLiteral("text"), QString::number(value, 'g', 6)},
                                                    {QStringLiteral("rawText"), rawText},
                                                    {QStringLiteral("color"), color}});
            }
        }
    }
}

bool PlotItem::cursorHit(double pixelX, int *cursorIndex) const
{
    if (m_cursorMode == NoCursor || width() <= 0) return false;
    const double scale = width() / qMax(m_xMaximum - m_xMinimum, 1e-12);
    const double tolerance = 8.0;
    const double d1 = qAbs((m_cursorX1 - m_xMinimum) * scale - pixelX);
    const double d2 = qAbs((m_cursorX2 - m_xMinimum) * scale - pixelX);
    if (d1 <= tolerance && (m_cursorMode != DoubleCursor || d1 <= d2)) { if (cursorIndex) *cursorIndex = 1; return true; }
    if (m_cursorMode == DoubleCursor && d2 <= tolerance) { if (cursorIndex) *cursorIndex = 2; return true; }
    return false;
}
