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
        anchors.topMargin: 56
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
        ComboBox {
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
        ToolButton { objectName: "trajectoryAdd"; text: "+"; implicitHeight: 24; onClicked: root.controller.addTrajectory(root.plotIndex) }
        ToolButton { objectName: "trajectoryRemove"; text: "−"; implicitHeight: 24; onClicked: root.controller.removeTrajectory(root.plotIndex, root.configuration.active) }
        CheckBox {
            objectName: "trajectoryVisible"
            text: "显示"
            checked: root.configuration.visible
            implicitHeight: 24
            onClicked: root.controller.styleTrajectory(root.plotIndex, root.configuration.name, root.configuration.color, root.configuration.width, checked)
        }
        ToolButton { objectName: "trajectoryProperties"; text: "样式 / 姿态"; implicitHeight: 24; onClicked: properties.open() }
    }
    RowLayout {
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
            if (trajectory.error.length > 0 || trajectory.attitudeStatus.length > 0) return [trajectory.error, trajectory.attitudeStatus].filter(value => value.length > 0).join(" | ")
            return cursors.length > 0 ? cursors.map(value => value.details).join("  |  ")
                : values.length > 0 ? values[values.length - 1].details : root.sourceDescription
        }
        HoverHandler { id: detailsHover }
        ToolTip.visible: detailsHover.hovered
        ToolTip.text: root.sourceDescription + "\n" + trajectory.referenceOrigin + "\n" + trajectory.error + "\n" + trajectory.attitudeStatus + "\n" + trajectory.markers.map(value => value.details).join("\n") + "\n" + root.gestureHint
    }
    // Explicit delegates avoid blank string-model entries and keep long source
    // labels inside the popup, in both source and compiled Bound QML.
    component PropertiesCombo: ComboBox {
        id: control
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: 30
        contentItem: Label {
            text: control.displayText
            color: root.textColor
            font.pixelSize: 12
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignVCenter
        }
        delegate: ItemDelegate {
            id: option
            required property var modelData
            required property int index
            width: control.width
            height: 32
            text: control.textRole.length > 0 ? String(modelData[control.textRole]) : String(modelData)
            highlighted: control.highlightedIndex === option.index
            contentItem: Label {
                text: option.text
                color: root.textColor
                font.pixelSize: 12
                elide: Text.ElideMiddle
                verticalAlignment: Text.AlignVCenter
            }
            ToolTip.visible: hovered
            ToolTip.text: text
        }
        ToolTip.visible: hovered && !popup.visible
        ToolTip.text: displayText
    }
    Dialog {
        id: properties
        objectName: "trajectoryPropertiesDialog"
        title: "活动航迹：样式与测量姿态"
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
            attitudeMode.currentIndex = root.configuration.attitudeMode
            radians.checked = root.configuration.radians
            eulerOrder.currentIndex = root.configuration.order
            scalarLast.checked = root.configuration.scalarLast
            inverseDirection.checked = root.configuration.navigationToBody
            properties.selectedSources = root.configuration.attitudeSources.slice()
        }
        onAccepted: {
            root.controller.styleTrajectory(root.plotIndex, trackName.text, trackColor.text, trackWidth.value, root.configuration.visible)
            const ids = properties.selectedSources
            root.controller.configureAttitude(root.plotIndex, attitudeMode.currentIndex, ids[0], ids[1], ids[2], ids[3], radians.checked, eulerOrder.currentIndex, scalarLast.checked, inverseDirection.checked)
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
                    Label { text: "测量姿态"; font.bold: true; color: root.textColor }
                    Label { text: "导航 NED：北 / 东 / 下；机体 FRD：前 / 右 / 下\n零姿态：机头北、右翼东、机腹下。XYZ 输入使用同一 NED 坐标系（米）。"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: root.textColor; font.pixelSize: 12 }
                    PropertiesCombo { id: attitudeMode; objectName: "trajectoryAttitudeMode"; model: ["关闭姿态", "滚转 / 俯仰 / 航向", "四元数"] }
                    Repeater {
                        model: 4
                        delegate: RowLayout {
                            id: attitudeBinding
                            required property int index
                            visible: attitudeMode.currentIndex > 0 && (attitudeMode.currentIndex === 2 || index < 3)
                            Layout.fillWidth: true
                            Label { text: attitudeMode.currentIndex === 1 ? ["滚转", "俯仰", "航向", ""][attitudeBinding.index] : (scalarLast.checked ? ["x", "y", "z", "w"] : ["w", "x", "y", "z"])[attitudeBinding.index]; Layout.preferredWidth: 46; color: root.textColor }
                            PropertiesCombo {
                                objectName: "trajectoryAttitudeSource" + attitudeBinding.index
                                model: root.availableSources
                                textRole: "label"
                                currentIndex: {
                                    const id = properties.selectedSources[attitudeBinding.index]
                                    for (let i = 0; i < root.availableSources.length; ++i) if (root.availableSources[i].id === id) return i
                                    return 0
                                }
                                onActivated: function(index) {
                                    const ids = properties.selectedSources.slice()
                                    ids[attitudeBinding.index] = root.availableSources[index].id
                                    properties.selectedSources = ids
                                }
                            }
                        }
                    }
                    CheckBox { id: radians; text: "角度单位为弧度（默认度）"; visible: attitudeMode.currentIndex === 1; font.pixelSize: 12 }
                    Label { text: "欧拉旋转顺序"; visible: attitudeMode.currentIndex === 1; color: root.textColor }
                    PropertiesCombo { id: eulerOrder; objectName: "trajectoryEulerOrder"; model: ["Rz(航向) · Ry(俯仰) · Rx(滚转)", "Rx(滚转) · Ry(俯仰) · Rz(航向)"]; visible: attitudeMode.currentIndex === 1 }
                    CheckBox { id: scalarLast; text: "四元数输入顺序 xyzw（默认 wxyz）"; visible: attitudeMode.currentIndex === 2; font.pixelSize: 12 }
                    CheckBox { id: inverseDirection; objectName: "trajectoryInverseDirection"; text: "输入为导航→机体（默认机体→导航）"; font.pixelSize: 12 }
                    Label { text: "姿态随时间游标显示；位置与姿态各取最近原始样本，不插值、不跨缺口。读数显示两者时间差。"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: root.textColor; font.pixelSize: 12 }
                    Label { text: trajectory.referenceOrigin; wrapMode: Text.Wrap; Layout.fillWidth: true; color: root.textColor; font.pixelSize: 12 }
                }
            }
        }
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
