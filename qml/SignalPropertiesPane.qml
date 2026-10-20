pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DataInspector

Pane {
    id: editor
    required property AppController controller
    required property bool darkTheme
    visible: false
    objectName: "signalPropertiesPane"
    property int sourceRow: -1
    property int plotIndex: -1
    property int backPlot: -1
    property string mode: "signal"
    property string errorText: ""
    property var drafts: ({})
    property var appliedDraft: null
    property bool appendedSources: false
    property var lanes: []
    property color lineColor: "#0072bd"
    property int colorRow: -1
    property string offsetText: "0"
    property int offsetScope: 0
    property string offsetGroup: ""
    property bool mixedOffsets: false
    readonly property bool offsetEditable: mode === "offset" || (sourceRow >= 0 && !controller.isDerivedSignal(sourceRow))
    readonly property bool offsetValid: /^[+-]?(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$/.test(offsetText.trim()) && isFinite(Number(offsetText))
    signal chooseStateSource(int plot)
    signal extractBits(int row)
    readonly property color foreground: darkTheme ? "#d7dee7" : "#303942"
    palette.text: editor.foreground
    palette.windowText: editor.foreground
    palette.buttonText: editor.foreground
    palette.base: darkTheme ? "#171c22" : "#ffffff"
    background: Rectangle { color: editor.darkTheme ? "#20252b" : "#f7f9fb" }
    ListModel { id: enumRows; objectName: "signalEnumRows" }
    function currentDraft() {
        const values=[]
        for(let i=0;i<enumRows.count;++i) values.push({code:enumRows.get(i).code,title:enumRows.get(i).title,shade:enumRows.get(i).shade})
        return {name:signalName.text,color:String(lineColor),width:lineWidth.value,style:lineStyle.currentIndex,values:values,offset:offsetText}
    }
    function saveDraft() {
        if (!visible || mode !== "signal" || sourceRow < 0) return
        const draft=currentDraft()
        if(JSON.stringify(draft) === JSON.stringify(appliedDraft)) delete drafts[sourceRow]
        else drafts[sourceRow]=draft
    }
    function editSignal(row, returnPlot = -1) {
        saveDraft()
        sourceRow=row; backPlot=returnPlot; mode="signal"; errorText=""; colorRow=-1
        appliedDraft={name:controller.signalName(row),color:String(controller.signalColor(row)),width:controller.signalWidth(row),style:controller.signalStyle(row)-1,
            values:controller.signalEnums(row).map(v=>({code:String(v.value),title:v.text,shade:v.color})),offset:String(controller.signalTimeOffset(row))}
        const draft=drafts[row] || appliedDraft
        signalName.text=draft.name; lineColor=draft.color; lineWidth.value=draft.width; lineStyle.currentIndex=draft.style; offsetText=draft.offset
        enumRows.clear()
        for(const value of draft.values) enumRows.append(value)
        visible=true
    }
    function editTimeOffset(scope,row,group) {
        if(scope === 0) { editSignal(row); return }
        saveDraft(); mode="offset"; offsetScope=scope; offsetGroup=group; errorText=""
        const current=controller.timeOffsetForScope(scope,row,group)
        mixedOffsets=current === undefined || current === null
        offsetText=mixedOffsets ? "" : String(current)
        visible=true
    }
    function focusSignalName() { signalName.selectAll(); signalName.forceActiveFocus() }
    function showStates(plot) {
        saveDraft(); mode = "states"; plotIndex = plot; backPlot = -1; errorText = ""; refreshStates(); visible = true
    }
    function refreshStates() { if (plotIndex >= 0) lanes = controller.stateTimelineState(plotIndex).lanes }
    function close() { saveDraft(); visible = false }
    function addEnum() {
        let next=0
        const used=new Set()
        for(let i=0;i<enumRows.count;++i) used.add(enumRows.get(i).code)
        while(used.has(String(next))) ++next
        enumRows.append({code:String(next),title:"状态 " + next,shade:String(controller.presetColors[next % controller.presetColors.length])})
    }
    function apply() {
        if (offsetEditable && !offsetValid) { errorText="请输入有效的时间偏移（秒），不接受千位分隔符"; return }
        if(mode === "offset") {
            if(controller.setTimeOffset(offsetScope,-1,offsetGroup,Number(offsetText))) errorText="已应用"
            else errorText=controller.status
            return
        }
        const values=[]
        for(let i=0;i<enumRows.count;++i) {
            const row=enumRows.get(i)
            if (!/^-?[0-9]+$/.test(row.code.trim()) || !Number.isSafeInteger(Number(row.code))) { errorText="第 " + (i+1) + " 行：请输入有效整数"; return }
            values.push({value:Number(row.code),text:row.title.trim(),color:row.shade})
        }
        if (signalName.text.trim().length === 0) { errorText="信号名称不能为空"; return }
        if (!controller.configureSignalProperties(sourceRow,{name:signalName.text.trim(),color:String(lineColor),width:lineWidth.value,style:lineStyle.currentIndex+1,enums:values,timeOffset:offsetEditable ? Number(offsetText) : null})) { errorText=controller.status; return }
        delete drafts[sourceRow]; appliedDraft=currentDraft(); errorText="已应用"; refreshStates()
    }
    Connections {
        target: editor.controller
        function onFileAppendCommitted() { editor.appendedSources = true }
        function onCurrentFileChanged() {
            if (editor.appendedSources) { editor.appendedSources = false; editor.refreshStates(); return }
            editor.visible=false; editor.drafts=({}); editor.sourceRow=-1
        }
        function onPlotBindingsChanged() { editor.refreshStates() }
    }
    component TimeOffsetFields: ColumnLayout {
        Label { text: "时间偏移（秒）"; font.bold: true }
        Label { visible: editor.mode === "offset" && editor.mixedOffsets; text: "当前偏移不一致，应用后统一设置。"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        TextField {
            objectName: editor.mode === "signal" ? "signalTimeOffsetField" : "scopeTimeOffsetField"
            visible: editor.offsetEditable
            text: editor.offsetText; placeholderText: "相对原始时间的偏移"
            Layout.fillWidth: true
            validator: DoubleValidator { locale: "C"; notation: DoubleValidator.ScientificNotation }
            onTextEdited: editor.offsetText=text
        }
        Label { visible: !editor.offsetEditable; text: "派生信号随输入来源的时间变化"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Button { objectName: "resetTimeOffsetButton"; visible: editor.offsetEditable; text: "重置为 0"; onClicked: editor.offsetText="0" }
        Label { text: "正值向右、负值向左，应用后生效。" + (editor.mode === "offset" ? "包含未勾选和被搜索隐藏的原始信号。" : ""); Layout.fillWidth: true; wrapMode: Text.WordWrap }
    }
    contentItem: ColumnLayout {
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            ToolButton { text: "‹"; visible: editor.mode === "signal" && editor.backPlot >= 0; onClicked: editor.showStates(editor.backPlot) }
            Label { text: editor.mode === "states" ? "状态属性 · 子图 " + (editor.plotIndex+1) : editor.mode === "offset" ? "时间偏移" : "信号属性"; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
            ToolButton { text: "×"; onClicked: editor.close() }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth; contentHeight: editor.mode === "states" ? stateContent.implicitHeight : editor.mode === "offset" ? offsetContent.implicitHeight : signalContent.implicitHeight; clip: true
            ColumnLayout {
                id: stateContent
                width: parent.width
                visible: editor.mode === "states"
                Label { text: "枚举含义和颜色在信号属性中编辑；位提取在“处理”中完成。"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Button { text: "从信号树添加"; Layout.fillWidth: true; onClicked: { editor.close(); editor.chooseStateSource(editor.plotIndex) } }
                Repeater {
                    model: editor.lanes
                    delegate: Frame {
                        id: laneRow
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true; padding: 4
                        contentItem: ColumnLayout {
                            RowLayout {
                                CheckBox { checked: laneRow.modelData.visible; onToggled: editor.controller.configureStateLane(editor.plotIndex,laneRow.index,{source:laneRow.modelData.source,name:laneRow.modelData.name,visible:checked}) }
                                Label { text: editor.controller.signalName(laneRow.modelData.source); Layout.fillWidth: true; elide: Text.ElideRight }
                                Button { text: "属性"; onClicked: editor.editSignal(laneRow.modelData.source,editor.plotIndex) }
                            }
                            RowLayout {
                                Button { text: "↑"; enabled: laneRow.index>0; onClicked: editor.controller.moveStateLane(editor.plotIndex,laneRow.index,-1) }
                                Button { text: "↓"; enabled: laneRow.index<editor.lanes.length-1; onClicked: editor.controller.moveStateLane(editor.plotIndex,laneRow.index,1) }
                                Item { Layout.fillWidth: true }
                                Button { text: "移除"; onClicked: editor.controller.removeStateLane(editor.plotIndex,laneRow.index) }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                id: offsetContent
                width: parent.width
                visible: editor.mode === "offset"
                Label { text: editor.offsetGroup; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                TimeOffsetFields { Layout.fillWidth: true }
            }
            ColumnLayout {
                id: signalContent
                width: parent.width
                visible: editor.mode === "signal"
                Label { text: "名称／重命名" }
                Label { text: "原名称：" + (editor.sourceRow>=0 ? editor.controller.originalSignalName(editor.sourceRow) : ""); Layout.fillWidth: true; elide: Text.ElideRight }
                TextField { id: signalName; objectName: "signalPropertyName"; Layout.fillWidth: true; maximumLength: 256 }
                Button { objectName: "resetSignalNameButton"; text: "恢复原名称"; onClicked: signalName.text=editor.controller.originalSignalName(editor.sourceRow) }
                TimeOffsetFields { Layout.fillWidth: true }

                Label { text: "曲线样式"; font.bold: true }
                ColorEditor {
                    Layout.fillWidth: true
                    presets: editor.controller.presetColors
                    text: String(editor.lineColor)
                    onEdited: value => { if(valid) editor.lineColor=value }
                }
                RowLayout {
                    Label { text: "线宽" }
                    SpinBox { id: lineWidth; from: 1; to: 20; editable: true; Layout.fillWidth: true }
                }
                ThemedComboBox { id: lineStyle; darkTheme: editor.darkTheme; model: ["实线","虚线","点线","点划线","双点划线"]; Layout.fillWidth: true }
                RowLayout {
                    Label { text: "枚举定义"; font.bold: true; Layout.fillWidth: true }
                    Button { objectName: "addSignalEnum"; text: "+"; enabled: enumRows.count<256; onClicked: editor.addEnum() }
                }
                Label { text: "数值 → 名称 · 颜色"; Layout.fillWidth: true }
                Repeater {
                    model: enumRows
                    delegate: RowLayout {
                        id: enumEntry
                        required property int index
                        required property string code
                        required property string title
                        required property string shade
                        Layout.fillWidth: true
                        TextField { text: enumEntry.code; Layout.preferredWidth: 48; maximumLength: 17; onTextEdited: enumRows.setProperty(enumEntry.index,"code",text) }
                        TextField { text: enumEntry.title; Layout.fillWidth: true; Layout.minimumWidth: 35; maximumLength: 256; onTextEdited: enumRows.setProperty(enumEntry.index,"title",text) }
                        Button { Layout.preferredWidth: 28; text: ""; background: Rectangle { color: enumEntry.shade; radius: 2; border.color: editor.palette.mid } onClicked: { editor.colorRow=enumEntry.index; enumColor.text=enumEntry.shade } }
                        Button { Layout.preferredWidth: 26; text: "×"; onClicked: { enumRows.remove(enumEntry.index); editor.colorRow=-1 } }
                    }
                }
                ColumnLayout {
                    visible: editor.colorRow >= 0 && editor.colorRow < enumRows.count
                    Layout.fillWidth: true
                    Flow {
                        Layout.fillWidth: true; spacing: 4
                        Repeater {
                            model: editor.controller.presetColors
                            delegate: Rectangle {
                                required property color modelData
                                width: 22; height: 20; color: modelData
                                TapHandler { onTapped: { enumRows.setProperty(editor.colorRow,"shade",String(parent.modelData)); enumColor.text=String(parent.modelData) } }
                            }
                        }
                    }
                    TextField { id: enumColor; Layout.fillWidth: true; placeholderText: "颜色 #RRGGBB"; onTextEdited: { if(editor.colorRow>=0) enumRows.setProperty(editor.colorRow,"shade",text) } }
                }
                Label { text: "未定义的数值保留编码，缺失值显示未知。枚举由所有引用此信号的状态图共用。"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Button { text: "到处理页提取 bit／位段"; Layout.fillWidth: true; onClicked: { editor.close(); editor.extractBits(editor.sourceRow) } }
            }
        }
        Label { text: editor.errorText; visible: text.length>0; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        RowLayout {
            visible: editor.mode !== "states"
            Button { objectName: "applySignalProperties"; text: "应用"; enabled: !editor.controller.loading && !editor.controller.exporting; onClicked: editor.apply() }
            Button { text: "还原"; onClicked: { if(editor.mode === "offset") editor.editTimeOffset(editor.offsetScope,-1,editor.offsetGroup); else { delete editor.drafts[editor.sourceRow]; editor.visible=false; editor.editSignal(editor.sourceRow,editor.backPlot) } } }
        }
    }
}
