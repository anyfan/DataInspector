pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DataInspector

Item {
    id: sessionDialogs
    required property AppController appController
    required property real hostWidth
    required property real hostHeight
    signal closeRequested()
    property string pendingUnsavedAction: ""
    function requestClose() {
        if (!appController.sessionModified) return true
        pendingUnsavedAction = "close"
        unsavedSessionDialog.open()
        return false
    }
    function saveSession() { saveSessionDialog.open() }
    function openSession() { openSessionDialog.open() }
    function saveTemplate() { saveViewTemplateDialog.open() }
    function openTemplate() { openViewTemplateDialog.open() }
    function showError(message) { sessionErrorDialog.message = message; sessionErrorDialog.open() }
    property url pendingSessionUrl: ""
    property var missingSources: []
    property var relocatedSources: ({})
    property int missingSourceIndex: 0
    function beginSessionRestore() {
        missingSources = sessionDialogs.appController.missingSessionFiles(pendingSessionUrl)
        relocatedSources = ({})
        missingSourceIndex = 0
        locateNextSource()
    }
    function locateNextSource() {
        if (missingSourceIndex < missingSources.length) {
            relocateFileDialog.title = "重新定位：" + missingSources[missingSourceIndex].path
            relocateFileDialog.open()
        } else sessionDialogs.appController.restoreSessionWithFiles(pendingSessionUrl, relocatedSources)
    }
    FileDialog {
        id: relocateFileDialog
        objectName: "relocateFileDialog"
        fileMode: FileDialog.OpenFile
        nameFilters: ["数据文件 (*.csv *.txt *.xlsx *.mat)"]
        onAccepted: {
            sessionDialogs.relocatedSources[String(sessionDialogs.missingSources[sessionDialogs.missingSourceIndex].index)] = selectedFile
            ++sessionDialogs.missingSourceIndex
            sessionDialogs.locateNextSource()
        }
    }
    function continueUnsavedAction() {
        const action = pendingUnsavedAction
        pendingUnsavedAction = ""
        if (action === "close") sessionDialogs.closeRequested()
        else if (action === "restore") sessionDialogs.beginSessionRestore()
    }
    Dialog {
        id: unsavedSessionDialog
        objectName: "unsavedSessionDialog"
        title: "当前会话尚未保存"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        Label { text: "是否保存当前的信号绑定、样式和视图？\n会话文件不包含原始数据。" }
        onAccepted: saveSessionDialog.open()
        onDiscarded: sessionDialogs.continueUnsavedAction()
        onRejected: sessionDialogs.pendingUnsavedAction = ""
    }
    FileDialog {
        id: saveSessionDialog
        objectName: "saveSessionDialog"
        title: "保存会话（不包含原始数据）"
        nameFilters: ["DataInspector 会话 (*.disession)", "JSON 文件 (*.json)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "disession"
        onAccepted: {
            if (sessionDialogs.appController.saveSession(selectedFile)) sessionDialogs.continueUnsavedAction()
        }
        onRejected: sessionDialogs.pendingUnsavedAction = ""
    }

    FileDialog {
        id: saveViewTemplateDialog
        objectName: "saveViewTemplateDialog"
        title: "保存视图模板"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "diview"
        nameFilters: ["DataInspector 视图模板 (*.diview)"]
        onAccepted: {
            if (!sessionDialogs.appController.saveViewTemplate(selectedFile)) {
                sessionErrorDialog.message = sessionDialogs.appController.status
                sessionErrorDialog.open()
            }
        }
    }
    FileDialog {
        id: openViewTemplateDialog
        objectName: "openViewTemplateDialog"
        title: "应用视图模板"
        fileMode: FileDialog.OpenFile
        nameFilters: ["DataInspector 视图模板 (*.diview)"]
        onAccepted: {
            const preview = sessionDialogs.appController.previewViewTemplate(selectedFile)
            if (preview.error.length > 0) {
                sessionErrorDialog.message = preview.error
                sessionErrorDialog.open()
                return
            }
            viewTemplateDialog.file = selectedFile
            viewTemplateDialog.preview = preview
            const mapping = {}
            for (const row of preview.rows) mapping[String(row.id)] = row.match
            viewTemplateDialog.mapping = mapping
            viewTemplateDialog.errorText = ""
            fixedTemplateRanges.checked = false
            viewTemplateDialog.open()
        }
    }
    Dialog {
        id: viewTemplateDialog
        objectName: "viewTemplateDialog"
        anchors.centerIn: parent
        modal: true
        width: Math.min(780, sessionDialogs.hostWidth - 40)
        height: Math.min(560, sessionDialogs.hostHeight - 40)
        title: "应用视图模板 · 确认信号匹配"
        property url file
        property var preview: ({ rows: [], options: [], plotCount: 0 })
        property var mapping: ({})
        property string errorText: ""
        readonly property bool complete: {
            const assigned = new Set()
            for (const row of preview.rows) {
                const value = Number(mapping[String(row.id)])
                if (!Number.isInteger(value) || value < 0 || assigned.has(value)) return false
                assigned.add(value)
            }
            return true
        }
        function assign(id, value) {
            const next = Object.assign({}, mapping)
            next[String(id)] = value
            mapping = next
        }
        function apply() {
            if (!complete) { errorText = "请补齐匹配，同一信号不能重复分配。"; return }
            if (!sessionDialogs.appController.applyViewTemplate(file, mapping, fixedTemplateRanges.checked)) {
                errorText = sessionDialogs.appController.status
                return
            }
            close()
        }
        contentItem: ColumnLayout {
            Label {
                text: "将替换为 " + viewTemplateDialog.preview.plotCount + " 个子图。请核对每项来源；缺失或重名信号需手动指定。"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ListView {
                objectName: "viewTemplateMatches"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: viewTemplateDialog.preview.rows
                ScrollBar.vertical: ScrollBar {}
                delegate: ColumnLayout {
                    id: templateMatchRow
                    required property var modelData
                    width: ListView.view.width
                    height: 72
                    spacing: 2
                    Label { text: templateMatchRow.modelData.label + " · " + templateMatchRow.modelData.hint; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    ComboBox {
                        objectName: "viewTemplateMatch" + templateMatchRow.modelData.id
                        Layout.fillWidth: true
                        model: viewTemplateDialog.preview.options
                        textRole: "label"
                        valueRole: "id"
                        currentIndex: {
                            const selected = Number(viewTemplateDialog.mapping[String(templateMatchRow.modelData.id)])
                            const options = viewTemplateDialog.preview.options
                            for (let i = 0; i < options.length; ++i) if (Number(options[i].id) === selected) return i
                            return 0
                        }
                        onActivated: function(index) { viewTemplateDialog.assign(templateMatchRow.modelData.id, Number(viewTemplateDialog.preview.options[index].id)) }
                    }
                }
            }
            CheckBox { id: fixedTemplateRanges; objectName: "fixedTemplateRanges"; text: "使用模板中的固定坐标范围（默认适应新数据）" }
            Label { text: "保留当前时间偏移；游标重新定位。应用后可从会话菜单撤销一次。"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label { text: viewTemplateDialog.errorText; visible: text.length > 0; color: "#df4652"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
        footer: DialogButtonBox {
            Button { text: "应用"; enabled: viewTemplateDialog.complete; onClicked: viewTemplateDialog.apply() }
            Button { text: "取消"; onClicked: viewTemplateDialog.close() }
        }
    }
    FileDialog {
        id: openSessionDialog
        objectName: "openSessionDialog"
        title: "恢复会话"
        nameFilters: ["DataInspector 会话 (*.disession *.json)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            sessionDialogs.pendingSessionUrl = selectedFile
            if (sessionDialogs.appController.sessionModified) {
                sessionDialogs.pendingUnsavedAction = "restore"
                unsavedSessionDialog.open()
            } else if (sessionDialogs.appController.loadedFileCount > 0) replaceSessionDialog.open()
            else sessionDialogs.beginSessionRestore()
        }
    }
    Dialog {
        id: replaceSessionDialog
        objectName: "replaceSessionDialog"
        title: "恢复会话"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: "恢复成功后将替换当前会话。\n如需保留当前视图，请先保存会话。\n文件缺失或恢复失败时，当前会话不变。" }
        onAccepted: sessionDialogs.beginSessionRestore()
    }
    Dialog {
        id: sessionErrorDialog
        objectName: "sessionErrorDialog"
        property string message: ""
        title: "会话操作未完成"
        modal: true
        anchors.centerIn: parent
        width: Math.min(580, sessionDialogs.hostWidth - 40)
        standardButtons: Dialog.Ok
        Label { width: parent.width; text: sessionErrorDialog.message; wrapMode: Text.WrapAnywhere }
    }
    Shortcut {
        sequence: "Ctrl+S"
        enabled: !sessionDialogs.appController.loading && !sessionDialogs.appController.exporting
        onActivated: saveSessionDialog.open()
    }
    Shortcut {
        sequence: "Ctrl+Shift+O"
        enabled: !sessionDialogs.appController.loading && !sessionDialogs.appController.exporting
        onActivated: openSessionDialog.open()
    }


}
