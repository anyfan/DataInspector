pragma ComponentBehavior: Bound

import QtQuick

// Hover/drag/wheel handling for one axis gutter of a QuickPlot.
// axis: 0 = X (bottom gutter), 1 = Y (left gutter).
MouseArea {
    id: area

    required property int axis
    required property var plot
    required property var renderer

    readonly property string cursorIcon: axis === 0 ? "qrc:/icons/zoom-x.svg" : "qrc:/icons/zoom-y.svg"

    function fraction(mouse) {
        return Math.max(0, Math.min(1, axis === 0 ? mouse.x / width : mouse.y / height))
    }
    function showCursor(px, py) {
        plot.updateAxisCursor(cursorIcon, x + px, y + py, true)
    }
    function leave() {
        plot.hoveredAxis = -1
        plot.updateAxisCursor("", 0, 0, false)
    }

    acceptedButtons: Qt.LeftButton
    hoverEnabled: true
    cursorShape: Qt.BlankCursor
    onEntered: {
        plot.hoveredAxis = axis
        showCursor(mouseX, mouseY)
    }
    onExited: if (!plot.axisSelecting) leave()
    onPressed: function(mouse) {
        showCursor(mouse.x, mouse.y)
        plot.beginAxisSelection(axis, fraction(mouse))
    }
    onPositionChanged: function(mouse) {
        showCursor(mouse.x, mouse.y)
        if (plot.axisSelecting && plot.axisSelection === axis)
            plot.updateAxisSelection(fraction(mouse))
    }
    onReleased: {
        plot.finishAxisSelection()
        if (!containsMouse) leave()
    }
    onCanceled: {
        plot.axisSelecting = false
        plot.axisSelection = -1
        leave()
    }
    onWheel: function(wheel) {
        renderer.zoomAxis(axis, axis === 0 ? wheel.x / width : wheel.y / height,
                          wheel.angleDelta.y / 120)
        wheel.accepted = true
    }
}
