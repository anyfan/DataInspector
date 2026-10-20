pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

Item {
    id: preview
    required property color lineColor
    required property real lineWidth
    required property int lineStyle
    signal clicked()
    implicitWidth: 42
    implicitHeight: 24
    Shape {
        anchors.fill: parent
        ShapePath {
            strokeColor: preview.lineColor
            strokeWidth: preview.lineWidth
            fillColor: "transparent"
            capStyle: ShapePath.FlatCap
            strokeStyle: preview.lineStyle >= 2 && preview.lineStyle <= 5 ? ShapePath.DashLine : ShapePath.SolidLine
            dashPattern: {
                const w = Math.max(1, preview.lineWidth)
                switch (preview.lineStyle) {
                case 2: return [8 / w, 4 / w]
                case 3: return [2 / w, 4 / w]
                case 4: return [8 / w, 4 / w, 2 / w, 4 / w]
                case 5: return [8 / w, 4 / w, 2 / w, 4 / w, 2 / w, 4 / w]
                default: return [4, 2]
                }
            }
            startX: 3; startY: preview.height / 2
            PathLine { x: preview.width - 3; y: preview.height / 2 }
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: preview.clicked()
    }
}
