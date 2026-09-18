import QtQuick
import QtQuick.Controls

// Wrapping legend strip at the top of a QuickPlot. Left click reveals the
// signal in the tree, right click opens the subplot menu, dragging moves the
// signal to another subplot (the drop preview is owned by the plot).
Flow {
    id: legend

    required property var plot
    required property var controller
    required property var renderer
    required property var dragPreview
    property bool darkTheme: false
    property color textColor: "#303942"
    readonly property int entryCount: legendRepeater.count

    visible: legendRepeater.count > 0
    spacing: 0

    Repeater {
        id: legendRepeater
        model: legend.controller.plotStateRevision >= 0
               ? legend.controller.plotSignalRows(legend.plot.plotIndex) : []
        delegate: Item {
            id: entry
            required property int modelData
            property int signalRow: modelData
            property int styleRevision: legend.controller.plotStateRevision
            property color previewColor: styleRevision >= 0
                                         ? legend.controller.signalColor(signalRow)
                                         : "transparent"
            readonly property bool highlighted: legend.renderer.highlightedSeries === signalRow
            implicitWidth: Math.min(legend.width, legendLabel.implicitWidth + 30)
            implicitHeight: Math.max(14, legendLabel.implicitHeight + 1)

            Rectangle {
                anchors.fill: parent
                color: legend.darkTheme ? "#334658" : "#dfedfa"
                visible: entry.highlighted
                radius: 2
            }
            Rectangle {
                objectName: "legendColorSquare"
                width: 8
                height: 8
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                color: entry.previewColor
            }
            Label {
                id: legendLabel
                anchors.left: parent.left
                anchors.leftMargin: 22
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - 22)
                text: legend.controller.signalName(entry.signalRow)
                font.bold: entry.highlighted
                elide: Text.ElideRight
                color: legend.textColor
                font.pixelSize: 10
            }
            MouseArea {
                id: legendMouse
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                property point pressPoint
                property bool moving: false
                property var destination: null
                cursorShape: moving ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                function clearDestination() {
                    if (destination) destination.dropHighlighted = false
                    destination = null
                }
                onPressed: function(mouse) {
                    pressPoint = Qt.point(mouse.x, mouse.y)
                    moving = false
                }
                onPositionChanged: function(mouse) {
                    if (!(pressedButtons & Qt.LeftButton)) return
                    if (!moving && Math.hypot(mouse.x - pressPoint.x, mouse.y - pressPoint.y) < 8) return
                    moving = true
                    const preview = legend.dragPreview
                    const previewPoint = mapToItem(preview.parent, mouse.x, mouse.y)
                    preview.x = Math.max(0, Math.min(preview.parent.width - preview.width, previewPoint.x + 14))
                    preview.y = Math.max(0, Math.min(preview.parent.height - preview.height, previewPoint.y + 16))
                    preview.signalName = legend.controller.signalName(entry.signalRow)
                    preview.signalColor = entry.previewColor
                    legend.plot.legendDragging = true
                    clearDestination()
                    for (const candidate of legend.plot.parent.children) {
                        if (candidate === legend.plot || !candidate.visible || candidate.renderer === undefined) continue
                        const point = mapToItem(candidate, mouse.x, mouse.y)
                        if (point.x >= 0 && point.y >= 0 && point.x < candidate.width && point.y < candidate.height) {
                            destination = candidate
                            destination.dropHighlighted = true
                            break
                        }
                    }
                }
                onReleased: {
                    legend.plot.legendDragging = false
                    if (moving && destination) {
                        const controller = legend.controller
                        const from = legend.plot.plotIndex
                        const to = destination.plotIndex
                        const row = entry.signalRow
                        // Moving changes the legend models and destroys this delegate.
                        Qt.callLater(function() { controller.moveLegendSignal(from, to, row) })
                    }
                    clearDestination()
                }
                onCanceled: { legend.plot.legendDragging = false; moving = false; clearDestination() }
                Component.onDestruction: legend.plot.legendDragging = false
                onClicked: function(mouse) {
                    if (moving) return
                    legend.controller.setActivePlot(legend.plot.plotIndex)
                    if (mouse.button === Qt.RightButton) legendMenu.popup()
                    else legend.controller.revealLegendSignal(legend.plot.plotIndex, entry.signalRow)
                }
                Menu {
                    id: legendMenu
                    MenuItem {
                        text: "移除“" + legend.controller.signalName(entry.signalRow) + "”"
                        onTriggered: legend.controller.removeLegendSignal(legend.plot.plotIndex, entry.signalRow)
                    }
                    MenuSeparator { }
                    MenuItem {
                        text: "自适应当前 Y 轴"
                        icon.source: "qrc:/icons/arrows_up_down.svg"
                        onTriggered: legend.controller.fitPlotY(legend.plot.plotIndex)
                    }
                    MenuItem {
                        text: "清除当前子图所有信号"
                        icon.source: "qrc:/icons/clear.svg"
                        onTriggered: legend.controller.clearPlotSignals(legend.plot.plotIndex)
                    }
                }
            }
        }
    }
}
