pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DataInspector

Rectangle {
    id: root

    required property int plotIndex
    required property AppController controller
    property TrajectoryPropertiesPane trajectoryEditor
    signal trajectoryConfigurationRequested(int plot)
    signal editObjectRequested(string objectId)
    property real graphLineWidth: 2
    property int graphCursorMode: 0
    // 0: disabled, 1: X/Y region, 2: X only, 3: Y only.
    property int graphZoomMode: 0
    property bool trajectoryMode: root.controller.trajectoryState(root.plotIndex).enabled
    property bool stateMode: false
    property int selectedStateRow: 0
    signal stateConfigurationRequested(int plot)
    signal signalPropertiesRequested(int row)
    function configureStateSource(source) {
        if (controller.addStateSignal(root.plotIndex,source)) signalPropertiesRequested(source)
    }
    function jumpState(direction) {
        if (plotItem.cursorMode === 0) root.controller.setCursorMode(1)
        const cursor = plotItem.activeCursorIndex
        const time = cursor === 2 ? plotItem.cursorX2 : plotItem.cursorX1
        const next = stateItem.transition(root.selectedStateRow, time, direction)
        if (next !== undefined && next !== null) {
            plotItem.editCursorTime(Number(next), cursor)
            if (next < plotItem.xMinimum || next > plotItem.xMaximum) {
                const span = plotItem.xMaximum - plotItem.xMinimum
                plotItem.setXRange(Number(next)-span/2, Number(next)+span/2)
            }
        }
    }
    function refreshTrajectoryMode() {
        if (root.controller) root.trajectoryMode = root.controller.trajectoryState(root.plotIndex).enabled
        if (root.controller) root.stateMode = root.controller.stateTimelineState(root.plotIndex).enabled
    }
    onPlotIndexChanged: refreshTrajectoryMode()
    onControllerChanged: refreshTrajectoryMode()
    property bool darkTheme: false
    // Place smaller-amplitude curves in front, keeping original colours.
    // Disabling this restores plain "last drawn wins".
    property bool blendOverlaps: true
    property color axisColor: darkTheme ? "#8f9aa7" : "#59636e"
    property color gridColor: darkTheme ? "#53606d" : "#c9d1d9"
    property color textColor: darkTheme ? "#d7dee7" : "#303942"
    property color plotColor: darkTheme ? "#14181d" : "#ffffff"
    property color frameColor: darkTheme ? "#3b4652" : "#d7dfe8"

    // Size the Y gutter from the widest label currently shown so short
    // tick texts such as "0.8" do not reserve scientific-notation width.
    readonly property string widestYTickLabel: {
        let widest = "0.0"
        const ticks = plotItem.yTicks
        for (let i = 0; i < ticks.length; ++i) {
            const label = root.yTickLabel(ticks[i])
            if (label.length > widest.length) widest = label
        }
        return widest
    }
    readonly property real yTickLabelMargin: 6
    // Width this subplot needs on its own; the parent may widen it via
    // sharedAxisLeft so all subplots keep identical X-axis extents.
    // Fixed gutter for glyph and active-cursor value; names live in the legend.
    readonly property real measuredAxisLeft: root.trajectoryMode ? 0 : root.stateMode ? 60 : Math.ceil(yTickMetrics.advanceWidth) + yTickLabelMargin + 2
    property real sharedAxisLeft: 0
    readonly property real axisLeft: Math.max(measuredAxisLeft, sharedAxisLeft)
    TextMetrics {
        id: yTickMetrics
        font.pixelSize: 10
        text: root.widestYTickLabel
    }
    readonly property real axisRight: 1
    readonly property real axisTop: root.stateMode ? Math.max(38, stateLegend.y + stateLegend.height + 4) : Math.max(20, legend.implicitHeight + 2)
    // Keep compact labels for ordinary ranges, but do not show identical text
    // on different ticks when a large Y offset is viewed through a narrow window.
    readonly property bool compactYTicksDistinct: {
        const labels = []
        for (const tick of plotItem.yTicks) {
            const label = root.formatYTick(tick.value)
            if (labels.indexOf(label) >= 0) return false
            labels.push(label)
        }
        return true
    }
    readonly property int yScientificPrecision: {
        for (let precision = 2; precision <= 16; ++precision) {
            const labels = []
            let distinct = true
            for (const tick of plotItem.yTicks) {
                const label = Number(tick.value).toExponential(precision)
                if (labels.indexOf(label) >= 0) distinct = false
                labels.push(label)
            }
            if (distinct) return precision
        }
        return 16
    }
    function yTickLabel(tick) {
        if (root.compactYTicksDistinct) return root.formatYTick(tick.value)
        return Number(tick.value).toExponential(root.yScientificPrecision).replace(/e([+-])0+/, "e$1")
    }
    function formatYTick(value) {
        if (value === 0) return "0"
        const compact = root.formatCompact(value, 4)
        if (Math.abs(value) >= 1000 || Math.abs(value) < 0.001 || compact.length > 7)
            return Number(value).toExponential(2).replace(/e([+-])0+/, "e$1")
        return compact
    }
    property bool legendDragging: false
    property bool axisSelecting: false
    property int hoveredAxis: -1
    property int axisSelection: -1
    property real axisSelectionStart: 0
    property real axisSelectionCurrent: 0
    property bool axisCursorVisible: false
    property string axisCursorSource: ""
    property real axisCursorX: 0
    property real axisCursorY: 0
    property bool graphSelecting: false
    property real graphSelectionStartX: 0
    property real graphSelectionStartY: 0
    property real graphSelectionCurrentX: 0
    property real graphSelectionCurrentY: 0
    function applyAxisSelection(axis, startFraction, endFraction) {
        const start = Math.max(0, Math.min(1, Math.min(startFraction, endFraction)))
        const end = Math.max(0, Math.min(1, Math.max(startFraction, endFraction)))
        if (end - start < 1e-6)
            return false
        if (axis === 0) {
            const span = plotItem.xMaximum - plotItem.xMinimum
            plotItem.setXRange(plotItem.xMinimum + start * span,
                               plotItem.xMinimum + end * span)
        } else if (axis === 1) {
            const span = plotItem.yMaximum - plotItem.yMinimum
            plotItem.setYRange(plotItem.yMaximum - end * span,
                               plotItem.yMaximum - start * span)
        } else {
            return false
        }
        return true
    }
    function beginAxisSelection(axis, position) {
        controller.setActivePlot(plotIndex)
        axisSelecting = true
        axisSelection = axis
        axisSelectionStart = position
        axisSelectionCurrent = position
    }
    function updateAxisSelection(position) {
        if (axisSelecting)
            axisSelectionCurrent = position
    }
    function finishAxisSelection() {
        if (!axisSelecting)
            return
        const axis = axisSelection
        const start = axisSelectionStart
        const end = axisSelectionCurrent
        axisSelecting = false
        axisSelection = -1
        applyAxisSelection(axis, start, end)
    }
    function updateAxisCursor(source, x, y, visible) {
        axisCursorSource = source
        axisCursorX = Math.max(0, Math.min(root.width - axisCursorImage.width, x + 2))
        axisCursorY = Math.max(0, Math.min(root.height - axisCursorImage.height, y + 2))
        axisCursorVisible = visible
    }
    function beginGraphSelection(xFraction, yFraction) {
        graphSelecting = true
        graphSelectionStartX = xFraction
        graphSelectionStartY = yFraction
        graphSelectionCurrentX = xFraction
        graphSelectionCurrentY = yFraction
        controller.setActivePlot(plotIndex)
    }
    function updateGraphSelection(xFraction, yFraction) {
        if (!graphSelecting)
            return
        graphSelectionCurrentX = Math.max(0, Math.min(1, xFraction))
        graphSelectionCurrentY = Math.max(0, Math.min(1, yFraction))
    }
    function finishGraphSelection() {
        if (!graphSelecting)
            return false
        const mode = graphZoomMode
        const left = Math.min(graphSelectionStartX, graphSelectionCurrentX)
        const right = Math.max(graphSelectionStartX, graphSelectionCurrentX)
        const top = Math.min(graphSelectionStartY, graphSelectionCurrentY)
        const bottom = Math.max(graphSelectionStartY, graphSelectionCurrentY)
        const xSpan = plotItem.xMaximum - plotItem.xMinimum
        const ySpan = plotItem.yMaximum - plotItem.yMinimum
        graphSelecting = false
        if ((mode === 1 || mode === 2) && right - left < 2 / Math.max(1, axisRect.width))
            return false
        if ((mode === 1 || mode === 3) && bottom - top < 2 / Math.max(1, axisRect.height))
            return false
        controller.beginViewChange()
        if (mode === 1 || mode === 2)
            plotItem.setXRange(plotItem.xMinimum + left * xSpan,
                               plotItem.xMinimum + right * xSpan)
        if (mode === 1 || mode === 3)
            plotItem.setYRange(plotItem.yMaximum - bottom * ySpan,
                               plotItem.yMaximum - top * ySpan)
        controller.endViewChange()
        return mode >= 1 && mode <= 3
    }
    Shortcut {
        sequence: "Space"
        // Over either axis gutter Space fits both axes; over the graph itself it
        // fits the Y axis of this subplot only.
        enabled: !root.trajectoryMode && (root.hoveredAxis >= 0 || graphHover.hovered)
        onActivated: {
            root.controller.setActivePlot(root.plotIndex)
            if (root.hoveredAxis >= 0) {
                root.controller.fitPlots(true, true)
            } else
                root.controller.fitPlotY(root.plotIndex)
        }
    }
    Image {
        id: axisCursorImage
        objectName: "axisZoomCursor"
        visible: root.axisCursorVisible
        source: root.axisCursorSource
        x: root.axisCursorX
        y: root.axisCursorY
        width: 24
        height: 24
        z: 1000
    }
    Rectangle {
        id: xSelectionOverlay
        objectName: "xAxisSelection"
        visible: root.axisSelecting && root.axisSelection === 0
        x: axisRect.x + Math.min(root.axisSelectionStart, root.axisSelectionCurrent) * axisRect.width
        y: axisRect.y
        width: Math.max(1, Math.abs(root.axisSelectionCurrent - root.axisSelectionStart) * axisRect.width)
        height: axisRect.height
        color: root.axisColor
        opacity: 0.18
        border.color: root.axisColor
        border.width: 1
        z: 6
    }
    Rectangle {
        id: ySelectionOverlay
        objectName: "yAxisSelection"
        visible: root.axisSelecting && root.axisSelection === 1
        x: axisRect.x
        y: axisRect.y + Math.min(root.axisSelectionStart, root.axisSelectionCurrent) * axisRect.height
        width: axisRect.width
        height: Math.max(1, Math.abs(root.axisSelectionCurrent - root.axisSelectionStart) * axisRect.height)
        color: root.axisColor
        opacity: 0.18
        border.color: root.axisColor
        border.width: 1
        z: 6
    }
    Rectangle {
        id: legendDragPreview
        objectName: "legendDragPreview"
        parent: root.Window.window ? root.Window.window.contentItem : root
        visible: root.legendDragging
        z: 10000
        width: Math.min(260, previewText.implicitWidth + 34)
        height: 26
        color: root.plotColor
        border.color: root.axisColor
        radius: 3
        property string signalName: ""
        property color signalColor: "transparent"
        Rectangle {
            x: 8; anchors.verticalCenter: parent.verticalCenter
            width: 8; height: 8; color: legendDragPreview.signalColor
        }
        Label {
            id: previewText
            x: 23; width: parent.width - 29
            anchors.verticalCenter: parent.verticalCenter
            text: legendDragPreview.signalName
            color: root.textColor
            elide: Text.ElideRight
            font.pixelSize: 11
        }
    }
    readonly property real axisBottom: 22
    property var rawReadouts: ({})
    property alias renderer: plotItem
    function toggleReadout(key) {
        controller.setActivePlot(plotIndex)
        const next = Object.assign({}, rawReadouts)
        next[key] = !next[key]
        rawReadouts = next
    }
    function cursorInView(value) {
        return value >= plotItem.xMinimum && value <= plotItem.xMaximum
    }
    readonly property bool bothCursorTimesVisible: plotItem.cursorMode === 2
        && cursorInView(plotItem.cursorX1) && cursorInView(plotItem.cursorX2)
    property var cursorTimeItems: [null, null]
    function refreshCursorTimeItems() {
        cursorTimeItems = [cursorTimes.itemAt(0), cursorTimes.itemAt(1)]
    }
    readonly property var cursorBadgePositions: {
        const labels = root.cursorTimeItems
        const values = [plotItem.cursorX1, plotItem.cursorX2]
        const positions = [root.axisLeft, root.axisLeft, root.axisLeft]
        const entries = []
        for (const i of [0, 2, 1]) {
            const value = i === 2 ? (values[0] + values[1]) / 2 : values[i]
            const width = i === 2 ? deltaBadge.width : (labels[i] ? labels[i].width : 0)
            const center = root.axisLeft + root.xPixel(value)
            positions[i] = Math.max(root.axisLeft, Math.min(root.axisLeft + axisRect.width - width, center - width / 2))
            if (i === 2 ? root.bothCursorTimesVisible : (i < plotItem.cursorMode && root.cursorInView(value)))
                entries.push({index: i, width: width, center: center})
        }
        entries.sort(function(a, b) { return a.center - b.center })
        let remaining = entries.reduce(function(total, item) { return total + item.width }, 0)
            + Math.max(0, entries.length - 1) * 4
        let left = root.axisLeft
        for (const entry of entries) {
            positions[entry.index] = Math.max(left, Math.min(entry.center - entry.width / 2,
                root.axisLeft + axisRect.width - remaining))
            left = positions[entry.index] + entry.width + 4
            remaining -= entry.width + 4
        }
        return positions
    }
    function beginCursorBadgeDrag(area, mouse, index) {
        controller.setActivePlot(plotIndex)
        plotItem.activeCursorIndex = index
        forceActiveFocus()
        area.startPixel = area.mapToItem(root, mouse.x, mouse.y).x
        area.startX1 = plotItem.cursorX1
        area.startX2 = plotItem.cursorX2
    }
    function moveCursorBadge(area, mouse, index) {
        if (!area.pressed) return
        const pixelOffset = area.mapToItem(root, mouse.x, mouse.y).x - area.startPixel
        if (Math.abs(pixelOffset) < 2) return
        const offset = pixelOffset / axisRect.width * (plotItem.xMaximum - plotItem.xMinimum)
        if (index === 0) plotItem.moveCursorPair(area.startX1, area.startX2, offset)
        else plotItem.setCursorX((index === 1 ? area.startX1 : area.startX2) + offset, index)
    }
    function openCursorEditor(index) {
        root.controller.setActivePlot(root.plotIndex)
        cursorEditor.targetIndex = index
        cursorInput.text = String(index === 0 ? plotItem.cursorDeltaT : (index === 1 ? plotItem.cursorX1 : plotItem.cursorX2))
        cursorEditor.errorText = ""
        cursorEditor.open()
        cursorInput.forceActiveFocus()
        cursorInput.selectAll()
    }
    Dialog {
        id: cursorEditor
        objectName: "cursorTimeEditor"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 340
        property int targetIndex: 1
        property string errorText: ""
        title: targetIndex === 0 ? "设置 ΔT（固定 T1）" : "设置 T" + targetIndex
        function apply() {
            const text = cursorInput.text.trim()
            const value = Number(text)
            if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(text)
                || !Number.isFinite(value) || !plotItem.editCursorTime(value, targetIndex)) {
                errorText = "请输入有限的秒数，并确认子图有可定位的原始样本。"
                return
            }
            const x = targetIndex === 1 ? plotItem.cursorX1 : plotItem.cursorX2
            if (x < plotItem.xMinimum || x > plotItem.xMaximum) {
                const span = plotItem.xMaximum - plotItem.xMinimum
                root.controller.beginViewChange()
                plotItem.setXRange(x - span / 2, x + span / 2)
                root.controller.endViewChange()
            }
            close()
            root.forceActiveFocus()
        }
        contentItem: ColumnLayout {
            TextField {
                id: cursorInput
                objectName: "cursorTimeInput"
                Layout.fillWidth: true
                selectByMouse: true
                onAccepted: cursorEditor.apply()
            }
            Label { text: "单位：秒。定位最近原始样本，以轴下实际示数为准。"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label { text: cursorEditor.errorText; visible: text.length > 0; color: "#df4652"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
        footer: DialogButtonBox {
            Button { text: "应用"; onClicked: cursorEditor.apply() }
            Button { text: "取消"; onClicked: cursorEditor.close() }
        }
    }
    function tickCovered(value, labelWidth) {
        const px = root.axisLeft + root.xPixel(value)
        for (let i = 0; i < cursorTimes.count; ++i) {
            const label = cursorTimes.itemAt(i)
            if (label && label.visible && px + labelWidth / 2 + 2 > label.x
                && px - labelWidth / 2 - 2 < label.x + label.width)
                return true
        }
        return deltaBadge.visible && px + labelWidth / 2 + 2 > deltaBadge.x
            && px - labelWidth / 2 - 2 < deltaBadge.x + deltaBadge.width
    }
    function formatCompact(value, precision) {
        const parts = Number(value).toPrecision(precision).split("e")
        parts[0] = parts[0].replace(/(\.\d*?)0+$/, "$1").replace(/\.$/, "")
        return parts.join("e")
    }
    function formatRaw(value) {
        return Number(value).toFixed(12).replace(/\.?0+$/, "")
    }
    readonly property var positionedReadouts: {
        const entries = plotItem.cursorReadouts.map(function(item) {
            return Object.assign({}, item, {labelY: root.yPixel(item.displayY) - 9})
        })
        for (let cursor = 1; cursor <= 2; ++cursor) {
            const group = entries.filter(function(item) { return item.cursorIndex === cursor })
            group.sort(function(a, b) { return a.labelY - b.labelY })
            let bottom = -2
            for (const item of group) {
                item.labelY = Math.max(2, item.labelY, bottom + 2)
                bottom = item.labelY + 18
            }
            // Shift the stack back inside the plot when there is sufficient room.
            if (group.length * 20 <= axisRect.height && bottom > axisRect.height - 2) {
                let top = axisRect.height - 2
                for (let i = group.length - 1; i >= 0; --i) {
                    group[i].labelY = Math.min(group[i].labelY, top - 18)
                    top = group[i].labelY - 2
                }
            }
        }
        return entries
    }

    property bool dropHighlighted: false
    focus: root.controller.activePlotIndex === root.plotIndex && root.graphCursorMode !== 0
    Keys.onLeftPressed: plotItem.stepCursor(-1)
    Keys.onRightPressed: plotItem.stepCursor(1)
    // Menus, legend buttons and the signal tree steal active focus from the
    // subplot. The binding above keeps `focus` true, but Qt does not hand
    // active focus back on its own, so Left/Right stopped moving the cursor
    // after actions such as clearing the signals of a plot from a menu.
    // Reclaim focus whenever plot state changes and no text editor is active.
    readonly property bool cursorKeysEnabled:
        root.graphCursorMode !== 0 && root.controller.activePlotIndex === root.plotIndex
    function ensureCursorFocus() {
        if (!root.cursorKeysEnabled || root.activeFocus)
            return
        const hostWindow = root.Window.window
        const focusItem = hostWindow ? hostWindow.activeFocusItem : null
        // Never interrupt text entry (signal search field, dialogs).
        if (focusItem instanceof TextInput || focusItem instanceof TextEdit)
            return
        root.forceActiveFocus(Qt.OtherFocusReason)
    }
    onCursorKeysEnabledChanged: Qt.callLater(root.ensureCursorFocus)
    Connections {
        target: root.controller
        function onPlotBindingsChanged() {
            root.refreshTrajectoryMode()
            Qt.callLater(root.ensureCursorFocus)
        }
        function onActivePlotChanged() { Qt.callLater(root.ensureCursorFocus) }
    }
    color: plotColor
    border.color: root.dropHighlighted || root.controller.activePlotIndex === root.plotIndex
                  ? "#0078d4" : frameColor
    border.width: root.controller.activePlotIndex === root.plotIndex ? 2 : 1
    radius: 0
    clip: true

    function xPixel(value) {
        return (value - plotItem.xMinimum)
                / Math.max(1e-12, plotItem.xMaximum - plotItem.xMinimum)
                * axisRect.width
    }

    function yPixel(value) {
        return (1 - (value - plotItem.yMinimum)
                / Math.max(1e-12, plotItem.yMaximum - plotItem.yMinimum))
                * axisRect.height
    }

    Item {
        id: axisRect
        visible: !root.trajectoryMode
        x: root.axisLeft
        y: root.axisTop
        width: Math.max(1, root.width - root.axisLeft - root.axisRight)
        height: Math.max(1, root.height - root.axisTop - root.axisBottom)
        clip: true

        Repeater {
            model: root.trajectoryMode ? [] : plotItem.xTicks
            delegate: Rectangle {
                required property var modelData
                x: root.xPixel(modelData.value)
                width: 1
                height: axisRect.height
                color: root.gridColor
                opacity: 0.32
            }
        }

        Repeater {
            model: root.trajectoryMode || root.stateMode ? [] : plotItem.yTicks
            delegate: Rectangle {
                required property var modelData
                y: root.yPixel(modelData.value)
                width: axisRect.width
                height: 1
                color: root.gridColor
                opacity: 0.32
            }
        }

        StateTimelineItem {
            id: stateItem
            objectName: "stateTimelineItem"
            width: axisRect.width
            height: Math.max(axisRect.height, rowNames.length * rowHeight)
            y: -stateScroll.position * height
            visible: root.stateMode
            minimum: plotItem.xMinimum
            maximum: plotItem.xMaximum
            Component.onCompleted: root.controller.attachStateTimeline(stateItem, root.plotIndex)
        }
        Repeater {
            model: root.stateMode ? stateItem.labels : []
            delegate: Label {
                required property var modelData
                x: modelData.x + 3; y: stateItem.y + modelData.y
                width: Math.max(0, modelData.width - 6); height: modelData.height
                verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter
                text: modelData.text; color: modelData.color; font.pixelSize: 11; elide: Text.ElideRight
            }
        }
        PlotItem {
            id: plotItem
            anchors.fill: parent
            lineWidth: root.graphLineWidth
            cursorMode: root.graphCursorMode
            blendMode: root.blendOverlaps ? PlotItem.AmplitudeLayers
                                          : PlotItem.OpaqueBlend
            z: 1
            Component.onCompleted: root.controller.attachPlot(plotItem, root.plotIndex)
            Component.onDestruction: root.controller.detachPlot(plotItem, root.plotIndex)
            onActivated: root.controller.setActivePlot(root.plotIndex)
            HoverHandler {
                id: graphHover
                objectName: "graphHoverHandler"
            }
            TapHandler {
                enabled: root.stateMode
                onTapped: point => {
                    root.selectedStateRow = Math.max(0, Math.min(stateItem.rowNames.length - 1,
                        Math.floor((point.position.y-stateItem.y) / stateItem.rowHeight)))
                }
            }
            readonly property string hoveredStateDetails: root.stateMode && graphHover.hovered && stateItem.hasFrame
                && (graphHover.point.position.y-stateItem.y) % stateItem.rowHeight >= stateItem.headerHeight
                ? stateItem.inspect(Math.floor((graphHover.point.position.y-stateItem.y) / stateItem.rowHeight),
                    plotItem.xMinimum + graphHover.point.position.x / Math.max(1, axisRect.width) * (plotItem.xMaximum-plotItem.xMinimum)) : ""
            readonly property bool stateInspectionVisible: hoveredStateDetails.length > 0
            ToolTip.visible: plotItem.stateInspectionVisible
            ToolTip.delay: 500
            ToolTip.text: plotItem.hoveredStateDetails
        }

        Rectangle {
            id: graphSelectionOverlay
            objectName: "graphZoomSelection"
            visible: root.graphSelecting && root.graphZoomMode > 0
            x: root.graphZoomMode === 3 ? 0
               : Math.min(root.graphSelectionStartX, root.graphSelectionCurrentX) * axisRect.width
            y: root.graphZoomMode === 2 ? 0
               : Math.min(root.graphSelectionStartY, root.graphSelectionCurrentY) * axisRect.height
            width: root.graphZoomMode === 3 ? axisRect.width
                   : Math.max(1, Math.abs(root.graphSelectionCurrentX - root.graphSelectionStartX) * axisRect.width)
            height: root.graphZoomMode === 2 ? axisRect.height
                    : Math.max(1, Math.abs(root.graphSelectionCurrentY - root.graphSelectionStartY) * axisRect.height)
            color: root.axisColor
            opacity: 0.18
            border.color: root.axisColor
            border.width: 1
            z: 8
        }

        MouseArea {
            id: graphZoomArea
            objectName: "graphZoomArea"
            anchors.fill: parent
            enabled: root.graphZoomMode > 0
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.CrossCursor
            preventStealing: true
            z: 9
            onPressed: function(mouse) {
                root.beginGraphSelection(mouse.x / width, mouse.y / height)
            }
            onPositionChanged: function(mouse) {
                root.updateGraphSelection(mouse.x / width, mouse.y / height)
            }
            onReleased: function(mouse) {
                if (!root.finishGraphSelection()
                    && Math.hypot(mouse.x / width - root.graphSelectionStartX,
                                  mouse.y / height - root.graphSelectionStartY) < 0.005)
                    plotItem.selectSeriesAt(mouse.x, mouse.y)
            }
            onCanceled: root.graphSelecting = false
        }

        Rectangle {
            objectName: "numericAxisFrame"
            visible: !root.stateMode
            anchors.fill: parent
            color: "transparent"
            border.color: root.axisColor
            border.width: 1
            z: 2
        }
        Item {
            visible: root.stateMode
            anchors.fill: parent
            z: 2
            Rectangle { objectName: "stateXAxisLine"; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.axisColor }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: root.axisColor }
            Rectangle { anchors.right: parent.right; height: parent.height; width: 1; color: root.axisColor }
        }

        Repeater {
            model: root.stateMode ? [] : plotItem.cursorReadouts
            delegate: Rectangle {
                required property var modelData
                objectName: "cursorSampleMarker"
                width: 5; height: 5; radius: 2.5
                x: root.xPixel(modelData.sampleX) - width / 2
                y: root.yPixel(modelData.displayY) - height / 2
                color: modelData.color
                border.color: modelData.color
                border.width: 1
                z: 3
            }
        }

        Repeater {
            model: root.stateMode ? [] : root.positionedReadouts
            delegate: Label {
                id: cursorValueLabel
                required property var modelData
                objectName: "cursorValueLabel"
                readonly property string formatKey: modelData.seriesId + ":" + modelData.cursorIndex
                x: Math.max(2, Math.min(axisRect.width - width - 2,
                                       root.xPixel(modelData.sampleX)
                                       + ((plotItem.cursorMode === 1 ||
                                           (modelData.cursorIndex === 1 ? plotItem.cursorX1 <= plotItem.cursorX2 : plotItem.cursorX2 < plotItem.cursorX1))
                                          ? -width - 7 : 7)))
                y: modelData.labelY
                text: root.rawReadouts[formatKey] ? modelData.rawText : modelData.text
                height: 18
                leftPadding: 5; rightPadding: 5; topPadding: 2; bottomPadding: 2
                background: Rectangle {
                    color: root.plotColor
                    border.color: cursorValueLabel.modelData.color
                    border.width: 1
                }
                color: modelData.color
                font.pixelSize: 10
                z: 4
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.toggleReadout(parent.formatKey)
                }
            }
        }
    }

    Repeater {
        id: cursorTimes
        model: plotItem.cursorMode
        // itemAt() alone does not notify bindings when delegates first appear.
        onItemAdded: { root.cursorTimeItems = [null, null]; Qt.callLater(root.refreshCursorTimeItems) }
        onItemRemoved: { root.cursorTimeItems = [null, null]; Qt.callLater(root.refreshCursorTimeItems) }
        delegate: Text {
            id: cursorTime
            required property int index
            readonly property real value: index === 0 ? plotItem.cursorX1 : plotItem.cursorX2
            readonly property string formatKey: "x:" + index
            objectName: "cursorTimeLabel"
            visible: root.cursorInView(value)
            x: root.cursorBadgePositions[index]
            y: axisRect.y + axisRect.height
            width: Math.min(implicitWidth, root.bothCursorTimesVisible
                ? Math.max(1, (axisRect.width - 8) / 3)
                : axisRect.width)
            text: root.formatCompact(value, 9)
            padding: 3
            font.pixelSize: 10
            elide: Text.ElideRight
            color: root.textColor
            Rectangle {
                anchors.fill: parent
                z: -1
                color: root.darkTheme ? "#29323d" : "#f5f7fa"
                radius: 3
                border.color: cursorTime.index === 0 ? "#ef4452" : "#4778ed"
                border.width: plotItem.activeCursorIndex === 0 || plotItem.activeCursorIndex === cursorTime.index + 1 ? 2 : 1
            }
            z: 10
            MouseArea {
                id: timeDrag
                anchors.fill: parent
                cursorShape: Qt.ArrowCursor
                hoverEnabled: true
                onEntered: { root.hoveredAxis = -1; root.updateAxisCursor("", 0, 0, false) }
                property real startPixel: 0
                property real startX1: 0
                property real startX2: 0
                onPressed: function(mouse) { root.beginCursorBadgeDrag(timeDrag, mouse, cursorTime.index + 1) }
                onDoubleClicked: root.openCursorEditor(cursorTime.index + 1)
                onPositionChanged: function(mouse) { root.moveCursorBadge(timeDrag, mouse, cursorTime.index + 1) }
            }
        }
    }

    Repeater {
        model: root.trajectoryMode ? [] : plotItem.xTicks
        delegate: Item {
            id: xTick
            required property var modelData
            x: root.axisLeft + root.xPixel(modelData.value)
            y: axisRect.y + axisRect.height
            width: 1
            height: root.axisBottom

            Rectangle {
                width: 1
                height: 5
                color: root.axisColor
            }
            Label {
                anchors.top: parent.top
                anchors.topMargin: 6
                x: Math.max(-parent.x, Math.min(root.width - parent.x - width, -width / 2))
                width: implicitWidth
                horizontalAlignment: Text.AlignHCenter
                text: xTick.modelData.label
                visible: !root.tickCovered(xTick.modelData.value, implicitWidth)
                color: root.textColor
                font.pixelSize: 10
            }
        }
    }

    Repeater {
        model: root.trajectoryMode || root.stateMode ? [] : plotItem.yTicks
        delegate: Item {
            id: yTick
            required property var modelData
            x: 0
            y: axisRect.y + root.yPixel(modelData.value)
            width: root.axisLeft
            height: 1

            Rectangle {
                anchors.right: parent.right
                width: 5
                height: 1
                color: root.axisColor
            }
            Label {
                anchors.right: parent.right
                anchors.rightMargin: root.yTickLabelMargin
                anchors.verticalCenter: parent.verticalCenter
                width: root.axisLeft - root.yTickLabelMargin
                horizontalAlignment: Text.AlignRight
                text: root.yTickLabel(yTick.modelData)
                color: root.textColor
                font.pixelSize: 10
            }
        }
    }

    function stateIdentityColor(row) {
        const colors = root.controller.presetColors
        return colors.length > 0 ? colors[row % colors.length] : root.textColor
    }
    function selectStateRow(row) {
        root.selectedStateRow = row
        const top = row * stateItem.rowHeight
        const offset = stateScroll.position * stateItem.height
        let desired = offset
        if (top < offset) desired = top
        else if (top + stateItem.rowHeight > offset + axisRect.height) desired = top + stateItem.rowHeight-axisRect.height
        stateScroll.position = Math.max(0,Math.min(1-stateScroll.size,desired/Math.max(1,stateItem.height)))
    }
    readonly property int stateReadoutCursor: plotItem.activeCursorIndex === 2 ? 2 : 1
    readonly property var stateCursorValues: {
        // Track source/visible-lane configuration changes as well as cursor time.
        const rows = stateItem.rowNames
        if (!root.stateMode || plotItem.cursorMode === 0 || rows.length === 0) return []
        return stateItem.cursorValues(root.stateReadoutCursor === 2 ? plotItem.cursorX2 : plotItem.cursorX1)
    }
    component StateCurveGlyph: Item {
        id: glyph
        property color strokeColor: "#0072bd"
        Rectangle { x: 1; y: 12; width: 8; height: 2; color: glyph.strokeColor }
        Rectangle { x: 7; y: 5; width: 2; height: 9; color: glyph.strokeColor }
        Rectangle { x: 7; y: 5; width: 9; height: 2; color: glyph.strokeColor }
        Rectangle { x: 14; y: 5; width: 2; height: 9; color: glyph.strokeColor }
        Rectangle { x: 14; y: 12; width: 9; height: 2; color: glyph.strokeColor }
    }
    Item {
        visible: root.stateMode
        x: 0; y: axisRect.y; width: root.axisLeft; height: axisRect.height; clip: true; z: 3
        Repeater {
            model: root.stateMode ? stateItem.rowNames : []
            delegate: Item {
                id: stateRowIcon
                objectName: "stateTimelineRow" + index
                required property int index
                required property string modelData
                x: Math.max(0,root.axisLeft-59); y: stateItem.y + index * stateItem.rowHeight
                width: 58; height: stateItem.rowHeight
                StateCurveGlyph { x: 1; anchors.verticalCenter: parent.verticalCenter; width: 24; height: 20; strokeColor: root.stateIdentityColor(stateRowIcon.index) }
                Label {
                    objectName: "stateTimelineValue" + stateRowIcon.index
                    x: 28; width: 26; height: parent.height
                    text: root.stateCursorValues[stateRowIcon.index] ?? "—"
                    color: root.textColor; font.pixelSize: 11
                    verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                }
                Rectangle { x: 0; y: 5; width: 2; height: parent.height-10; color: root.textColor; visible: stateRowIcon.index === root.selectedStateRow }
                TapHandler { onTapped: root.selectStateRow(stateRowIcon.index) }
                HoverHandler { id: stateRowHover }
                ToolTip.visible: stateRowHover.hovered
                ToolTip.delay: 400
                ToolTip.text: stateRowIcon.modelData + (plotItem.cursorMode === 0 ? "" : "\nT" + root.stateReadoutCursor + "：" + (root.stateCursorValues[stateRowIcon.index] ?? "—"))
            }
        }
    }
    Flow {
        id: stateLegend
        objectName: "stateTimelineLegend"
        visible: root.stateMode
        readonly property bool besideToolbar: root.width-stateToolbar.width-35 >= 200
        x: besideToolbar ? stateToolbar.x+stateToolbar.width+8 : 28
        y: besideToolbar ? 2 : stateToolbar.y+stateToolbar.height+3
        width: Math.max(1,root.width-x-8)
        spacing: 4
        z: 6
        Repeater {
            model: root.stateMode ? stateItem.rowNames : []
            delegate: Item {
                id: stateLegendEntry
                objectName: "stateLegendEntry" + index
                required property int index
                required property string modelData
                width: Math.min(stateLegend.width,stateLegendText.implicitWidth+34)
                height: 22
                StateCurveGlyph { x: 2; width: 24; height: 20; anchors.verticalCenter: parent.verticalCenter; strokeColor: root.stateIdentityColor(stateLegendEntry.index) }
                Label {
                    id: stateLegendText
                    x: 30; width: Math.max(0,parent.width-32); height: parent.height
                    text: stateLegendEntry.modelData; color: root.textColor
                    font.pixelSize: 11; font.bold: stateLegendEntry.index === root.selectedStateRow
                    verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                }
                TapHandler { onTapped: root.selectStateRow(stateLegendEntry.index) }
                HoverHandler { id: stateLegendHover }
                ToolTip.visible: stateLegendHover.hovered
                ToolTip.delay: 400
                ToolTip.text: stateLegendEntry.modelData
            }
        }
    }
    MouseArea {
        visible: root.stateMode
        x: 0; y: axisRect.y; width: root.axisLeft; height: axisRect.height
        acceptedButtons: Qt.NoButton
        onWheel: wheel => { stateScroll.position = Math.max(0, Math.min(1-stateScroll.size, stateScroll.position-wheel.angleDelta.y/120*stateItem.rowHeight/stateItem.height)) }
    }
    ScrollBar {
        id: stateScroll; objectName: "stateTimelineScrollBar"
        visible: root.stateMode && size < 1
        x: axisRect.x + axisRect.width - width; y: axisRect.y; height: axisRect.height; z: 7
        orientation: Qt.Vertical
        size: Math.min(1, axisRect.height / Math.max(1,stateItem.height))
        policy: ScrollBar.AlwaysOn
        onSizeChanged: position = Math.max(0,Math.min(position,1-size))
    }
    Row {
        id: stateToolbar
        visible: root.stateMode; x: 25; y: 2; spacing: 3; z: 6
        Button { text: "状态行…"; height: 30; onClicked: root.stateConfigurationRequested(root.plotIndex) }
        Button { text: "上次变化"; height: 30; enabled: stateItem.hasFrame; onClicked: root.jumpState(-1) }
        Button { text: "下次变化"; height: 30; enabled: stateItem.hasFrame; onClicked: root.jumpState(1) }
    }
    Label {
        objectName: "stateTimelineStatusMessage"
        visible: root.stateMode && ((stateItem.pending && !stateItem.hasFrame) || stateItem.error.length > 0 || stateItem.rowNames.length === 0)
        x: axisRect.x + 8; y: axisRect.y + 8; width: Math.max(1, axisRect.width - 16); z: 5
        color: root.textColor; wrapMode: Text.WordWrap
        text: stateItem.pending && !stateItem.hasFrame ? "正在准备状态区间…" : stateItem.error.length > 0 ? stateItem.error : "点击‘状态行…’，再从信号树添加来源"
    }
    Loader {
        anchors.fill: parent
        anchors.margins: 2
        z: 20
        active: root.trajectoryMode
        sourceComponent: Component {
            TrajectoryPlot {
                controller: root.controller
                plotIndex: root.plotIndex
                plotColor: root.plotColor
                textColor: root.textColor
                axisColor: root.axisColor
                graphLineWidth: root.graphLineWidth
                darkTheme: root.darkTheme
                onEditObjectRequested: objectId => root.editObjectRequested(objectId)
                onEditAxesRequested: { if(root.trajectoryEditor) root.trajectoryEditor.editPlot(root.plotIndex); root.trajectoryConfigurationRequested(root.plotIndex) }
            }
        }
    }
    Menu {
        id: plotContextMenu
        objectName: "plotContextMenu"
        ViewModeMenu {
            controller: root.controller
            plotIndex: root.plotIndex
            currentMode: root.trajectoryMode ? "trajectory" : root.stateMode ? "states" : "curve"
        }
        MenuSeparator { }
        MenuItem {
            text: "自适应当前 Y 轴"
            icon.source: "qrc:/icons/arrows_up_down.svg"
            onTriggered: root.controller.fitPlotY(root.plotIndex)
        }
        MenuItem {
            text: "清除当前子图所有信号"
            icon.source: "qrc:/icons/clear.svg"
            onTriggered: root.controller.clearPlotSignals(root.plotIndex)
        }
        MenuSeparator { }
        Menu {
            title: "设置"
            icon.source: "qrc:/icons/settings.svg"
            MenuItem {
                text: "归一化 Y 轴 (Normalize y-axis)"
                checkable: true
                checked: plotItem.normalizeY
                onTriggered: plotItem.normalizeY = !plotItem.normalizeY
            }
            MenuItem {
                text: "小幅值曲线优先置顶（保留原色）"
                checkable: true
                checked: root.blendOverlaps
                onTriggered: root.blendOverlaps = !root.blendOverlaps
            }
        }
    }
    MouseArea {
        id: plotContextArea
        objectName: "plotContextArea"
        visible: !root.trajectoryMode
        x: axisRect.x; y: axisRect.y
        width: axisRect.width; height: axisRect.height
        acceptedButtons: Qt.RightButton
        z: 4
        onClicked: function(mouse) {
            root.controller.setActivePlot(root.plotIndex)
            plotContextMenu.popup()
        }
    }

    PlotAxisArea {
        objectName: "xAxisArea"
        visible: !root.trajectoryMode
        axis: 0
        plot: root
        renderer: plotItem
        x: axisRect.x; y: axisRect.y + axisRect.height
        width: axisRect.width; height: root.axisBottom
    }
    PlotAxisArea {
        objectName: "yAxisArea"
        visible: !root.trajectoryMode && !root.stateMode
        axis: 1
        plot: root
        renderer: plotItem
        x: 0; y: axisRect.y
        width: root.axisLeft; height: axisRect.height
    }
    PlotLegend {
        id: legend
        visible: !root.trajectoryMode && !root.stateMode
        x: 30
        y: 1
        width: Math.max(1, root.width - 36)
        z: 5
        plot: root
        controller: root.controller
        renderer: plotItem
        dragPreview: legendDragPreview
        darkTheme: root.darkTheme
        textColor: root.textColor
    }

    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 6
        text: root.plotIndex + 1
        color: root.textColor
        opacity: 0.45
    }

    Rectangle {
        id: deltaBadge
        objectName: "cursorDeltaBadge"
        x: root.cursorBadgePositions[2]
        y: axisRect.y + axisRect.height
        z: 9
        visible: root.bothCursorTimesVisible && root.cursorTimeItems[0] !== null && root.cursorTimeItems[1] !== null
        color: "#59636e"
        radius: 3
        border.color: root.frameColor
        border.width: plotItem.activeCursorIndex === 0 ? 2 : 1
        implicitWidth: deltaLabel.implicitWidth + 14
        width: Math.min(implicitWidth, Math.max(1, (axisRect.width - 8) / 3))
        implicitHeight: deltaLabel.implicitHeight + 5
        Text {
            id: deltaLabel
            anchors.centerIn: parent
            width: Math.max(0, parent.width - 14)
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            text: root.formatCompact(plotItem.cursorDeltaT, 7)
            color: "#ffffff"
            font.pixelSize: 10
        }
        MouseArea {
            id: deltaDrag
            anchors.fill: parent
            cursorShape: Qt.ArrowCursor
            hoverEnabled: true
            onEntered: { root.hoveredAxis = -1; root.updateAxisCursor("", 0, 0, false) }
            property real startPixel: 0
            property real startX1: 0
            property real startX2: 0
            onPressed: function(mouse) { root.beginCursorBadgeDrag(deltaDrag, mouse, 0) }
            onDoubleClicked: root.openCursorEditor(0)
            onPositionChanged: function(mouse) { root.moveCursorBadge(deltaDrag, mouse, 0) }
        }
    }
}
