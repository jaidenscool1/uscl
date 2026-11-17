import QtQuick

// An inner XMB screen (kodi-xmb gen_menus.py settings page, 1920x1080 stage): the breadcrumb column on the
// left (category icon above, the opened item with an arrow below) and the page's rows in an XMB column,
// values in a grey column on the right, the focused row's help under its name.
Item {
    id: page
    property string theme: "ps3"
    property string heading: ""
    property string presentation: ""
    property int filterIndex: 0
    property bool filterFocus: false
    property int libraryNavIndex: 0
    readonly property var libraryTools: [{key:"add",title:qsTr("Add Games")},{key:"updates",title:qsTr("Game Updates")},{key:"help",title:qsTr("How to Play")}]
    signal libraryAction(string section)
    property var settingsSections: []
    property int settingsSection: -1
    property bool sectionFocus: false
    property int sectionCursor: 0
    signal settingsSectionChosen(int section)
    onSettingsSectionChanged: { sectionCursor = Math.max(0,settingsSection); sectionFocus = false }
    readonly property var filters: [qsTr("All Games")].concat(Array.from(new Set(rows.filter(function(e) { return !!e.console }).map(function(e) { return e.console }))))
    readonly property var displayRows: modernSoftware && filterIndex > 0 ? rows.filter(function(e) { return e.console === filters[filterIndex] }) : rows
    readonly property int gridColumns: theme === "x360classic" ? 1 : theme === "switch" || theme === "switch2" ? 4 : 5
    onPresentationChanged: { filterIndex = 0; libraryNavIndex = 0; filterFocus = false }
    onFiltersChanged: { if (filterIndex >= filters.length) filterIndex = 0 }
    function selectFilter(index) { filterIndex = Math.max(0, Math.min(filters.length-1,index)); libraryNavIndex = filterIndex; current = 0; sound("cursor") }
    readonly property bool modernSoftware: theme !== "ps3" && (presentation === "library" || rows.some(function(row) { return !!row.console }))
    property string catIcon: ""
    property string parentIcon: ""
    property var rows: []           // [{ title, value, help, icon, disabled, action }]
    property int current: 0
    property bool fromHome: true
    signal sound(string name)
    signal back()
    signal showOptions(var entry)   // △ on a row with its own options (a game in its console's list)

    readonly property int colLeft: 460      // 200 + SHIFT
    readonly property int focTop: 440
    readonly property int rowH: 84
    readonly property int focH: 150
    readonly property var cur: displayRows[current]

    onDisplayRowsChanged: { if (current >= displayRows.length) current = Math.max(0, displayRows.length - 1) }

    function input(b) {
        if ((theme === "switch" || theme === "switch2") && settingsSection >= 0) {
            if (sectionFocus) {
                if (b === "up" || b === "down") { sectionCursor = Math.max(0,Math.min(settingsSections.length-1,sectionCursor+(b === "down" ? 1 : -1))); sound("cursor") }
                else if (b === "right" || b === "accept") settingsSectionChosen(sectionCursor)
                else if (b === "back") { sound("back"); back() }
                return
            }
            if (b === "left") { sectionFocus = true; return }
        }
        if (modernSoftware) {
            if (filterFocus) {
                if (b === "up" || b === "down") {
                    libraryNavIndex = Math.max(0,Math.min(filters.length+libraryTools.length-1,libraryNavIndex+(b === "down" ? 1 : -1)))
                    if (libraryNavIndex < filters.length) { filterIndex = libraryNavIndex; current = 0 }
                    sound("cursor")
                } else if (b === "accept" && libraryNavIndex >= filters.length) libraryAction(libraryTools[libraryNavIndex-filters.length].key)
                else if (b === "right" || b === "accept") filterFocus = false
                else if (b === "back") { sound("back"); back() }
                return
            }
            if (["left", "right", "up", "down", "l1", "r1"].indexOf(b) >= 0) {
                if (b === "left" && current % gridColumns === 0) { filterFocus = true; libraryNavIndex = filterIndex; return }
                var delta = b === "left" ? -1 : b === "right" ? 1 : b === "up" ? -gridColumns : b === "down" ? gridColumns : b === "l1" ? -gridColumns*2 : gridColumns*2
                var next = current+delta
                if (next >= 0 && next < displayRows.length) { current = next; sound("cursor") }
                return
            }
        }
        if (b === "info" && cur && cur.info) { sound("select"); cur.info(); return }

        if ((b === "back" || b === "left")) { sound("back"); back() }
        else if (b === "up" && current > 0) { current--; sound("cursor") }
        else if (b === "down" && current < displayRows.length - 1) { current++; sound("cursor") }
        else if (b === "l1" && current > 0) { current = Math.max(0, current - 5); sound("cursor") }
        else if (b === "r1" && current < displayRows.length - 1) { current = Math.min(displayRows.length - 1, current + 5); sound("cursor") }
        else if (b === "options" && cur && cur.options) { sound("select"); showOptions(cur) }
        else if ((b === "accept" || b === "right" || b === "options") && cur) {
            if (cur.disabled) { if (b === "accept") sound("error"); return }
            if (b === "right" && !cur.choices) return
            sound("select")
            if (cur.action) cur.action()
        }
    }

    // ---- breadcrumb ----
    Item {
        id: crumb
        visible: page.theme === "ps3"
        width: page.width; height: page.height
        transform: Translate { id: crumbShift }
        Image {
            id: catImg
            x: 225; y: 228; width: 104; height: 104
            source: page.catIcon; opacity: 0.85; mipmap: true; fillMode: Image.PreserveAspectFit
        }
        Image {
            id: parentImg
            x: 220; y: 440; width: 110; height: 110
            source: page.parentIcon; opacity: 0.85; mipmap: true; fillMode: Image.PreserveAspectFit
        }
        Image { id: arrow; x: 356; y: 479; width: 24; height: 32; source: "qrc:/qt/qml/Uscl/xmb/ui/arrow_left.png"; opacity: 0.75 }
    }

    // ---- rows ----
    Item {
        id: list
        visible: page.theme === "ps3"
        width: page.width; height: page.height
        transform: Translate { id: listShift }

        Image {
            id: halo
            x: page.colLeft + 60; y: page.focTop - 60; width: 240; height: 240
            source: "qrc:/qt/qml/Uscl/xmb/halo.png"
            visible: page.rows.length > 0
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                running: page.visible && page.theme === "ps3" && halo.Window.active // see Xmb.qml: no redraws while a game has focus
                NumberAnimation { from: 0.09; to: 0.25; duration: 1700; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.09; duration: 1700; easing.type: Easing.InOutSine }
            }
        }
        Repeater {
            model: page.rows
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool sel: index === page.current
                x: page.colLeft
                y: sel ? page.focTop
                   : index < page.current ? page.focTop - (page.current - index) * page.rowH
                   : page.focTop + page.focH + (index - page.current - 1) * page.rowH
                width: page.width - page.colLeft; height: sel ? page.focH : page.rowH
                visible: y > -page.rowH && y < page.height
                opacity: modelData.disabled ? 0.45 : 1
                Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutSine } }

                MouseArea {
                    anchors.fill: parent; enabled: page.theme === "ps3"
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        var wasSelected = row.sel
                        page.current = index
                        if (mouse.button === Qt.RightButton) page.input("options")
                        else if (wasSelected) page.input("accept")
                        else page.sound("cursor")
                    }
                }

                // wide: a PS3 game icon (320x176), right-aligned to the same edge so titles line up
                Image {
                    readonly property real h: row.sel ? 58 : 42
                    width: row.modelData.wide ? h * 1.82 : h; height: h
                    x: (row.sel ? 208 : 200) - width; y: row.sel ? 32 : 21
                    source: row.modelData.icon || "qrc:/qt/qml/Uscl/xmb/ui/setting_dot.png"
                    opacity: row.sel ? 1 : 0.63; mipmap: true; fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
                Text {
                    x: 230; y: row.sel ? 32 : 0
                    width: 560; height: row.sel ? 58 : page.rowH
                    verticalAlignment: Text.AlignVCenter
                    text: row.modelData.title
                    color: row.sel ? "white" : "#a8ffffff"
                    font.pixelSize: row.sel ? 40 : 29
                    elide: Text.ElideRight
                    style: Text.Raised; styleColor: row.sel ? "#90000000" : "#70000000"
                }
                Text {
                    x: 820; y: row.sel ? 32 : 0
                    width: 520; height: row.sel ? 58 : page.rowH
                    verticalAlignment: Text.AlignVCenter
                    text: row.modelData.value || ""
                    color: row.sel ? "#f0ffffff" : "#80ffffff"
                    font.pixelSize: row.sel ? 29 : 25
                    elide: Text.ElideRight
                    style: Text.Raised; styleColor: "#70000000"
                }
                Text {
                    visible: row.sel && !!row.modelData.help
                    x: 232; y: 92; width: 1100; height: 38
                    verticalAlignment: Text.AlignVCenter
                    text: row.modelData.help || ""
                    color: "#a0ffffff"; font.pixelSize: 25
                    elide: Text.ElideRight
                    style: Text.Raised; styleColor: "#70000000"
                }
            }
        }
    }

    ConsolePage {
        id: modernPage
        anchors.fill: parent
        transform: Translate { id: modernShift }
        visible: page.theme !== "ps3"
        controller: page
        theme: page.theme
        heading: page.heading
    }

    // ---- open / close: rows slide in from the right; from the crossbar the breadcrumb shrinks into place ----
    function open() {
        enter.stop()
        modernEnter.stop()
        if (theme === "ps3") enter.restart()
        else {
            page.opacity = 1
            modernEnter.restart()
        }
    }
    ParallelAnimation {
        id: modernEnter
        NumberAnimation { target: modernPage; property: "opacity"; from: 0; to: 1; duration: 180 }
        NumberAnimation { target: modernShift; property: "y"; from: page.theme === "ps4" || page.theme === "ps5" ? 24 : 12; to: 0; duration: 200; easing.type: Easing.OutCubic }
    }
    ParallelAnimation {
        id: enter
        NumberAnimation { target: page; property: "opacity"; from: 0; to: 1; duration: 260 }
        NumberAnimation { target: listShift; property: "x"; from: 120; to: 0; duration: 260; easing.type: Easing.OutSine }
        NumberAnimation { target: crumbShift; property: "x"; from: page.fromHome ? 145 : 60; to: 0; duration: 300; easing.type: Easing.OutSine }
        NumberAnimation { target: catImg; property: "scale"; from: page.fromHome ? 1.44 : 1; to: 1; duration: 300; easing.type: Easing.OutSine }
        NumberAnimation { target: parentImg; property: "scale"; from: page.fromHome ? 1.36 : 1; to: 1; duration: 300; easing.type: Easing.OutSine }
        SequentialAnimation {
            PropertyAction { target: arrow; property: "opacity"; value: 0 }
            PauseAnimation { duration: 180 }
            NumberAnimation { target: arrow; property: "opacity"; to: 0.75; duration: 200 }
        }
    }
}
