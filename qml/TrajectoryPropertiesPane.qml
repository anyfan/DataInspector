pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DataInspector

Pane {
    id: editor
    required property AppController controller
    required property bool darkTheme
    objectName: "trajectoryPropertiesPane"
    visible: false
    property int plotIndex: -1
    property string styleObjectId: ""
    property var configuration: ({tracks: [], active: 0, objectId: ""})
    property string errorText: ""
    signal editObjectRequested(string objectId)
    readonly property var aircraftObjects: controller.dataObjects.filter(object => object.type === "aircraft")
    readonly property color foreground: darkTheme ? "#d7dee7" : "#303942"
    palette.text: editor.foreground
    palette.windowText: editor.foreground
    palette.buttonText: editor.foreground
    palette.base: darkTheme ? "#171c22" : "#ffffff"
    background: Rectangle { color: editor.darkTheme ? "#20252b" : "#f7f9fb" }
    function refresh() {
        if (plotIndex < 0) return
        const state=controller.trajectoryState(plotIndex)
        if (styleObjectId.length>0) {
            const index=state.tracks.findIndex(track=>track.objectId===styleObjectId)
            const object=aircraftObjects.find(value=>value.id===styleObjectId)
            const track=index>=0 ? state.tracks[index] : {name:object ? object.name : "航迹",color:"#0072bd",width:2,visible:true,objectId:styleObjectId}
            configuration=Object.assign({},state,track,{active:index,objectId:styleObjectId})
        } else configuration=state
        trackName.text=configuration.name; trackColor.text=configuration.color
        trackWidth.value=configuration.width; trackVisible.checked=configuration.visible

    }
    function editPlot(plot) { styleObjectId=""; plotIndex=plot; errorText=""; refresh(); visible=true }
    function editObject(objectId,plot) { styleObjectId=objectId; plotIndex=plot; errorText=""; refresh(); visible=true }
    function close() { visible=false }
    function apply() {
        if (!trackName.text.trim().length || !trackColor.valid) {
            errorText="请输入名称与有效颜色（#RRGGBB 或 #AARRGGBB）"; return
        }
        const name=trackName.text.trim(), color=trackColor.text.trim(), width=trackWidth.value, shown=trackVisible.checked
        if (styleObjectId.length>0) {
            const state=controller.trajectoryState(plotIndex)
            const index=state.tracks.findIndex(track=>track.objectId===styleObjectId)
            if(index<0 && !shown) { errorText="已应用"; return }
            if(shown ? !controller.showObjectTrajectory(styleObjectId,plotIndex) : !controller.selectTrajectory(plotIndex,index)) {
                errorText=controller.status; return
            }
        }
        if (controller.styleTrajectory(plotIndex,name,color,width,shown)) errorText="已应用"
        else errorText=controller.status
    }
    Connections {
        target: editor.controller
        function onCurrentFileChanged() { if(editor.visible) editor.refresh() }
        function onPlotBindingsChanged() {
            if (!editor.visible) return
            const state=editor.controller.trajectoryState(editor.plotIndex)
            if (!state.enabled && editor.styleObjectId.length===0) { editor.close(); return }
            editor.refresh()
        }
    }
    contentItem: ColumnLayout {
        RowLayout {
            Label { text: (editor.styleObjectId.length>0 ? "航迹样式" : "航迹属性") + " · 子图 " + (editor.plotIndex+1); font.bold: true; Layout.fillWidth: true }
            ToolButton { text: "×"; onClicked: editor.close() }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                objectName: "trajectoryConfigurationRow"
                width: parent.width
                spacing: 8
                Label { visible: editor.styleObjectId.length===0; text: "活动航迹"; font.bold: true }
                ThemedComboBox {
                    visible: editor.styleObjectId.length===0
                    objectName: "trajectorySelector"
                    darkTheme: editor.darkTheme; Layout.fillWidth: true
                    model: editor.configuration.tracks; textRole: "name"
                    currentIndex: editor.configuration.active
                    onActivated: index => editor.controller.selectTrajectory(editor.plotIndex,index)
                }
                RowLayout {
                    visible: editor.styleObjectId.length===0
                    Button { objectName: "trajectoryAdd"; text: "添加"; onClicked: editor.controller.addTrajectory(editor.plotIndex) }
                    Button { objectName: "trajectoryRemove"; text: "移除"; onClicked: editor.controller.removeTrajectory(editor.plotIndex,editor.configuration.active) }
                }
                Label { text: "飞机对象"; font.bold: true }
                ThemedComboBox {
                    id: objectChoice; objectName: "trajectoryObjectSelector"
                    enabled: editor.styleObjectId.length===0
                    darkTheme: editor.darkTheme; Layout.fillWidth: true
                    model: editor.aircraftObjects; textRole: "name"; valueRole: "id"
                    currentIndex: editor.aircraftObjects.findIndex(object=>object.id===editor.configuration.objectId)
                    displayText: currentIndex>=0 ? currentText : "未绑定对象"
                    onActivated: index => editor.controller.showObjectTrajectory(editor.aircraftObjects[index].id,editor.plotIndex)
                }
                Button { text: "对象配置…"; onClicked: editor.editObjectRequested(editor.configuration.objectId) }
                Label { text: "位置、坐标系与测量姿态统一在飞机对象中配置。请先选择对象；需要修改来源时点击“对象配置…”。"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Label { text: "航迹样式"; font.bold: true }
                Label { text: "名称" }
                TextField { id: trackName; objectName: "trajectoryName"; placeholderText: "名称"; maximumLength: 256; Layout.fillWidth: true }
                Label { text: "颜色" }
                ColorEditor { id: trackColor; objectName: "trajectoryColor"; presets: editor.controller.presetColors; Layout.fillWidth: true }
                RowLayout {
                    Label { text: "线宽" }
                    SpinBox { id: trackWidth; objectName: "trajectoryWidth"; from: 1; to: 12; editable: true; Layout.fillWidth: true }
                }
                CheckBox { id: trackVisible; objectName: "trajectoryVisible"; text: "显示航迹" }
            }
        }
        Label { text: editor.errorText; visible: text.length>0; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        RowLayout {
            Button { objectName: "applyTrajectoryProperties"; text: "应用"; enabled: !editor.controller.loading && !editor.controller.restoringSession; onClicked: editor.apply() }
            Button { text: "还原"; onClicked: { editor.refresh(); editor.errorText="" } }
        }
    }
}
