import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DataInspector

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    visible: true
    title: appController.currentFile.length > 0 ? "DataInspector · " + appController.currentFile : "DataInspector"
    color: panelColor
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
    property bool darkTheme: false
    property int editingSignalIndex: -1
    property string pendingFileRemoval: ""
    property int pendingExportScope: 0
    property bool exportZipCompression: true

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
        onAccepted: appController.setSignalPen(
            window.editingSignalIndex, colorButton.selectedColor,
            widthSpin.value, lineStyleModel.get(styleCombo.currentIndex).value)

        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { text: window.editingSignalIndex >= 0 ? appController.signalName(window.editingSignalIndex) : ""; color: window.treeTextColor; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
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
                    model: appController.presetColors
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
            text: "移除文件 '" + fileContextMenu.fileName + "'"
            onTriggered: window.requestRemoveFile(fileContextMenu.fileName)
        }
    }

    Dialog {
        id: removeFileDialog
        title: "移除文件"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 380
        onAccepted: appController.removeFile(window.pendingFileRemoval)
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
        colorButton.selectedColor = appController.signalColor(signalIndex)
        widthSpin.value = Math.round(appController.signalWidth(signalIndex))
        const style = appController.signalStyle(signalIndex)
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

    FileDialog {
        id: fileDialog
        title: "打开数据文件"
        nameFilters: ["数据文件 (*.csv *.txt *.xlsx *.mat)", "CSV/TXT 文件 (*.csv *.txt)", "Excel 文件 (*.xlsx)", "MAT 文件 (*.mat)", "所有文件 (*)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: appController.loadFiles(selectedFiles)
    }

    FileDialog {
        id: exportDialog
        title: "导出 Excel"
        nameFilters: ["Excel 工作簿 (*.xlsx)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "xlsx"
        onAccepted: appController.exportXlsx(
                        selectedFile, window.pendingExportScope,
                        window.exportZipCompression)
    }

    Dialog {
        id: layoutDialog
        title: "自定义子图布局"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            layoutRows.value = appController.plotRows
            layoutColumns.value = appController.plotColumns
        }
        onAccepted: appController.setLayout(layoutRows.value, layoutColumns.value)
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
            if (urls.length > 0 && appController.loadFiles(urls) > 0)
                drop.acceptProposedAction()
        }
    }

    Shortcut { sequence: "Ctrl+Alt+T"; onActivated: appController.fitPlots(true, false) }
    Shortcut { sequence: "Ctrl+Alt+Y"; onActivated: appController.fitPlots(false, true) }
    Connections {
        target: appController
        function onLayoutChanged() { window.subplotMaximized = false }
        function onRevealSignalRequested(row) {
            signalSearch.clear()
            const modelRow = appController.signalModel.revealSignal(row)
            signalList.currentIndex = modelRow
            signalList.positionViewAtIndex(modelRow, ListView.Center)
        }
    }

    property bool subplotMaximized: false
    property int visibilityBeforeFullscreen: Window.Windowed
    function toggleFullscreen() {
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
        enabled: window.visibility === Window.FullScreen || window.subplotMaximized
        onActivated: {
            if (window.visibility === Window.FullScreen) window.toggleFullscreen()
            else window.subplotMaximized = false
        }
    }
    property int selectedCursorMode: 0
    property int preferredCursorMode: 1
    property bool zoomToolEnabled: false
    property int selectedZoomMode: 1
    readonly property int activeZoomMode: zoomToolEnabled ? selectedZoomMode : 0
    function toggleCursorTool() {
        selectedCursorMode = selectedCursorMode === 0 ? preferredCursorMode : 0
    }
    function selectCursorMode(mode) {
        preferredCursorMode = mode
        if (selectedCursorMode !== 0)
            selectedCursorMode = mode
    }
    function toggleZoomTool() {
        zoomToolEnabled = !zoomToolEnabled
    }
    function selectZoomMode(mode) {
        selectedZoomMode = mode
    }
    function zoomCurrent(axis, steps) {
        const plot = plotRepeater.itemAt(appController.activePlotIndex)
        if (!plot) return
        if (axis !== 1) plot.renderer.zoomAxis(0, 0.5, steps)
        if (axis !== 0) plot.renderer.zoomAxis(1, 0.5, steps)
    }
    Shortcut { sequence: "Ctrl+I"; onActivated: window.toggleCursorTool() }
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
        ToolTip.visible: hovered
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
                ToolTip.visible: hovered
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
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "选择" + splitTool.hint + "模式"
            }
        }
    }
    header: ToolBar {
        height: 48
        background: Rectangle { color: window.panelColor; border.color: window.borderColor }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 8; spacing: 3
            Label { text: "DataInspector"; font.pixelSize: 17; font.weight: Font.DemiBold; color: window.accentColor }
            ToolButton {
                text: "打开…"
                enabled: !appController.loading && !appController.exporting
                onClicked: fileDialog.open()
            }
            IconTool {
                icon.source: "qrc:/icons/download.svg"
                hint: "导出 Excel"
                enabled: appController.signalCount > 0
                         && !appController.loading && !appController.exporting
                onClicked: exportMenu.popup()
                Menu {
                    id: exportMenu
                    MenuItem {
                        text: "全部已加载数据"
                        onTriggered: {
                            window.pendingExportScope = 0
                            exportDialog.open()
                        }
                    }
                    MenuItem {
                        text: "当前所有子图已绘制的信号"
                        onTriggered: {
                            window.pendingExportScope = 1
                            exportDialog.open()
                        }
                    }
                    MenuSeparator { }
                    MenuItem {
                        text: "ZIP 压缩"
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
                                    checked: appController.plotRows === modelData.r && appController.plotColumns === modelData.c
                                    onClicked: { appController.setLayout(modelData.r, modelData.c); layoutPopup.close() }
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
                    MenuItem { text: "单游标"; icon.source: "qrc:/icons/cursor_1.svg"; checkable: true; checked: window.preferredCursorMode === 1; onTriggered: window.selectCursorMode(1) }
                    MenuItem { text: "双游标"; icon.source: "qrc:/icons/cursor_2.svg"; checkable: true; checked: window.preferredCursorMode === 2; onTriggered: window.selectCursorMode(2) }
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
                    MenuItem { text: "自适应视图"; icon.source: "qrc:/icons/fit-view.svg"; onTriggered: appController.fitPlots(true, true) }
                    MenuItem { text: "自适应时间轴    Ctrl+Alt+T"; icon.source: "qrc:/icons/arrows_left_right.svg"; onTriggered: appController.fitPlots(true, false) }
                    MenuItem { text: "自适应当前 Y 轴    Ctrl+Alt+Y"; icon.source: "qrc:/icons/arrows_up_down.svg"; onTriggered: appController.fitPlots(false, true) }
                    MenuItem { text: "自适应全部 Y 轴"; onTriggered: appController.fitPlots(false, true, true) }
                }
            }
            ToolSeparator { }
            IconTool {
                menuArrow: false
                icon.source: window.subplotMaximized ? "qrc:/icons/arrows-angle-contract.svg" : "qrc:/icons/arrows-angle-expand.svg"
                hint: window.subplotMaximized ? "恢复子图平铺（Esc）" : "最大化当前选中子图"
                checked: window.subplotMaximized
                enabled: appController.plotRows * appController.plotColumns > 1
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
                icon.source: "qrc:/icons/settings.svg"
                hint: "设置"
                onClicked: settingsMenu.popup()
                Menu {
                    id: settingsMenu
                    MenuItem { text: "深色主题"; checkable: true; checked: window.darkTheme; onTriggered: window.darkTheme = !window.darkTheme }
                    MenuItem { text: "清除所有信号"; icon.source: "qrc:/icons/clear.svg"; onTriggered: appController.clearAllPlotSignals() }
                }
            }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: Rectangle {
            implicitWidth: 6
            color: SplitHandle.pressed ? window.accentColor : SplitHandle.hovered ? "#a9cbed" : window.borderColor
            HoverHandler { cursorShape: Qt.SplitHCursor }
        }
        Rectangle {
            SplitView.preferredWidth: 280
            SplitView.minimumWidth: 180
            SplitView.maximumWidth: Math.max(180, window.width - 320)
            color: panelColor
            ColumnLayout { anchors.fill: parent; anchors.margins: 10; spacing: 8
                Label { text: "信号 · 子图 " + (appController.activePlotIndex + 1); color: treeTextColor; font.pixelSize: 15; font.weight: Font.DemiBold }
                Label { text: appController.currentFile.length > 0 ? appController.currentFile : "未加载文件"; color: treeTextColor; elide: Text.ElideMiddle; Layout.fillWidth: true; opacity: 0.78 }
                RowLayout { Layout.fillWidth: true; spacing: 4
                    TextField { id: signalSearch; Layout.fillWidth: true; placeholderText: "搜索信号…"; onTextChanged: appController.filterSignals(text) }
                    ToolButton { text: "×"; enabled: signalSearch.text.length > 0; onClicked: signalSearch.clear(); ToolTip.visible: hovered; ToolTip.text: "清除搜索" }
                }
                ListView { id: signalList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: appController.signalModel
                    delegate: Item {
                        id: signalDelegate
                        required property int index
                        required property string signalName
                        required property int signalIndex
                        required property bool signalChecked
                        required property color signalColor
                        required property string groupName
                        required property bool groupNode
                        required property bool fileNode
                        required property bool groupExpanded
                        required property int nodeDepth
                        required property real signalWidth
                        required property int signalLineStyle
                        property bool rowHovered: false
                        onSignalColorChanged: penPreview.requestPaint()
                        onSignalWidthChanged: penPreview.requestPaint()
                        onSignalLineStyleChanged: penPreview.requestPaint()
                        width: signalList.width
                        height: groupNode ? 28 : 30

                        Rectangle {
                            anchors.fill: parent
                            color: signalDelegate.index === signalList.currentIndex
                                   ? (window.darkTheme ? "#29333d" : "#d9eafa")
                                   : signalDelegate.rowHovered
                                     ? (window.darkTheme ? "#252d35" : "#eef4fa")
                                     : window.panelColor
                        }
                        HoverHandler { onHoveredChanged: signalDelegate.rowHovered = hovered }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: signalDelegate.nodeDepth * 14 + 4
                            anchors.rightMargin: 4
                            spacing: 6
                            ToolButton {
                                visible: signalDelegate.groupNode
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                text: signalDelegate.groupExpanded ? "▾" : "▸"
                                palette.button: window.panelColor
                                palette.buttonText: signalDelegate.groupNode
                                                    ? window.accentColor
                                                    : window.treeTextColor
                                onClicked: appController.signalModel.toggleGroup(signalDelegate.groupName)
                            }
                            CheckBox {
                                id: signalCheck
                                visible: !signalDelegate.groupNode
                                checked: signalDelegate.signalChecked
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                palette.window: window.panelColor
                                palette.base: window.panelColor
                                palette.text: window.treeTextColor
                                palette.buttonText: window.treeTextColor
                                palette.highlight: window.accentColor
                                palette.highlightedText: "#ffffff"
                                onClicked: {
                                    signalList.currentIndex = signalDelegate.index
                                    appController.toggleSignal(signalDelegate.signalIndex)
                                }
                            }
                            Label {
                                text: signalDelegate.signalName
                                font.weight: signalDelegate.groupNode ? Font.DemiBold : Font.Normal
                                color: signalDelegate.groupNode
                                       ? window.accentColor : window.treeTextColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        signalList.currentIndex = signalDelegate.index
                                        if (signalDelegate.groupNode)
                                            appController.signalModel.toggleGroup(signalDelegate.groupName)
                                    }
                                }
                            }
                            ToolButton {
                                visible: signalDelegate.fileNode
                                Layout.preferredWidth: 26
                                Layout.preferredHeight: 24
                                text: "×"
                                palette.button: window.panelColor
                                palette.buttonText: window.treeTextColor
                                onClicked: window.requestRemoveFile(
                                               signalDelegate.groupName)
                                ToolTip.visible: hovered
                                ToolTip.text: "移除文件"
                            }
                            Canvas {
                                id: penPreview
                                visible: !signalDelegate.groupNode
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 24
                                onPaint: {
                                    const context = getContext("2d")
                                    context.reset()
                                    context.strokeStyle = signalDelegate.signalColor
                                    context.lineWidth = signalDelegate.signalWidth
                                    if (signalDelegate.signalLineStyle === 2) context.setLineDash([8, 4])
                                    else if (signalDelegate.signalLineStyle === 3) context.setLineDash([2, 4])
                                    else if (signalDelegate.signalLineStyle === 4) context.setLineDash([8, 4, 2, 4])
                                    else if (signalDelegate.signalLineStyle === 5) context.setLineDash([8, 4, 2, 4, 2, 4])
                                    else context.setLineDash([])
                                    context.beginPath()
                                    context.moveTo(3, height / 2)
                                    context.lineTo(width - 3, height / 2)
                                    context.stroke()
                                }
                                onVisibleChanged: requestPaint()
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    onDoubleClicked: window.editSignalPen(signalDelegate.signalIndex)
                                    ToolTip.visible: containsMouse
                                    ToolTip.text: "双击编辑信号线属性"
                                    hoverEnabled: true
                                }
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            enabled: signalDelegate.fileNode
                            onTapped: {
                                signalList.currentIndex = signalDelegate.index
                                fileContextMenu.fileName = signalDelegate.groupName
                                fileContextMenu.popup()
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar { }
                }
                Label {
                    Layout.fillWidth: true
                    text: appController.status
                    color: window.treeTextColor
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    ToolTip.visible: statusHover.hovered
                    ToolTip.text: text
                    HoverHandler { id: statusHover }
                }
            }
        }
        Rectangle { id: plotPanel; SplitView.fillWidth: true; SplitView.minimumWidth: 280; color: darkTheme ? "#14181d" : "#ffffff"; clip: true
            // Shared Y gutter: every visible subplot uses the widest measured
            // tick-label width so their X axes line up column by column.
            property real sharedAxisLeft: 0
            function updateSharedAxisLeft() {
                let widest = 0
                for (let i = 0; i < plotRepeater.count; ++i) {
                    const item = plotRepeater.itemAt(i)
                    if (item && item.visible) widest = Math.max(widest, item.measuredAxisLeft)
                }
                if (widest !== sharedAxisLeft) sharedAxisLeft = widest
            }
            GridLayout { anchors.fill: parent; rows: window.subplotMaximized ? 1 : appController.plotRows; columns: window.subplotMaximized ? 1 : appController.plotColumns; columnSpacing: 0; rowSpacing: 0; uniformCellWidths: true; uniformCellHeights: true
                Repeater { id: plotRepeater; model: appController.plotRows * appController.plotColumns
                    delegate: QuickPlot {
                        required property int index
                        plotIndex: index
                        visible: !window.subplotMaximized || index === appController.activePlotIndex
                        controller: appController
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
                visible: appController.loading || appController.exporting
                z: 20
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: appController.exporting
                           ? appController.exportProgress
                           : appController.loadingProgress
                }
                ToolButton {
                    visible: appController.exporting
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    text: "×"
                    onClicked: appController.cancelExport()
                    ToolTip.visible: hovered
                    ToolTip.text: "取消导出"
                }
            }
        }
    }
}
