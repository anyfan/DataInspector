pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: combo
    property bool darkTheme: false
    contentItem: Label {
        text: combo.displayText
        color: combo.palette.buttonText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        leftPadding: combo.leftPadding
        rightPadding: combo.rightPadding
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
