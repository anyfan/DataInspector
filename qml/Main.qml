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
        title: "打开时间序列文件"
        nameFilters: ["CSV/TXT 文件 (*.csv *.txt)", "所有文件 (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: appController.loadCsv(selectedFile.toLocalFile())
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
            CheckBox { id: cursorCheck; text: "游标"; checked: false }
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
                Label { text: "信号"; font.pixelSize: 15; font.weight: Font.DemiBold }
                Label { text: appController.currentFile.length > 0 ? appController.currentFile : "未加载文件"; elide: Text.ElideMiddle; Layout.fillWidth: true; opacity: 0.62 }
                ListView { id: signalList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: appController.signalModel
                    delegate: ItemDelegate { width: signalList.width; text: name; highlighted: index === signalList.currentIndex; checkable: true; checked: model.checked; onClicked: { signalList.currentIndex = index; appController.toggleSignal(index) } }
                    ScrollBar.vertical: ScrollBar { }
                }
            }
        }
        Rectangle { id: plotPanel; Layout.fillWidth: true; Layout.fillHeight: true; color: darkTheme ? "#14181d" : "#ffffff"; border.color: borderColor; radius: 5; clip: true
            GridLayout { anchors.fill: parent; anchors.margins: 42; rows: appController.plotRows; columns: appController.plotColumns; columnSpacing: 10; rowSpacing: 10
                Repeater { model: appController.plotRows * appController.plotColumns
                    delegate: Rectangle { color: darkTheme ? "#14181d" : "#ffffff"; border.color: borderColor; radius: 3; Layout.fillWidth: true; Layout.fillHeight: true
                        PlotItem { id: plotItem; anchors.fill: parent; anchors.margins: 4; lineWidth: widthSlider.value; cursorEnabled: cursorCheck.checked; Component.onCompleted: appController.attachPlot(plotItem, index) }
                        Label { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 6; text: index + 1; opacity: 0.45 }
                        Rectangle {
                            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 6
                            visible: cursorCheck.checked
                            color: darkTheme ? "#26313d" : "#eef4fb"
                            radius: 3; border.color: borderColor
                            implicitWidth: cursorValue.implicitWidth + 14; implicitHeight: cursorValue.implicitHeight + 6
                            Label { id: cursorValue; anchors.centerIn: parent; text: "X = " + Number(plotItem.cursorX).toPrecision(7); color: darkTheme ? "#e7edf5" : "#243447"; font.pixelSize: 11 }
                        }
                    }
                }
            }
            Label { anchors.left: parent.left; anchors.leftMargin: 10; anchors.top: parent.top; anchors.topMargin: 10; text: appController.status; elide: Text.ElideRight; width: parent.width - 20; opacity: 0.7 }
        }
    }
}
