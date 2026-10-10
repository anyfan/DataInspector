pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DataInspector

Pane {
    id: manager
    required property AppController appController
    objectName: "objectManager"
    visible: false
    required property bool darkTheme
    property bool dropHighlighted: false
    property string selectedFieldId: ""
    palette.window: darkTheme ? "#20252b" : "#f7f9fb"
    palette.windowText: darkTheme ? "#e6edf3" : "#202830"
    palette.text: manager.textColor
    palette.buttonText: manager.textColor
    palette.base: darkTheme ? "#171c22" : "#ffffff"
    palette.button: darkTheme ? "#303842" : "#edf1f5"
    palette.placeholderText: darkTheme ? "#a2adba" : "#667380"
    palette.highlight: "#0078d4"
    palette.highlightedText: "#ffffff"
    readonly property color textColor: darkTheme ? "#e6edf3" : "#202830"
    readonly property color selectionColor: darkTheme ? "#293e52" : "#d9eafa"
    component ObjectCombo: ThemedComboBox { darkTheme: manager.darkTheme }
    property string dropFieldId: ""
    function fieldAt(x: real, y: real): string {
        const point = fieldList.mapFromItem(manager, x, y)
        if (point.x < 0 || point.y < 0 || point.x >= fieldList.width || point.y >= fieldList.height) return ""
        const index = fieldList.indexAt(point.x + fieldList.contentX, point.y + fieldList.contentY)
        return index >= 0 && index < visibleFields.length ? visibleFields[index].id : ""
    }
    function dropSource(row: int, fieldId: string): void {
        if (blocked || !selected || appController.isDerivedSignal(row)) return
        const target = fieldId.length > 0 ? fieldId : selectedFieldId
        const field = selected.fields.find(value => value.id === target)
        if (!field) return
        selectedFieldId = field.id
        if (field.series === row) return
        if (appController.bindObjectField(selectedId, field.id, field.name, field.role, row).length === 0) errorText = appController.status
        else errorText = ""
    }
    function renameObject(id: string): void {
        const object = objects.find(value => value.id === id)
        if (!object || blocked) return
        selectedId = id; nameDialog.targetId = id; objectNameField.text = object.name; nameDialog.open()
    }
    background: Rectangle { color: manager.palette.window; border.width: manager.dropHighlighted ? 2 : 0; border.color: manager.palette.highlight }
    function open(): void { visible = true; if (!selected && objects.length > 0) selectedId = objects[0].id }
    function close(): void { visible = false; pendingSource = -1 }
    property string selectedId: ""
    property string errorText: ""
    readonly property bool blocked: appController.loading || appController.exporting || appController.restoringSession
    readonly property bool timePlot: { const revision = appController.plotStateRevision; return !appController.trajectoryState(appController.activePlotIndex).enabled }
    readonly property var objects: appController.dataObjects
    onSelectedIdChanged: selectedFieldId = ""
    readonly property var selectedField: {
        if (selected) for (let i = 0; i < selected.fields.length; ++i) if (selected.fields[i].id === selectedFieldId) return selected.fields[i]
        return null
    }
    function sourceChecked(row: int): bool { return !!selected && selected.fields.some(field => field.series === row) }
    function selectSource(row: int): void {
        if (blocked || !selected) { errorText = "请先新建或选择对象"; return }
        if (appController.isDerivedSignal(row)) { errorText = "对象字段只能绑定原始信号"; return }
        if (sourceChecked(row)) {
            const fields = selected.fields.filter(field => field.series === row)
            for (const field of fields) if (appController.bindObjectField(selectedId, field.id, field.name, field.role, -1).length === 0) { errorText = appController.status; return }
            errorText = ""; return
        }
        let field = selectedField
        if (!field && selected.type === "aircraft") { errorText = "请先选择要绑定的飞机字段"; return }
        if (!field) { addField(); field = selectedField; if (!field) return }
        const id = appController.bindObjectField(selectedId, field.id, field.name, field.role, field.series === row ? -1 : row)
        if (id.length > 0) { selectedFieldId = id; errorText = "" } else errorText = appController.status
    }
    function addField(): void {
        const id = appController.addDataObjectField(selectedId)
        if (id.length > 0) { selectedFieldId = id; errorText = "" } else errorText = appController.status
    }
    function setAircraftOption(key: string, value: var): void {
        if (!selected) return
        const config = {geographic: selected.geographic, attitudeMode: selected.attitudeMode, radians: selected.radians, order: selected.order, scalarLast: selected.scalarLast, navigationToBody: selected.navigationToBody}
        config[key] = value
        if (!appController.configureDataObjectAircraft(selectedId, config)) errorText = appController.status
    }
    readonly property var visibleFields: {
        if (!selected) return []
        const roles = ["latitude", "longitude", "height", "roll", "pitch", "yaw", "x", "y", "z", "qw", "qx", "qy", "qz"]
        return selected.fields.filter(field => {
            if (!field.fixed || selected.type !== "aircraft") return true
            if (["latitude", "longitude", "height"].indexOf(field.role) >= 0) return selected.geographic
            if (["x", "y", "z"].indexOf(field.role) >= 0) return !selected.geographic
            if (["roll", "pitch", "yaw"].indexOf(field.role) >= 0) return selected.attitudeMode === 1
            return selected.attitudeMode === 2
        }).sort((a, b) => {
            const left = roles.indexOf(a.role), right = roles.indexOf(b.role)
            return (left < 0 ? roles.length : left) - (right < 0 ? roles.length : right)
        })
    }
    onVisibleFieldsChanged: {
        if (selectedFieldId.length > 0 && !visibleFields.some(field => field.id === selectedFieldId)) selectedFieldId = ""
    }
    readonly property var selected: {
        for (let i = 0; i < objects.length; ++i) if (objects[i].id === selectedId) return objects[i]
        return null
    }
    function editField(field: var, row: int): void {
        if (!selected || !field) return
        fieldDialog.fieldId = field.id
        fieldName.text = field.name
        fieldDialog.open()
    }
    function bindSource(row: int): void {
        open()
        if (!selected && objects.length > 0) selectedId = objects[0].id
        if (!selected) selectedId = appController.addDataObject("general")
        pendingSource = row
        if (selected) { selectSource(row); pendingSource = -1 }
    }
    property int pendingSource: -1
    function revealOutput(row: int): void {
        for (let i = 0; i < objects.length; ++i) for (let j = 0; j < objects[i].rules.length; ++j) {
            const outputs = objects[i].rules[j].outputs
            for (let k = 0; k < outputs.length; ++k) if (outputs[k].series === row) { selectedId = objects[i].id; tabs.currentIndex = 1; open(); return }
        }
        open()
    }
    function editRule(rule: var): void {
        ruleDialog.ruleId = rule ? rule.id : ""
        ruleName.text = rule ? rule.name : ""
        operationCombo.currentIndex = 0
        if (rule) for (let i = 0; i < operationCombo.model.length; ++i) if (operationCombo.model[i].id === rule.operation) operationCombo.currentIndex = i
        const options = selected ? selected.inputs : []
        inputX.currentIndex = options.length > 0 ? 0 : -1
        inputY.currentIndex = 0; inputZ.currentIndex = 0
        if (rule) for (let i = 0; i < options.length; ++i) {
            if (rule.inputIds[0] === options[i].id) inputX.currentIndex = i
            if (rule.inputIds[1] === options[i].id) inputY.currentIndex = i + 1
            if (rule.inputIds[2] === options[i].id) inputZ.currentIndex = i + 1
        }
        wordWidth.currentIndex = rule ? [8, 16, 32].indexOf(rule.wordBits) : 0
        startBit.value = rule ? rule.startBit : 0
        bitCount.value = rule ? rule.bitCount : 8
        signedField.checked = rule ? rule.signedField : false
        factor.text = rule ? String(rule.factor) : "1"; bias.text = rule ? String(rule.bias) : "0"
        expression.text = rule && rule.expression.length > 0 ? rule.expression : "x"
        ruleDialog.errorText = ""; ruleDialog.open()
    }
    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Label { text: "对象与派生数据"; font.bold: true; font.pixelSize: 18; Layout.fillWidth: true }
            Button { objectName: "closeObjectManagerButton"; text: "返回曲线"; onClicked: manager.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { objectName: "createObjectButton"; text: "新建对象"; enabled: !manager.blocked; onClicked: { const id = manager.appController.addDataObject("general"); if (id.length > 0) manager.selectedId = id; else manager.errorText = manager.appController.status } }
            Button { text: "复制配置"; enabled: !!manager.selected && !manager.blocked; onClicked: { const id = manager.appController.duplicateDataObject(manager.selectedId); if (id.length > 0) manager.selectedId = id; else manager.errorText = manager.appController.status } }
            Button { text: "删除对象…"; enabled: !!manager.selected && !manager.blocked; onClicked: deleteObjectDialog.open() }
            Item { Layout.fillWidth: true }
            Label { text: manager.appController.deriving ? "正在计算…" : "" }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true
            ScrollView {
                Layout.preferredWidth: 180; Layout.fillHeight: true
                ListView {
                    id: objectList
                    objectName: "objectList"
                    model: manager.objects
                    clip: true
                    delegate: ItemDelegate {
                        id: objectRow
                        objectName: "dataObjectRow"
                        required property var modelData
                        width: ListView.view.width
                        text: modelData.name + (modelData.type === "aircraft" ? " · 飞机" : " · 常规")
                        highlighted: modelData.id === manager.selectedId
                        contentItem: Label {
                            objectName: "dataObjectRowText"; text: objectRow.text; color: manager.textColor; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                            MouseArea { anchors.fill: parent; onClicked: { manager.selectedId = objectRow.modelData.id; manager.errorText = "" } onDoubleClicked: manager.renameObject(objectRow.modelData.id) }
                        }
                        background: Rectangle { color: objectRow.highlighted ? manager.selectionColor : objectRow.hovered ? manager.palette.button : "transparent"; radius: 4 }
                        onClicked: { manager.selectedId = modelData.id; manager.errorText = "" }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true
                RowLayout {
                    Label { text: manager.selected ? manager.selected.name : "先新建或选择一个对象"; font.bold: true; Layout.fillWidth: true }
                    ObjectCombo {
                        objectName: "objectTypeSelector"
                        model: ["常规", "飞机"]
                        enabled: !!manager.selected && !manager.blocked
                        currentIndex: manager.selected && manager.selected.type === "aircraft" ? 1 : 0
                        onActivated: { if (!manager.appController.setDataObjectType(manager.selectedId, currentIndex === 1 ? "aircraft" : "general")) { manager.errorText = manager.appController.status; currentIndex = manager.selected && manager.selected.type === "aircraft" ? 1 : 0 } }
                    }
                }
                TabBar {
                    id: tabs
                    Layout.fillWidth: true
                    TabButton { id: fieldsTab; text: "数据字段"; contentItem: Label { text: fieldsTab.text; color: manager.textColor; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } background: Rectangle { color: fieldsTab.checked ? manager.selectionColor : manager.palette.button } }
                    TabButton { id: rulesTab; text: "派生数据"; contentItem: Label { text: rulesTab.text; color: manager.textColor; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } background: Rectangle { color: rulesTab.checked ? manager.selectionColor : manager.palette.button } }
                }
                StackLayout {
                    currentIndex: tabs.currentIndex
                    Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true
                    ColumnLayout {
                        RowLayout {
                            Button { objectName: "addObjectFieldButton"; text: "添加数值字段"; enabled: !!manager.selected && !manager.blocked; onClicked: manager.addField() }
                            Button { text: "显示飞机航迹"; visible: !!manager.selected && manager.selected.type === "aircraft"; enabled: !manager.blocked; onClicked: { if (!manager.appController.showObjectTrajectory(manager.selectedId, manager.appController.activePlotIndex)) manager.errorText = manager.appController.status; else manager.close() } }
                        }
                        ColumnLayout {
                            visible: !!manager.selected && manager.selected.type === "aircraft"
                            Layout.fillWidth: true
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "位置坐标"; color: manager.textColor }
                                ObjectCombo { objectName: "objectAircraftCoordinates"; model: ["经纬高（度 / 米）", "XYZ（NED，米）"]; currentIndex: manager.selected && !manager.selected.geographic ? 1 : 0; enabled: !manager.blocked; onActivated: manager.setAircraftOption("geographic", currentIndex === 0) }
                                Label { text: "测量姿态"; color: manager.textColor }
                                ObjectCombo { objectName: "objectAircraftAttitude"; model: ["关闭姿态", "滚转 / 俯仰 / 航向", "四元数"]; currentIndex: manager.selected ? manager.selected.attitudeMode : 0; enabled: !manager.blocked; onActivated: manager.setAircraftOption("attitudeMode", currentIndex) }
                            }
                            RowLayout {
                                visible: !!manager.selected && manager.selected.attitudeMode === 1
                                CheckBox { text: "角度使用弧度"; checked: !!manager.selected && manager.selected.radians; enabled: !manager.blocked; onClicked: manager.setAircraftOption("radians", checked) }
                                ObjectCombo { objectName: "objectAircraftEulerOrder"; model: ["Rz(航向) · Ry(俯仰) · Rx(滚转)", "Rx(滚转) · Ry(俯仰) · Rz(航向)"]; currentIndex: manager.selected ? manager.selected.order : 0; enabled: !manager.blocked; onActivated: manager.setAircraftOption("order", currentIndex) }
                            }
                            RowLayout {
                                visible: !!manager.selected && manager.selected.attitudeMode > 0
                                CheckBox { text: "四元数顺序 xyzw（默认 wxyz）"; visible: !!manager.selected && manager.selected.attitudeMode === 2; checked: !!manager.selected && manager.selected.scalarLast; enabled: !manager.blocked; onClicked: manager.setAircraftOption("scalarLast", checked) }
                                CheckBox { text: "输入为导航→机体（默认机体→导航）"; checked: !!manager.selected && manager.selected.navigationToBody; enabled: !manager.blocked; onClicked: manager.setAircraftOption("navigationToBody", checked) }
                            }
                            Label { text: "导航 NED：北 / 东 / 下；机体 FRD：前 / 右 / 下。飞机预置参数不可删除，可解除来源绑定。"; color: manager.textColor; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        Label { text: "左侧勾选显示当前对象的全部来源。选中字段后勾选来源，或将信号直接拖到字段；取消勾选可解除当前对象的绑定。"; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap }
                        ScrollView {
                            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true
                            ListView {
                                id: fieldList
                                model: manager.visibleFields
                                clip: true
                                delegate: ItemDelegate {
                                    id: fieldRow
                                    objectName: "objectFieldRow"
                                    required property var modelData
                                    width: ListView.view.width; height: 48; padding: 4
                                    highlighted: manager.selectedFieldId === modelData.id
                                    background: Rectangle { color: fieldRow.highlighted || manager.dropFieldId === fieldRow.modelData.id ? manager.selectionColor : fieldRow.hovered ? manager.palette.button : "transparent"; radius: 4; border.width: manager.dropFieldId === fieldRow.modelData.id ? 2 : 0; border.color: manager.palette.highlight }
                                    onClicked: manager.selectedFieldId = modelData.id
                                    contentItem: RowLayout {
                                    Label {
                                        objectName: "objectFieldRowName"
                                        text: fieldRow.modelData.name; color: manager.textColor; Layout.preferredWidth: 120; elide: Text.ElideRight
                                        MouseArea { anchors.fill: parent; onClicked: manager.selectedFieldId = fieldRow.modelData.id; onDoubleClicked: if (!manager.blocked) manager.editField(fieldRow.modelData, -1) }
                                    }
                                    Label { objectName: "objectFieldRowSource"; text: fieldRow.modelData.source; color: manager.textColor; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideMiddle }
                                    Button { text: "删除"; visible: !fieldRow.modelData.fixed; enabled: !manager.blocked; onClicked: { if (!manager.appController.removeObjectField(manager.selectedId, fieldRow.modelData.id)) manager.errorText = manager.appController.status } }
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        Button { text: "新增派生规则…"; objectName: "addObjectRuleButton"; enabled: !!manager.selected && manager.selected.inputs.length > 0 && !manager.blocked; onClicked: manager.editRule(null) }
                        RowLayout {
                            visible: !manager.timePlot
                            Label { text: "当前子图是航迹，绘制派生曲线请切换到时间图。"; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap }
                            Button {
                                objectName: "objectSwitchTimePlot"
                                text: "切换为时间图"; enabled: !manager.blocked
                                onClicked: {
                                    const index = manager.appController.activePlotIndex
                                    const state = manager.appController.trajectoryState(index)
                                    manager.appController.configureTrajectory(index, false, state.x, state.y, state.z, state.geographic)
                                }
                            }
                        }
                        ScrollView {
                            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true
                            ListView {
                                model: manager.selected ? manager.selected.rules : []
                                clip: true; spacing: 10
                                delegate: ColumnLayout {
                                    id: ruleRow
                                    required property var modelData
                                    width: ListView.view.width
                                    RowLayout {
                                        Label { text: ruleRow.modelData.name + " · " + ruleRow.modelData.operation; Layout.fillWidth: true; Layout.minimumWidth: 0; font.bold: true; elide: Text.ElideRight }
                                        Button { text: "编辑…"; enabled: !manager.blocked; onClicked: manager.editRule(ruleRow.modelData) }
                                        Button { text: "删除规则…"; enabled: !manager.blocked; onClicked: { deleteRuleDialog.ruleId = ruleRow.modelData.id; deleteRuleDialog.ruleName = ruleRow.modelData.name; deleteRuleDialog.open() } }
                                    }
                                    Label { text: "输入：" + ruleRow.modelData.sources; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideMiddle }
                                    Label {
                                        text: ruleRow.modelData.operation === "expression" ? "公式：" + ruleRow.modelData.expression
                                            : ruleRow.modelData.operation === "scale" ? "换算：x × " + ruleRow.modelData.factor + " + " + ruleRow.modelData.bias
                                            : ruleRow.modelData.operation === "bits" || ruleRow.modelData.operation === "bitfield"
                                                ? ruleRow.modelData.wordBits + " 位 · bit" + ruleRow.modelData.startBit + " 起，共 " + ruleRow.modelData.bitCount + " 位" + (ruleRow.modelData.signedField ? " · 有符号" : "") : "三轴模长"
                                        Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap
                                    }
                                    Label { text: ruleRow.modelData.error; visible: text.length > 0; color: "#df4652"; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap }
                                    Repeater {
                                        model: ruleRow.modelData.outputs
                                        delegate: RowLayout {
                                            id: outputRow
                                            required property var modelData
                                            Layout.fillWidth: true
                                            CheckBox {
                                                text: outputRow.modelData.name
                                                Layout.fillWidth: true
                                                checked: { const revision = manager.appController.plotStateRevision; return manager.timePlot && manager.appController.plotSignalEnabled(manager.appController.activePlotIndex, outputRow.modelData.series) }
                                                enabled: !manager.blocked && !manager.appController.deriving && manager.timePlot
                                                onClicked: manager.appController.toggleSignal(outputRow.modelData.series)
                                            }
                                            Button { text: "改名"; enabled: !manager.blocked; onClicked: { outputNameDialog.series = outputRow.modelData.series; outputNameField.text = outputRow.modelData.name; outputNameDialog.open() } }
                                            Label { text: outputRow.modelData.preview; Layout.preferredWidth: 170; elide: Text.ElideRight }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Label { text: manager.errorText; visible: text.length > 0; color: "#df4652"; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap }
    }
    Dialog {
        id: nameDialog
        objectName: "objectNameDialog"
        palette: manager.palette
        title: "重命名对象"
        property string targetId: ""
        anchors.centerIn: parent; modal: true; width: 360
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            width: parent.width
            TextField { id: objectNameField; objectName: "objectNameField"; Layout.fillWidth: true; Layout.minimumWidth: 0; placeholderText: "例如：飞机 A、飞控、设备 1"; maximumLength: 256 }
        }
        onAccepted: {
            if (!manager.appController.renameDataObject(targetId, objectNameField.text)) manager.errorText = "对象名称不能为空"
        }
    }
    Dialog {
        id: fieldDialog
        palette: manager.palette
        objectName: "objectFieldDialog"
        title: "重命名字段"
        property string fieldId: ""
        anchors.centerIn: parent; modal: true; width: Math.min(660, manager.width - 30)
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            width: parent.width
            Label { text: "字段名称" }
            TextField { id: fieldName; objectName: "objectFieldName"; Layout.fillWidth: true; Layout.minimumWidth: 0; maximumLength: 256 }
        }
        onAccepted: { if (!manager.appController.renameDataObjectField(manager.selectedId, fieldId, fieldName.text)) manager.errorText = "字段名称无效"; else manager.selectedFieldId = fieldId }
    }
    Dialog {
        id: ruleDialog
        palette: manager.palette
        objectName: "objectRuleDialog"
        property string ruleId: ""
        property string errorText: ""
        title: ruleId.length > 0 ? "编辑派生规则" : "新增派生规则"
        anchors.centerIn: parent; modal: true; width: Math.min(660, manager.width - 30)
        readonly property string operation: String(operationCombo.currentValue)
        readonly property var inputOptions: {
            if (!manager.selected) return []
            if (ruleId.length === 0) return manager.selected.inputs
            const options = []
            const fields = manager.selected.fields
            for (let i = 0; i < fields.length; ++i) options.push({id: fields[i].id, name: fields[i].name})
            const rules = manager.selected.rules
            for (let i = 0; i < rules.length; ++i) {
                if (rules[i].id === ruleId) break
                for (let j = 0; j < rules[i].outputs.length; ++j) options.push({id: rules[i].outputs[j].id, name: rules[i].outputs[j].name})
            }
            return options
        }
        ColumnLayout {
            width: parent.width
            Label { text: "规则 / 输出名称" }
            TextField { id: ruleName; objectName: "objectRuleName"; Layout.fillWidth: true; Layout.minimumWidth: 0; maximumLength: 256 }
            ObjectCombo {
                id: operationCombo; objectName: "objectRuleOperation"; textRole: "name"; valueRole: "id"; Layout.fillWidth: true
                model: [{id: "bits", name: "状态字拆位"}, {id: "bitfield", name: "提取位段"}, {id: "scale", name: "比例与偏置换算"}, {id: "magnitude", name: "三轴模长"}, {id: "expression", name: "自定义公式"}]
            }
            Label { text: "输入 x（对象字段或此前派生结果）" }
            ObjectCombo { id: inputX; objectName: "objectRuleInputX"; model: ruleDialog.inputOptions; textRole: "name"; valueRole: "id"; Layout.fillWidth: true }
            Label { text: "输入 y / z"; visible: ruleDialog.operation === "magnitude" || ruleDialog.operation === "expression" }
            RowLayout {
                visible: ruleDialog.operation === "magnitude" || ruleDialog.operation === "expression"
                ObjectCombo { id: inputY; model: [{id: "", name: "不使用"}].concat(ruleDialog.inputOptions); textRole: "name"; valueRole: "id"; Layout.fillWidth: true }
                ObjectCombo { id: inputZ; enabled: inputY.currentIndex > 0; model: [{id: "", name: "不使用"}].concat(ruleDialog.inputOptions); textRole: "name"; valueRole: "id"; Layout.fillWidth: true }
            }
            RowLayout {
                visible: ruleDialog.operation === "bits" || ruleDialog.operation === "bitfield"
                Label { text: "字宽" }
                ObjectCombo { id: wordWidth; model: [8, 16, 32]; onActivated: { startBit.value = Math.min(startBit.value, Number(currentText) - 1); bitCount.value = Number(currentText) - startBit.value } }
                Label { text: "起始 bit" }
                SpinBox { id: startBit; from: 0; to: Number(wordWidth.currentText) - 1 }
                Label { text: "位数" }
                SpinBox { id: bitCount; from: 1; to: Number(wordWidth.currentText) - startBit.value; value: 8 }
            }
            CheckBox { id: signedField; text: "位段按二补码有符号整数解释"; visible: ruleDialog.operation === "bitfield" }
            Label { text: "bit0 为最低有效位；仅接受合法整数，缺失或越界样本留空。"; visible: ruleDialog.operation === "bits" || ruleDialog.operation === "bitfield"; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap }
            RowLayout {
                visible: ruleDialog.operation === "scale"
                Label { text: "比例" }
                TextField { id: factor; text: "1"; Layout.fillWidth: true }
                Label { text: "偏置" }
                TextField { id: bias; text: "0"; Layout.fillWidth: true }
            }
            TextField { id: expression; objectName: "objectRuleExpression"; visible: ruleDialog.operation === "expression"; text: "sqrt(x*x+y*y+z*z)"; Layout.fillWidth: true; Layout.minimumWidth: 0; maximumLength: 1024 }
            Label { text: "支持 + − * /、比较、&& / || / !、括号、abs/sqrt/min/max；多输入要求相同时间基。"; visible: ruleDialog.operation === "expression" || ruleDialog.operation === "magnitude"; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap }
            Label { text: ruleDialog.errorText; visible: text.length > 0; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap; color: "#df4652" }
        }
        function submit(): void {
            const inputs = [String(inputX.currentValue)]
            if (operation === "magnitude" || operation === "expression") {
                if (String(inputY.currentValue).length > 0) inputs.push(String(inputY.currentValue))
                if (inputZ.enabled && String(inputZ.currentValue).length > 0) inputs.push(String(inputZ.currentValue))
            }
            const config = {name: ruleName.text, operation: operation, inputs: inputs, wordBits: Number(wordWidth.currentText), startBit: startBit.value, bitCount: bitCount.value, signedField: signedField.checked, factor: Number(factor.text), bias: Number(bias.text), expression: expression.text}
            const success = ruleId.length > 0 ? manager.appController.editObjectRule(manager.selectedId, ruleId, config)
                : manager.appController.addObjectRule(manager.selectedId, config).length > 0
            if (success) close()
            else errorText = manager.appController.status.length > 0 ? manager.appController.status : "请检查规则名称、输入和参数"
        }
        footer: DialogButtonBox {
            Button { text: "确定"; enabled: !manager.blocked && ruleName.text.trim().length > 0; onClicked: ruleDialog.submit() }
            Button { text: "取消"; onClicked: ruleDialog.close() }
        }
    }
    Dialog {
        id: deleteObjectDialog
        palette: manager.palette
        title: "删除对象"
        anchors.centerIn: parent; modal: true; width: 380; standardButtons: Dialog.Ok | Dialog.Cancel
        Label { width: parent.width; text: "删除对象及其全部派生规则、结果和绘图引用。原始文件与信号保留。"; wrapMode: Text.WordWrap }
        onAccepted: { manager.appController.removeDataObject(manager.selectedId); manager.selectedId = "" }
    }
    Dialog {
        id: deleteRuleDialog
        palette: manager.palette
        property string ruleId: ""
        property string ruleName: ""
        title: "删除派生规则"
        anchors.centerIn: parent; modal: true; width: 380; standardButtons: Dialog.Ok | Dialog.Cancel
        Label { width: parent.width; text: "删除“" + deleteRuleDialog.ruleName + "”的全部输出和绘图引用。有下游依赖时需先删除下游规则。"; wrapMode: Text.WordWrap }
        onAccepted: if (!manager.appController.removeObjectRule(manager.selectedId, ruleId)) manager.errorText = manager.appController.status
    }
    Dialog {
        id: outputNameDialog
        palette: manager.palette
        property int series: -1
        title: "派生输出名称"
        anchors.centerIn: parent; modal: true; width: 360; standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: outputNameField; width: parent.width; maximumLength: 256 }
        onAccepted: if (!manager.appController.renameSignal(series, outputNameField.text)) manager.errorText = "名称不能为空"
    }
}
