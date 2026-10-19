pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import DataInspector

Menu {
    id: menu
    required property AppController controller
    required property int plotIndex
    required property string currentMode
    title: "切换视图"
    objectName: "switchViewMenu"
    MenuItem {
        objectName: "curveModeMenuItem"
        text: "曲线视图"
        checkable: true
        checked: menu.currentMode === "curve"
        onTriggered: {
            const state = menu.controller.trajectoryState(menu.plotIndex)
            if (menu.controller.configureTrajectory(menu.plotIndex, false, state.x, state.y, state.z, state.geographic))
                menu.controller.setStateTimelineEnabled(menu.plotIndex, false)
        }
    }
    MenuItem {
        objectName: "trajectoryModeMenuItem"
        text: "航迹视图"
        checkable: true
        checked: menu.currentMode === "trajectory"
        onTriggered: menu.controller.enterTrajectoryMode(menu.plotIndex)
    }
    MenuItem {
        objectName: "switchToStateTimeline"
        text: "状态时间图"
        checkable: true
        checked: menu.currentMode === "states"
        onTriggered: menu.controller.setStateTimelineEnabled(menu.plotIndex, true)
    }
}
