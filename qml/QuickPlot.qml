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
    property bool darkTheme: false
    property color axisColor: darkTheme ? "#8f9aa7" : "#59636e"
    property color gridColor: darkTheme ? "#53606d" : "#c9d1d9"
    property color textColor: darkTheme ? "#d7dee7" : "#303942"
    property color plotColor: darkTheme ? "#14181d" : "#ffffff"
    property color frameColor: darkTheme ? "#3b4652" : "#d7dfe8"

    readonly property real axisLeft: 64
    readonly property real axisRight: 18
    readonly property real axisTop: controller.legendMode === 0 && legend.visible
                                    ? Math.max(30, legend.implicitHeight + 10) : 12
    readonly property real axisBottom: 38
    property alias renderer: plotItem

    color: plotColor
    border.color: root.controller.activePlotIndex === root.plotIndex
                  ? "#0078d4" : frameColor
    border.width: root.controller.activePlotIndex === root.plotIndex ? 2 : 1
    radius: 3
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
            onActivated: root.controller.setActivePlot(root.plotIndex)
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
            delegate: Label {
                required property var modelData
                property bool compactFormat: true
                x: Math.max(2, Math.min(axisRect.width - width - 2,
                                       root.xPixel(modelData.x) + 5))
                y: Math.max(2, Math.min(axisRect.height - height - 2,
                                       root.yPixel(modelData.y) - height / 2))
                text: compactFormat ? modelData.text : modelData.rawText
                color: modelData.color
                font.pixelSize: 10
                z: 4
                MouseArea {
                    anchors.fill: parent
                    onClicked: parent.compactFormat = !parent.compactFormat
                }
            }
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
                anchors.horizontalCenter: parent.horizontalCenter
                width: 90
                horizontalAlignment: Text.AlignHCenter
                text: modelData.label
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
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: root.axisLeft - 10
                horizontalAlignment: Text.AlignRight
                text: modelData.label
                color: root.textColor
                font.pixelSize: 10
            }
        }
    }

    Flow {
        id: legend
        x: root.controller.legendMode === 2 ? root.width - width - 6
           : root.controller.legendMode === 0 ? 30 : 6
        y: root.controller.legendMode === 0 ? 4 : axisRect.y + 5
        width: root.controller.legendMode === 0 ? root.width - 36 : root.width * 0.46
        visible: root.controller.legendMode !== 3 && legendRepeater.count > 0
        spacing: 7
        z: 5

        Repeater {
            id: legendRepeater
            model: root.controller.plotStateRevision >= 0
                   ? root.controller.plotSignalRows(root.plotIndex) : []
            delegate: Item {
                required property int modelData
                property int signalRow: modelData
                implicitWidth: Math.min(legend.width,
                                        legendLabel.implicitWidth + 18)
                implicitHeight: 18
                opacity: root.controller.plotStateRevision >= 0
                         && root.controller.plotSignalVisible(root.plotIndex,
                                                              signalRow)
                         ? 1.0 : 0.42

                Rectangle {
                    width: 10
                    height: 3
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.controller.signalColor(parent.signalRow)
                }
                Label {
                    id: legendLabel
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, parent.width - 14)
                    text: root.controller.signalName(parent.signalRow)
                    elide: Text.ElideRight
                    color: root.textColor
                    font.pixelSize: 10
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: root.controller.setActivePlot(root.plotIndex)
                    onClicked: root.controller.togglePlotSignal(root.plotIndex,
                                                                 parent.signalRow)
                }
            }
        }
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
        x: Math.max(root.axisLeft,
                    Math.min(root.width - root.axisRight - width,
                             root.axisLeft
                             + root.xPixel((plotItem.cursorX1 + plotItem.cursorX2) * 0.5)
                             - width / 2))
        y: axisRect.y + axisRect.height + 17
        visible: plotItem.cursorMode === 2
        color: root.darkTheme ? "#26313d" : "#eef4fb"
        radius: 3
        border.color: root.frameColor
        implicitWidth: deltaLabel.implicitWidth + 14
        implicitHeight: deltaLabel.implicitHeight + 5
        Label {
            id: deltaLabel
            anchors.centerIn: parent
            text: "ΔT = " + Number(plotItem.cursorDeltaT).toPrecision(7)
            color: root.textColor
            font.pixelSize: 10
        }
    }
}
