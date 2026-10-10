#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QtMath>
namespace JsonValidation {
inline bool integer(const QJsonValue &value, int minimum, int maximum, int *out)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!qIsFinite(number) || number < minimum || number > maximum || qFloor(number) != number)
        return false;
    *out = int(number);
    return true;
}
inline bool number(const QJsonValue &value, double *out)
{
    if (!value.isDouble() || !qIsFinite(value.toDouble())) return false;
    *out = value.toDouble();
    return true;
}
inline bool range(const QJsonValue &value, double *minimum, double *maximum)
{
    const auto array = value.toArray();
    return value.isArray() && array.size() == 2
        && number(array[0], minimum) && number(array[1], maximum)
        && *minimum < *maximum && qIsFinite(*maximum - *minimum);
}
inline bool text(const QJsonValue &value, QString *out, bool allowEmpty = false)
{
    if (!value.isString() || value.toString().size() > 32768) return false;
    *out = value.toString();
    return !out->contains(QChar::Null) && (allowEmpty || !out->trimmed().isEmpty());
}

}
