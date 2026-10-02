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
    enabled: !root.controller.restoringSession
    color: plotColor
    clip: true
    objectName: "trajectoryView"

    property var configuration: root.controller.trajectoryState(root.plotIndex)
    readonly property bool planarMode: root.configuration.planar
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
    readonly property string selectionHint: root.configuration.signalCount === 0
        ? "从信号树依次勾选" + (root.configuration.geographic ? "纬度、经度、高度" : "X、Y、Z")
            + "，自动填入空轴；顶部下拉框可调整来源"
        : "已加入 " + root.configuration.signalCount + " 个信号；新选信号自动填入空轴，顶部下拉框可调整来源"
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
        anchors.topMargin: 28
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
                x: modelData.x
                y: modelData.y
                Rectangle { x: -4; y: -4; width: 8; height: 8; radius: 4; color: parent.modelData.color }
                Label { x: 7; y: -6; text: parent.modelData.text; color: parent.modelData.color; font.pixelSize: 11 }
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
            visible: (trajectory.pending && trajectory.axisLabels.length === 0) || trajectory.error.length > 0
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
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 3
        height: 24
        spacing: 2
        Label {
            text: (root.plotIndex + 1) + (root.planarMode ? " · 2D航迹" : " · 3D航迹")
            visible: root.width >= 700
            color: root.textColor
            font.pixelSize: 11
        }
        Repeater {
            model: 3
            delegate: ComboBox {
                id: axisBinding
                required property int index
                readonly property int sourceId: [root.configuration.x, root.configuration.y, root.configuration.z][axisBinding.index]
                readonly property string roleLabel: (root.configuration.geographic ? ["纬", "经", "高"] : ["X", "Y", "Z"])[axisBinding.index]
                readonly property string sourceName: {
                    const options = root.availableSources
                    for (const option of options) if (option.id === axisBinding.sourceId && option.id >= 0) return option.name
                    return "未绑定"
                }
                objectName: "trajectoryBindingAxis" + index
                Layout.fillWidth: true
                Layout.minimumWidth: 40
                Layout.preferredWidth: 100
                implicitHeight: 24
                leftPadding: 3
                rightPadding: 14
                topPadding: 0
                bottomPadding: 0
                spacing: 2
                indicator: Label {
                    x: axisBinding.width - width - 2
                    y: (axisBinding.height - height) / 2
                    width: 10
                    height: 14
                    text: "▾"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: root.textColor
                    font.pixelSize: 10
                }
                model: root.availableSources
                textRole: "label"
                valueRole: "id"
                delegate: ItemDelegate {
                    id: sourceOption
                    required property var modelData
                    required property int index
                    width: axisBinding.width
                    height: 30
                    text: modelData.label
                    highlighted: axisBinding.highlightedIndex === sourceOption.index
                    contentItem: Label {
                        text: sourceOption.text
                        elide: Text.ElideMiddle
                        verticalAlignment: Text.AlignVCenter
                        color: root.textColor
                        font.pixelSize: 11
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: text
                }
                currentIndex: {
                    const options = root.availableSources
                    for (let i = 0; i < options.length; ++i) if (options[i].id === axisBinding.sourceId) return i
                    return 0
                }
                contentItem: Label {
                    text: axisBinding.roleLabel + ": " + axisBinding.sourceName
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    color: root.textColor
                    font.pixelSize: 10
                }
                onActivated: function(optionIndex) {
                    root.controller.bindTrajectoryAxis(root.plotIndex, axisBinding.index, Number(root.availableSources[optionIndex].id))
                }
                ToolTip.visible: axisBinding.hovered && !axisBinding.popup.visible
                ToolTip.text: roleLabel + ": " + sourceName + "\n从本子图已加入的信号中选择来源；右键取消绑定"
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: root.controller.bindTrajectoryAxis(root.plotIndex, axisBinding.index, -1)
                }
            }
        }

        ToolButton { text: "坐标"; implicitHeight: 24; font.pixelSize: 11; objectName: "trajectoryAxesButton"; onClicked: { root.controller.setActivePlot(root.plotIndex); root.editAxesRequested() } }
        ToolButton { text: "视角"; visible: !root.planarMode; implicitHeight: 24; font.pixelSize: 11; onClicked: viewMenu.popup() }
        ToolButton { text: "适应"; implicitHeight: 24; font.pixelSize: 11; onClicked: trajectory.fitView() }
        ToolButton {
            text: "时间图"
            implicitHeight: 24
            font.pixelSize: 11
            objectName: "trajectory2DButton"
            onClicked: {
                const state = root.configuration
                root.controller.configureTrajectory(root.plotIndex, false, state.x, state.y, state.z, state.geographic)
            }
        }
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
            return cursors.length > 0 ? cursors.map(value => value.details).join("  |  ")
                : values.length > 0 ? values[values.length - 1].details : root.sourceDescription
        }
        HoverHandler { id: detailsHover }
        ToolTip.visible: detailsHover.hovered
        ToolTip.text: root.sourceDescription + "\n" + trajectory.markers.map(value => value.details).join("\n") + "\n" + root.gestureHint
    }
    Shortcut {
        sequence: "Space"
        enabled: root.visible && trajectoryInput.containsMouse
        onActivated: trajectory.fitView()
    }
    Menu {
        id: viewMenu
        MenuItem { text: "俯视 XY"; onTriggered: trajectory.presetView(1) }
        MenuItem { text: "正视 YZ"; onTriggered: trajectory.presetView(2) }
        MenuItem { text: "侧视 XZ"; onTriggered: trajectory.presetView(3) }
        MenuItem { text: "复位"; onTriggered: trajectory.presetView(0) }
    }
    Menu {
        id: contextMenu
        MenuItem { text: "选择坐标与信号…"; onTriggered: root.editAxesRequested() }
        MenuItem { text: "适应轨迹"; onTriggered: trajectory.fitView() }
        MenuItem { text: "等轴测视角"; visible: !root.planarMode; onTriggered: trajectory.presetView(0) }
        MenuItem { text: "俯视 XY"; visible: !root.planarMode; onTriggered: trajectory.presetView(1) }
        MenuItem { text: "正视 YZ"; visible: !root.planarMode; onTriggered: trajectory.presetView(2) }
        MenuItem { text: "侧视 XZ"; visible: !root.planarMode; onTriggered: trajectory.presetView(3) }
    }
}
