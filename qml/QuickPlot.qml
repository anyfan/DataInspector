import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DataInspector

Rectangle {
    id: root

    required property int plotIndex
    required property var controller
    property real graphLineWidth: 2
    property int graphCursorMode: 0
    // 0: disabled, 1: X/Y region, 2: X only, 3: Y only.
    property int graphZoomMode: 0
    property bool darkTheme: false
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
            const label = root.formatYTick(ticks[i].value)
            if (label.length > widest.length) widest = label
        }
        return widest
    }
    readonly property real yTickLabelMargin: 6
    // Width this subplot needs on its own; the parent may widen it via
    // sharedAxisLeft so all subplots keep identical X-axis extents.
    readonly property real measuredAxisLeft: Math.ceil(yTickMetrics.advanceWidth) + yTickLabelMargin + 2
    property real sharedAxisLeft: 0
    readonly property real axisLeft: Math.max(measuredAxisLeft, sharedAxisLeft)
    TextMetrics {
        id: yTickMetrics
        font.pixelSize: 10
        text: root.widestYTickLabel
    }
    readonly property real axisRight: 1
    readonly property real axisTop: Math.max(20, legend.implicitHeight + 2)
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
        if (mode === 1 || mode === 2)
            plotItem.setXRange(plotItem.xMinimum + left * xSpan,
                               plotItem.xMinimum + right * xSpan)
        if (mode === 1 || mode === 3)
            plotItem.setYRange(plotItem.yMaximum - bottom * ySpan,
                               plotItem.yMaximum - top * ySpan)
        return mode >= 1 && mode <= 3
    }
    Shortcut {
        sequence: "Space"
        enabled: root.hoveredAxis >= 0
        onActivated: root.controller.fitPlots(root.hoveredAxis === 0,
                                              root.hoveredAxis === 1)
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
        id: dragPreview
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
            width: 8; height: 8; color: dragPreview.signalColor
        }
        Label {
            id: previewText
            x: 23; width: parent.width - 29
            anchors.verticalCenter: parent.verticalCenter
            text: dragPreview.signalName
            color: root.textColor
            elide: Text.ElideRight
            font.pixelSize: 11
        }
    }
    readonly property real axisBottom: 22
    property var rawReadouts: ({})
    property alias renderer: plotItem
    function toggleReadout(key) {
        const next = Object.assign({}, rawReadouts)
        next[key] = !next[key]
        rawReadouts = next
    }
    function cursorInView(value) {
        return value >= plotItem.xMinimum && value <= plotItem.xMaximum
    }
    function tickCovered(value) {
        const px = root.axisLeft + root.xPixel(value)
        for (let i = 0; i < cursorTimes.count; ++i) {
            const label = cursorTimes.itemAt(i)
            if (label && label.visible && px + 22 > label.x && px - 22 < label.x + label.width)
                return true
        }
        return deltaBadge.visible && px + 22 > deltaBadge.x && px - 22 < deltaBadge.x + deltaBadge.width
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
            return Object.assign({}, item, {labelY: root.yPixel(item.y) - 9})
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
        x: root.axisLeft
        y: root.axisTop
        width: Math.max(1, root.width - root.axisLeft - root.axisRight)
        height: Math.max(1, root.height - root.axisTop - root.axisBottom)
        clip: true

        Repeater {
            model: plotItem.xTicks
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
            model: plotItem.yTicks
            delegate: Rectangle {
                required property var modelData
                y: root.yPixel(modelData.value)
                width: axisRect.width
                height: 1
                color: root.gridColor
                opacity: 0.32
            }
        }

        PlotItem {
            id: plotItem
            anchors.fill: parent
            lineWidth: root.graphLineWidth
            cursorMode: root.graphCursorMode
            z: 1
            Component.onCompleted: root.controller.attachPlot(plotItem, root.plotIndex)
            Component.onDestruction: root.controller.detachPlot(plotItem, root.plotIndex)
            onActivated: root.controller.setActivePlot(root.plotIndex)
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
            onReleased: root.finishGraphSelection()
            onCanceled: root.graphSelecting = false
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: root.axisColor
            border.width: 1
            z: 2
        }

        Repeater {
            model: plotItem.cursorReadouts
            delegate: Rectangle {
                required property var modelData
                objectName: "cursorSampleMarker"
                width: 5; height: 5; radius: 2.5
                x: root.xPixel(modelData.sampleX) - width / 2
                y: root.yPixel(modelData.y) - height / 2
                color: modelData.color
                border.color: modelData.color
                border.width: 1
                z: 3
            }
        }

        Repeater {
            model: root.positionedReadouts
            delegate: Label {
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
                    border.color: modelData.color
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
        delegate: Label {
            required property int index
            readonly property real value: index === 0 ? plotItem.cursorX1 : plotItem.cursorX2
            readonly property string formatKey: "x:" + index
            objectName: "cursorTimeLabel"
            visible: root.cursorInView(value)
            x: Math.max(root.axisLeft, Math.min(root.width - root.axisRight - width,
                        root.axisLeft + root.xPixel(value) - width / 2))
            y: axisRect.y + axisRect.height + 3
            text: root.rawReadouts[formatKey] ? root.formatRaw(value) : root.formatCompact(value, 10)
            padding: 3
            color: "#ffffff"
            background: Rectangle { color: "#59636e" }
            z: 10
            MouseArea { anchors.fill: parent; onClicked: root.toggleReadout(parent.formatKey) }
        }
    }

    Repeater {
        model: plotItem.xTicks
        delegate: Item {
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
                text: modelData.label
                visible: !root.tickCovered(modelData.value)
                color: root.textColor
                font.pixelSize: 10
            }
        }
    }

    Repeater {
        model: plotItem.yTicks
        delegate: Item {
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
                text: root.formatYTick(modelData.value)
                color: root.textColor
                font.pixelSize: 10
            }
        }
    }

    Menu {
        id: plotContextMenu
        objectName: "plotContextMenu"
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
    }
    MouseArea {
        id: plotContextArea
        objectName: "plotContextArea"
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
        axis: 0
        plot: root
        renderer: plotItem
        x: axisRect.x; y: axisRect.y + axisRect.height
        width: axisRect.width; height: root.axisBottom
    }
    PlotAxisArea {
        objectName: "yAxisArea"
        axis: 1
        plot: root
        renderer: plotItem
        x: 0; y: axisRect.y
        width: root.axisLeft; height: axisRect.height
    }
    PlotLegend {
        id: legend
        x: 30
        y: 1
        width: Math.max(1, root.width - 36)
        z: 5
        plot: root
        controller: root.controller
        renderer: plotItem
        dragPreview: dragPreview
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
        x: Math.max(root.axisLeft,
                    Math.min(root.width - root.axisRight - width,
                             root.axisLeft
                             + root.xPixel((plotItem.cursorX1 + plotItem.cursorX2) * 0.5)
                             - width / 2))
        y: axisRect.y + axisRect.height + 3
        z: 9
        visible: plotItem.cursorMode === 2
                 && root.cursorInView(plotItem.cursorX1) && root.cursorInView(plotItem.cursorX2)
                 && cursorTimes.count === 2
                 && Math.abs(root.xPixel(plotItem.cursorX2) - root.xPixel(plotItem.cursorX1))
                    > width + Math.max(cursorTimes.itemAt(0) ? cursorTimes.itemAt(0).width : 0,
                                       cursorTimes.itemAt(1) ? cursorTimes.itemAt(1).width : 0) + 12
        color: "#59636e"
        radius: 3
        border.color: root.frameColor
        implicitWidth: deltaLabel.implicitWidth + 14
        implicitHeight: deltaLabel.implicitHeight + 5
        Label {
            id: deltaLabel
            anchors.centerIn: parent
            text: root.formatCompact(plotItem.cursorDeltaT, 7)
            color: "#ffffff"
            font.pixelSize: 10
        }
    }
}
