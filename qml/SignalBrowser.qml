pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Shapes
import DataInspector
Rectangle {
    id: signalBrowser
    required property AppController appController
    required property ObjectManager objectEditor
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
    readonly property var activeTrajectoryObject: {
        const revision = signalBrowser.appController.plotStateRevision
        const state = signalBrowser.appController.trajectoryState(signalBrowser.appController.activePlotIndex)
        return state.enabled ? signalBrowser.appController.dataObjects.find(object => object.id === state.objectId) || null : null
    }
    signal dragCancelRequested()
    signal dragFinishRequested()
    signal dragUpdateRequested(Item source, real x, real y, var rowItem, point pressPoint)
    signal editSignalRequested(int row)
    signal removeFileRequested(string group)
    signal contextRequested(int kind, int row, string group)
    signal hideRequested()
    function clearSearch() {
        signalSearch.clear()
        searchTimer.stop()
        signalSearch.filtering = false
    }
    function revealSignal(row) {
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
        Button { objectName: "objectManagerButton"; text: "对象与派生数据…"; Layout.fillWidth: true; enabled: !signalBrowser.appController.restoringSession && !signalBrowser.appController.imageExporting; onClicked: signalBrowser.objectEditor.open() }
        RowLayout { Layout.fillWidth: true; spacing: 4
            TextField {
                id: signalSearch
                objectName: "signalSearch"
                Layout.fillWidth: true
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
            ToolButton { text: "×"; enabled: signalSearch.text.length > 0; onClicked: signalSearch.clear(); ToolTip.visible: hovered && !signalBrowser.signalTreeDragging; ToolTip.text: "清除搜索" }
            ToolButton { text: "‹"; onClicked: signalBrowser.hideRequested(); ToolTip.visible: hovered && !signalBrowser.signalTreeDragging; ToolTip.text: "隐藏信号树（可从设置恢复）" }
        }
        Item { Layout.fillWidth: true; Layout.fillHeight: true
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
                        checked: signalBrowser.objectEditor.visible ? signalBrowser.objectEditor.sourceChecked(signalDelegate.signalIndex) : signalBrowser.activeTrajectoryObject ? signalBrowser.activeTrajectoryObject.fields.some(field => field.series === signalDelegate.signalIndex) : signalDelegate.signalChecked
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
                            if (signalBrowser.objectEditor.visible) signalBrowser.objectEditor.selectSource(signalDelegate.signalIndex)
                            else if (signalBrowser.appController.trajectoryState(signalBrowser.appController.activePlotIndex).enabled) {
                                if (signalBrowser.activeTrajectoryObject) signalBrowser.objectEditor.selectedId = signalBrowser.activeTrajectoryObject.id
                                signalBrowser.objectEditor.open()
                                signalBrowser.objectEditor.selectSource(signalDelegate.signalIndex)
                            }
                            else signalBrowser.appController.toggleSignal(signalDelegate.signalIndex)
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
                                else if (mapToItem(penPreview, mouse.x, mouse.y).x >= 0)
                                    signalBrowser.editSignalRequested(signalDelegate.signalIndex)
                            }
                        }
                    }
                    ToolButton {
                        visible: signalDelegate.fileNode
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
                        signalList.currentIndex = signalDelegate.index
                        if (signalDelegate.fileNode) {
                            signalBrowser.contextRequested(2, -1, signalDelegate.groupName)
                        } else if (signalDelegate.groupNode) {
                            signalBrowser.contextRequested(1, -1, signalDelegate.groupName)
                        } else {
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
