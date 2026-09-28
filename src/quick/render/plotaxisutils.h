#pragma once

#include <QPair>
#include <QVariantList>
#include <optional>

QVariantList makePlotAxisTicks(double minimum, double maximum, int targetIntervals = 10);

// Returns a finite, representable range containing both endpoints, or no range
// when even the unpadded span cannot be represented as a double.
std::optional<QPair<double, double>> paddedPlotRange(
    double minimum, double maximum, double fraction, double flatPadding);
