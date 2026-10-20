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
    property int lastShownVisibility: Window.Windowed
    onVisibilityChanged: {
        if (window.visibility === Window.Windowed || window.visibility === Window.Maximized || window.visibility === Window.FullScreen)
            window.lastShownVisibility = window.visibility
    }
    function presentSessionPrompt(): void {
        if (visibility === Window.Minimized || visibility === Window.Hidden) {
            if (lastShownVisibility === Window.Maximized) showMaximized()
            else if (lastShownVisibility === Window.FullScreen) showFullScreen()
            else showNormal()
        }
        raise()
        requestActivate()
    }
    AboutDialog {
        id: aboutDialog
        parent: Overlay.overlay
        appController: window.appController
    }
    property alias pendingUnsavedAction: sessionDialogs.pendingUnsavedAction
    AutomationDialog {
        id: automationDialog
        parent: Overlay.overlay
        appController: window.appController
    }
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
        processingManager.dropRuleInput = ""
        ++signalDragRevision
        if (signalDragDestination) signalDragDestination.dropHighlighted = false
        signalDragDestination = null
        signalDragRow = -1
        signalTreeDragPreview.visible = false
    }
    function updateSignalTreeDrag(source, x, y, rowItem, pressPoint) {
        if (signalDragDestination) signalDragDestination.dropHighlighted = false
        processingManager.dropRuleInput = ""
        signalDragDestination = null
        signalDragRow = rowItem.signalIndex
        signalTreeDragPreview.signalName = rowItem.signalName
        signalTreeDragPreview.signalColor = rowItem.signalColor
        const point = source.mapToItem(window.contentItem, x, y)
        signalTreeDragPreview.x = point.x - pressPoint.x - 22
        signalTreeDragPreview.y = point.y - pressPoint.y - 6
        signalTreeDragPreview.visible = true
        if (processingManager.visible) {
            const position = source.mapToItem(processingManager, x, y)
            const slot = processingManager.ruleInputAt(position.x, position.y)
            processingManager.dropRuleInput = slot
            processingManager.dropRuleAllowed = processingManager.ruleSourceAllowed(signalDragRow)
            if (slot.length > 0) signalDragDestination = processingManager
            return
        }
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
        const ruleInput = processingManager.dropRuleInput
        cancelSignalTreeDrag()
        if (destination === objectManager) objectManager.dropSource(row, fieldId)
        else if (destination === processingManager) processingManager.bindRuleSource(row, ruleInput, false)
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
            text: "信号属性"
            enabled: !window.appController.loading && !window.appController.exporting
            onTriggered: window.editSignalPen(signalContextMenu.signalIndex)
        }
        MenuItem {
            text: "管理处理规则…"
            visible: window.appController.isDerivedSignal(signalContextMenu.signalIndex)
            enabled: !window.appController.loading && !window.appController.exporting
            onTriggered: processingManager.revealOutput(signalContextMenu.signalIndex)
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
        objectManager.close(); processingManager.close()
        propertiesPane.editTimeOffset(scope,row,group)
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
        signalBrowser.editSignalProperties(signalIndex)
    }

    function requestRemoveFile(fileName) {
        pendingFileRemoval = fileName
        removeFileDialog.open()
    }

    function requestRenameSignal(signalIndex) {
        if (signalIndex < 0) return
        window.editSignalPen(signalIndex)
        propertiesPane.focusSignalName()
    }

    SessionDialogs {
        id: sessionDialogs
        anchors.fill: parent
        appController: window.appController
        hostWidth: window.width
        hostHeight: window.height
        onAttentionRequested: {
            window.presentSessionPrompt()
            // Repeat after the close event and popup have finished processing.
            Qt.callLater(() => {
                if (sessionDialogs.pendingUnsavedAction === "close") window.presentSessionPrompt()
            })
        }
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
            processingManager.close()
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
                    MenuItem { objectName: "automationMenuItem"; text: "CLI 自动化…"; onTriggered: automationDialog.open() }
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
            processingEditor: processingManager
            trajectoryEditor: trajectoryProperties
            propertiesEditor: propertiesPane
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
            onShowRequested: window.signalTreeVisible = true
            onContextRequested: (kind, row, group) => {
                if (kind === 2) { fileContextMenu.fileName = group; fileContextMenu.popup() }
                else if (kind === 1) { groupContextMenu.groupName = group; groupContextMenu.popup() }
                else { signalContextMenu.signalIndex = row; signalContextMenu.popup() }
            }
        }
        Rectangle { id: plotPanel; SplitView.fillWidth: true; SplitView.minimumWidth: 280; color: window.darkTheme ? "#14181d" : "#ffffff"; clip: true
            ObjectManager {
                id: objectManager
                anchors.fill: parent
                appController: window.appController
                darkTheme: window.darkTheme
                onVisibleChanged: if (visible) { processingManager.close(); signalBrowser.showSection(2) }
            }
            ObjectManager {
                id: processingManager
                objectName: "processingManager"
                processingMode: true
                anchors.fill: parent
                appController: window.appController
                darkTheme: window.darkTheme
                onVisibleChanged: if (visible) { objectManager.close(); signalBrowser.showSection(1) }
            }
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
            GridLayout { id: plotGrid; objectName: "plotGrid"; visible: !objectManager.visible && !processingManager.visible; enabled: !window.appController.imageExporting; anchors.fill: parent; rows: window.subplotMaximized && !window.imageCaptureAll ? 1 : window.appController.plotRows; columns: window.subplotMaximized && !window.imageCaptureAll ? 1 : window.appController.plotColumns; columnSpacing: 0; rowSpacing: 0; uniformCellWidths: true; uniformCellHeights: true
                Repeater { id: plotRepeater; model: window.appController.plotRows * window.appController.plotColumns
                    delegate: QuickPlot {
                        required property int index
                        plotIndex: index
                        visible: window.imageCaptureAll || !window.subplotMaximized || index === window.appController.activePlotIndex
                        controller: window.appController
                        trajectoryEditor: trajectoryProperties
                        onTrajectoryConfigurationRequested: propertiesPane.close()
                        onEditObjectRequested: objectId => { if (objectId.length > 0) objectManager.selectedId = objectId; objectManager.open() }
                        sharedAxisLeft: plotPanel.sharedAxisLeft
                        onMeasuredAxisLeftChanged: plotPanel.updateSharedAxisLeft()
                        onVisibleChanged: plotPanel.updateSharedAxisLeft()
                        Component.onCompleted: plotPanel.updateSharedAxisLeft()
                        Component.onDestruction: Qt.callLater(plotPanel.updateSharedAxisLeft)
                        onStateConfigurationRequested: plot => signalBrowser.editStateRows(plot)
                        onSignalPropertiesRequested: row => window.editSignalPen(row)
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
        SignalPropertiesPane {
            id: propertiesPane
            onVisibleChanged: if (visible) trajectoryProperties.close()
            controller: window.appController
            darkTheme: window.darkTheme
            SplitView.preferredWidth: 320
            SplitView.minimumWidth: 260
            SplitView.maximumWidth: Math.max(260,window.width-560)
            onChooseStateSource: plot => { window.signalTreeVisible=true; signalBrowser.statePickTarget=plot; signalBrowser.showSection(0) }
            onExtractBits: row => { signalBrowser.showSection(1); processingManager.startBitExtraction(row) }
        }
        TrajectoryPropertiesPane {
            id: trajectoryProperties
            controller: window.appController
            darkTheme: window.darkTheme
            SplitView.preferredWidth: 320
            SplitView.minimumWidth: 260
            SplitView.maximumWidth: Math.max(260,window.width-560)
            onEditObjectRequested: objectId => { if(objectId.length>0) objectManager.selectedId=objectId; objectManager.open() }
        }

    }
}
