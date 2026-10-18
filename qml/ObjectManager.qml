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
    property bool processingMode: false
    property bool sidebarNavigation: false
    property bool dropHighlighted: false
    property string selectedFieldId: ""
    property int rulePage: 0
    readonly property int rulePageSize: 20
    readonly property var matchingRules: {
        const query = ruleSearch.text.toLowerCase()
        const state = ruleState.currentIndex
        const pending = appController.deriving
        return selected ? selected.rules.filter(rule => {
            const failed = rule.state === "failed"
            return (query.length === 0 || (rule.name + " " + rule.sources + " " + rule.error).toLowerCase().indexOf(query) >= 0)
                && (state === 0 || (state === 1 && !pending && !failed) || (state === 2 && !pending && failed) || (state === 3 && pending))
        }) : []
    }
    readonly property var pageRules: matchingRules.slice(rulePage * rulePageSize, (rulePage + 1) * rulePageSize)
    onMatchingRulesChanged: rulePage = Math.min(rulePage, Math.max(0, Math.ceil(matchingRules.length / rulePageSize) - 1))
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
    signal sourcesRequested()
    signal sourceRequested(int row)
    function activateRuleInput(slot: string): void {
        selectedRuleInput = slot
        sourcesRequested()
    }
    function browseFieldSource(field: var): void {
        if (blocked || !selected || !field) return
        selectedFieldId = field.id
        sourceRequested(field.series)
    }
    function createObject(): void {
        if (blocked) return
        const id = appController.addDataObject(processingMode ? "processing" : "aircraft")
        if (id.length > 0) { selectedId = id; errorText = "" } else errorText = appController.status
    }
    function duplicateObject(): void {
        if (blocked || !selected) return
        const id = appController.duplicateDataObject(selectedId)
        if (id.length > 0) { selectedId = id; errorText = "" } else errorText = appController.status
    }
    function deleteObject(): void { if (!blocked && selected) deleteObjectDialog.open() }
    function open(): void { visible = true; if (!selected && objects.length > 0) selectedId = objects[0].id }
    function close(): void { visible = false; pendingSource = -1 }
    property string selectedId: ""
    property string errorText: ""
    readonly property bool blocked: appController.loading || appController.exporting || appController.restoringSession
    readonly property bool timePlot: { const revision = appController.plotStateRevision; return !appController.trajectoryState(appController.activePlotIndex).enabled }
    readonly property var objects: processingMode ? appController.processingConfigurations : appController.objectConfigurations
    onSelectedIdChanged: { selectedFieldId = ""; rulePage = 0; ruleDialog.close() }
    readonly property bool choosingRuleSource: processingMode && visible && ruleDialog.editing
    property string selectedRuleInput: "x"
    property string dropRuleInput: ""
    property bool dropRuleAllowed: true
    readonly property bool multipleRuleInputs: ruleDialog.operation === "magnitude" || ruleDialog.operation === "expression"
    function ruleInputValue(slot: string): string {
        if (slot === "x") return ruleDialog.inputXId
        if (slot === "y") return ruleDialog.inputYId
        if (slot === "z") return ruleDialog.inputZId
        return slot.startsWith("merge:") ? ruleDialog.mergeInputs[Number(slot.slice(6))] || "" : ""
    }
    function ruleInputName(id: string): string {
        if (id.length === 0) return "未绑定：从信号树选择或拖入信号"
        return ruleDialog.inputIndex.names.get(id) || "来源不可用"
    }
    function clearRuleInput(slot: string): void {
        if (blocked) return
        if (slot === "x") ruleDialog.inputXId = ""
        else if (slot === "y") ruleDialog.inputYId = ""
        else if (slot === "z") ruleDialog.inputZId = ""
        else if (slot.startsWith("merge:")) {
            const ids = ruleDialog.mergeInputs.slice(); ids.splice(Number(slot.slice(6)), 1); ruleDialog.mergeInputs = ids
        }
    }
    function ruleInputId(row: int): string {
        return ruleDialog.inputIndex.rows.get(row) || ""
    }
    function ruleSourceAllowed(row: int): bool {
        const id = ruleInputId(row)
        return !blocked && !ruleDialog.sourcesChanged && id.length > 0 && ruleDialog.inputIndex.names.has(id)
    }
    function ruleSourceChecked(row: int): bool {
        const id = ruleInputId(row)
        if (id.length === 0) return false
        return ruleDialog.operation === "merge" ? ruleDialog.mergeInputs.indexOf(id) >= 0 : id === ruleInputValue(selectedRuleInput)
    }
    function selectRuleSource(row: int): void {
        bindRuleSource(row, ruleDialog.operation === "merge" ? "merge" : selectedRuleInput, true)
    }
    function bindRuleSource(row: int, slot: string, toggle: bool): void {
        if (!choosingRuleSource || blocked) return
        if (ruleDialog.sourcesChanged) { ruleDialog.errorText = "数据来源已变更，请重新打开规则并确认输入信号"; return }
        const id = ruleInputId(row)
        if (!ruleSourceAllowed(row)) { ruleDialog.errorText = "请选择原始信号或本配置中此规则之前的处理结果"; return }
        if (ruleDialog.operation === "merge") {
            const ids = ruleDialog.mergeInputs.slice()
            const existing = ids.indexOf(id)
            if (slot.startsWith("merge:")) {
                const index = Number(slot.slice(6))
                if (index < 0 || index >= ids.length) return
                if (existing >= 0 && existing !== index) { ruleDialog.errorText = "合并输入不能重复"; return }
                ids[index] = id
            } else if (existing < 0) ids.push(id)
            else if (toggle) ids.splice(existing, 1)
            ruleDialog.mergeInputs = ids
        } else {
            if (slot !== "x" && (!multipleRuleInputs || (slot !== "y" && slot !== "z"))) return
            selectedRuleInput = slot
            if (slot === "x") ruleDialog.inputXId = id
            else if (slot === "y") ruleDialog.inputYId = id
            else ruleDialog.inputZId = id
        }
        ruleDialog.errorText = ""
    }
    function ruleInputAt(x: real, y: real): string {
        if (!choosingRuleSource || blocked) return ""
        const viewport = ruleScroll.mapFromItem(manager, x, y)
        if (viewport.x < 0 || viewport.y < 0 || viewport.x >= ruleScroll.width || viewport.y >= ruleScroll.height) return ""
        const slots = ruleDialog.operation === "merge" ? mergeSlots : variableSlots
        for (let i = 0; i < slots.count; ++i) {
            const item = slots.itemAt(i) as RuleInputSlot
            if (!item) continue
            const point = item.mapFromItem(manager, x, y)
            if (point.x >= 0 && point.y >= 0 && point.x < item.width && point.y < item.height) return item.slotKey
        }
        const point = mergeDropArea.mapFromItem(manager, x, y)
        return ruleDialog.operation === "merge" && point.x >= 0 && point.y >= 0 && point.x < mergeDropArea.width && point.y < mergeDropArea.height ? "merge" : ""
    }
    function browseRuleSource(): void {
        let row = -1
        const id = ruleInputValue(selectedRuleInput)
        if (id.startsWith("raw:")) row = Number(id.slice(4))
        else if (selected) for (const rule of selected.rules)
            for (const output of rule.outputs) if (output.id === id) row = output.series
        sourceRequested(row)
    }
    component RuleInputSlot: Frame {
        id: inputSlot
        required property string slotKey
        required property string label
        objectName: "ruleInputSlot_" + slotKey
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        readonly property bool active: manager.selectedRuleInput === slotKey && ruleDialog.operation !== "merge"
        background: Rectangle {
            color: inputSlot.active ? manager.selectionColor : manager.palette.base
            radius: 4
            border.width: manager.dropRuleInput === inputSlot.slotKey || inputSlot.active ? 2 : 1
            border.color: manager.dropRuleInput === inputSlot.slotKey ? manager.dropRuleAllowed ? manager.palette.highlight : "#df4652" : inputSlot.active ? manager.palette.highlight : manager.palette.mid
        }
        MouseArea {
            parent: inputSlot
            anchors.fill: parent
            z: 0
            enabled: !manager.blocked
            preventStealing: true
            onClicked: manager.activateRuleInput(inputSlot.slotKey)
        }
        contentItem: RowLayout {
            z: 1
            Label {
                objectName: "ruleInputLabel_" + inputSlot.slotKey
                text: inputSlot.label; font.bold: true; Layout.preferredWidth: 100; color: manager.textColor
                MouseArea { anchors.fill: parent; onClicked: manager.activateRuleInput(inputSlot.slotKey) }
            }
            Label {
                text: manager.ruleInputName(manager.ruleInputValue(inputSlot.slotKey))
                Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WrapAnywhere
                color: manager.textColor
                MouseArea { anchors.fill: parent; onClicked: manager.activateRuleInput(inputSlot.slotKey) }
            }
            Button { objectName: "chooseRuleInput_" + inputSlot.slotKey; text: "从信号树选择"; enabled: !manager.blocked; onClicked: manager.activateRuleInput(inputSlot.slotKey) }
            Button { text: inputSlot.slotKey.startsWith("merge:") ? "移除" : "解除"; enabled: !manager.blocked && manager.ruleInputValue(inputSlot.slotKey).length > 0; onClicked: manager.clearRuleInput(inputSlot.slotKey) }
        }
    }
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
        const fields = selected.fields.filter(field => {
            if (!field.fixed || selected.type !== "aircraft") return true
            if (["latitude", "longitude", "height"].indexOf(field.role) >= 0) return selected.geographic
            if (["x", "y", "z"].indexOf(field.role) >= 0) return !selected.geographic
            if (["roll", "pitch", "yaw"].indexOf(field.role) >= 0) return selected.attitudeMode === 1
            return selected.attitudeMode === 2
        })
        return fields.slice().sort((a, b) => {
            const left = roles.indexOf(a.role), right = roles.indexOf(b.role)
            return (left < 0 ? roles.length : left) - (right < 0 ? roles.length : right)
                || fields.indexOf(a) - fields.indexOf(b)
        })
    }
    ListModel { id: fieldRows; dynamicRoles: true }
    function syncFieldRows(): void {
        // Keep delegates and the viewport alive across dataObjects notifications.
        const wanted = new Set(visibleFields.map(field => field.id))
        for (let i = fieldRows.count - 1; i >= 0; --i)
            if (!wanted.has(fieldRows.get(i).fieldData.id)) fieldRows.remove(i)
        for (let i = 0; i < visibleFields.length; ++i) {
            const field = visibleFields[i]
            let existing = i
            while (existing < fieldRows.count && fieldRows.get(existing).fieldData.id !== field.id) ++existing
            if (existing === fieldRows.count) fieldRows.insert(i, {fieldData: field})
            else {
                if (existing !== i) fieldRows.move(existing, i, 1)
                fieldRows.setProperty(i, "fieldData", field)
            }
        }
    }
    Component.onCompleted: syncFieldRows()
    onVisibleFieldsChanged: {
        syncFieldRows()
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
    function nextRuleName(): string {
        const names = new Set()
        if (selected) for (const rule of selected.rules) {
            names.add(rule.name)
            for (const output of rule.outputs) names.add(output.name)
        }
        let number = 1
        while (names.has("规则" + number)) ++number
        return "规则" + number
    }
    function revealOutput(row: int): void {
        for (let i = 0; i < objects.length; ++i) for (let j = 0; j < objects[i].rules.length; ++j) {
            const outputs = objects[i].rules[j].outputs
            for (let k = 0; k < outputs.length; ++k) if (outputs[k].series === row) { selectedId = objects[i].id; open(); return }
        }
        open()
    }
    function editRule(rule: var): void {
        ruleDialog.initializing = true
        ruleDialog.sourcesChanged = false
        ruleDialog.ruleId = rule ? rule.id : ""
        selectedRuleInput = "x"
        ruleDialog.mergeInputs = rule ? rule.inputIds.slice() : []
        ruleName.text = rule ? rule.name : nextRuleName()
        operationCombo.currentIndex = 0
        if (rule) for (let i = 0; i < operationCombo.model.length; ++i) if (operationCombo.model[i].id === rule.operation) operationCombo.currentIndex = i
        ruleDialog.inputXId = rule ? rule.inputIds[0] || "" : ""
        ruleDialog.inputYId = rule ? rule.inputIds[1] || "" : ""
        ruleDialog.inputZId = rule ? rule.inputIds[2] || "" : ""
        wordWidth.currentIndex = rule ? [8, 16, 32].indexOf(rule.wordBits) : 0
        startBit.value = rule ? rule.startBit : 0
        bitCount.value = rule ? rule.bitCount : 8
        signedField.checked = rule ? rule.signedField : false
        factor.text = rule ? String(rule.factor) : "1"; bias.text = rule ? String(rule.bias) : "0"
        samplePeriod.text = rule && (rule.samplePeriod > 0 || rule.effectiveSamplePeriod > 0) ? String(rule.samplePeriod > 0 ? rule.samplePeriod : rule.effectiveSamplePeriod) : ""
        rateMode.currentIndex = rule ? Math.max(0, ["count", "cumulative", "window"].indexOf(rule.rateMode)) : 2
        rateWindow.text = rule ? String(rule.rateWindowSeconds) : "1"
        lossMode.currentIndex = rule ? Math.max(0, ["time", "sequence", "stride", "rate"].indexOf(rule.lossMode)) : 0
        counterStep.text = rule ? String(rule.counterStep) : "1"
        expectedStride.text = rule ? String(rule.expectedStride) : "1"
        counterModulus.text = rule ? String(rule.counterModulus) : "0"
        counterRate.text = rule ? String(rule.counterRate) : "1"
        counterTolerance.text = rule ? String(rule.counterTolerance) : "0"
        expression.text = rule && rule.expression.length > 0 ? rule.expression : "x"
        ruleDialog.initializing = false
        if (samplePeriod.text.length === 0) ruleDialog.estimatePeriod()
        ruleDialog.errorText = ""; ruleDialog.open()
    }
    contentItem: ColumnLayout {
        visible: !ruleDialog.editing
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Label { text: manager.processingMode ? "信号处理" : "对象管理"; font.bold: true; font.pixelSize: 18; Layout.fillWidth: true }
            Button { objectName: "closeObjectManagerButton"; text: "返回曲线"; onClicked: manager.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { objectName: "createObjectButton"; text: manager.processingMode ? "新建处理配置" : "新建对象"; enabled: !manager.blocked; onClicked: manager.createObject() }
            Button { text: "复制配置"; enabled: !!manager.selected && !manager.blocked; onClicked: manager.duplicateObject() }
            Button { text: manager.processingMode ? "删除处理配置…" : "删除对象…"; enabled: !!manager.selected && !manager.blocked; onClicked: manager.deleteObject() }
            Item { Layout.fillWidth: true }
            Label { text: manager.appController.deriving ? "正在计算…" : "" }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true
            ScrollView {
                visible: manager.processingMode || !manager.sidebarNavigation
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
                        text: modelData.name + (modelData.type === "aircraft" ? " · 飞机" : manager.processingMode ? "" : " · 常规")
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
                    Label { text: manager.selected ? manager.selected.name : manager.processingMode ? "先新建或选择处理配置" : "先新建或选择一个对象"; font.bold: true; Layout.fillWidth: true }
                    ObjectCombo {
                        objectName: "objectTypeSelector"
                        visible: !manager.processingMode
                        readonly property var types: ["general", "aircraft"]
                        model: ["常规", "飞机"]
                        enabled: !!manager.selected && !manager.blocked
                        currentIndex: manager.selected ? types.indexOf(manager.selected.type) : 0
                        onActivated: { if (!manager.appController.setDataObjectType(manager.selectedId, types[currentIndex])) { manager.errorText = manager.appController.status; currentIndex = manager.selected ? types.indexOf(manager.selected.type) : 0 } }
                    }
                }
                StackLayout {
                    currentIndex: manager.processingMode ? 1 : 0
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
                        Button { objectName: "browseObjectSourcesButton"; text: "选择来源…"; enabled: !!manager.selected && !manager.blocked; onClicked: manager.sourcesRequested() }
                        Label { text: "切换到左侧信号栏选择来源，勾选显示当前对象的全部来源。选中字段后勾选来源，或将信号直接拖到字段；取消勾选可解除当前对象的绑定。"; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap }
                        ScrollView {
                            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true
                            ListView {
                                id: fieldList
                                objectName: "objectFieldList"
                                model: fieldRows
                                currentIndex: -1
                                clip: true
                                delegate: ItemDelegate {
                                    id: fieldRow
                                    objectName: "objectFieldRow"
                                    required property var fieldData
                                    width: ListView.view.width; height: 48; padding: 4
                                    highlighted: manager.selectedFieldId === fieldData.id
                                    background: Rectangle { color: fieldRow.highlighted || manager.dropFieldId === fieldRow.fieldData.id ? manager.selectionColor : fieldRow.hovered ? manager.palette.button : "transparent"; radius: 4; border.width: manager.dropFieldId === fieldRow.fieldData.id ? 2 : 0; border.color: manager.palette.highlight }
                                    onClicked: manager.selectedFieldId = fieldData.id
                                    contentItem: RowLayout {
                                    Label {
                                        objectName: "objectFieldRowName"
                                        text: fieldRow.fieldData.name; color: manager.textColor; Layout.preferredWidth: 120; elide: Text.ElideRight
                                        MouseArea { anchors.fill: parent; onClicked: manager.selectedFieldId = fieldRow.fieldData.id; onDoubleClicked: if (!manager.blocked) manager.editField(fieldRow.fieldData, -1) }
                                    }
                                    Label {
                                        objectName: "objectFieldRowSource"
                                        text: fieldRow.fieldData.source; color: manager.textColor
                                        Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideMiddle
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: !manager.blocked
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: manager.browseFieldSource(fieldRow.fieldData)
                                        }
                                    }
                                    Button { text: "删除"; visible: !fieldRow.fieldData.fixed; enabled: !manager.blocked; onClicked: { if (!manager.appController.removeObjectField(manager.selectedId, fieldRow.fieldData.id)) manager.errorText = manager.appController.status } }
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        Button { text: "新增派生规则…"; objectName: "addObjectRuleButton"; enabled: !!manager.selected && manager.selected.inputs.length > 0 && !manager.blocked; onClicked: manager.editRule(null) }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: ruleSearch; objectName: "ruleSearch"; placeholderText: "搜索规则或来源…"; Layout.fillWidth: true; Layout.minimumWidth: 0; onTextChanged: manager.rulePage = 0 }
                            ObjectCombo { id: ruleState; objectName: "ruleStateFilter"; model: ["全部", "成功", "失败", "计算中"]; onActivated: manager.rulePage = 0 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: {
                                    const rules = manager.selected ? manager.selected.rules : []
                                    const failed = rules.filter(rule => rule.state === "failed").length
                                    const warnings = rules.filter(rule => rule.warning).length
                                    return manager.appController.deriving ? "计算中 · " + rules.length + " 条规则"
                                        : "成功 " + (rules.length - failed) + " · 失败 " + failed + " · 警告 " + warnings + " · 匹配 " + manager.matchingRules.length
                                }
                            }
                            Button { objectName: "rulesPreviousPage"; text: "上一页"; enabled: manager.rulePage > 0; onClicked: --manager.rulePage }
                            Label { text: (manager.rulePage + 1) + " / " + Math.max(1, Math.ceil(manager.matchingRules.length / manager.rulePageSize)) }
                            Button { objectName: "rulesNextPage"; text: "下一页"; enabled: (manager.rulePage + 1) * manager.rulePageSize < manager.matchingRules.length; onClicked: ++manager.rulePage }
                        }
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
                                model: manager.pageRules
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
                                                ? ruleRow.modelData.wordBits + " 位 · bit" + ruleRow.modelData.startBit + " 起，共 " + ruleRow.modelData.bitCount + " 位" + (ruleRow.modelData.signedField ? " · 有符号" : "") : ruleRow.modelData.operation === "merge" ? "按时间合并同源采样点" : ruleRow.modelData.operation === "packetloss" ? (ruleRow.modelData.lossMode !== "time" ? "Y 计数 · " + ({sequence: "逐帧", stride: "固定抽样", rate: "按时间与频率"})[ruleRow.modelData.lossMode] + (ruleRow.modelData.lossMode !== "rate" ? " · 重复采样 " + ruleRow.modelData.duplicateSamples : "") : "采样周期：" + (ruleRow.modelData.samplePeriod > 0 ? ruleRow.modelData.samplePeriod + " 秒" : ruleRow.modelData.effectiveSamplePeriod > 0 ? ruleRow.modelData.effectiveSamplePeriod + " 秒" : "未能估算，请编辑输入")) : "三轴模长"
                                        Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap
                                    }
                                    Label {
                                        visible: ruleRow.modelData.operation === "packetloss"
                                        text: ruleRow.modelData.rateMode === "window" ? "窗口丢包率(%) · " + ruleRow.modelData.rateWindowSeconds + " 秒" : ruleRow.modelData.rateMode === "count" ? "累计丢包数" : "累计丢包率(%)"
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
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
        title: manager.processingMode ? "重命名处理配置" : "重命名对象"
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
    Pane {
        id: ruleDialog
        parent: manager
        palette: manager.palette
        objectName: "objectRuleDialog"
        property string ruleId: ""
        property string inputXId: ""
        property string inputYId: ""
        property string inputZId: ""
        onInputXIdChanged: estimatePeriod()
        property string errorText: ""
        property bool editing: false
        visible: editing
        anchors.fill: parent
        z: 2
        background: Rectangle { color: manager.palette.window }
        property alias footer: ruleFooter
        function open(): void { editing = true; manager.browseRuleSource() }
        function close(): void { editing = false }
        property bool initializing: false
        property bool sourcesChanged: false
        property bool submitting: false
        Connections {
            target: manager.appController
            function onCurrentFileChanged(): void {
                if (manager.processingMode && ruleDialog.editing && !ruleDialog.submitting)
                    ruleDialog.sourcesChanged = true
            }
        }
        function estimatePeriod(): void {
            if (initializing || operation !== "packetloss" || lossMode.currentIndex !== 0) return
            const value = manager.appController.estimateObjectSamplePeriod(manager.selectedId, inputXId)
            samplePeriod.text = value > 0 ? String(value) : ""
        }
        property var mergeInputs: []
        readonly property string operation: String(operationCombo.currentValue)
        onOperationChanged: { manager.selectedRuleInput = operation === "merge" ? "merge" : "x"; estimatePeriod() }
        readonly property var inputOptions: {
            if (!manager.selected) return []
            if (ruleId.length === 0) return manager.selected.inputs
            const options = []
            if (manager.processingMode) {
                const outputIds = new Set()
                for (const rule of manager.selected.rules)
                    for (const output of rule.outputs) outputIds.add(output.id)
                for (const input of manager.selected.inputs)
                    if (!outputIds.has(input.id)) options.push(input)
            } else {
                const fields = manager.selected.fields
                for (let i = 0; i < fields.length; ++i) options.push({id: fields[i].id, name: fields[i].name})
            }
            const rules = manager.selected.rules
            for (let i = 0; i < rules.length; ++i) {
                if (rules[i].id === ruleId) break
                for (let j = 0; j < rules[i].outputs.length; ++j) options.push({id: rules[i].outputs[j].id, name: rules[i].outputs[j].name})
            }
            return options
        }
        // Build once per allowed-source snapshot, rather than scan the full
        // QVariant list on every label update, checkbox binding or drag move.
        readonly property var inputIndex: {
            const names = new Map(), rows = new Map()
            for (const input of inputOptions) {
                names.set(input.id, input.name)
                if (input.id.startsWith("raw:")) rows.set(Number(input.id.slice(4)), input.id)
            }
            if (manager.selected) for (const rule of manager.selected.rules)
                for (const output of rule.outputs)
                    if (names.has(output.id)) rows.set(output.series, output.id)
            return {names: names, rows: rows}
        }
        contentItem: ColumnLayout {
        RowLayout {
            Layout.fillWidth: true
            Label { text: (ruleDialog.ruleId.length > 0 ? "编辑派生规则" : "新增派生规则") + " · " + (manager.selected ? manager.selected.name : ""); font.bold: true; font.pixelSize: 18; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: "返回曲线"; onClicked: manager.close() }
        }
        ScrollView {
            id: ruleScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            rightPadding: ScrollBar.vertical.width + 4
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
            id: ruleContent
            width: ruleScroll.availableWidth
            Label { text: "规则 / 输出名称" }
            TextField { id: ruleName; objectName: "objectRuleName"; Layout.fillWidth: true; Layout.minimumWidth: 0; maximumLength: 256 }
            ObjectCombo {
                id: operationCombo; objectName: "objectRuleOperation"; textRole: "name"; valueRole: "id"; Layout.fillWidth: true
                model: [{id: "bits", name: "状态字拆位"}, {id: "bitfield", name: "提取位段"}, {id: "scale", name: "比例与偏置换算"}, {id: "magnitude", name: "三轴模长"}, {id: "expression", name: "自定义公式"}, {id: "merge", name: "按时间合并同源信号"}, {id: "packetloss", name: "丢包检测"}]
            }
            RowLayout {
                visible: ruleDialog.operation === "packetloss" && ruleDialog.ruleId.length === 0
                Layout.fillWidth: true
                Button {
                    objectName: "identifyPacketLossButton"
                    text: "自动识别"
                    enabled: !!manager.selected && !manager.blocked && !manager.appController.deriving && manager.appController.loadedFileCount > 0
                    onClicked: {
                        if (ruleDialog.sourcesChanged) { ruleDialog.errorText = "数据来源已变更，请重新打开规则并确认输入信号"; return }
                        ruleDialog.submitting = true
                        const accepted = manager.appController.identifyPacketLoss(manager.selectedId, ["count", "cumulative", "window"][rateMode.currentIndex], rateMode.currentIndex === 2 ? Number(rateWindow.text) : 1)
                        ruleDialog.submitting = false
                        if (accepted) ruleDialog.close()
                        else ruleDialog.errorText = manager.appController.status
                    }
                }
                Label {
                    text: "按已加载文件的采样时间组批量添加检测；保留已有规则和手动周期。"
                    Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap
                }
            }
            Label {
                text: manager.multipleRuleInputs ? "输入变量（原始信号或此前处理结果）" : ruleDialog.operation === "merge" ? "合并输入" : "输入 X（原始信号或此前处理结果）"
                font.bold: true
            }
            Repeater {
                id: variableSlots
                model: ruleDialog.operation === "merge" ? [] : manager.multipleRuleInputs ? ["x", "y", "z"] : ["x"]
                delegate: RuleInputSlot {
                    required property string modelData
                    slotKey: modelData
                    label: modelData.toUpperCase() + (modelData === "x" || ruleDialog.operation === "magnitude" ? " · 必填" : " · 可选")
                }
            }
            Label {
                visible: ruleDialog.operation !== "merge"
                text: "点击对应输入选择来源，或将左侧信号直接拖入该输入；树中勾选表示当前正在绑定的变量。"
                Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            Repeater {
                id: mergeSlots
                model: ruleDialog.operation === "merge" ? ruleDialog.mergeInputs : []
                delegate: RuleInputSlot {
                    required property int index
                    slotKey: "merge:" + index
                    label: "输入 " + (index + 1)
                }
            }
            Frame {
                id: mergeDropArea
                objectName: "ruleMergeDropArea"
                visible: ruleDialog.operation === "merge"
                Layout.fillWidth: true
                background: Rectangle {
                    color: manager.palette.base; radius: 4
                    border.width: manager.dropRuleInput === "merge" ? 2 : 1
                    border.color: manager.dropRuleInput === "merge" ? manager.dropRuleAllowed ? manager.palette.highlight : "#df4652" : manager.palette.mid
                }
                contentItem: RowLayout {
                    Label { text: "从左侧勾选多个信号，或拖入此处添加输入"; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap }
                    Button { text: "从信号树添加"; enabled: !manager.blocked; onClicked: manager.activateRuleInput("merge") }
                }
            }
            Label {
                visible: ruleDialog.operation === "merge"
                text: "至少两个同源输入；拖入已有输入可替换，移除或取消树中勾选可删除。按含偏移的时间排序，不插值；同时间同值去重，不同有效值报错。"
                Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap
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
            ColumnLayout {
                visible: ruleDialog.operation === "packetloss"
                Layout.fillWidth: true
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "输出指标" }
                    ObjectCombo { id: rateMode; objectName: "objectRuleRateMode"; model: ["累计丢包数", "累计丢包率(%)", "窗口丢包率(%)"]; Layout.fillWidth: true }
                }
                RowLayout {
                    visible: rateMode.currentIndex === 2
                    Label { text: "窗口时长（秒）" }
                    TextField { id: rateWindow; objectName: "objectRuleRateWindow"; text: "1"; Layout.fillWidth: true; maximumLength: 64 }
                }
                Label {
                    text: rateMode.currentIndex === 2
                        ? "统计最近窗口内的丢失帧 /（丢失帧 + 收到帧）×100%。缺失计入发现它的采样时刻；窗口为左开右闭，不插值或均摊缺失。仅生成所选的窗口丢包率结果。"
                        : rateMode.currentIndex === 0 ? "仅生成从记录起点累计的丢失帧数，单位为帧。"
                        : "仅生成从记录起点累计的丢包百分比：丢失帧 /（丢失帧 + 收到帧）×100%，不是每秒丢包数。"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
                ComboBox {
                    id: lossMode; objectName: "objectRuleLossMode"; Layout.fillWidth: true
                    model: ["X 采样时间", "Y 计数：逐帧", "Y 计数：固定间隔抽样", "Y 计数：按时间与频率"]
                    onActivated: ruleDialog.estimatePeriod()
                }
                GridLayout {
                    visible: lossMode.currentIndex !== 0; columns: 2; Layout.fillWidth: true
                    Label { text: "每帧计数步长" }
                    TextField { id: counterStep; objectName: "objectRuleCounterStep"; text: "1"; Layout.fillWidth: true; maximumLength: 32 }
                    Label { text: "正常跨过帧数"; visible: lossMode.currentIndex === 2 }
                    TextField { id: expectedStride; objectName: "objectRuleExpectedStride"; visible: lossMode.currentIndex === 2; text: "1"; Layout.fillWidth: true; maximumLength: 32 }
                    Label { text: "应收频率（帧/秒）"; visible: lossMode.currentIndex === 3 }
                    TextField { id: counterRate; objectName: "objectRuleCounterRate"; visible: lossMode.currentIndex === 3; text: "1"; Layout.fillWidth: true; maximumLength: 32 }
                    Label { text: "允许误差（帧）"; visible: lossMode.currentIndex === 3 }
                    TextField { id: counterTolerance; objectName: "objectRuleCounterTolerance"; visible: lossMode.currentIndex === 3; text: "0"; Layout.fillWidth: true; maximumLength: 32 }
                    Label { text: "回绕范围" }
                    ComboBox {
                        Layout.fillWidth: true
                        model: ["不回绕", "8 位：0～255", "16 位：0～65535", "32 位：0～4294967295", "自定义模数"]
                        currentIndex: [0, 256, 65536, 4294967296].indexOf(Number(counterModulus.text)) >= 0 ? [0, 256, 65536, 4294967296].indexOf(Number(counterModulus.text)) : 4
                        onActivated: if (currentIndex < 4) counterModulus.text = String([0, 256, 65536, 4294967296][currentIndex])
                    }
                    Label { text: "回绕模数（0 为关闭）" }
                    TextField { id: counterModulus; objectName: "objectRuleCounterModulus"; text: "0"; Layout.fillWidth: true; maximumLength: 32 }
                }
                Label {
                    visible: lossMode.currentIndex !== 0
                    text: lossMode.currentIndex === 3
                        ? "输入必须是累计实际接收帧数；预期帧数 = 时间差 × 应收频率。差额超过容差才计丢包，首点仅作基线。发送序号不适用于此模式。"
                        : "逐帧要求完整记录；固定抽样扣除正常跨过的帧数。发送序号的缺口可能包含未记录帧，结果不一定是通信丢包。重复值不增加丢包或收到帧数。"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
                Label {
                    visible: lossMode.currentIndex !== 0
                    text: "回绕范围为 0～模数−1，正常回绕不计丢包。须保证两点间增量小于半个范围；复位、乱序或多次回绕无法仅凭 Y 区分。参数未知时不能可靠计算丢包。"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
                RowLayout {
                    visible: lossMode.currentIndex === 0
                    Label { text: "采样周期（秒）" }
                    TextField { id: samplePeriod; objectName: "objectRuleSamplePeriod"; placeholderText: "无法估算，请输入正数"; text: ""; Layout.fillWidth: true; maximumLength: 64 }
                }
                Label {
                    visible: lossMode.currentIndex === 0
                    text: "采样周期已按原始时间间隔估算，可直接修改；多数间隔缺包或变频采样时请手动核定。\n丢包数 = max(四舍五入(时间差 / 周期) − 1, 0)；丢包率按上方选择的累计或窗口范围计算。时间戳须严格递增，首点为 0，不推断首尾以外的丢包。"
                    Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap
                }
            }
            TextField { id: expression; objectName: "objectRuleExpression"; visible: ruleDialog.operation === "expression"; text: "sqrt(x*x+y*y+z*z)"; Layout.fillWidth: true; Layout.minimumWidth: 0; maximumLength: 1024 }
            Label {
                text: ruleDialog.operation === "magnitude" ? "计算 sqrt(x*x+y*y+z*z)，X、Y、Z 均须绑定，且时间基相同。"
                    : "支持 + − * /、比较、&& / || / !、括号、abs/sqrt/min/max；输入按 X、Y、Z 连续绑定，使用 Z 时须先绑定 Y；多输入要求相同时间基。"
                visible: manager.multipleRuleInputs; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap
            }
            Label { text: ruleDialog.errorText; visible: text.length > 0; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap; color: "#df4652" }
        }
        }
        DialogButtonBox {
            id: ruleFooter
            Layout.fillWidth: true
            Button { text: "应用"; enabled: !manager.blocked && ruleName.text.trim().length > 0; onClicked: ruleDialog.submit() }
            Button { text: "取消"; onClicked: ruleDialog.close() }
        }
        }
        function submit(): void {
            if (sourcesChanged) {
                errorText = "数据来源已变更，请重新打开规则并确认输入信号"
                return
            }
            if (operation !== "merge" && inputXId.length === 0) { errorText = "请绑定输入 X"; return }
            if (operation === "magnitude" && (inputYId.length === 0 || inputZId.length === 0)) { errorText = "三轴模长需要绑定 X、Y、Z 三个输入"; return }
            if (operation === "expression" && inputZId.length > 0 && inputYId.length === 0) { errorText = "已绑定 Z，请先绑定 Y，或解除 Z；输入必须按 X、Y、Z 连续绑定"; return }
            if (operation === "merge" && mergeInputs.length < 2) { errorText = "合并至少需要两个输入"; return }
            const inputs = operation === "merge" ? mergeInputs.slice() : [inputXId]
            if (manager.multipleRuleInputs) {
                if (inputYId.length > 0) inputs.push(inputYId)
                if (inputZId.length > 0) inputs.push(inputZId)
            }
            if (operation === "packetloss" && lossMode.currentIndex === 0 && (!isFinite(Number(samplePeriod.text)) || Number(samplePeriod.text) <= 0)) {
                errorText = "采样周期必须为正数秒数"; return
            }
            if (operation === "packetloss" && rateMode.currentIndex === 2 && (!isFinite(Number(rateWindow.text)) || Number(rateWindow.text) <= 0)) {
                errorText = "窗口时长必须为正数秒数"; return
            }
            const config = {rateMode: ["count", "cumulative", "window"][rateMode.currentIndex], rateWindowSeconds: isFinite(Number(rateWindow.text)) && Number(rateWindow.text) > 0 ? Number(rateWindow.text) : 1, lossMode: ["time", "sequence", "stride", "rate"][lossMode.currentIndex], counterStep: Number(counterStep.text), expectedStride: Number(expectedStride.text), counterModulus: Number(counterModulus.text), counterRate: Number(counterRate.text), counterTolerance: Number(counterTolerance.text), samplePeriod: operation === "packetloss" && lossMode.currentIndex === 0 ? Number(samplePeriod.text) : 0, name: ruleName.text, operation: operation, inputs: inputs, wordBits: Number(wordWidth.currentText), startBit: startBit.value, bitCount: bitCount.value, signedField: signedField.checked, factor: Number(factor.text), bias: Number(bias.text), expression: expression.text}
            submitting = true
            const success = ruleId.length > 0 ? manager.appController.editObjectRule(manager.selectedId, ruleId, config)
                : manager.appController.addObjectRule(manager.selectedId, config).length > 0
            submitting = false
            if (success) close()
            else errorText = manager.appController.status.length > 0 ? manager.appController.status : "请检查规则名称、输入和参数"
        }
    }
    Dialog {
        id: deleteObjectDialog
        palette: manager.palette
        title: manager.processingMode ? "删除处理配置" : "删除对象"
        anchors.centerIn: parent; modal: true; width: 380; standardButtons: Dialog.Ok | Dialog.Cancel
        Label { width: parent.width; text: manager.processingMode ? "删除此处理配置及其全部规则、结果和绘图引用。原始信号与对象保留。" : "删除此对象及其航迹关联。独立处理配置和原始信号保留。"; wrapMode: Text.WordWrap }
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
