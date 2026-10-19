pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var presets
    property alias text: colorInput.text
    readonly property bool valid: /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/.test(text.trim())
    signal edited(string value)
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: editor.presets
            delegate: Rectangle {
                required property color modelData
                width: 22; height: 20; color: modelData; radius: 2
                border.width: String(modelData).toLowerCase() === editor.text.toLowerCase() ? 3 : 1
                border.color: colorInput.palette.text
                TapHandler { onTapped: { colorInput.text=String(parent.modelData); editor.edited(colorInput.text) } }
            }
        }
    }
    TextField {
        id: colorInput
        Layout.fillWidth: true
        placeholderText: "颜色 #RRGGBB 或 #AARRGGBB"
        maximumLength: 9
        onTextEdited: editor.edited(text)
    }
}
