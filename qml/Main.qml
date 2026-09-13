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
    color: "#edf1f5"
    property color panelColor: darkTheme ? "#20252b" : "#f7f9fb"
    property color borderColor: darkTheme ? "#3b4652" : "#d7dfe8"
    property color accentColor: "#0078d4"
    property color treeTextColor: darkTheme ? "#e6edf3" : "#202830"
    property bool darkTheme: false
    property int editingSignalIndex: -1
    property string pendingFileRemoval: ""

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
        nameFilters: ["数据文件 (*.csv *.txt *.mat)", "CSV/TXT 文件 (*.csv *.txt)", "MAT 文件 (*.mat)", "所有文件 (*)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: appController.loadFiles(selectedFiles)
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

    header: ToolBar {
        RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
            Label { text: "DataInspector"; font.pixelSize: 18; font.weight: Font.DemiBold; color: accentColor }
            ToolSeparator { }
            ToolButton { text: "打开"; onClicked: fileDialog.open() }
            ToolButton { text: "适应"; onClicked: appController.fitAllPlots() }
            ToolButton { text: "清空"; onClicked: appController.clear() }
            ToolButton { text: "主题"; onClicked: window.darkTheme = !window.darkTheme }
            ComboBox { id: cursorModeSelector; model: ["关闭游标", "单游标", "双游标"]; currentIndex: 0 }
            ComboBox { id: legendModeSelector; model: ["顶部图例", "左上图例", "右上图例", "隐藏图例"]; currentIndex: appController.legendMode; onCurrentIndexChanged: appController.setLegendMode(currentIndex) }
            ToolButton { text: "1×1"; onClicked: appController.setLayout(1, 1) }
            ToolButton { text: "1×2"; onClicked: appController.setLayout(1, 2) }
            ToolButton { text: "2×1"; onClicked: appController.setLayout(2, 1) }
            ToolButton { text: "2×2"; onClicked: appController.setLayout(2, 2) }
            ToolButton { text: "布局…"; onClicked: layoutDialog.open() }
            Item { Layout.fillWidth: true }
            Label { text: "Qt Quick · Scene Graph"; opacity: 0.62 }
        }
    }

    RowLayout { anchors.fill: parent; anchors.margins: 10; spacing: 10
        Rectangle { Layout.preferredWidth: 280; Layout.fillHeight: true; color: panelColor; border.color: borderColor; radius: 5
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
            }
        }
        Rectangle { id: plotPanel; Layout.fillWidth: true; Layout.fillHeight: true; color: darkTheme ? "#14181d" : "#ffffff"; border.color: borderColor; radius: 5; clip: true
            GridLayout { anchors.fill: parent; anchors.margins: 42; rows: appController.plotRows; columns: appController.plotColumns; columnSpacing: 10; rowSpacing: 10
                Repeater { model: appController.plotRows * appController.plotColumns
                    delegate: QuickPlot {
                        required property int index
                        plotIndex: index
                        controller: appController
                        graphLineWidth: 2
                        graphCursorMode: cursorModeSelector.currentIndex
                        darkTheme: window.darkTheme
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }
            Label { anchors.left: parent.left; anchors.leftMargin: 10; anchors.top: parent.top; anchors.topMargin: 10; text: appController.status; elide: Text.ElideRight; width: parent.width - 20; opacity: 0.7 }
            ProgressBar {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.topMargin: 30
                from: 0
                to: 100
                value: appController.loadingProgress
                visible: appController.loading
                z: 20
            }
        }
    }
}
