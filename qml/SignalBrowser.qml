pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Shapes
import DataInspector
Rectangle {
    id: signalBrowser
    objectName: "signalBrowser"
    required property AppController appController
    required property ObjectManager objectEditor
    required property ObjectManager processingEditor
    required property color panelColor
    required property color borderColor
    required property color accentColor
    required property color treeTextColor
    required property color treeHeaderColor
    required property color treeHeaderTextColor
    required property int treeHeaderHeight
    required property bool darkTheme
    required property real hostWidth
    required property bool signalTreeDragging
    required property int signalDragRevision
    readonly property color secondaryTextColor: darkTheme ? "#9aa6b3" : "#737e8a"
    readonly property var activeTrajectoryObject: {
        const revision = signalBrowser.appController.plotStateRevision
        const state = signalBrowser.appController.trajectoryState(signalBrowser.appController.activePlotIndex)
        return state.enabled ? signalBrowser.appController.dataObjects.find(object => object.id === state.objectId) || null : null
    }
    property TrajectoryPropertiesPane trajectoryEditor
    property SignalPropertiesPane propertiesEditor
    property int statePickTarget: -1
    function editSignalProperties(row) { statePickTarget=-1; objectEditor.close(); processingEditor.close(); signalBrowser.propertiesEditor.editSignal(row) }
    function editStateRows(plot) { statePickTarget=-1; objectEditor.close(); processingEditor.close(); signalBrowser.propertiesEditor.showStates(plot) }
    function followSignalProperties(row) {
        if (propertiesEditor && propertiesEditor.visible && propertiesEditor.mode !== "states"
            && statePickTarget < 0 && !processingEditor.choosingRuleSource && !objectEditor.visible
            && (propertiesEditor.mode !== "signal" || propertiesEditor.sourceRow !== row))
            propertiesEditor.editSignal(row)
    }
    function addPickedState(row) {
        if (appController.addStateSignal(statePickTarget,row)) { const plot=statePickTarget; statePickTarget=-1; signalBrowser.propertiesEditor.showStates(plot) }
    }
    signal dragCancelRequested()
    signal dragFinishRequested()
    signal dragUpdateRequested(Item source, real x, real y, var rowItem, point pressPoint)
    signal editSignalRequested(int row)
    signal removeFileRequested(string group)
    signal contextRequested(int kind, int row, string group)
    readonly property var matchingObjects: {
        const query = objectSearch.text.trim().toLowerCase()
        return appController.objectConfigurations.filter(object =>
            (object.name + " " + (object.type === "aircraft" ? "飞机" : "常规")).toLowerCase().includes(query))
    }
    readonly property var objectTrajectoryState: {
        const revision = appController.plotStateRevision
        return appController.trajectoryState(appController.activePlotIndex)
    }
    function applyObjectTrajectory(id: string, plot: int, color: color, width: real, visible: bool): bool {
        let state = appController.trajectoryState(plot)
        let index = state.tracks.findIndex(track => track.objectId === id)
        if (index < 0 && !visible) return true
        if (visible) {
            if (!appController.showObjectTrajectory(id, plot)) return false
        } else if (!appController.selectTrajectory(plot, index)) return false
        state = appController.trajectoryState(plot)
        if (state.objectId !== id) return false
        if (!appController.styleTrajectory(plot, state.name, color, width, visible)) return false
        objectEditor.close()
        processingEditor.close()
        return true
    }
    function editObjectTrajectory(id: string): void {
        propertiesEditor.close(); objectEditor.close(); processingEditor.close()
        trajectoryEditor.editObject(id,appController.activePlotIndex)
    }
    function editObject(id: string): void {
        objectEditor.selectedId = id
        objectEditor.errorText = ""
        objectEditor.open()
    }
    Connections {
        target: signalBrowser.processingEditor
        function onSourcesRequested(): void {
            // Choosing X/Y/Z changes the binding target only. Keep the user's
            // current search, tab, expansion and viewport while selecting inputs.
            signalBrowser.showRequested()
        }
        function onSourceRequested(row: int): void {
            signalBrowser.showRequested()
            if (row >= 0) signalBrowser.revealSignal(row)
            else { signalBrowser.showSection(0); signalBrowser.clearSearch(); signalBrowser.appController.filterSignals("") }
        }
    }
    Connections {
        target: signalBrowser.objectEditor
        function onSourcesRequested(): void { signalBrowser.showRequested(); signalBrowser.showSection(0) }
        function onSourceRequested(row: int): void {
            signalBrowser.showRequested()
            if (row >= 0) signalBrowser.revealSignal(row)
            else {
                signalBrowser.showSection(0)
                signalBrowser.clearSearch()
                signalBrowser.appController.filterSignals("")
            }
        }
    }
    Binding {
        target: signalBrowser.objectEditor
        property: "sidebarNavigation"
        value: browserTabs.currentIndex === 2
    }
    signal hideRequested()
    signal showRequested()
    Component.onCompleted: {
        appController.setSignalBrowserSection(0)
    }
    function showSection(index: int): void { browserTabs.currentIndex = index }
    function clearSearch() {
        signalSearch.clear()
        searchTimer.stop()
        signalSearch.filtering = false
    }
    function revealSignal(row) {
        browserTabs.currentIndex = appController.isDerivedSignal(row) ? 1 : 0
        clearSearch()
        const modelRow = signalBrowser.appController.signalModel.revealSignal(row)
        signalList.currentIndex = modelRow
        signalList.positionViewAtIndex(modelRow, ListView.Center)
    }
    function signalGroupTitle(group) {
        const parts = group.split("/")
        return parts.length > 1 ? parts.slice(1).join(" / ") : group
    }

    function highlightedSearchText(value) {
        function escaped(text) {
            return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        }
        const query = signalSearch.text.trim()
        if (!query.length) return escaped(value)
        const lower = value.toLowerCase()
        const needle = query.toLowerCase()
        const color = signalBrowser.darkTheme ? "#ffca70" : "#a34700"
        let result = "", offset = 0, match = lower.indexOf(needle)
        while (match >= 0) {
            result += escaped(value.slice(offset, match))
                    + '<font color="' + color + '"><b>'
                    + escaped(value.slice(match, match + query.length)) + '</b></font>'
            offset = match + query.length
            match = lower.indexOf(needle, offset)
        }
        return result + escaped(value.slice(offset))
    }


    SplitView.preferredWidth: 280
    SplitView.minimumWidth: 180
    SplitView.maximumWidth: Math.max(180, signalBrowser.hostWidth - 320)
    color: signalBrowser.panelColor
    ColumnLayout { anchors.fill: parent; anchors.margins: 10; anchors.rightMargin: 0; spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Basic.TabBar {
                id: browserTabs
                objectName: "signalBrowserTabs"
                Layout.fillWidth: true
                onCurrentIndexChanged: {
                    signalBrowser.dragCancelRequested()
                    signalBrowser.appController.setSignalBrowserSection(currentIndex === 1 ? 1 : 0)
                    if (currentIndex === 1) signalBrowser.objectEditor.close()
                    else if (currentIndex === 2) signalBrowser.processingEditor.close()
                }
                background: Rectangle { color: signalBrowser.panelColor }
                Repeater {
                    model: ["信号", "处理", "对象"]
                    delegate: Basic.TabButton {
                        id: browserTab
                        required property string modelData
                        text: modelData
                        implicitHeight: 36
                        padding: 8
                        contentItem: Text {
                            text: browserTab.text
                            color: browserTab.checked ? signalBrowser.accentColor : signalBrowser.secondaryTextColor
                            font.weight: browserTab.checked ? Font.DemiBold : Font.Normal
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: browserTab.hovered
                                ? (signalBrowser.darkTheme ? "#29333d" : "#eef4fa")
                                : signalBrowser.panelColor
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                width: 24
                                height: 2
                                radius: 1
                                visible: browserTab.checked
                                color: signalBrowser.accentColor
                            }
                        }
                    }
                }
            }
            ToolButton {
                objectName: "hideSignalBrowserButton"
                text: "‹"
                Layout.preferredWidth: 24
                Layout.preferredHeight: 28
                onClicked: signalBrowser.hideRequested()
                ToolTip.visible: hovered && !signalBrowser.signalTreeDragging
                ToolTip.text: "隐藏信号树（可从设置恢复）"
            }
        }
        RowLayout {
            visible: signalBrowser.statePickTarget >= 0
            Layout.fillWidth: true
            Label { text: "为子图 " + (signalBrowser.statePickTarget+1) + " 选择状态信号"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Button { text: "取消"; onClicked: { const plot=signalBrowser.statePickTarget; signalBrowser.statePickTarget=-1; signalBrowser.propertiesEditor.showStates(plot) } }
        }
        Button { objectName: "objectManagerButton"; visible: browserTabs.currentIndex === 1; text: "管理处理规则…"; Layout.fillWidth: true; enabled: !signalBrowser.appController.restoringSession && !signalBrowser.appController.imageExporting; onClicked: { if (browserTabs.currentIndex === 1) signalBrowser.processingEditor.open(); else signalBrowser.objectEditor.open() } }
        CheckBox {
            objectName: "logicalResultsToggle"
            visible: browserTabs.currentIndex === 1
            text: "隐藏失败的合并结果"
            checked: signalBrowser.appController.logicalSignalView
            onToggled: signalBrowser.appController.setLogicalSignalView(checked)
            ToolTip.visible: hovered
            ToolTip.text: "隐藏失败的合并输出；原始来源始终保留在信号栏"
        }
        TextField {
            id: signalSearch
            visible: browserTabs.currentIndex !== 2
            objectName: "signalSearch"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.rightMargin: 6
            implicitHeight: 30
            leftPadding: 8
            rightPadding: 30
            background: Rectangle {
                radius: 3
                color: signalBrowser.darkTheme ? "#171c22" : "#ffffff"
                border.color: signalSearch.activeFocus ? signalBrowser.accentColor : signalBrowser.borderColor
            }
            ToolButton {
                objectName: "clearSignalSearchButton"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: parent.height
                text: "×"
                visible: signalSearch.text.length > 0
                onClicked: signalSearch.clear()
                ToolTip.visible: hovered && !signalBrowser.signalTreeDragging
                ToolTip.text: "清除搜索"
            }
            placeholderText: "搜索信号…"
            property bool filtering: false
            property real previousContentY: 0
            onTextChanged: {
                if (!filtering && text.trim().length > 0) {
                    previousContentY = signalList.contentY
                    filtering = true
                }
                searchTimer.restart()
            }
            Timer {
                id: searchTimer
                interval: 150
                onTriggered: {
                    signalBrowser.appController.filterSignals(signalSearch.text)
                    if (signalSearch.filtering && signalSearch.text.trim().length === 0) {
                        signalList.forceLayout()
                        signalList.contentY = Math.max(signalList.originY,
                            Math.min(signalList.originY + Math.max(0, signalList.contentHeight - signalList.height),
                                signalSearch.previousContentY))
                        signalSearch.filtering = false
                    }
                }
            }
        }
        ColumnLayout {
            visible: browserTabs.currentIndex === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.rightMargin: 6
            spacing: 8
            Button {
                objectName: "createSidebarObjectButton"
                text: "新建对象"
                Layout.fillWidth: true
                enabled: !signalBrowser.objectEditor.blocked && !signalBrowser.appController.imageExporting
                onClicked: {
                    const id = signalBrowser.appController.addDataObject("aircraft")
                    if (id.length > 0) signalBrowser.editObject(id)
                    else signalBrowser.objectEditor.errorText = signalBrowser.appController.status
                }
            }
            TextField {
                id: objectSearch
                objectName: "objectSearch"
                Layout.fillWidth: true
                implicitHeight: 30
                leftPadding: 8
                rightPadding: 30
                placeholderText: "搜索对象…"
                background: Rectangle {
                    radius: 3
                    color: signalBrowser.darkTheme ? "#171c22" : "#ffffff"
                    border.color: objectSearch.activeFocus ? signalBrowser.accentColor : signalBrowser.borderColor
                }
                ToolButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28; height: parent.height; text: "×"
                    visible: objectSearch.text.length > 0
                    onClicked: objectSearch.clear()
                }
            }
            ListView {
                id: sidebarObjects
                objectName: "sidebarObjectList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: signalBrowser.matchingObjects
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: Basic.ItemDelegate {
                    id: sidebarObject
                    objectName: "sidebarObjectRow"
                    required property var modelData
                    width: ListView.view.width
                    readonly property var track: signalBrowser.objectTrajectoryState.tracks.find(value => value.objectId === modelData.id) || null
                    implicitHeight: 50
                    enabled: !signalBrowser.objectEditor.blocked && !signalBrowser.appController.imageExporting
                    highlighted: signalBrowser.objectEditor.selectedId === modelData.id
                    onClicked: signalBrowser.editObject(modelData.id)
                    contentItem: RowLayout {
                        spacing: 6
                        CheckBox {
                            objectName: "objectTrajectoryCheck"
                            visible: sidebarObject.modelData.type === "aircraft"
                            checked: !!sidebarObject.track && signalBrowser.objectTrajectoryState.enabled && sidebarObject.track.visible
                            onClicked: {
                                if (!signalBrowser.applyObjectTrajectory(sidebarObject.modelData.id, signalBrowser.appController.activePlotIndex,
                                    sidebarObject.track ? sidebarObject.track.color : "#0072bd", sidebarObject.track ? sidebarObject.track.width : 2, checked))
                                    signalBrowser.objectEditor.errorText = signalBrowser.appController.status
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: "显示或隐藏此飞机在当前子图的航迹"
                        }
                        Column {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 3
                            Text { width: parent.width; text: sidebarObject.modelData.name; color: signalBrowser.treeTextColor; elide: Text.ElideRight; font.weight: Font.Medium }
                            Text {
                                width: parent.width
                                text: (sidebarObject.modelData.type === "aircraft" ? "飞机" : "常规") + " · "
                                    + sidebarObject.modelData.fields.filter(field => field.series >= 0).length + "/" + sidebarObject.modelData.fields.length + " 字段已绑定"
                                color: signalBrowser.secondaryTextColor
                                elide: Text.ElideRight
                                font.pixelSize: 12
                            }
                        }
                        Basic.ToolButton {
                            id: objectLineButton
                            objectName: "objectTrajectoryStyleButton"
                            visible: sidebarObject.modelData.type === "aircraft"
                            implicitWidth: 42; implicitHeight: 30
                            onClicked: signalBrowser.editObjectTrajectory(sidebarObject.modelData.id)
                            contentItem: Item {
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 28
                                    height: sidebarObject.track ? sidebarObject.track.width : 2
                                    color: sidebarObject.track ? sidebarObject.track.color : "#0072bd"
                                }
                            }
                            background: Rectangle { radius: 3; color: objectLineButton.hovered ? (signalBrowser.darkTheme ? "#354451" : "#e0eaf4") : "transparent" }
                            ToolTip.visible: hovered
                            ToolTip.text: "配置此飞机的航迹颜色和线宽"
                        }
                    }
                    background: Rectangle {
                        radius: 3
                        color: sidebarObject.highlighted ? (signalBrowser.darkTheme ? "#293e52" : "#d9eafa")
                            : sidebarObject.hovered ? (signalBrowser.darkTheme ? "#29333d" : "#eef4fa") : "transparent"
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: { signalBrowser.editObject(sidebarObject.modelData.id); objectMenu.popup() }
                    }
                    Menu {
                        id: objectMenu
                        MenuItem { text: "重命名…"; onTriggered: signalBrowser.objectEditor.renameObject(sidebarObject.modelData.id) }
                        MenuItem { text: "复制对象"; onTriggered: signalBrowser.objectEditor.duplicateObject() }
                        MenuItem { text: "删除对象…"; onTriggered: signalBrowser.objectEditor.deleteObject() }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 16
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: signalBrowser.secondaryTextColor
                    visible: sidebarObjects.count === 0
                    text: signalBrowser.appController.objectConfigurations.length === 0
                        ? "尚无对象，点击“新建对象”添加" : "没有匹配的对象"
                }
            }
        }
        Item { visible: browserTabs.currentIndex !== 2; Layout.fillWidth: true; Layout.fillHeight: true
        ListView { id: signalList; enabled: !signalBrowser.appController.imageExporting; anchors.fill: parent; clip: true; model: signalBrowser.appController.signalModel
            objectName: "signalList"
            anchors.rightMargin: 5
            // Overlay navigation must never resize the viewport or thumb.
            boundsBehavior: Flickable.StopAtBounds
            // Sticky hierarchy: the file/table ancestors of the row at the
            // top edge, but only those already scrolled out of view.
            // Recomputed at most once per stickyTimer tick and only when
            // the top row actually changes, so flicking stays cheap.
            property var stickyPath: []
            property int stickyTopRow: -1
            property string stickyKey: ""
            cacheBuffer: 600
            Timer {
                id: stickyTimer
                interval: 40
                onTriggered: signalList.updateStickyPath()
            }
            function scheduleStickyPath() {
                if (!stickyTimer.running) stickyTimer.start()
            }
            function resetStickyPath() {
                stickyTopRow = -1
                stickyKey = ""
                stickyPath = []
                scheduleStickyPath()
            }
            function toggleGroup(group, row) {
                cancelFlick()
                const item = itemAtIndex(row)
                const offset = item ? item.y - contentY : 0
                model.toggleGroup(group)
                forceLayout()
                if (item) {
                    positionViewAtIndex(row, ListView.Beginning)
                    forceLayout()
                    const anchor = itemAtIndex(row)
                    if (anchor) {
                        const end = Math.max(originY, originY + contentHeight - height)
                        contentY = Math.max(originY, Math.min(end, anchor.y - offset))
                    }
                }
                stickyTopRow = -1
                updateStickyPath()
            }
            function navigationPath(row, item) {
                const path = model.ancestorPath(row)
                if (item && item.groupNode && !item.fileNode)
                    path.push({name: item.signalName, group: item.groupName,
                               depth: item.nodeDepth, row: row})
                return path
            }
            function updateStickyPath() {
                if (count === 0 || !model) {
                    stickyTopRow = -1
                    if (stickyKey !== "") { stickyKey = ""; stickyPath = [] }
                    return
                }
                const topRow = indexAt(1, contentY + 1)
                // No delegate under the top edge yet (still flicking);
                // keep the previous header and try again shortly.
                if (topRow < 0) { scheduleStickyPath(); return }
                if (topRow === stickyTopRow) return
                stickyTopRow = topRow
                // A group aligned at the top is covered by the navigation;
                // include it so jumping to pN keeps pN in the path.
                const path = navigationPath(topRow, itemAtIndex(topRow))
                const hidden = []
                let key = ""
                for (let i = 0; i < path.length; ++i)
                    if (path[i].row >= 0 && path[i].row <= topRow) {
                        hidden.push(path[i])
                        key += path[i].row + ":" + path[i].group + "|"
                    }
                if (key !== stickyKey) { stickyKey = key; stickyPath = hidden }
            }
            onContentYChanged: scheduleStickyPath()
            onMovementEnded: updateStickyPath()
            onCountChanged: { stickyTopRow = -1; scheduleStickyPath() }
            onHeightChanged: scheduleStickyPath()
            Connections {
                target: signalList.model
                function onModelReset() { signalBrowser.dragCancelRequested(); signalList.resetStickyPath() }
            }
            delegate: Item {
                id: signalDelegate
                objectName: "signalTreeRow"
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
                readonly property string displayName: groupNode
                    ? signalBrowser.signalGroupTitle(groupName) : signalName
                width: signalList.width
                height: groupNode ? signalBrowser.treeHeaderHeight : 30

                Rectangle {
                    anchors.fill: parent
                    color: signalDelegate.index === signalList.currentIndex
                           ? (signalBrowser.darkTheme ? "#29333d" : "#d9eafa")
                           : signalDelegate.rowHovered
                             ? (signalBrowser.darkTheme ? "#252d35" : "#eef4fa")
                             : signalDelegate.groupNode
                               ? signalBrowser.treeHeaderColor
                               : signalBrowser.panelColor
                }
                HoverHandler { onHoveredChanged: signalDelegate.rowHovered = hovered }
                ToolTip.visible: rowHovered && !signalBrowser.signalTreeDragging
                ToolTip.delay: 600
                ToolTip.text: groupNode ? groupName : groupName + " / " + signalName

                Rectangle {
                    visible: signalDelegate.groupNode
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: signalBrowser.borderColor
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    id: signalRowLayout
                    anchors.rightMargin: 4
                    spacing: 4
                    ToolButton {
                        visible: signalDelegate.groupNode
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        text: signalDelegate.groupExpanded ? "▾" : "▸"
                        palette.button: signalBrowser.panelColor
                        palette.buttonText: signalDelegate.groupNode
                                            ? signalBrowser.treeHeaderTextColor
                                            : signalBrowser.treeTextColor
                        onClicked: signalList.toggleGroup(signalDelegate.groupName, signalDelegate.index)
                    }
                    CheckBox {
                        id: signalCheck
                        objectName: "signalTreeCheck"
                        readonly property int sourceRow: signalDelegate.signalIndex
                        visible: !signalDelegate.groupNode
                        checked: signalBrowser.processingEditor.choosingRuleSource ? signalBrowser.processingEditor.ruleSourceChecked(signalDelegate.signalIndex) : browserTabs.currentIndex === 1 ? signalDelegate.signalChecked : signalBrowser.objectEditor.visible ? signalBrowser.objectEditor.sourceChecked(signalDelegate.signalIndex) : signalBrowser.activeTrajectoryObject ? signalBrowser.activeTrajectoryObject.fields.some(field => field.series === signalDelegate.signalIndex) : signalDelegate.signalChecked
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        palette.window: signalBrowser.panelColor
                        palette.base: signalBrowser.panelColor
                        palette.text: signalBrowser.treeTextColor
                        palette.buttonText: signalBrowser.treeTextColor
                        palette.highlight: signalBrowser.accentColor
                        palette.highlightedText: "#ffffff"
                        onClicked: {
                            signalList.currentIndex = signalDelegate.index
                            if (signalBrowser.statePickTarget >= 0) signalBrowser.addPickedState(signalDelegate.signalIndex)
                            else if (signalBrowser.processingEditor.choosingRuleSource) signalBrowser.processingEditor.selectRuleSource(signalDelegate.signalIndex)
                            else if (browserTabs.currentIndex === 1) {
                                if (signalBrowser.appController.trajectoryState(signalBrowser.appController.activePlotIndex).enabled) signalBrowser.processingEditor.open()
                                else signalBrowser.appController.toggleSignal(signalDelegate.signalIndex)
                            }
                            else if (signalBrowser.objectEditor.visible) signalBrowser.objectEditor.selectSource(signalDelegate.signalIndex)
                            else if (signalBrowser.appController.trajectoryState(signalBrowser.appController.activePlotIndex).enabled) {
                                if (signalBrowser.activeTrajectoryObject) signalBrowser.objectEditor.selectedId = signalBrowser.activeTrajectoryObject.id
                                signalBrowser.objectEditor.open()
                                signalBrowser.objectEditor.selectSource(signalDelegate.signalIndex)
                            }
                            else signalBrowser.appController.toggleSignal(signalDelegate.signalIndex)
                            signalBrowser.followSignalProperties(signalDelegate.signalIndex)
                        }
                    }
                    Label {
                        id: signalNameLabel
                        objectName: "signalTreeName"
                        text: signalBrowser.highlightedSearchText(signalDelegate.displayName)
                        textFormat: Text.StyledText
                        font.weight: Font.Normal
                        color: signalDelegate.groupNode ? signalBrowser.treeHeaderTextColor : signalBrowser.treeTextColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        MouseArea {
                            id: signalNameMouse
                            parent: signalDelegate.groupNode ? signalNameLabel : signalDelegate
                            anchors.fill: parent
                            anchors.leftMargin: signalDelegate.groupNode ? 0
                                : signalRowLayout.x + signalCheck.x + signalCheck.width
                            z: 1
                            property point pressPoint
                            property point namePressPoint
                            property bool moving: false
                            property bool canceledDrag: false
                            property int gestureRevision: -1
                            preventStealing: !signalDelegate.groupNode
                            cursorShape: moving ? Qt.BlankCursor : Qt.ArrowCursor
                            onPressed: mouse => {
                                pressPoint = Qt.point(mouse.x, mouse.y)
                                namePressPoint = mapToItem(signalNameLabel, mouse.x, mouse.y)
                                moving = false
                                canceledDrag = false
                                gestureRevision = signalBrowser.signalDragRevision
                            }
                            onPositionChanged: mouse => {
                                if (signalDelegate.groupNode || !(pressedButtons & Qt.LeftButton) || canceledDrag
                                    || gestureRevision !== signalBrowser.signalDragRevision) return
                                if (!moving && Math.hypot(mouse.x - pressPoint.x, mouse.y - pressPoint.y) < 8) return
                                moving = true
                                const point = mapToItem(signalNameLabel, mouse.x, mouse.y)
                                signalBrowser.dragUpdateRequested(signalNameLabel, point.x, point.y,
                                    signalDelegate, namePressPoint)
                            }
                            onReleased: {
                                if (moving && !canceledDrag) signalBrowser.dragFinishRequested()
                                canceledDrag = canceledDrag || moving
                                moving = false
                            }
                            onCanceled: { canceledDrag = true; moving = false; signalBrowser.dragCancelRequested() }
                            Component.onDestruction: { if (moving) signalBrowser.dragCancelRequested() }
                            onClicked: mouse => {
                                if (moving || canceledDrag) return
                                signalList.currentIndex = signalDelegate.index
                                if (signalDelegate.groupNode)
                                    signalList.toggleGroup(signalDelegate.groupName, signalDelegate.index)
                                else if (signalBrowser.statePickTarget >= 0) signalBrowser.addPickedState(signalDelegate.signalIndex)
                                else if (signalBrowser.processingEditor.choosingRuleSource)
                                    signalBrowser.processingEditor.selectRuleSource(signalDelegate.signalIndex)
                                else if (signalBrowser.propertiesEditor && signalBrowser.propertiesEditor.visible && signalBrowser.propertiesEditor.mode !== "states")
                                    signalBrowser.followSignalProperties(signalDelegate.signalIndex)
                                else if (mapToItem(penPreview, mouse.x, mouse.y).x >= 0)
                                    signalBrowser.editSignalRequested(signalDelegate.signalIndex)
                            }
                        }
                    }
                    ToolButton {
                        visible: signalDelegate.fileNode && browserTabs.currentIndex !== 1
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 24
                        text: "×"
                        palette.button: signalBrowser.panelColor
                        palette.buttonText: signalBrowser.treeTextColor
                        onClicked: signalBrowser.removeFileRequested(
                                       signalDelegate.groupName)
                        ToolTip.visible: hovered && !signalBrowser.signalTreeDragging
                        ToolTip.text: "移除文件"
                    }
                    // Scene-graph stroke instead of a Canvas: no per-row
                    // FBO/threaded repaint, so flicking stays smooth and
                    // rows never flash blank while a paint is pending.
                    Item {
                        id: penPreview
                        visible: !signalDelegate.groupNode
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 24
                        Shape {
                            anchors.fill: parent
                            ShapePath {
                                strokeColor: signalDelegate.signalColor
                                strokeWidth: signalDelegate.signalWidth
                                fillColor: "transparent"
                                capStyle: ShapePath.FlatCap
                                strokeStyle: signalDelegate.signalLineStyle >= 2 && signalDelegate.signalLineStyle <= 5
                                             ? ShapePath.DashLine : ShapePath.SolidLine
                                // Dash lengths are in units of strokeWidth.
                                dashPattern: {
                                    const w = Math.max(1, signalDelegate.signalWidth)
                                    switch (signalDelegate.signalLineStyle) {
                                    case 2: return [8 / w, 4 / w]
                                    case 3: return [2 / w, 4 / w]
                                    case 4: return [8 / w, 4 / w, 2 / w, 4 / w]
                                    case 5: return [8 / w, 4 / w, 2 / w, 4 / w, 2 / w, 4 / w]
                                    default: return [4, 2]
                                    }
                                }
                                startX: 3; startY: penPreview.height / 2
                                PathLine { x: penPreview.width - 3; y: penPreview.height / 2 }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                signalList.currentIndex = signalDelegate.index
                                signalBrowser.editSignalRequested(signalDelegate.signalIndex)
                            }
                        }
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        if (browserTabs.currentIndex === 1 && signalDelegate.groupNode) return
                        signalList.currentIndex = signalDelegate.index
                        if (signalDelegate.fileNode) {
                            signalBrowser.contextRequested(2, -1, signalDelegate.groupName)
                        } else if (signalDelegate.groupNode) {
                            signalBrowser.contextRequested(1, -1, signalDelegate.groupName)
                        } else {
                            signalBrowser.followSignalProperties(signalDelegate.signalIndex)
                            signalBrowser.contextRequested(0, signalDelegate.signalIndex, "")
                        }
                    }
                }
            }
            ScrollBar.vertical: Basic.ScrollBar {
                id: signalScrollBar
                objectName: "signalScrollBar"
                parent: signalList.parent
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: 5
                padding: 0
                minimumSize: 0.05
                policy: ScrollBar.AsNeeded
                contentItem: Rectangle {
                    implicitWidth: 5
                    radius: 2
                    color: signalScrollBar.pressed ? signalBrowser.accentColor
                           : signalScrollBar.hovered ? (signalBrowser.darkTheme ? "#a4a4a4" : "#929292")
                           : (signalBrowser.darkTheme ? "#777777" : "#b8b8b8")
                }
                background: null
            }
        }
        Rectangle {
            id: stickyHeader
            objectName: "signalStickyHeader"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.top: parent.top
            height: signalBrowser.treeHeaderHeight
            visible: signalList.stickyPath.length > 0
            color: signalBrowser.treeHeaderColor
            z: 2
            readonly property string pathText: signalList.stickyPath.map(entry => entry.name).join(" › ")
            function navigate(index, collapse) {
                const entry = signalList.stickyPath[index]
                if (!entry) return
                if (collapse) signalList.toggleGroup(entry.group, entry.row)
                signalList.positionViewAtIndex(Math.min(entry.row, signalList.count - 1), ListView.Beginning)
                signalList.currentIndex = Math.min(entry.row, signalList.count - 1)
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: signalBrowser.darkTheme ? "#3b4652" : "#d7dfe8"
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                preventStealing: true
            }
            RowLayout {
                id: breadcrumb
                objectName: "signalBreadcrumb"
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 0
                Repeater {
                    model: signalList.stickyPath
                    delegate: RowLayout {
                        id: breadcrumbSegment
                        required property var modelData
                        required property int index
                        readonly property real segmentWidth: index === 0 ? implicitWidth
                            : Math.min(implicitWidth, Math.max(20,
                                (breadcrumb.width - 40) / Math.max(1, signalList.stickyPath.length - 1)))
                        Layout.fillWidth: index === 0
                        Layout.preferredWidth: segmentWidth
                        Layout.maximumWidth: segmentWidth
                        Layout.minimumWidth: index === 0 ? Math.min(implicitWidth, 40) : segmentWidth
                        spacing: 0
                        Label {
                            objectName: "signalBreadcrumbSegment"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: signalBrowser.highlightedSearchText(breadcrumbSegment.modelData.name)
                            textFormat: Text.StyledText
                            color: signalBrowser.treeHeaderTextColor
                            font.weight: Font.Normal
                            elide: Text.ElideMiddle
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                preventStealing: true
                                onClicked: stickyHeader.navigate(breadcrumbSegment.index, false)
                                onDoubleClicked: stickyHeader.navigate(breadcrumbSegment.index, true)
                            }
                        }
                        Label {
                            id: separator
                            visible: breadcrumbSegment.index < signalList.stickyPath.length - 1
                            text: visible ? " › " : ""
                            color: signalBrowser.treeHeaderTextColor
                        }
                    }
                }
                Item { Layout.fillWidth: true; Layout.preferredWidth: 0 }
            }
            HoverHandler { id: stickyHover }
            ToolTip.visible: stickyHover.hovered && !signalBrowser.signalTreeDragging
            ToolTip.delay: 600
            ToolTip.text: pathText + "\n单击路径回到该层级，双击折叠"
        }
        }
        Label {
            Layout.fillWidth: true
            text: signalBrowser.appController.status
            color: signalBrowser.treeTextColor
            font.pixelSize: 11
            elide: Text.ElideRight
            ToolTip.visible: statusHover.hovered && !signalBrowser.signalTreeDragging
            ToolTip.text: text
            HoverHandler { id: statusHover }
        }
    }
}
