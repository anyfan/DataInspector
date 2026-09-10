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
    property bool darkTheme: false

    FileDialog {
        id: fileDialog
        title: "打开数据文件"
        nameFilters: ["数据文件 (*.csv *.txt *.mat)", "CSV/TXT 文件 (*.csv *.txt)", "MAT 文件 (*.mat)", "所有文件 (*)"]
        fileMode: FileDialog.OpenFile
        // Qt 6.8 exposes selectedFile as a URL value without the QObject
        // toLocalFile() helper in some QML runtimes. AppController accepts
        // both local paths and file: URLs and performs the conversion in C++.
        onAccepted: appController.loadCsv(selectedFile.toString())
    }

    header: ToolBar {
        RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
            Label { text: "DataInspector"; font.pixelSize: 18; font.weight: Font.DemiBold; color: accentColor }
            ToolSeparator { }
            ToolButton { text: "打开"; onClicked: fileDialog.open() }
            ToolButton { text: "适应"; onClicked: appController.selectSignal(-1) }
            ToolButton { text: "清空"; onClicked: appController.clear() }
            ToolButton { text: "主题"; onClicked: window.darkTheme = !window.darkTheme }
            Label { text: "线宽"; opacity: 0.65 }
            Slider { id: widthSlider; from: 1; to: 8; value: 2; stepSize: 0.5; Layout.preferredWidth: 100 }
            ComboBox { id: cursorModeSelector; model: ["关闭游标", "单游标", "双游标"]; currentIndex: 0 }
            ComboBox { id: legendModeSelector; model: ["顶部图例", "左上图例", "右上图例", "隐藏图例"]; currentIndex: appController.legendMode; onCurrentIndexChanged: appController.setLegendMode(currentIndex) }
            ToolButton { text: "1×1"; onClicked: appController.setLayout(1, 1) }
            ToolButton { text: "1×2"; onClicked: appController.setLayout(1, 2) }
            ToolButton { text: "2×2"; onClicked: appController.setLayout(2, 2) }
            Item { Layout.fillWidth: true }
            Label { text: "Qt Quick · Scene Graph"; opacity: 0.62 }
        }
    }

    RowLayout { anchors.fill: parent; anchors.margins: 10; spacing: 10
        Rectangle { Layout.preferredWidth: 280; Layout.fillHeight: true; color: panelColor; border.color: borderColor; radius: 5
            ColumnLayout { anchors.fill: parent; anchors.margins: 10; spacing: 8
                RowLayout { Layout.fillWidth: true
                    Label { text: "信号"; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Label { text: "已选 " + appController.signalModel.checkedCount + " / " + appController.signalModel.rowCount(); opacity: 0.58; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight }
                }
                Label { text: appController.currentFile.length > 0 ? appController.currentFile : "未加载文件"; elide: Text.ElideMiddle; Layout.fillWidth: true; opacity: 0.62 }
                RowLayout { Layout.fillWidth: true; spacing: 4
                    TextField { id: signalSearch; Layout.fillWidth: true; placeholderText: "搜索信号…"; onTextChanged: appController.filterSignals(text) }
                    ToolButton { text: "×"; enabled: signalSearch.text.length > 0; onClicked: signalSearch.clear(); ToolTip.visible: hovered; ToolTip.text: "清除搜索" }
                }
                RowLayout { Layout.fillWidth: true; spacing: 6
                    Button { text: "全选"; Layout.fillWidth: true; onClicked: appController.setAllSignalsChecked(true) }
                    Button { text: "清空选择"; Layout.fillWidth: true; onClicked: appController.setAllSignalsChecked(false) }
                }
                ListView { id: signalList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: appController.signalModel; section.property: "group"; section.criteria: ViewSection.FullString; section.delegate: Label { width: signalList.width; height: text.length ? 24 : 0; text: section; visible: text.length > 0; color: accentColor; font.pixelSize: 11; font.weight: Font.DemiBold; leftPadding: 6; verticalAlignment: Text.AlignVCenter }
                    delegate: ItemDelegate { width: signalList.width; highlighted: index === signalList.currentIndex; checkable: true; checked: model.checked; onClicked: { signalList.currentIndex = index; appController.toggleSignal(signalIndex) }
                        contentItem: RowLayout { spacing: 8
                            Rectangle { width: 10; height: 10; radius: 2; color: model.color; Layout.alignment: Qt.AlignVCenter }
                            Label { text: name; elide: Text.ElideRight; Layout.fillWidth: true; color: highlighted ? accentColor : palette.text }
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
                        graphLineWidth: widthSlider.value
                        graphCursorMode: cursorModeSelector.currentIndex
                        darkTheme: window.darkTheme
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }
            Label { anchors.left: parent.left; anchors.leftMargin: 10; anchors.top: parent.top; anchors.topMargin: 10; text: appController.status; elide: Text.ElideRight; width: parent.width - 20; opacity: 0.7 }
        }
    }
}
