pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DataInspector

Dialog {
    id: dialog
    objectName: "aboutDialog"
    required property AppController appController
    anchors.centerIn: parent
    width: Math.min(680, parent ? parent.width - 24 : 680)
    height: Math.min(620, parent ? parent.height - 24 : 620)
    title: "关于 DataInspector"
    modal: true
    standardButtons: Dialog.Close

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Image { source: "qrc:/icons/DataInspector.svg"; Layout.preferredWidth: 48; Layout.preferredHeight: 48 }
            ColumnLayout {
                Label { text: "DataInspector  " + dialog.appController.aboutInfo.version; font.pixelSize: 22; font.bold: true }
                Label { text: "工程时间序列数据与运动轨迹查看器"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Layout.fillWidth: true
            }
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "构建信息" }
            TabButton { text: "更新记录" }
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                clip: true
                contentWidth: availableWidth
                TextArea {
                    objectName: "aboutBuildInfo"
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    text: {
                        const info = dialog.appController.aboutInfo
                        return "版本号：" + info.version + "\n\n构建时间：" + info.buildTime
                            + "\n\nGit hash：" + info.gitHash + "\n\nGit 分支：" + info.gitBranch
                            + "\n\n工作区状态：" + info.gitState + "\n\n构建类型：" + info.buildType
                            + "\n\nQt 版本：" + info.qtVersion + "\n\n架构：" + info.architecture
                            + "\n\nMAT 支持：" + info.matSupport
                    }
                }
            }
            ScrollView {
                clip: true
                contentWidth: availableWidth
                TextArea {
                    objectName: "aboutReleaseNotes"
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WordWrap
                    textFormat: TextEdit.MarkdownText
                    text: dialog.appController.releaseNotes
                }
            }
        }
        Button { text: "复制构建信息"; onClicked: dialog.appController.copyAboutInfo() }
    }
}
