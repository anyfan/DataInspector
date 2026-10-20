pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DataInspector

Dialog {
    id: dialog
    required property AppController appController
    objectName: "automationDialog"
    title: "CLI 自动化"
    modal: true
    standardButtons: Dialog.Close
    width: Math.min(640, Math.max(1, (parent ? parent.width : 700) - 40))
    anchors.centerIn: parent
    ColumnLayout {
        width: parent.width
        spacing: 12
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: dialog.appController.automationEnabled ? "CLI 已开启，控制当前窗口。" : "CLI 未开启。开启后可用脚本控制当前窗口，无需重新加载数据。"
        }
        Button {
            objectName: "automationToggleButton"
            text: dialog.appController.automationEnabled ? "关闭 CLI" : "开启 CLI"
            onClicked: {
                if (dialog.appController.automationEnabled) dialog.appController.disableAutomation()
                else dialog.appController.enableAutomation()
            }
        }
        Label {
            Layout.fillWidth: true
            visible: dialog.appController.automationError.length > 0
            wrapMode: Text.Wrap
            text: dialog.appController.automationError
            color: "#c73535"
        }
        Label { visible: dialog.appController.automationEnabled; text: "连接文件" }
        TextField {
            objectName: "automationConnectionPath"
            Layout.fillWidth: true
            visible: dialog.appController.automationEnabled
            readOnly: true
            selectByMouse: true
            text: dialog.appController.automationConnectionFile
            onTextChanged: cursorPosition = 0
        }
        Label { visible: dialog.appController.automationEnabled; text: "PowerShell 调用示例" }
        TextArea {
            Layout.fillWidth: true
            visible: dialog.appController.automationEnabled
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere
            text: dialog.appController.automationCommand
        }
        RowLayout {
            visible: dialog.appController.automationEnabled
            Button { text: "复制连接路径"; onClicked: dialog.appController.copyAutomationConnectionFile() }
            Button { text: "复制调用命令"; onClicked: dialog.appController.copyAutomationCommand() }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "调用端需要 Python 3.9+ 和随包提供的 datainspector_cli.py。仅允许本机连接；连接文件包含访问令牌，请勿分享。关闭窗口后自动关闭 CLI，下次启动默认关闭。"
        }
    }
}
