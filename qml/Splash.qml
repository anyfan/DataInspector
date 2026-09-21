pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

// Deliberately uses only QtQuick: it must be cheaper than Main.qml and must be
// visible before Controls, Dialogs, plots and the renderer are initialized.
Window {
    id: root
    objectName: "startupSplash"
    width: 460
    height: 286
    visible: true
    color: "transparent"
    flags: Qt.SplashScreen | Qt.FramelessWindowHint
    title: "DataInspector"

    signal dismissed()

    Rectangle {
        anchors.fill: parent
        radius: 22
        color: "#0f2034"
        border.color: "#4b83ad"
        border.width: 2

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: 20
            color: "#183653"
            opacity: 0.72
        }

        Image {
            id: icon
            anchors.left: parent.left
            anchors.leftMargin: 34
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/icons/DataInspector.svg"
            sourceSize.width: 118
            sourceSize.height: 118
            width: 118
            height: 118
            smooth: true
        }

        Column {
            anchors.left: icon.right
            anchors.leftMargin: 25
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Text {
                text: "DataInspector"
                color: "#eef7ff"
                font.pixelSize: 25
                font.weight: Font.DemiBold
            }

            Text {
                text: "时间序列数据查看器"
                color: "#b8cee2"
                font.pixelSize: 14
            }

            Row {
                spacing: 6
                topPadding: 13

                Repeater {
                    model: 4
                    delegate: Rectangle {
                        width: 7
                        height: 7
                        radius: 3.5
                        color: "#4fc3f7"

                        SequentialAnimation on opacity {
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.35; to: 1.0; duration: 260; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 0.35; duration: 420; easing.type: Easing.InOutSine }
                            PauseAnimation { duration: 180 }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: progressTrack
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 34
            anchors.rightMargin: 34
            anchors.bottomMargin: 25
            height: 3
            radius: 1.5
            color: "#315774"

            Rectangle {
                id: progressBar
                width: 84
                height: parent.height
                radius: parent.radius
                color: "#ffd166"

                SequentialAnimation on x {
                    loops: Animation.Infinite
                    NumberAnimation {
                        from: 0
                        to: progressTrack.width - progressBar.width
                        duration: 1250
                        easing.type: Easing.InOutCubic
                    }
                    NumberAnimation {
                        from: progressTrack.width - progressBar.width
                        to: 0
                        duration: 1250
                        easing.type: Easing.InOutCubic
                    }
                }
            }
        }
    }

    NumberAnimation {
        id: fadeOut
        target: root
        property: "opacity"
        to: 0
        duration: 180
        easing.type: Easing.OutCubic
        onFinished: {
            root.visible = false
            root.dismissed()
        }
    }

    function dismiss() {
        if (!fadeOut.running) fadeOut.start()
    }
}
