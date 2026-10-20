pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DataInspector

Rectangle {
    id: root
    required property AppController controller
    required property int plotIndex
    required property color plotColor
    required property color textColor
    required property color axisColor
    required property real graphLineWidth
    signal editAxesRequested()
    signal editObjectRequested(string objectId)
    property bool darkTheme: false
    component TrackCombo: ThemedComboBox { darkTheme: root.darkTheme }
    readonly property var aircraftObjects: root.controller.dataObjects.filter(object => object.type === 'aircraft')
    enabled: !root.controller.restoringSession
    color: plotColor
    clip: true
    objectName: "trajectoryView"

    property var configuration: root.controller.trajectoryState(root.plotIndex)
    readonly property bool planarMode: trajectory.planar
    property var availableSources: root.controller.trajectorySignalOptions(root.plotIndex)
    // Explicit notification survives qmlcachegen eliminating unused revision reads.
    function refreshBindings() {
        if (!root.controller) return
        root.configuration = root.controller.trajectoryState(root.plotIndex)
        root.availableSources = root.controller.trajectorySignalOptions(root.plotIndex)
    }
    onPlotIndexChanged: refreshBindings()
    onControllerChanged: refreshBindings()
    Connections {
        target: root.controller
        function onPlotBindingsChanged() { root.refreshBindings() }
    }
    readonly property string selectionHint: "右键选择“航迹配置…”选择飞机对象；坐标与姿态来源在对象中配置。"
    readonly property string sourceDescription: {
        const state = root.configuration
        const labels = state.geographic ? ["纬度", "经度", "高度"] : ["X", "Y", "Z"]
        const ids = [state.x, state.y, state.z]
        const parts = []
        const options = root.availableSources
        for (let i = 0; i < 3; ++i) if (ids[i] >= 0) {
            const source = options.find(option => option.id === ids[i])
            if (source) parts.push(labels[i] + ": " + source.name)
        }
        return parts.join("    ")
    }
    readonly property string gestureHint: root.planarMode ? "左键平移 · 滚轮缩放 · 空格适应视图"
        : "靠近左下坐标轴展开旋转环 · 拖红/绿/蓝环绕 X/Y/Z 轴旋转 · 环内自由旋转 · 画布或 Shift+左键平移 · 中键 / Alt+左键自由旋转 · 滚轮缩放 · 空格适应"

    Item {
        id: viewport
        objectName: "trajectoryViewport"
        anchors.fill: parent
        anchors.topMargin: 30
        anchors.bottomMargin: 18
        clip: true
        TrajectoryItem {
            id: trajectory
            objectName: "trajectoryItem"
            anchors.fill: parent
            lineWidth: root.graphLineWidth
            axisColor: root.axisColor
            Component.onCompleted: root.controller.attachTrajectory(trajectory, root.plotIndex)
            Component.onDestruction: root.controller.detachTrajectory(trajectory, root.plotIndex)
        }
        Repeater {
            model: trajectory.axisLabels
            delegate: Label {
                required property var modelData
                x: Math.max(0, Math.min(viewport.width - width, modelData.x + 5))
                y: Math.max(0, Math.min(viewport.height - height, modelData.y + 5))
                text: modelData.text
                color: modelData.color
                font.pixelSize: 10
            }
        }
        Repeater {
            model: trajectory.markers
            delegate: Item {
                required property var modelData
                readonly property bool hasAttitude: modelData.attitude !== undefined && modelData.attitude.length === 4
                x: modelData.x
                y: modelData.y
                Rectangle {
                    width: parent.hasAttitude ? 3 : 8
                    height: width
                    x: -width / 2
                    y: -height / 2
                    radius: width / 2
                    color: parent.modelData.color
                }
                Label { x: parent.hasAttitude ? 24 : 7; y: -6; text: parent.modelData.name + " " + parent.modelData.text; color: parent.modelData.color; font.pixelSize: 11 }
            }
        }
        Item {
            objectName: "trajectoryRotationCenter"
            anchors.centerIn: parent
            width: 26
            height: 26
            visible: trajectory.rotating
            opacity: 0.6
            Rectangle { anchors.centerIn: parent; width: 14; height: 1; color: root.axisColor }
            Rectangle { anchors.centerIn: parent; width: 1; height: 14; color: root.axisColor }
        }
        Label {
            anchors.centerIn: parent
            width: Math.max(1, parent.width - 30)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: trajectory.pending ? "正在构建轨迹…"
                : (root.configuration.x < 0 ? 0 : 1) + (root.configuration.y < 0 ? 0 : 1) + (root.configuration.z < 0 ? 0 : 1) < 2
                    ? root.selectionHint : trajectory.error
            color: root.textColor
            visible: trajectory.axisLabels.length === 0
        }
        MouseArea {
            id: trajectoryInput
            objectName: "trajectoryInteractionArea"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            hoverEnabled: true
            preventStealing: true
            function onOrientationAxes(x: real, y: real): bool {
                const rect = trajectory.orientationRect
                return rect.width > 0 && x >= rect.x && x <= rect.x + rect.width
                    && y >= rect.y && y <= rect.y + rect.height
            }
            cursorShape: trajectory.rotating ? Qt.ClosedHandCursor
                : trajectory.rotationHandle >= 0 || onOrientationAxes(mouseX, mouseY) ? Qt.OpenHandCursor : Qt.ArrowCursor
            onPressed: function(mouse) {
                if (mouse.button !== Qt.RightButton)
                    trajectory.beginPointerDrag(Qt.point(mouse.x, mouse.y), mouse.button, mouse.modifiers)
            }
            onPositionChanged: function(mouse) {
                if (pressed) trajectory.dragTo(Qt.point(mouse.x, mouse.y))
                else trajectory.hoverAt(Qt.point(mouse.x, mouse.y))
            }
            onEntered: trajectory.hoverAt(Qt.point(mouseX, mouseY))
            onExited: trajectory.hoverAt(Qt.point(-1000000, -1000000))
            onReleased: function(mouse) {
                trajectory.endDrag()
                trajectory.hoverAt(Qt.point(mouse.x, mouse.y))
            }
            onCanceled: trajectory.endDrag()
            onWheel: function(wheel) {
                trajectory.zoomAt(Qt.point(wheel.x, wheel.y), wheel.angleDelta.y || wheel.pixelDelta.y * 2)
                wheel.accepted = true
            }
            onClicked: function(mouse) {
                if (mouse.button !== Qt.RightButton) return
                root.controller.setActivePlot(root.plotIndex)
                contextMenu.popup()
            }
        }
        Label {
            id: rotationFeedback
            objectName: "trajectoryRotationFeedback"
            readonly property int handle: trajectory.rotationHandle
            readonly property vector3d angles: trajectory.viewAngles
            function degrees(value: real): string { return (Math.abs(value) < 0.05 ? 0 : value).toFixed(1) + "°" }
            width: Math.min(270, Math.max(1, viewport.width - 6))
            wrapMode: Text.Wrap
            textFormat: Text.RichText
            x: Math.max(3, Math.min(viewport.width - width - 3, trajectory.rotationRect.x))
            y: Math.max(3, trajectory.rotationRect.y - height - 6)
            visible: trajectory.rotationGizmoVisible
            font.pixelSize: 11
            color: handle > 0 ? ["#e45b5b", "#36ac72", "#478fe0"][handle - 1] : root.textColor
            text: (handle > 0 ? "绕 " + ["X", "Y", "Z"][handle - 1] + " 轴" : "自由旋转")
                + "<br>视角 <font color='#d94b4b'>X " + rotationFeedback.degrees(angles.x)
                + "</font>  <font color='#27945b'>Y " + rotationFeedback.degrees(angles.y)
                + "</font>  <font color='#397bc5'>Z " + rotationFeedback.degrees(angles.z) + "</font>"
        }
    }
    RowLayout {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 3; height: 24
        Label { text: root.configuration.name; color: root.textColor; elide: Text.ElideRight; Layout.fillWidth: true }
    }
    Label {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 3
        height: 14
        elide: Text.ElideRight
        color: root.textColor
        font.pixelSize: 10
        text: {
            const values = trajectory.markers
            const cursors = values.filter(value => value.text.indexOf("游标") === 0)
            if (trajectory.error.length > 0 || trajectory.attitudeStatus.length > 0) return [trajectory.error, trajectory.attitudeStatus].filter(value => value.length > 0).join(" | ")
            return cursors.length > 0 ? cursors.map(value => value.details).join("  |  ")
                : values.length > 0 ? values[values.length - 1].details : root.sourceDescription
        }
        HoverHandler { id: detailsHover }
        MouseArea {
            anchors.fill: parent
            enabled: trajectory.error.length > 0 || trajectory.attitudeStatus.length > 0
            onClicked: {
                root.editAxesRequested()
            }
        }
        ToolTip.visible: detailsHover.hovered
        ToolTip.text: root.sourceDescription + "\n" + trajectory.referenceOrigin + "\n" + trajectory.error + "\n" + trajectory.attitudeStatus + "\n" + trajectory.markers.map(value => value.details).join("\n") + "\n" + root.gestureHint
    }
    Shortcut {
        sequence: "Space"
        enabled: root.visible && trajectoryInput.containsMouse
        onActivated: { root.controller.setActivePlot(root.plotIndex); trajectory.fitView() }
    }
    PlotMenu {
        id: contextMenu
        objectName: "trajectoryContextMenu"
        delegate: PlotMenuItem { available: subMenu !== trajectoryViews || !root.planarMode }
        ViewModeMenu {
            controller: root.controller
            plotIndex: root.plotIndex
            currentMode: "trajectory"
        }
        PlotMenuSeparator { }
        PlotMenuItem { objectName: "trajectoryConfigurationMenuItem"; text: "航迹配置…"; onTriggered: root.editAxesRequested() }
        PlotMenuItem { text: "适应轨迹"; icon.source: "qrc:/icons/arrows_up_down.svg"; onTriggered: trajectory.fitView() }
        PlotMenu {
            id: trajectoryViews
            objectName: "trajectoryViewMenu"
            title: "视角"
            enabled: !root.planarMode
            PlotMenuItem { text: "等轴测视角（复位）"; onTriggered: trajectory.presetView(0) }
            PlotMenuItem { text: "俯视 XY"; onTriggered: trajectory.presetView(1) }
            PlotMenuItem { text: "正视 YZ"; onTriggered: trajectory.presetView(2) }
            PlotMenuItem { text: "侧视 XZ"; onTriggered: trajectory.presetView(3) }
        }
    }
}
