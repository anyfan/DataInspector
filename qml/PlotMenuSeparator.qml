pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Fusion

MenuSeparator {
    property bool available: true
    visible: available
    height: available ? implicitHeight : 0
}
