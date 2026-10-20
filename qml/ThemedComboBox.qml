pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes

ComboBox {
    id: combo
    property bool darkTheme: false
    hoverEnabled: true
    leftPadding: 12
    rightPadding: indicator.width + 12
    topPadding: 8
    bottomPadding: 8
    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 36
        radius: 4
        color: !combo.enabled ? (combo.darkTheme ? "#252c34" : "#edf1f5")
            : combo.down ? (combo.darkTheme ? "#293e52" : "#d9eafa")
            : combo.hovered ? (combo.darkTheme ? "#263440" : "#f0f7ff")
            : combo.darkTheme ? "#171c22" : "#ffffff"
        border.width: combo.activeFocus || combo.popup.visible ? 2 : 1
        border.color: !combo.enabled ? (combo.darkTheme ? "#424d59" : "#c5ced8")
            : combo.activeFocus || combo.popup.visible || combo.hovered ? "#0078d4"
            : combo.darkTheme ? "#8192a4" : "#84909c"
    }
    indicator: Item {
        width: 32
        height: 24
        x: combo.width - width - 4
        y: (combo.height - height) / 2
        opacity: combo.enabled ? 1 : 0.4
        Rectangle {
            width: 1
            height: parent.height - 4
            anchors.verticalCenter: parent.verticalCenter
            color: combo.darkTheme ? "#53606d" : "#c5ced8"
        }
        Shape {
            anchors.centerIn: parent
            width: 12
            height: 8
            rotation: combo.popup.visible ? 180 : 0
            ShapePath {
                strokeColor: combo.darkTheme ? "#e6edf3" : "#344454"
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: 2; startY: 2
                PathLine { x: 6; y: 6 }
                PathLine { x: 10; y: 2 }
            }
        }
    }
    contentItem: Label {
        text: combo.displayText
        color: combo.palette.buttonText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        opacity: combo.enabled ? 1 : 0.45
    }
    delegate: ItemDelegate {
        id: option
        objectName: "themedComboOption"
        required property int index
        required property var modelData
        width: combo.popup.width
        text: combo.textRole.length > 0 ? String(modelData[combo.textRole]) : String(modelData)
        highlighted: combo.highlightedIndex === index
        readonly property bool selected: highlighted || combo.currentIndex === index
        contentItem: Label { text: option.text; color: combo.palette.text; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { color: option.selected ? (combo.darkTheme ? "#293e52" : "#d9eafa") : combo.palette.base }
    }
}
