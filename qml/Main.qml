pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import DataInspector

ApplicationWindow {
    id: window
    // Explicit injection keeps the controller visible to QML tooling and tests.
    required property AppController appController
    width: 1440
    height: 900
    visible: true
    title: (window.appController.currentFile.length > 0 ? "DataInspector · " + window.appController.currentFile : "DataInspector")
        + (window.appController.sessionModified ? " *" : "")
    color: window.panelColor
    onActiveChanged: if (!active) window.cancelSignalTreeDrag()
    property bool allowClose: false
    AboutDialog {
        id: aboutDialog
        parent: Overlay.overlay
        appController: window.appController
    }
    property alias pendingUnsavedAction: sessionDialogs.pendingUnsavedAction
    onClosing: close => {
        window.cancelSignalTreeDrag()
        if (!allowClose && !sessionDialogs.requestClose()) close.accepted = false
    }
    // Keep every Fusion control in the app theme, independent of the OS theme.
    palette.window: panelColor
    palette.windowText: treeTextColor
    palette.base: darkTheme ? "#171c22" : "#ffffff"
    palette.alternateBase: panelColor
    palette.text: treeTextColor
    palette.button: darkTheme ? "#303842" : "#edf1f5"
    palette.buttonText: treeTextColor
    palette.placeholderText: darkTheme ? "#a2adba" : "#667380"
    palette.highlight: accentColor
    palette.highlightedText: "#ffffff"
    palette.toolTipBase: panelColor
    palette.toolTipText: treeTextColor
    palette.light: darkTheme ? "#53606d" : "#ffffff"
    palette.midlight: darkTheme ? "#424d59" : "#e5eaf0"
    palette.mid: borderColor
    palette.dark: darkTheme ? "#11161c" : "#84909c"
    palette.shadow: darkTheme ? "#080c10" : "#667380"
    property color panelColor: darkTheme ? "#20252b" : "#f7f9fb"
    property color borderColor: darkTheme ? "#3b4652" : "#d7dfe8"
    property color accentColor: "#0078d4"
    property color treeTextColor: darkTheme ? "#e6edf3" : "#202830"
    readonly property color treeHeaderColor: darkTheme ? "#25313d" : "#edf4fb"
    readonly property color treeHeaderTextColor: darkTheme ? "#b2c9dc" : "#4e7da2"
    readonly property int treeHeaderHeight: 28
    property bool darkTheme: false
    property bool signalTreeVisible: true
    property int editingSignalIndex: -1
    property string pendingFileRemoval: ""
    property int pendingExportScope: 0
    property bool exportZipCompression: true

    property var signalDragDestination: null
    property int signalDragRow: -1
    property int signalDragRevision: 0
    readonly property bool signalTreeDragging: signalTreeDragPreview.visible
    function cancelSignalTreeDrag() {
        objectManager.dropFieldId = ""
        ++signalDragRevision
        if (signalDragDestination) signalDragDestination.dropHighlighted = false
        signalDragDestination = null
        signalDragRow = -1
        signalTreeDragPreview.visible = false
    }
    function updateSignalTreeDrag(source, x, y, rowItem, pressPoint) {
        if (signalDragDestination) signalDragDestination.dropHighlighted = false
        signalDragDestination = null
        signalDragRow = rowItem.signalIndex
        signalTreeDragPreview.signalName = rowItem.signalName
        signalTreeDragPreview.signalColor = rowItem.signalColor
        const point = source.mapToItem(window.contentItem, x, y)
        signalTreeDragPreview.x = point.x - pressPoint.x - 22
        signalTreeDragPreview.y = point.y - pressPoint.y - 6
        signalTreeDragPreview.visible = true
        if (objectManager.visible) {
            const position = source.mapToItem(objectManager, x, y)
            const fieldId = objectManager.fieldAt(position.x, position.y)
            objectManager.dropFieldId = fieldId
            if (fieldId.length > 0) {
                signalDragDestination = objectManager
                objectManager.dropHighlighted = true
            }
            return
        }
        for (let i = 0; i < plotRepeater.count; ++i) {
            const plot = (plotRepeater.itemAt(i) as QuickPlot)
            if (!plot || !plot.visible) continue
            const position = source.mapToItem(plot, x, y)
            if (position.x >= 0 && position.y >= 0 && position.x < plot.width && position.y < plot.height) {
                signalDragDestination = plot
                plot.dropHighlighted = true
                break
            }
        }
    }
    function finishSignalTreeDrag() {
        const destination = signalDragDestination
        const row = signalDragRow
        const fieldId = objectManager.dropFieldId
        cancelSignalTreeDrag()
        if (destination === objectManager) objectManager.dropSource(row, fieldId)
        else if (destination) {
            const state = window.appController.trajectoryState(destination.plotIndex)
            if (state.enabled) { window.appController.setActivePlot(destination.plotIndex); if (state.objectId.length > 0) objectManager.selectedId = state.objectId; objectManager.open() }
            else window.appController.addSignalToPlot(destination.plotIndex, row)
        }
    }
    Rectangle {
        id: signalTreeDragPreview
        objectName: "signalTreeDragPreview"
        parent: window.contentItem
        z: 2000
        visible: false
        property string signalName: ""
        property color signalColor: "transparent"
        width: Math.min(280, signalDragLabel.implicitWidth + 30)
        height: signalDragLabel.implicitHeight + 12
        radius: 4
        color: window.panelColor
        border.color: window.accentColor
        Rectangle { x: 8; anchors.verticalCenter: parent.verticalCenter; width: 8; height: 8; color: signalTreeDragPreview.signalColor }
        Label {
            id: signalDragLabel
            x: 22
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - 28)
            text: signalTreeDragPreview.signalName
            color: window.treeTextColor
            elide: Text.ElideRight
        }
    }
    MouseArea {
        objectName: "signalDragCursor"
        parent: window.contentItem
        anchors.fill: parent
        z: 2001
        visible: window.signalTreeDragging
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        cursorShape: Qt.BlankCursor
    }

    function highlightedSearchText(value) { return signalBrowser.highlightedSearchText(value) }

    ListModel {
        id: lineStyleModel
        ListElement { text: "实线"; value: 1 }
        ListElement { text: "虚线"; value: 2 }
        ListElement { text: "点线"; value: 3 }
        ListElement { text: "点划线"; value: 4 }
        ListElement { text: "双点划线"; value: 5 }
    }

    Dialog {
        id: signalPropertiesDialog
        title: "信号属性"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 300
        onAccepted: window.appController.setSignalPen(
            window.editingSignalIndex, colorButton.selectedColor,
            widthSpin.value, lineStyleModel.get(styleCombo.currentIndex).value)

        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { text: window.editingSignalIndex >= 0 ? window.appController.signalName(window.editingSignalIndex) : ""; color: window.treeTextColor; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
            RowLayout {
                Label { text: "颜色"; color: window.treeTextColor; Layout.preferredWidth: 54 }
                Rectangle {
                    id: colorButton
                    property color selectedColor: "#4ea1ff"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 26
                    color: selectedColor
                    border.color: window.borderColor
                    radius: 2
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            colorDialog.selectedColor = colorButton.selectedColor
                            colorDialog.open()
                        }
                    }
                }
            }
            Grid {
                columns: 7
                spacing: 6
                Layout.alignment: Qt.AlignHCenter
                Repeater {
                    model: window.appController.presetColors
                    delegate: Rectangle {
                        required property color modelData
                        width: 28; height: 24; radius: 2
                        color: modelData
                        border.width: colorButton.selectedColor === modelData ? 3 : 1
                        border.color: colorButton.selectedColor === modelData
                                      ? window.treeTextColor : window.borderColor
                        MouseArea {
                            anchors.fill: parent
                            onClicked: colorButton.selectedColor = parent.modelData
                        }
                    }
                }
            }
            RowLayout {
                Label { text: "宽度"; color: window.treeTextColor; Layout.preferredWidth: 54 }
                SpinBox { id: widthSpin; from: 1; to: 20; editable: true; Layout.fillWidth: true }
                Label { text: "px"; color: window.treeTextColor }
            }
            RowLayout {
                Label { text: "线型"; color: window.treeTextColor; Layout.preferredWidth: 54 }
                ComboBox { id: styleCombo; model: lineStyleModel; textRole: "text"; Layout.fillWidth: true }
            }
        }
    }

    ColorDialog {
        id: colorDialog
        title: "选择信号颜色"
        selectedColor: "#4ea1ff"
        onAccepted: colorButton.selectedColor = selectedColor
    }

    Menu {
        id: fileContextMenu
        property string fileName: ""
        MenuItem {
            text: "时间偏移…"
            enabled: !window.appController.loading && !window.appController.exporting
            onTriggered: window.requestTimeOffset(2, -1, fileContextMenu.fileName)
        }
        MenuItem {
            text: "移除文件 '" + fileContextMenu.fileName + "'"
            onTriggered: window.requestRemoveFile(fileContextMenu.fileName)
        }
    }

    Menu {
        id: signalContextMenu
        property int signalIndex: -1
        MenuItem {
            text: window.appController.isDerivedSignal(signalContextMenu.signalIndex) ? "管理所属对象…" : "绑定到对象…"
            enabled: !window.appController.loading && !window.appController.exporting
            onTriggered: {
                if (window.appController.isDerivedSignal(signalContextMenu.signalIndex)) objectManager.revealOutput(signalContextMenu.signalIndex)
                else objectManager.bindSource(signalContextMenu.signalIndex)
            }
        }
        MenuItem {
            text: "时间偏移…"
            enabled: !window.appController.loading && !window.appController.exporting && !window.appController.isDerivedSignal(signalContextMenu.signalIndex)
            onTriggered: window.requestTimeOffset(0, signalContextMenu.signalIndex, "")
        }
        MenuItem {
            text: "重命名…"
            onTriggered: window.requestRenameSignal(signalContextMenu.signalIndex)
        }
    }

    Menu {
        id: groupContextMenu
        property string groupName: ""
        MenuItem {
            text: "时间偏移…"
            enabled: !window.appController.loading && !window.appController.exporting
            onTriggered: window.requestTimeOffset(1, -1, groupContextMenu.groupName)
        }
    }

    function requestTimeOffset(scope, row, group) {
        timeOffsetDialog.scope = scope
        timeOffsetDialog.signalIndex = row
        timeOffsetDialog.groupName = group
        timeOffsetDialog.open()
    }

    Dialog {
        id: timeOffsetDialog
        property int scope: 0
        property bool mixedOffsets: false
        property int signalIndex: -1
        property string groupName: ""
        readonly property bool valueValid: offsetField.acceptableInput
            && offsetField.text.trim().length > 0 && isFinite(Number(offsetField.text))
        objectName: "timeOffsetDialog"
        title: "设置时间偏移"
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: {
            const current = window.appController.timeOffsetForScope(scope, signalIndex, groupName)
            mixedOffsets = current === undefined || current === null
            offsetField.text = mixedOffsets ? "" : String(current)
            const button = standardButton(Dialog.Ok)
            if (button) button.enabled = Qt.binding(function() {
                return timeOffsetDialog.valueValid && !window.appController.loading && !window.appController.exporting
            })
            offsetField.selectAll()
            offsetField.forceActiveFocus()
        }
        onAccepted: window.appController.setTimeOffset(scope, signalIndex, groupName, Number(offsetField.text))
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label {
                text: "目标：" + (timeOffsetDialog.scope === 0
                    ? window.appController.signalName(timeOffsetDialog.signalIndex) : timeOffsetDialog.groupName)
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }
            Label {
                visible: timeOffsetDialog.mixedOffsets
                text: "当前偏移不一致，输入后将统一设置。"
            }
            Label {
                text: "设置相对原始时间的偏移：正值向右，负值向左。\n组和文件操作包含未勾选、被搜索隐藏的信号。"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            TextField {
                id: offsetField
                objectName: "timeOffsetField"
                Layout.fillWidth: true
                placeholderText: "时间偏移（秒）"
                selectByMouse: true
                validator: DoubleValidator { locale: "C"; notation: DoubleValidator.ScientificNotation }
                onAccepted: if (timeOffsetDialog.valueValid && !window.appController.loading && !window.appController.exporting)
                    timeOffsetDialog.accept()
            }
            Button {
                objectName: "resetTimeOffsetButton"
                text: "重置为 0"
                onClicked: offsetField.text = "0"
            }
            Label {
                text: "点击确定后应用修改或重置。"
                opacity: 0.7
            }
        }
    }

    Dialog {
        id: renameSignalDialog
        objectName: "renameSignalDialog"
        property int signalIndex: -1
        readonly property bool nameValid: renameField.text.trim().length > 0
        title: "重命名信号"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 380
        onAboutToShow: {
            // The footer buttons exist by now; keep OK disabled for blank names.
            const okButton = standardButton(Dialog.Ok)
            if (okButton) okButton.enabled = Qt.binding(function() { return renameSignalDialog.nameValid })
            renameField.text = window.appController.signalName(signalIndex)
            renameField.selectAll()
            renameField.forceActiveFocus()
        }
        onAccepted: window.appController.renameSignal(signalIndex, renameField.text)
        ColumnLayout {
            width: parent.width
            spacing: 8
            Label {
                text: "原名称：" + (renameSignalDialog.signalIndex >= 0
                                   ? window.appController.originalSignalName(renameSignalDialog.signalIndex) : "")
                color: window.treeTextColor
                opacity: 0.78
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            TextField {
                id: renameField
                objectName: "renameSignalField"
                Layout.fillWidth: true
                placeholderText: "新名称"
                selectByMouse: true
                onAccepted: if (renameSignalDialog.nameValid) renameSignalDialog.accept()
            }
            Button {
                objectName: "resetSignalNameButton"
                text: "重置为原名称"
                onClicked: renameField.text = window.appController.originalSignalName(renameSignalDialog.signalIndex)
            }
            Label {
                text: "点击确定后应用修改或重置。"
                opacity: 0.7
            }
        }
    }

    Dialog {
        id: removeFileDialog
        title: "移除文件"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 380
        onAccepted: window.appController.removeFile(window.pendingFileRemoval)
        Label {
            width: parent.width - 24
            wrapMode: Text.Wrap
            color: window.treeTextColor
            text: "确定移除文件 '" + window.pendingFileRemoval
                  + "' 及其所有曲线吗？"
        }
    }

    function editSignalPen(signalIndex) {
        editingSignalIndex = signalIndex
        colorButton.selectedColor = window.appController.signalColor(signalIndex)
        widthSpin.value = Math.round(window.appController.signalWidth(signalIndex))
        const style = window.appController.signalStyle(signalIndex)
        let found = 0
        for (let index = 0; index < lineStyleModel.count; ++index)
            if (lineStyleModel.get(index).value === style) found = index
        styleCombo.currentIndex = found
        signalPropertiesDialog.open()
    }

    function requestRemoveFile(fileName) {
        pendingFileRemoval = fileName
        removeFileDialog.open()
    }

    function requestRenameSignal(signalIndex) {
        if (signalIndex < 0) return
        renameSignalDialog.signalIndex = signalIndex
        renameSignalDialog.open()
    }

    SessionDialogs {
        id: sessionDialogs
        anchors.fill: parent
        appController: window.appController
        hostWidth: window.width
        hostHeight: window.height
        onCloseRequested: { window.allowClose = true; window.close() }
    }
    FileDialog {
        id: fileDialog
        title: "打开数据文件"
        nameFilters: ["数据文件 (*.csv *.txt *.xlsx *.mat)", "CSV/TXT 文件 (*.csv *.txt)", "Excel 文件 (*.xlsx)", "MAT 文件 (*.mat)", "所有文件 (*)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: window.appController.loadFiles(selectedFiles)
    }

    FileDialog {
        id: exportDialog
        title: "导出 Excel"
        nameFilters: ["Excel 工作簿 (*.xlsx)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "xlsx"
        onAccepted: window.appController.exportXlsx(
                        selectedFile, window.pendingExportScope,
                        window.exportZipCompression)
    }

    FileDialog {
        id: exportMatDialog
        title: "导出 MAT"
        nameFilters: ["MAT 文件 (*.mat)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mat"
        onAccepted: window.appController.exportMat(selectedFile, window.pendingExportScope)
    }

    property bool exportImageAll: false
    property bool imageCaptureAll: false
    property url imageExportUrl: ""
    FileDialog {
        id: exportImageDialog
        objectName: "exportImageDialog"
        title: "导出 PNG（2 倍分辨率）"
        nameFilters: ["PNG 图片 (*.png)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        onAccepted: {
            window.imageExportUrl = selectedFile
            objectManager.close()
            window.imageCaptureAll = window.exportImageAll
            imageCaptureTimer.restart()
        }
    }
    Timer {
        id: imageCaptureTimer
        interval: 60
        onTriggered: {
            const target = window.exportImageAll ? plotGrid
                : plotRepeater.itemAt(window.appController.activePlotIndex)
            if (!window.appController.exportPlotImage(target, window.imageExportUrl, 2)) {
                window.imageCaptureAll = false
                sessionDialogs.showError("无法导出当前绘图区，请确认视图可见、没有其他加载或导出任务。")
            }
        }
    }

    Dialog {
        id: layoutDialog
        title: "自定义子图布局"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            layoutRows.value = window.appController.plotRows
            layoutColumns.value = window.appController.plotColumns
        }
        onAccepted: window.appController.setLayout(layoutRows.value, layoutColumns.value)
        GridLayout {
            columns: 2
            Label { text: "行数" }
            SpinBox { id: layoutRows; from: 1; to: 8; editable: true }
            Label { text: "列数" }
            SpinBox { id: layoutColumns; from: 1; to: 8; editable: true }
        }
    }

    DropArea {
        anchors.fill: parent
        z: 1000
        function droppedFileUrls(drop) {
            if (drop.hasUrls && drop.urls.length > 0)
                return drop.urls
            const urls = []
            if (drop.hasText) {
                const lines = drop.text.split(/\r?\n/)
                for (let index = 0; index < lines.length; ++index) {
                    const value = lines[index].trim()
                    if (value.indexOf("file:") === 0) urls.push(value)
                }
            }
            return urls
        }
        onEntered: function(drag) {
            if (drag.hasUrls || drag.hasText) drag.acceptProposedAction()
        }
        onDropped: function(drop) {
            const urls = droppedFileUrls(drop)
            if (urls.length > 0 && window.appController.loadFiles(urls) > 0)
                drop.acceptProposedAction()
        }
    }

    // Fit scope: plain shortcut = current subplot, +Shift = every subplot.
    Shortcut { sequence: "Ctrl+Alt+F"; onActivated: window.appController.fitPlots(true, true, false) }
    Shortcut { sequence: "Ctrl+Alt+Shift+F"; onActivated: window.appController.fitPlots(true, true, true) }
    Shortcut { sequence: "Ctrl+Alt+T"; onActivated: window.appController.fitPlots(true, false, false) }
    Shortcut { sequence: "Ctrl+Alt+Shift+T"; onActivated: window.appController.fitPlots(true, false, true) }
    Shortcut { sequence: "Ctrl+Alt+Y"; onActivated: window.appController.fitPlots(false, true, false) }
    Shortcut { sequence: "Ctrl+Alt+Shift+Y"; onActivated: window.appController.fitPlots(false, true, true) }
    Connections {
        target: window.appController
        function onLayoutChanged() { window.cancelSignalTreeDrag(); window.subplotMaximized = false }
        function onImageExportFinished(success, message) {
            window.imageCaptureAll = false
            if (!success) { sessionDialogs.showError(message) }
        }
        function onSessionRestored(cursorMode) {
            signalBrowser.clearSearch()
            window.selectedCursorMode = cursorMode
            if (cursorMode !== 0) window.preferredCursorMode = cursorMode
            window.subplotMaximized = window.appController.soloPlotIndex >= 0
        }
        function onSessionError(message) {
            sessionDialogs.showError(message)
        }
        function onRevealSignalRequested(row) {
            signalBrowser.clearSearch()
            signalBrowser.revealSignal(row)
        }
    }

    property bool subplotMaximized: false
    // Mirror the maximize state into the controller so X/Y fitting only
    // considers the subplot the user can actually see.
    onSubplotMaximizedChanged: window.appController.setSoloPlot(
                                   window.subplotMaximized ? window.appController.activePlotIndex : -1)
    Connections {
        target: window.appController
        enabled: window.subplotMaximized
        function onActivePlotChanged() { window.appController.setSoloPlot(window.appController.activePlotIndex) }
    }
    property int visibilityBeforeFullscreen: Window.Windowed
    function toggleFullscreen() {
        if (window.appController.restoringSession) return
        if (visibility === Window.FullScreen)
            visibility = visibilityBeforeFullscreen
        else {
            visibilityBeforeFullscreen = visibility === Window.Maximized ? Window.Maximized : Window.Windowed
            showFullScreen()
        }
    }
    Shortcut { sequence: "F11"; onActivated: window.toggleFullscreen() }
    Shortcut {
        sequence: "Escape"
        enabled: !window.appController.restoringSession
                 && (window.signalTreeDragging || window.visibility === Window.FullScreen || window.subplotMaximized)
        onActivated: {
            if (window.signalTreeDragging) window.cancelSignalTreeDrag()
            else if (window.visibility === Window.FullScreen) window.toggleFullscreen()
            else window.subplotMaximized = false
        }
    }
    property int selectedCursorMode: 0
    property int preferredCursorMode: 1
    property bool zoomToolEnabled: false
    property int selectedZoomMode: 1
    readonly property int activeZoomMode: zoomToolEnabled ? selectedZoomMode : 0
    function toggleCursorTool() {
        if (window.appController.restoringSession) return
        const mode = selectedCursorMode === 0 ? preferredCursorMode : 0
        window.appController.setCursorMode(mode)
        selectedCursorMode = mode
    }
    function selectCursorMode(mode) {
        if (window.appController.restoringSession) return
        preferredCursorMode = mode
        window.appController.setCursorMode(mode)
        selectedCursorMode = mode
    }
    function toggleZoomTool() {
        zoomToolEnabled = !zoomToolEnabled
    }
    function selectZoomMode(mode) {
        selectedZoomMode = mode
        zoomToolEnabled = true
    }
    function zoomCurrent(axis, steps) {
        if (window.appController.restoringSession) return
        const plot = (plotRepeater.itemAt(window.appController.activePlotIndex) as QuickPlot)
        if (!plot) return
        window.appController.beginViewChange()
        if (axis !== 1) plot.renderer.zoomAxis(0, 0.5, steps)
        if (axis !== 0) plot.renderer.zoomAxis(1, 0.5, steps)
        window.appController.endViewChange()
    }
    Shortcut { sequence: "Ctrl+I"; onActivated: window.toggleCursorTool() }
    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: window.appController.canUndoView
                 && !(window.activeFocusItem instanceof TextInput)
                 && !(window.activeFocusItem instanceof TextEdit)
        onActivated: window.appController.undoView()
    }
    Shortcut { sequence: "Ctrl++"; onActivated: window.zoomCurrent(2, 1) }
    Shortcut { sequence: "Ctrl+-"; onActivated: window.zoomCurrent(2, -1) }
    Shortcut { sequence: "Ctrl+Shift+T"; onActivated: window.zoomCurrent(0, 1) }
    Shortcut { sequence: "Ctrl+Shift+Y"; onActivated: window.zoomCurrent(1, 1) }

    component IconTool: ToolButton {
        property string hint: ""
        property bool menuArrow: true
        display: AbstractButton.TextBesideIcon
        text: menuArrow ? "▾" : ""
        icon.width: 24; icon.height: 24
        icon.color: checked ? window.accentColor : window.treeTextColor
        implicitHeight: 40
        implicitWidth: menuArrow ? 54 : 40
        Accessible.name: hint
        ToolTip.visible: hovered && !window.signalTreeDragging
        ToolTip.delay: 400
        ToolTip.text: hint
    }
    component SplitIconTool: Item {
        id: splitTool
        property url iconSource
        property string hint: ""
        property bool checked: false
        signal primaryClicked()
        signal arrowClicked()
        implicitWidth: 58
        implicitHeight: 40

        Row {
            anchors.fill: parent
            spacing: 0
            ToolButton {
                width: 40
                height: parent.height
                icon.source: splitTool.iconSource
                icon.width: 24
                icon.height: 24
                icon.color: splitTool.checked ? window.accentColor : window.treeTextColor
                highlighted: splitTool.checked
                onClicked: splitTool.primaryClicked()
                Accessible.name: splitTool.hint
                ToolTip.visible: hovered && !window.signalTreeDragging
                ToolTip.delay: 400
                ToolTip.text: splitTool.hint
            }
            ToolButton {
                width: 18
                height: parent.height
                text: "▾"
                font.pixelSize: 10
                onClicked: splitTool.arrowClicked()
                Accessible.name: "选择" + splitTool.hint + "模式"
                ToolTip.visible: hovered && !window.signalTreeDragging
                ToolTip.delay: 400
                ToolTip.text: "选择" + splitTool.hint + "模式"
            }
        }
    }
    header: ToolBar {
        enabled: !window.appController.restoringSession && !window.appController.imageExporting
        height: 48
        background: Rectangle { color: window.panelColor; border.color: window.borderColor }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 8; spacing: 3
            Label { text: "DataInspector"; font.pixelSize: 17; font.weight: Font.DemiBold; color: window.accentColor }
            ToolButton {
                text: "打开…"
                enabled: !window.appController.loading && !window.appController.exporting
                onClicked: fileDialog.open()
            }
            ToolButton {
                objectName: "sessionMenuButton"
                text: "会话 ▾"
                enabled: !window.appController.loading && !window.appController.exporting
                onClicked: sessionMenu.popup()
                Menu {
                    id: sessionMenu
                    MenuItem { text: "保存会话…  Ctrl+S"; onTriggered: sessionDialogs.saveSession() }
                    MenuItem { text: "恢复会话…  Ctrl+Shift+O"; onTriggered: sessionDialogs.openSession() }
                    MenuSeparator {}
                    MenuItem { objectName: "saveViewTemplateMenu"; text: "保存为视图模板…"; enabled: window.appController.signalCount > 0; onTriggered: sessionDialogs.saveTemplate() }
                    MenuItem { objectName: "applyViewTemplateMenu"; text: "应用视图模板…"; enabled: window.appController.signalCount > 0; onTriggered: sessionDialogs.openTemplate() }
                    MenuItem { text: "撤销上次模板应用"; enabled: window.appController.canUndoViewTemplate; onTriggered: window.appController.undoViewTemplate() }
                }
            }
            IconTool {
                icon.source: "qrc:/icons/download.svg"
                hint: "导出数据"
                enabled: window.appController.signalCount > 0
                         && !window.appController.loading && !window.appController.exporting
                onClicked: exportMenu.popup()
                Menu {
                    id: exportMenu
                    MenuItem {
                        text: "PNG · 当前子图"
                        onTriggered: { window.exportImageAll = false; exportImageDialog.open() }
                    }
                    MenuItem {
                        text: "PNG · 全部子图"
                        onTriggered: { window.exportImageAll = true; exportImageDialog.open() }
                    }
                    MenuSeparator { }
                    MenuItem {
                        text: "Excel · 全部已加载数据"
                        onTriggered: {
                            window.pendingExportScope = 0
                            exportDialog.open()
                        }
                    }
                    MenuItem {
                        text: "Excel · 当前所有子图已绘制的信号"
                        onTriggered: {
                            window.pendingExportScope = 1
                            exportDialog.open()
                        }
                    }
                    MenuSeparator { visible: window.appController.matExportSupported }
                    MenuItem {
                        text: "MAT · 全部已加载数据"
                        visible: window.appController.matExportSupported
                        onTriggered: {
                            window.pendingExportScope = 0
                            exportMatDialog.open()
                        }
                    }
                    MenuItem {
                        text: "MAT · 当前所有子图已绘制的信号"
                        visible: window.appController.matExportSupported
                        onTriggered: {
                            window.pendingExportScope = 1
                            exportMatDialog.open()
                        }
                    }
                    MenuSeparator { }
                    MenuItem {
                        text: "ZIP 压缩（Excel）"
                        checkable: true
                        checked: window.exportZipCompression
                        onTriggered: window.exportZipCompression = checked
                    }
                }
            }
            Item { Layout.fillWidth: true }
            IconTool {
                id: layoutTool
                objectName: "layoutTool"
                menuArrow: false
                icon.source: "qrc:/icons/grid.svg"
                hint: "子图布局"
                onClicked: layoutPopup.open()
                Popup {
                    id: layoutPopup
                    y: parent.height
                    x: Math.min(0, window.width - layoutTool.mapToItem(window.contentItem, 0, 0).x - width - 8)
                    width: 290
                    height: 142
                    padding: 14
                    ColumnLayout {
                        anchors.fill: parent
                        Label { text: "基本布局"; font.bold: true }
                        RowLayout {
                            Repeater {
                                model: [ {r: 1, c: 1, svg: "layout-single"}, {r: 2, c: 1, svg: "layout-rows"}, {r: 1, c: 2, svg: "layout-columns"}, {r: 2, c: 2, svg: "grid"} ]
                                delegate: IconTool {
                                    required property var modelData
                                    menuArrow: false
                                    Layout.preferredWidth: 58
                                    icon.width: 38; icon.height: 38
                                    icon.source: "qrc:/icons/" + modelData.svg + ".svg"
                                    hint: modelData.r + " 行 × " + modelData.c + " 列"
                                    checked: window.appController.plotRows === modelData.r && window.appController.plotColumns === modelData.c
                                    onClicked: { window.appController.setLayout(modelData.r, modelData.c); layoutPopup.close() }
                                }
                            }
                        }
                        Button { text: "自定义行列…"; Layout.fillWidth: true; onClicked: { layoutPopup.close(); layoutDialog.open() } }
                    }
                }
            }
            ToolSeparator { }
            SplitIconTool {
                objectName: "cursorSplitTool"
                iconSource: (window.selectedCursorMode === 2 ||
                             (window.selectedCursorMode === 0 && window.preferredCursorMode === 2))
                            ? "qrc:/icons/cursor_2.svg" : "qrc:/icons/cursor_1.svg"
                checked: window.selectedCursorMode !== 0
                hint: "游标"
                onPrimaryClicked: window.toggleCursorTool()
                onArrowClicked: cursorMenu.popup()
                Menu {
                    id: cursorMenu
                    ButtonGroup { id: cursorModeGroup }
                    MenuItem { objectName: "singleCursorOption"; text: "单游标"; icon.source: "qrc:/icons/cursor_1.svg"; checkable: true; ButtonGroup.group: cursorModeGroup; checked: window.preferredCursorMode === 1; onTriggered: window.selectCursorMode(1) }
                    MenuItem { objectName: "doubleCursorOption"; text: "双游标"; icon.source: "qrc:/icons/cursor_2.svg"; checkable: true; ButtonGroup.group: cursorModeGroup; checked: window.preferredCursorMode === 2; onTriggered: window.selectCursorMode(2) }
                }
            }
            SplitIconTool {
                objectName: "zoomSplitTool"
                iconSource: window.selectedZoomMode === 1 ? "qrc:/icons/zoom-in.svg"
                            : window.selectedZoomMode === 2 ? "qrc:/icons/zoom-x.svg"
                            : "qrc:/icons/zoom-y.svg"
                checked: window.zoomToolEnabled
                hint: "缩放"
                onPrimaryClicked: window.toggleZoomTool()
                onArrowClicked: zoomMenu.popup()
                Menu {
                    id: zoomMenu
                    MenuItem { text: "区域缩放"; icon.source: "qrc:/icons/zoom-in.svg"; checkable: true; checked: window.selectedZoomMode === 1; onTriggered: window.selectZoomMode(1) }
                    MenuItem { text: "X 轴缩放"; icon.source: "qrc:/icons/zoom-x.svg"; checkable: true; checked: window.selectedZoomMode === 2; onTriggered: window.selectZoomMode(2) }
                    MenuItem { text: "Y 轴缩放"; icon.source: "qrc:/icons/zoom-y.svg"; checkable: true; checked: window.selectedZoomMode === 3; onTriggered: window.selectZoomMode(3) }
                }
            }
            IconTool {
                icon.source: "qrc:/icons/fit-view.svg"
                hint: "自适应视图"
                onClicked: fitMenu.popup()
                Menu {
                    id: fitMenu
                    MenuItem { text: "自适应当前视图    Ctrl+Alt+F"; icon.source: "qrc:/icons/fit-view.svg"; onTriggered: window.appController.fitPlots(true, true, false) }
                    MenuItem { text: "自适应全部视图    Ctrl+Alt+Shift+F"; icon.source: "qrc:/icons/fit-view.svg"; onTriggered: window.appController.fitPlots(true, true, true) }
                    MenuSeparator { }
                    MenuItem { text: "自适应当前时间轴    Ctrl+Alt+T"; icon.source: "qrc:/icons/arrows_left_right.svg"; onTriggered: window.appController.fitPlots(true, false, false) }
                    MenuItem { text: "自适应全部时间轴    Ctrl+Alt+Shift+T"; icon.source: "qrc:/icons/arrows_left_right.svg"; onTriggered: window.appController.fitPlots(true, false, true) }
                    MenuSeparator { }
                    MenuItem { text: "自适应当前 Y 轴    Ctrl+Alt+Y"; icon.source: "qrc:/icons/arrows_up_down.svg"; onTriggered: window.appController.fitPlots(false, true, false) }
                    MenuItem { text: "自适应全部 Y 轴    Ctrl+Alt+Shift+Y"; icon.source: "qrc:/icons/arrows_up_down.svg"; onTriggered: window.appController.fitPlots(false, true, true) }
                }
            }
            ToolSeparator { }
            IconTool {
                menuArrow: false
                icon.source: window.subplotMaximized ? "qrc:/icons/arrows-angle-contract.svg" : "qrc:/icons/arrows-angle-expand.svg"
                hint: window.subplotMaximized ? "恢复子图平铺（Esc）" : "最大化当前选中子图"
                checked: window.subplotMaximized
                enabled: window.appController.plotRows * window.appController.plotColumns > 1
                onClicked: window.subplotMaximized = !window.subplotMaximized
            }
            IconTool {
                menuArrow: false
                icon.source: window.visibility === Window.FullScreen ? "qrc:/icons/fullscreen-exit.svg" : "qrc:/icons/fullscreen.svg"
                hint: window.visibility === Window.FullScreen ? "退出全屏（F11 / Esc）" : "程序全屏（F11）"
                checked: window.visibility === Window.FullScreen
                onClicked: window.toggleFullscreen()
            }
            IconTool {
                objectName: "settingsTool"
                icon.source: "qrc:/icons/settings.svg"
                hint: "设置"
                onClicked: settingsMenu.popup()
                Menu {
                    id: settingsMenu
                    MenuItem { text: "显示信号树"; checkable: true; checked: window.signalTreeVisible; onTriggered: window.signalTreeVisible = !window.signalTreeVisible }
                    MenuItem { text: "深色主题"; checkable: true; checked: window.darkTheme; onTriggered: window.darkTheme = !window.darkTheme }
                    MenuItem { text: "清除所有信号"; icon.source: "qrc:/icons/clear.svg"; onTriggered: window.appController.clearAllPlotSignals() }
                    MenuSeparator { }
                    MenuItem { objectName: "aboutMenuItem"; text: "关于"; onTriggered: aboutDialog.open() }
                }
            }
        }
    }

    SplitView {
        enabled: !window.appController.restoringSession
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: Rectangle {
            implicitWidth: 1
            color: SplitHandle.pressed ? window.accentColor : SplitHandle.hovered ? "#a9cbed" : window.borderColor
            HoverHandler { cursorShape: Qt.SplitHCursor }
        }
        SignalBrowser {
            id: signalBrowser
            visible: window.signalTreeVisible
            appController: window.appController
            objectEditor: objectManager
            panelColor: window.panelColor
            borderColor: window.borderColor
            accentColor: window.accentColor
            treeTextColor: window.treeTextColor
            treeHeaderColor: window.treeHeaderColor
            treeHeaderTextColor: window.treeHeaderTextColor
            treeHeaderHeight: window.treeHeaderHeight
            darkTheme: window.darkTheme
            hostWidth: window.width
            signalTreeDragging: window.signalTreeDragging
            signalDragRevision: window.signalDragRevision
            onDragCancelRequested: window.cancelSignalTreeDrag()
            onDragFinishRequested: window.finishSignalTreeDrag()
            onDragUpdateRequested: (source, x, y, rowItem, pressPoint) => window.updateSignalTreeDrag(source, x, y, rowItem, pressPoint)
            onEditSignalRequested: row => window.editSignalPen(row)
            onRemoveFileRequested: group => window.requestRemoveFile(group)
            onHideRequested: window.signalTreeVisible = false
            onContextRequested: (kind, row, group) => {
                if (kind === 2) { fileContextMenu.fileName = group; fileContextMenu.popup() }
                else if (kind === 1) { groupContextMenu.groupName = group; groupContextMenu.popup() }
                else { signalContextMenu.signalIndex = row; signalContextMenu.popup() }
            }
        }
        Rectangle { id: plotPanel; SplitView.fillWidth: true; SplitView.minimumWidth: 280; color: window.darkTheme ? "#14181d" : "#ffffff"; clip: true
            ObjectManager { id: objectManager; anchors.fill: parent; appController: window.appController; darkTheme: window.darkTheme }
            // Shared Y gutter: every visible subplot uses the widest measured
            // tick-label width so their X axes line up column by column.
            property real sharedAxisLeft: 0
            function updateSharedAxisLeft() {
                let widest = 0
                for (let i = 0; i < plotRepeater.count; ++i) {
                    const item = (plotRepeater.itemAt(i) as QuickPlot)
                    if (item && item.visible) widest = Math.max(widest, item.measuredAxisLeft)
                }
                if (widest !== sharedAxisLeft) sharedAxisLeft = widest
            }
            GridLayout { id: plotGrid; objectName: "plotGrid"; visible: !objectManager.visible; enabled: !window.appController.imageExporting; anchors.fill: parent; rows: window.subplotMaximized && !window.imageCaptureAll ? 1 : window.appController.plotRows; columns: window.subplotMaximized && !window.imageCaptureAll ? 1 : window.appController.plotColumns; columnSpacing: 0; rowSpacing: 0; uniformCellWidths: true; uniformCellHeights: true
                Repeater { id: plotRepeater; model: window.appController.plotRows * window.appController.plotColumns
                    delegate: QuickPlot {
                        required property int index
                        plotIndex: index
                        visible: window.imageCaptureAll || !window.subplotMaximized || index === window.appController.activePlotIndex
                        controller: window.appController
                        onEditObjectRequested: objectId => { if (objectId.length > 0) objectManager.selectedId = objectId; objectManager.open() }
                        sharedAxisLeft: plotPanel.sharedAxisLeft
                        onMeasuredAxisLeftChanged: plotPanel.updateSharedAxisLeft()
                        onVisibleChanged: plotPanel.updateSharedAxisLeft()
                        Component.onCompleted: plotPanel.updateSharedAxisLeft()
                        Component.onDestruction: Qt.callLater(plotPanel.updateSharedAxisLeft)
                        graphLineWidth: 2
                        graphCursorMode: window.selectedCursorMode
                        graphZoomMode: window.activeZoomMode
                        darkTheme: window.darkTheme
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: 0
                        Layout.minimumHeight: 0
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 1
                    }
                }
            }
            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.topMargin: 0
                spacing: 4
                visible: window.appController.loading || window.appController.exporting
                z: 20
                Basic.ProgressBar {
                    id: transferProgress
                    objectName: "transferProgress"
                    Layout.fillWidth: true
                    readonly property color progressColor: window.appController.exporting
                                                           ? "#e58a24" : window.accentColor
                    background: Rectangle {
                        implicitHeight: 8
                        radius: 4
                        color: window.darkTheme ? "#35414d" : "#dce5ed"
                    }
                    contentItem: Item {
                        implicitHeight: 8
                        Rectangle {
                            width: parent.width * transferProgress.visualPosition
                            height: parent.height
                            radius: 4
                            color: transferProgress.progressColor
                        }
                    }
                    from: 0
                    to: 100
                    value: window.appController.exporting
                           ? window.appController.exportProgress
                           : window.appController.loadingProgress
                }
                ToolButton {
                    visible: window.appController.exporting
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    text: "×"
                    onClicked: window.appController.cancelExport()
                    ToolTip.visible: hovered && !window.signalTreeDragging
                    ToolTip.text: "取消导出"
                }
            }
        }
    }
}
