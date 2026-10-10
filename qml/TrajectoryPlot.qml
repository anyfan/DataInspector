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
    property bool configurationExpanded: root.controller.trajectoryConfigurationExpanded(root.plotIndex)
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
    readonly property string selectionHint: "通过飞机对象配置位置和姿态来源；点击对象配置管理绑定。"
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
        anchors.topMargin: root.configurationExpanded ? 56 : 30
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
        id: management
        objectName: "trajectoryManagement"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 3
        height: 24
        spacing: 2
        TrackCombo {
            id: trackSelector
            objectName: "trajectorySelector"
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            implicitHeight: 24
            model: root.configuration.tracks
            textRole: "name"
            currentIndex: root.configuration.active
            onActivated: function(index) { root.controller.selectTrajectory(root.plotIndex, index) }
        }
        ToolButton { visible: root.configurationExpanded; objectName: "trajectoryAdd"; text: "+"; implicitHeight: 24; onClicked: root.editAxesRequested() }
        ToolButton { visible: root.configurationExpanded; objectName: "trajectoryRemove"; text: "−"; implicitHeight: 24; onClicked: root.controller.removeTrajectory(root.plotIndex, root.configuration.active) }
        CheckBox {
            objectName: "trajectoryVisible"
            text: "显示"
            checked: root.configuration.visible
            implicitHeight: 24
            onClicked: root.controller.styleTrajectory(root.plotIndex, root.configuration.name, root.configuration.color, root.configuration.width, checked)
        }
        ToolButton { visible: root.configurationExpanded; objectName: "trajectoryProperties"; text: "样式"; implicitHeight: 24; onClicked: properties.open() }
        ToolButton {
            objectName: "trajectoryConfigurationToggle"
            text: root.configurationExpanded ? "收起 ▴" : "配置 ▾"
            implicitHeight: 24
            onClicked: {
                root.configurationExpanded = !root.configurationExpanded
                root.controller.setTrajectoryConfigurationExpanded(root.plotIndex, root.configurationExpanded)
            }
        }
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
    RowLayout {
        objectName: "trajectoryConfigurationRow"
        visible: root.configurationExpanded
        anchors.top: management.bottom
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
        TrackCombo {
            objectName: "trajectoryObjectSelector"
            Layout.fillWidth: true
            Layout.minimumWidth: 40
            implicitHeight: 24
            model: root.aircraftObjects
            textRole: "name"
            currentIndex: root.aircraftObjects.findIndex(object => object.id === root.configuration.objectId)
            displayText: currentIndex >= 0 ? currentText : "选择飞机对象"
            onActivated: function(index) { root.controller.showObjectTrajectory(root.aircraftObjects[index].id, root.plotIndex) }
        }
        ToolButton { text: "选择对象"; implicitHeight: 24; font.pixelSize: 11; objectName: "trajectoryAxesButton"; onClicked: { root.controller.setActivePlot(root.plotIndex); root.editAxesRequested() } }
        ToolButton { text: "视角"; visible: !root.planarMode; implicitHeight: 24; font.pixelSize: 11; onClicked: { root.controller.setActivePlot(root.plotIndex); viewMenu.popup() } }
        ToolButton { text: "适应"; implicitHeight: 24; font.pixelSize: 11; onClicked: { root.controller.setActivePlot(root.plotIndex); trajectory.fitView() } }

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
                root.configurationExpanded = true
                root.controller.setTrajectoryConfigurationExpanded(root.plotIndex, true)
            }
        }
        ToolTip.visible: detailsHover.hovered
        ToolTip.text: root.sourceDescription + "\n" + trajectory.referenceOrigin + "\n" + trajectory.error + "\n" + trajectory.attitudeStatus + "\n" + trajectory.markers.map(value => value.details).join("\n") + "\n" + root.gestureHint
    }
    Dialog {
        id: properties
        objectName: "trajectoryPropertiesDialog"
        title: "活动航迹：样式"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.max(1, Math.min(600, root.Window.width - 24))
        height: Math.max(1, Math.min(640, root.Window.height - 24))
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        property var selectedSources: [-1, -1, -1, -1]
        onOpened: {
            propertiesFlick.contentY = 0
            trackName.text = root.configuration.name
            trackColor.text = root.configuration.color.toString()
            trackWidth.value = root.configuration.width
        }
        onAccepted: {
            root.controller.styleTrajectory(root.plotIndex, trackName.text, trackColor.text, trackWidth.value, root.configuration.visible)
        }
        contentItem: ScrollView {
            id: propertiesScroll
            objectName: "trajectoryPropertiesScroll"
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: ScrollBar {
                objectName: "trajectoryPropertiesScrollBar"
                policy: propertiesFlick.contentHeight > propertiesFlick.height + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            contentItem: Flickable {
                id: propertiesFlick
                objectName: "trajectoryPropertiesFlick"
                clip: true
                contentWidth: width
                contentHeight: propertiesForm.implicitHeight + 8
                boundsBehavior: Flickable.StopAtBounds
                ColumnLayout {
                    id: propertiesForm
                    objectName: "trajectoryPropertiesForm"
                    width: Math.max(1, propertiesFlick.width - propertiesScroll.effectiveScrollBarWidth - 8)
                    spacing: 8
                    Label { text: "航迹样式"; font.bold: true; color: root.textColor }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "名称"; Layout.preferredWidth: 46; color: root.textColor }
                        TextField { id: trackName; objectName: "trajectoryName"; Layout.fillWidth: true; Layout.minimumWidth: 0; maximumLength: 256; implicitHeight: 30 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "颜色"; Layout.preferredWidth: 46; color: root.textColor }
                        TextField { id: trackColor; objectName: "trajectoryColor"; Layout.fillWidth: true; Layout.minimumWidth: 0; placeholderText: "#RRGGBB 或 #AARRGGBB"; implicitHeight: 30 }
                        Label { text: "线宽"; color: root.textColor }
                        SpinBox { id: trackWidth; from: 1; to: 12; implicitHeight: 30 }
                    }
                    Label { text: "位置与测量姿态使用飞机对象中的配置。"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: root.textColor }
                    Button { text: "对象配置…"; onClicked: { properties.close(); root.editObjectRequested(root.configuration.objectId) } }
                    Label { text: trajectory.referenceOrigin; wrapMode: Text.Wrap; Layout.fillWidth: true; color: root.textColor; font.pixelSize: 12 }
                }
            }
        }
    }
    Shortcut {
        sequence: "Space"
        enabled: root.visible && trajectoryInput.containsMouse
        onActivated: { root.controller.setActivePlot(root.plotIndex); trajectory.fitView() }
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
        MenuItem { text: "选择飞机对象…"; onTriggered: root.editAxesRequested() }
        MenuItem { text: "适应轨迹"; onTriggered: trajectory.fitView() }
        MenuItem { text: "等轴测视角"; visible: !root.planarMode; onTriggered: trajectory.presetView(0) }
        MenuItem { text: "俯视 XY"; visible: !root.planarMode; onTriggered: trajectory.presetView(1) }
        MenuItem { text: "正视 YZ"; visible: !root.planarMode; onTriggered: trajectory.presetView(2) }
        MenuItem { text: "侧视 XZ"; visible: !root.planarMode; onTriggered: trajectory.presetView(3) }
    }
}
