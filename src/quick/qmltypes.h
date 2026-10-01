#pragma once

#include <QtQml/qqmlregistration.h>
#include "appcontroller.h"
#include "plotitem.h"
#include "trajectoryitem.h"
#include "signalmodel.h"

// Keep QML registration and tooling metadata together without coupling the
// controller/model headers (also used by non-QML tests) to QtQml.
struct PlotItemQmlRegistration
{
    Q_GADGET
    QML_FOREIGN(PlotItem)
    QML_NAMED_ELEMENT(PlotItem)
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
