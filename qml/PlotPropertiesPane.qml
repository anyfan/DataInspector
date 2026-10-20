pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DataInspector

Pane {
    id: editor
    objectName: "plotPropertiesPane"
    required property AppController controller
    required property bool darkTheme
    property int plotIndex: -1
    property string errorText: ""
    property bool targetAvailable: false
    property bool normalizedEdited: false
    property bool blendEdited: false
    readonly property bool blocked: controller.loading || controller.exporting || controller.restoringSession
    visible: false
    palette.text: darkTheme ? "#e6edf3" : "#202830"
    palette.windowText: palette.text
    palette.buttonText: palette.text
    palette.base: darkTheme ? "#171c22" : "#ffffff"
    background: Rectangle { color: editor.darkTheme ? "#20252b" : "#f7f9fb" }
    function close(): void { visible = false }
    function editPlot(index: int): void {
        if (blocked || controller.plotAxisProperties(index).xMinimum === undefined) return
        plotIndex = index
        controller.setActivePlot(index)
        refresh()
        visible = true
    }
    function refresh(): void {
        rangeUpdate.stop()
        syncRange(true)
        errorText = ""
    }
    function scheduleSync(): void {
        if (visible && !blocked) rangeUpdate.restart()
    }
    onVisibleChanged: if (!visible) rangeUpdate.stop()
    onBlockedChanged: if (!blocked) scheduleSync()
    function syncRange(force = false): void {
        if (plotIndex >= controller.plotRows * controller.plotColumns) {
            plotIndex = controller.activePlotIndex
            force = true
        }
        const state = controller.plotAxisProperties(plotIndex)
        targetAvailable = state.xMinimum !== undefined
        if (!targetAvailable) return
        const fields = [xMinimum, xMaximum, yMinimum, yMaximum]
        const values = [state.xMinimum, state.xMaximum, state.yMinimum, state.yMaximum]
        for (let i = 0; i < fields.length; ++i) {
            if (force || (!fields[i].modified && !fields[i].activeFocus)) {
                fields[i].text = String(values[i])
                fields[i].modified = false
            }
        }
        if (force || !normalizedEdited) { normalized.checked = state.normalizeY; normalizedEdited = false }
        if (force || !blendEdited) { blendOverlaps.checked = state.blendOverlaps; blendEdited = false }
    }
    function apply(): void {
        if (blocked || !targetAvailable) return
        const fields = [xMinimum, xMaximum, yMinimum, yMaximum]
        for (const field of fields) {
            if (!/^[+-]?(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$/.test(field.text.trim())) {
                errorText = "请输入有效数值，可使用科学计数法"
                return
            }
        }
        const config = {xMinimum:Number(xMinimum.text), xMaximum:Number(xMaximum.text),
            yMinimum:Number(yMinimum.text), yMaximum:Number(yMaximum.text), normalizeY:normalized.checked, blendOverlaps:blendOverlaps.checked}
        if (!controller.configurePlotAxes(plotIndex, config)) { errorText = controller.status; return }
        refresh()
    }
    Connections {
        target: editor.controller
        function onLayoutChanged() {
            if (editor.visible) Qt.callLater(function() { editor.syncRange() })
        }
        function onCurrentFileChanged() { editor.scheduleSync() }
        function onPlotBindingsChanged() { editor.scheduleSync() }
        function onPlotAxisPropertiesChanged(index: int) {
            if (index === editor.plotIndex) editor.scheduleSync()
        }
    }
    Timer { id: rangeUpdate; interval: 500; repeat: false; onTriggered: if (editor.visible && !editor.blocked) editor.syncRange() }
    component RangeField: TextField {
        property bool modified: false
        Layout.fillWidth: true; Layout.minimumWidth: 0
        selectByMouse: true; maximumLength: 64
        onTextEdited: modified = true
        onActiveFocusChanged: if (!activeFocus && !modified) editor.scheduleSync()
    }
    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Label { text: "子图属性 · " + (editor.plotIndex + 1); font.bold: true; Layout.fillWidth: true }
            ToolButton { text: "×"; onClicked: editor.close() }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: parent.width
                spacing: 8
                Label { text: "X 轴范围（时间）"; font.bold: true }
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Label { text: "最小值" }
                        RangeField { id: xMinimum; objectName: "plotXMinimum" }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Label { text: "最大值" }
                        RangeField { id: xMaximum; objectName: "plotXMaximum" }
                    }
                }
                Label { text: "X 轴与所有时间子图联动。"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Label { text: "Y 轴范围"; font.bold: true }
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Label { text: "最小值" }
                        RangeField { id: yMinimum; objectName: "plotYMinimum" }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Label { text: "最大值" }
                        RangeField { id: yMaximum; objectName: "plotYMaximum" }
                    }
                }
                Label { text: "Y 轴范围只影响此子图。"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                CheckBox { id: normalized; objectName: "plotNormalizeY"; text: "归一化 Y 轴"; onToggled: editor.normalizedEdited = true }
                Label { text: "归一化仅改变绘制位置，游标仍显示原始值。范围填写归一化后的坐标。"; visible: normalized.checked; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                CheckBox { id: blendOverlaps; objectName: "plotBlendOverlaps"; text: "小幅值曲线优先置顶"; onToggled: editor.blendEdited = true }
                Label { text: "保留原色；关闭后按原始绘制顺序覆盖。"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Label { text: editor.errorText; visible: text.length > 0; color: "#df4652"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
        }
        RowLayout {
            Button { objectName: "applyPlotProperties"; text: "应用"; enabled: !editor.blocked && editor.targetAvailable; onClicked: editor.apply() }
            Button { text: "还原"; enabled: !editor.blocked && editor.targetAvailable; onClicked: editor.refresh() }
        }
    }
}
