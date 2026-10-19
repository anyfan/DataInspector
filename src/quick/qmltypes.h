#pragma once

#include <QtQml/qqmlregistration.h>
#include "controller/appcontroller.h"
#include "items/plotitem.h"
#include "items/trajectoryitem.h"
#include "items/statetimelineitem.h"
#include "models/signalmodel.h"

// Keep QML registration and tooling metadata together without coupling the
// controller/model headers (also used by non-QML tests) to QtQml.
struct PlotItemQmlRegistration
{
    Q_GADGET
    QML_FOREIGN(PlotItem)
    QML_NAMED_ELEMENT(PlotItem)
};
struct StateTimelineItemQmlRegistration {
    Q_GADGET
    QML_FOREIGN(StateTimelineItem)
    QML_NAMED_ELEMENT(StateTimelineItem)
};

struct AppControllerQmlRegistration
{
    Q_GADGET
    QML_FOREIGN(AppController)
    QML_NAMED_ELEMENT(AppController)
    QML_UNCREATABLE("AppController is owned by the application")
};

struct SignalModelQmlRegistration
{
    Q_GADGET
    QML_FOREIGN(SignalModel)
    QML_NAMED_ELEMENT(SignalModel)
    QML_UNCREATABLE("SignalModel is owned by AppController")
};

struct TrajectoryItemQmlRegistration
{
    Q_GADGET
    QML_FOREIGN(TrajectoryItem)
    QML_NAMED_ELEMENT(TrajectoryItem)
};
