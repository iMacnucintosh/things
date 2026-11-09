import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "components"

ApplicationWindow {
    id: win
    visible: true
    width: 1180
    height: 760
    minimumWidth: 880
    minimumHeight: 520
    title: Things.mode === "installed" ? qsTr("Instalados — Paquetes") : qsTr("Paquetes")
    color: Theme.surface
    font.family: Theme.uiFamily

    readonly property int railWidth: 232
    readonly property bool installedMode: Things.mode === "installed"
    property string selKey: ""
    property bool detailOpen: false
    // source:id → true while queued/running (drives row spinners)
    property var busyMap: ({})

    function keyOf(source, id) { return source + ":" + id }

    function openDetail(source, id, repo) {
        selKey = keyOf(source, id)
        detailOpen = true
        Things.loadDetail(source, id, repo)
    }
    function closeDetail() {
        detailOpen = false
        selKey = ""
        Things.closeDetail()
    }

    function requestAction(kind, source, id, name, repo) {
        confirm.kind = kind
        confirm.source = source
        confirm.pkgId = id
        confirm.pkgName = name
        confirm.repo = repo
        confirm.loading = true
        confirm.ok = true
        confirm.error = ""
        confirm.note = ""
        confirm.packages = []
        confirm.total = 0
        confirm.open = true
        Ops.prepare(kind, source, id, repo)
    }

    Connections {
        target: Ops
        function onPrepared(r) {
            if (!confirm.open || r.id !== confirm.pkgId || r.kind !== confirm.kind)
                return
            confirm.loading = false
            confirm.ok = r.ok
            confirm.error = r.error
            confirm.note = r.note
            confirm.packages = r.packages
            confirm.total = r.total
        }
        function onQueueChanged() {
            const m = {}
            const q = Ops.queue
            for (let i = 0; i < q.length; ++i)
                if (q[i].status === "queued" || q[i].status === "running" || q[i].status === "waiting")
                    m[q[i].source + ":" + q[i].pkg] = true
            win.busyMap = m
        }
    }

    Connections {
        target: Things
        function onQueryChanged() {
            if (search.text !== Things.query)
                search.text = Things.query
        }
    }

    // Shortcuts ----------------------------------------------------------------
    Shortcut {
        sequence: "Ctrl+L"
        onActivated: search.focusAll()
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: search.focusAll()
    }
    Shortcut {
        sequence: "Escape"
        enabled: !confirm.open && !Ops.awaitingPassword
        onActivated: {
            if (win.detailOpen)
                win.closeDetail()
            else if (search.text.length > 0)
                search.text = ""
            else
                list.focus = false
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: confirm.open
        onActivated: confirm.open = false
    }
    Shortcut {
        sequence: "Ctrl+1"
        onActivated: Things.mode = "search"
    }
    Shortcut {
        sequence: "Ctrl+2"
        onActivated: Things.mode = "installed"
    }
    Shortcut {
        sequence: "F5"
        onActivated: { Things.refreshInstalled(); Things.retrySearch() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Toolbar ────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: Theme.surfaceContainer
            z: 30

            RowLayout {
                anchors.fill: parent
                anchors.rightMargin: 12
                spacing: 0

                // Same width as the rail so the search bar meets the content edge.
                Item {
                    Layout.preferredWidth: win.railWidth
                    Layout.fillHeight: true
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 18
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 34
                            height: 34
                            source: "qrc:/data/things.svg"
                            sourceSize: Qt.size(68, 68)
                            smooth: true
                        }
                        StyledText {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Paquetes")
                            font.pixelSize: 18
                        }
                    }
                }

                SearchBar {
                    id: search
                    Layout.fillWidth: true
                    Layout.maximumWidth: 760
                    Layout.preferredHeight: 48
                    Layout.leftMargin: 8
                    placeholder: win.installedMode ? qsTr("Filtrar paquetes instalados…  (Ctrl+L)")
                                                   : qsTr("Buscar en pacman, AUR y Flatpak…  (Ctrl+L)")
                    busy: !win.installedMode && Things.searching
                    onTextChanged: if (Things.query !== text) Things.query = text
                    onDownPressed: { list.forceActiveFocus(); if (list.currentIndex < 0 && list.count > 0) list.currentIndex = 0 }
                    onAccepted: { list.forceActiveFocus(); if (list.count > 0 && list.currentIndex < 0) list.currentIndex = 0 }
                    Component.onCompleted: {
                        text = Things.query
                        focusField()
                    }
                }
                Item { Layout.fillWidth: true }

                IconButton {
                    icon: "refresh"
                    onClicked: { Things.refreshInstalled(); Things.retrySearch() }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Rail ───────────────────────────────────────────────────────
            Rectangle {
                Layout.preferredWidth: win.railWidth
                Layout.fillHeight: true
                color: Theme.surfaceContainerLow
                clip: true

                Item {
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.topMargin: 12

                    Rectangle {
                        id: selection
                        width: parent.width
                        height: 44
                        radius: selPressed ? 12 : height / 2
                        color: Theme.primaryContainer
                        y: win.installedMode ? 46 : 0
                        property bool selPressed: win.installedMode ? rInst.pressed : rSearch.pressed
                        scale: selPressed ? 1.04 : 1
                        z: 1
                        Behavior on y { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                        Behavior on radius { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                    }
                    Rectangle {
                        width: parent.width
                        height: 44
                        radius: height / 2
                        color: Theme.surfaceContainerHigh
                        y: rSearch.hovered && !rSearch.active ? 0 : (rInst.hovered && !rInst.active ? 46 : 0)
                        opacity: (rSearch.hovered && !rSearch.active) || (rInst.hovered && !rInst.active) ? 1 : 0
                        z: 0
                        Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                        Behavior on opacity { NumberAnimation { duration: 100 } }
                    }

                    Column {
                        width: parent.width
                        spacing: 2
                        z: 2
                        RailItem {
                            id: rSearch
                            width: parent.width
                            symbol: "travel_explore"
                            label: qsTr("Explorar")
                            active: !win.installedMode
                            onClicked: Things.mode = "search"
                        }
                        RailItem {
                            id: rInst
                            width: parent.width
                            symbol: "inventory_2"
                            label: qsTr("Instalados")
                            trailing: Things.installedTotal > 0 ? Things.installedTotal : ""
                            active: win.installedMode
                            onClicked: Things.mode = "installed"
                        }
                    }
                }
            }

            // ── Content ────────────────────────────────────────────────────
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle { anchors.fill: parent; color: Theme.surfaceContainerLow }

                Rectangle {
                    id: card
                    anchors.fill: parent
                    anchors.margins: 12
                    anchors.leftMargin: 8
                    radius: 10 // match Hyprland decoration.rounding
                    color: Theme.surface
                    clip: true

                    readonly property real detailWidth: Math.round(Math.min(420, Math.max(330, width * 0.4)))

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

                        // List column
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 0

                            // Filter chips
                            Flow {
                                id: filters
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                Layout.topMargin: 14
                                Layout.bottomMargin: 10
                                spacing: 8

                                Chip {
                                    label: qsTr("Todos")
                                    count: Things.counts.all
                                    selected: Things.sourceFilter === "all"
                                    onClicked: Things.sourceFilter = "all"
                                }
                                Chip {
                                    label: "pacman"
                                    source: "pacman"
                                    count: Things.counts.pacman
                                    selected: Things.sourceFilter === "pacman"
                                    onClicked: Things.sourceFilter = "pacman"
                                }
                                Chip {
                                    label: "AUR"
                                    source: "aur"
                                    count: Things.counts.aur
                                    selected: Things.sourceFilter === "aur"
                                    onClicked: Things.sourceFilter = "aur"
                                }
                                Chip {
                                    label: "Flatpak"
                                    source: "flatpak"
                                    count: Things.counts.flatpak
                                    selected: Things.sourceFilter === "flatpak"
                                    onClicked: Things.sourceFilter = "flatpak"
                                }
                                Item { width: 10; height: 1 }
                                // State filters (search) / kind filters (installed)
                                Chip {
                                    visible: !win.installedMode
                                    label: qsTr("Cualquiera")
                                    selected: Things.stateFilter === "all"
                                    onClicked: Things.stateFilter = "all"
                                }
                                Chip {
                                    visible: !win.installedMode
                                    label: qsTr("Instalados")
                                    icon: "check_circle"
                                    selected: Things.stateFilter === "installed"
                                    onClicked: Things.stateFilter = "installed"
                                }
                                Chip {
                                    visible: !win.installedMode
                                    label: qsTr("Disponibles")
                                    icon: "download"
                                    selected: Things.stateFilter === "available"
                                    onClicked: Things.stateFilter = "available"
                                }
                                Chip {
                                    visible: win.installedMode
                                    label: qsTr("Cualquiera")
                                    selected: Things.kindFilter === "all"
                                    onClicked: Things.kindFilter = "all"
                                }
                                Chip {
                                    visible: win.installedMode
                                    label: qsTr("Explícitos")
                                    selected: Things.kindFilter === "explicit"
                                    onClicked: Things.kindFilter = "explicit"
                                }
                                Chip {
                                    visible: win.installedMode
                                    label: qsTr("Dependencias")
                                    selected: Things.kindFilter === "dep"
                                    onClicked: Things.kindFilter = "dep"
                                }
                                Chip {
                                    visible: win.installedMode
                                    label: qsTr("Huérfanos")
                                    icon: "link_off"
                                    selected: Things.kindFilter === "orphan"
                                    onClicked: Things.kindFilter = "orphan"
                                }
                            }

                            // Notices (AUR offline…)
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                Layout.bottomMargin: 8
                                visible: !win.installedMode && Things.notice.length > 0
                                implicitHeight: noticeText.implicitHeight + 16
                                radius: 12
                                color: Theme.surfaceContainerHigh
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 10
                                    MaterialIcon { icon: "info"; size: 18; colour: Theme.onSurfaceVariant }
                                    StyledText {
                                        id: noticeText
                                        Layout.fillWidth: true
                                        text: Things.notice
                                        color: Theme.onSurfaceVariant
                                        font.pixelSize: 12
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: Theme.outlineVariant
                                opacity: 0.45
                            }

                            // List + states
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                ListView {
                                    id: list
                                    anchors.fill: parent
                                    model: Things.model
                                    clip: true
                                    boundsBehavior: Flickable.StopAtBounds
                                    reuseItems: true
                                    cacheBuffer: 400
                                    currentIndex: -1
                                    keyNavigationEnabled: true
                                    topMargin: 6
                                    bottomMargin: sheet.visible ? sheet.height + 28 : 12
                                    visible: count > 0
                                    activeFocusOnTab: true

                                    ScrollBar.vertical: ScrollBar {
                                        policy: ScrollBar.AsNeeded
                                        implicitWidth: 8
                                        contentItem: Rectangle {
                                            implicitWidth: 4
                                            radius: 2
                                            color: Theme.outline
                                            opacity: parent.active ? 0.6 : 0
                                            Behavior on opacity { NumberAnimation { duration: 150 } }
                                        }
                                        background: null
                                    }

                                    Keys.onReturnPressed: openCurrent()
                                    Keys.onEnterPressed: openCurrent()
                                    Keys.onPressed: (e) => {
                                        if (e.key === Qt.Key_Up && currentIndex <= 0) {
                                            search.focusField()
                                            e.accepted = true
                                        }
                                    }
                                    function openCurrent() {
                                        const it = itemAtIndex(currentIndex)
                                        if (it)
                                            win.openDetail(it.source, it.pkgId, it.repo)
                                    }

                                    delegate: PackageRow {
                                        selected: win.selKey === win.keyOf(source, pkgId)
                                        current: ListView.isCurrentItem && list.activeFocus
                                        busy: !!win.busyMap[win.keyOf(source, pkgId)]
                                        compact: card.width - (win.detailOpen ? card.detailWidth : 0) < 640
                                        onActivated: {
                                            list.currentIndex = index
                                            win.openDetail(source, pkgId, repo)
                                        }
                                        onActionRequested: win.requestAction(installed ? "remove" : "install",
                                                                             source, pkgId, name, repo)
                                    }
                                }

                                // — empty / loading states —
                                EmptyState {
                                    anchors.fill: parent
                                    visible: list.count === 0 && !win.installedMode && Things.query.trim().length < 2
                                    icon: "travel_explore"
                                    title: qsTr("Busca software en un solo sitio")
                                    subtitle: qsTr("Repositorios de Arch, AUR y Flathub a la vez. Escribe al menos dos letras.")
                                }
                                EmptyState {
                                    anchors.fill: parent
                                    visible: list.count === 0 && !win.installedMode && Things.query.trim().length >= 2 && Things.searching
                                    busy: true
                                    title: qsTr("Buscando…")
                                    subtitle: qsTr("Consultando pacman, AUR y Flatpak")
                                }
                                EmptyState {
                                    anchors.fill: parent
                                    visible: list.count === 0 && !win.installedMode && Things.query.trim().length >= 2 && !Things.searching
                                    icon: "search_off"
                                    title: qsTr("Sin resultados para «%1»").arg(Things.query.trim())
                                    subtitle: (Things.sourceFilter !== "all" || Things.stateFilter !== "all")
                                              ? qsTr("Prueba a quitar los filtros de origen o estado.")
                                              : qsTr("Revisa la ortografía o prueba con otro término.")
                                    ActionButton {
                                        visible: Things.sourceFilter !== "all" || Things.stateFilter !== "all"
                                        text: qsTr("Quitar filtros")
                                        tone: "tonal"
                                        onClicked: { Things.sourceFilter = "all"; Things.stateFilter = "all" }
                                    }
                                }
                                EmptyState {
                                    anchors.fill: parent
                                    visible: list.count === 0 && win.installedMode && Things.loadingInstalled
                                    busy: true
                                    title: qsTr("Leyendo paquetes instalados…")
                                }
                                EmptyState {
                                    anchors.fill: parent
                                    visible: list.count === 0 && win.installedMode && !Things.loadingInstalled
                                    icon: "inventory_2"
                                    title: Things.query.trim().length ? qsTr("Ningún instalado coincide con «%1»").arg(Things.query.trim())
                                                                      : qsTr("No hay paquetes en esta vista")
                                    subtitle: Things.kindFilter === "orphan" && !Things.query.length ? qsTr("¡Perfecto! No hay dependencias huérfanas.") : ""
                                }
                            }
                        }

                        // Divider + detail drawer
                        Item {
                            id: detailHost
                            Layout.fillHeight: true
                            property real hostWidth: win.detailOpen ? card.detailWidth : 0
                            Layout.preferredWidth: hostWidth
                            clip: true
                            visible: hostWidth > 1
                            Behavior on hostWidth { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }

                            DetailPanel {
                                x: 0
                                width: card.detailWidth
                                height: parent.height
                                detail: Things.detail
                                loading: Things.detailLoading
                                busy: !!win.busyMap[win.keyOf(Things.detail.source || "", Things.detail.id || "")]
                                onCloseRequested: win.closeDetail()
                                onInstallRequested: win.requestAction("install", Things.detail.source, Things.detail.id,
                                                                      Things.detail.name || Things.detail.id, Things.detail.repo || "")
                                onRemoveRequested: win.requestAction("remove", Things.detail.source, Things.detail.id,
                                                                     Things.detail.name || Things.detail.id, Things.detail.repo || "")
                            }
                        }
                    }

                    // Activity sheet floats over the bottom of the list.
                    OpsSheet {
                        id: sheet
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 14
                        width: parent.width - 28 - (win.detailOpen ? card.detailWidth : 0)
                        z: 20
                        Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                    }
                }
            }
        }
    }

    // ── Developer demo (headless screenshots): things --demo tour --shot-dir DIR ──
    property int demoIdx: 0
    readonly property var demoSteps: Dev.demo === "tour" ? [
        { wait: 3500, run: function () { Dev.take("01-search") } },
        { wait: 200, run: function () { win.openDetail("pacman", "firefox", "extra") } },
        { wait: 1800, run: function () { Dev.take("02-detail") } },
        { wait: 200, run: function () { win.openDetail("flatpak", "org.mozilla.firefox", "flathub") } },
        { wait: 1500, run: function () { Dev.take("03-detail-flatpak") } },
        { wait: 200, run: function () { win.openDetail("aur", "yay", "aur") } },
        { wait: 2500, run: function () { Dev.take("04-detail-aur"); win.closeDetail() } },
        { wait: 200, run: function () { search.text = ""; Things.mode = "installed" } },
        { wait: 2000, run: function () { Dev.take("05-installed") } },
        { wait: 200, run: function () { win.requestAction("remove", "pacman", "blender", "blender", "extra") } },
        { wait: 2000, run: function () { Dev.take("06-confirm-remove"); confirm.open = false } },
        { wait: 300, run: function () { win.requestAction("install", "aur", "yay-bin", "yay-bin", "aur") } },
        { wait: 1500, run: function () { Dev.take("07-confirm-install-aur"); confirm.open = false } },
        { wait: 300, run: function () { Things.mode = "search"; search.text = "blender" } },
        { wait: 4000, run: function () { Dev.take("08-search-blender"); Ops.enqueue("install", "pacman", "gnuplot", "gnuplot", "extra") } },
        { wait: 1500, run: function () { Dev.take("09-ops-running") } },
        { wait: 3500, run: function () { Dev.take("10-ops-done") } },
        { wait: 200, run: function () { Dev.quit() } }
    ] : Dev.demo === "password" ? [
        { wait: 2500, run: function () { Ops.enqueue("install", "pacman", "gnuplot", "gnuplot", "extra") } },
        { wait: 1200, run: function () { Dev.take("pw-1-prompt"); Ops.submitPassword("wrong") } },
        { wait: 1500, run: function () { Dev.take("pw-2-error"); Ops.submitPassword("demo") } },
        { wait: 2200, run: function () { Dev.take("pw-3-running") } },
        { wait: 2500, run: function () { Dev.take("pw-4-done") } },
        { wait: 200, run: function () { Dev.quit() } }
    ] : []

    Timer {
        id: demoTimer
        repeat: false
        running: Dev.enabled && win.demoSteps.length > 0
        interval: win.demoSteps.length ? win.demoSteps[0].wait : 1000
        onTriggered: {
            const st = win.demoSteps[win.demoIdx]
            st.run()
            win.demoIdx++
            if (win.demoIdx < win.demoSteps.length) {
                interval = win.demoSteps[win.demoIdx].wait
                restart()
            }
        }
    }

    ConfirmDialog {
        id: confirm
        onConfirmed: Ops.enqueue(kind, source, pkgId, pkgName, repo)
    }

    PasswordDialog {
        id: passwordDialog
    }
}
