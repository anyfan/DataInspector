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
                TextField { id: signalSearch; Layout.fillWidth: true; placeholderText: "搜索信号…"; onTextChanged: appController.filterSignals(text) }
                RowLayout { Layout.fillWidth: true; spacing: 6
                    Button { text: "全选"; Layout.fillWidth: true; onClicked: appController.setAllSignalsChecked(true) }
                    Button { text: "清空选择"; Layout.fillWidth: true; onClicked: appController.setAllSignalsChecked(false) }
                }
                ListView { id: signalList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: appController.signalModel
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
                    delegate: Rectangle { property int plotIndex: index; color: darkTheme ? "#14181d" : "#ffffff"; border.color: borderColor; radius: 3; Layout.fillWidth: true; Layout.fillHeight: true
                        PlotItem { id: plotItem; anchors.fill: parent; anchors.topMargin: 28; anchors.bottomMargin: 22; lineWidth: widthSlider.value; cursorMode: cursorModeSelector.currentIndex; Component.onCompleted: appController.attachPlot(plotItem, plotIndex) }
                        Repeater { model: plotItem.xTicks; delegate: Rectangle { x: 42 + (modelData.value - plotItem.xMinimum) / Math.max(1e-12, plotItem.xMaximum - plotItem.xMinimum) * (parent.width - 50); y: 28; width: 1; height: parent.height - 50; color: borderColor; opacity: 0.18 } }
                        Repeater { model: plotItem.yTicks; delegate: Rectangle { x: 42; y: 28 + (1 - (modelData.value - plotItem.yMinimum) / Math.max(1e-12, plotItem.yMaximum - plotItem.yMinimum)) * (parent.height - 50); width: parent.width - 50; height: 1; color: borderColor; opacity: 0.18 } }
                        Row { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.leftMargin: 42; anchors.rightMargin: 8; anchors.bottomMargin: 2; spacing: 0
                            Repeater { model: plotItem.xTicks; delegate: Label { x: (modelData.value - plotItem.xMinimum) / Math.max(1e-12, plotItem.xMaximum - plotItem.xMinimum) * (parent.width - 50) - width / 2; width: 60; text: modelData.label; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 9; opacity: 0.58 } }
                        }
                        Column { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.topMargin: 28; anchors.bottomMargin: 22; width: 38; spacing: 0
                            Repeater { model: plotItem.yTicks; delegate: Label { y: (1 - (modelData.value - plotItem.yMinimum) / Math.max(1e-12, plotItem.yMaximum - plotItem.yMinimum)) * (parent.height - 50) - height / 2; height: 18; text: modelData.label; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; font.pixelSize: 9; opacity: 0.58 } }
                        }
                        Flow { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 4; spacing: 6
                            Repeater { model: appController.signalModel
                                delegate: Item {
                                    property int signalRow: index
                                    visible: appController.plotStateRevision >= 0 && appController.plotSignalEnabled(plotIndex, signalRow)
                                    implicitWidth: legendLabel.implicitWidth + 18; implicitHeight: 18
                                    Rectangle { width: 10; height: 10; anchors.verticalCenter: parent.verticalCenter; color: model.color; radius: 2 }
                                    Label { id: legendLabel; anchors.left: parent.left; anchors.leftMargin: 13; anchors.verticalCenter: parent.verticalCenter; text: model.name; font.pixelSize: 10 }
                                    MouseArea { anchors.fill: parent; onClicked: appController.togglePlotSignal(plotIndex, signalRow) }
                                }
                            }
                        }
                        Label { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 6; text: index + 1; opacity: 0.45 }
                        Repeater { model: plotItem.cursorReadouts; delegate: Label { x: 42 + (modelData.x - plotItem.xMinimum) / Math.max(1e-12, plotItem.xMaximum - plotItem.xMinimum) * (parent.width - 50) + 5; y: 28 + (1 - (modelData.y - plotItem.yMinimum) / Math.max(1e-12, plotItem.yMaximum - plotItem.yMinimum)) * (parent.height - 50) - height / 2; text: modelData.text; color: modelData.color; font.pixelSize: 10; z: 4 } }
                        Rectangle {
                            x: 42 + ((plotItem.cursorX1 + plotItem.cursorX2) * 0.5 - plotItem.xMinimum) / Math.max(1e-12, plotItem.xMaximum - plotItem.xMinimum) * (parent.width - 50) - width / 2
                            anchors.bottom: parent.bottom; anchors.bottomMargin: 2
                            visible: plotItem.cursorMode === 2
                            color: darkTheme ? "#26313d" : "#eef4fb"
                            radius: 3; border.color: borderColor
                            implicitWidth: cursorValue.implicitWidth + 14; implicitHeight: cursorValue.implicitHeight + 6
                            Label { id: cursorValue; anchors.centerIn: parent; text: "ΔT = " + Number(plotItem.cursorDeltaT).toPrecision(7); color: darkTheme ? "#e7edf5" : "#243447"; font.pixelSize: 11 }
                        }
                    }
                }
            }
            Label { anchors.left: parent.left; anchors.leftMargin: 10; anchors.top: parent.top; anchors.topMargin: 10; text: appController.status; elide: Text.ElideRight; width: parent.width - 20; opacity: 0.7 }
        }
    }
}
