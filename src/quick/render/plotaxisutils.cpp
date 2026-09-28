#include "plotaxisutils.h"

#include <QtMath>
#include <cmath>
#include <limits>

std::optional<QPair<double, double>> paddedPlotRange(
    double minimum, double maximum, double fraction, double flatPadding)
{
    const double span = maximum - minimum;
    if (!qIsFinite(minimum) || !qIsFinite(maximum) || minimum > maximum
        || !qIsFinite(span) || !qIsFinite(fraction) || fraction < 0
        || !qIsFinite(flatPadding) || flatPadding <= 0) return {};
    const double padding = span > 0 ? span * fraction : flatPadding;
    double lo = minimum - padding, hi = maximum + padding;
    if (lo == minimum) lo = std::nextafter(minimum, -std::numeric_limits<double>::infinity());
    if (hi == maximum) hi = std::nextafter(maximum, std::numeric_limits<double>::infinity());
    if (!qIsFinite(lo)) lo = minimum;
    if (!qIsFinite(hi)) hi = maximum;
    if (!qIsFinite(hi - lo)) { lo = minimum; hi = maximum; }
    if (!(hi > lo)) return {};
    return qMakePair(lo, hi);
}

QVariantList makePlotAxisTicks(double minimum, double maximum, int targetIntervals)
{
    QVariantList ticks;
    const double span = maximum - minimum;
    if (!qIsFinite(minimum) || !qIsFinite(maximum) || !(span > 0) || !qIsFinite(span))
        return ticks;

    QVector<double> values;
    targetIntervals = qBound(2, targetIntervals, 24);
    const double raw = span / targetIntervals;
    if (raw > 0) {
        const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
        if (magnitude > 0 && qIsFinite(magnitude)) {
            const double normalized = raw / magnitude;
            const double step = (normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 2.5 ? 2.5 : normalized <= 5 ? 5 : 10) * magnitude;
            // Never convert an absolute tick index to an integer: Unix time and
            // narrow windows routinely produce indices beyond 32-bit limits.
            const double first = std::ceil(minimum / step) * step;
            for (int i = 0; i < targetIntervals + 2; ++i) {
                const double value = std::fma(double(i), step, first);
                if (!qIsFinite(value) || value > maximum) break;
                if (value >= minimum && (values.isEmpty() || value > values.last()))
                    values.append(value);
            }
        }
    }
    // Subnormal steps and windows only one ULP wide cannot use regular ticks.
    if (values.size() < 2) values = {minimum, maximum};

    QStringList labels;
    for (int precision = 12; precision <= 17; ++precision) {
        labels.clear();
        bool distinct = true;
        for (double value : values) {
            const QString label = QString::number(value, 'g', precision);
            if (!labels.isEmpty() && label == labels.last()) distinct = false;
            labels.append(label);
        }
        if (distinct) break;
    }
    for (qsizetype i = 0; i < values.size(); ++i)
        ticks.append(QVariantMap{{QStringLiteral("value"), values.at(i)},
                                 {QStringLiteral("label"), labels.at(i)}});
    return ticks;
}
