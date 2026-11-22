pragma ComponentBehavior: Bound
import QtQuick

// A player's profile over the menus, in the theme's colours: picture, name and status on top, then two tabs in
// the same layout. USCL: trophies, play time and the latest games (Profile::summary, or a friend's from Steam
// Rich Presence). Steam: their Steam Community profile (Online::loadSteamProfile), shown here instead of in Steam.
// Up/down move through the games (the page scrolls with them), left/right or L1/R1 change tab, ✕ opens what's
// selected, ○ closes. Mouse: wheel scrolls, a click selects, a second click opens.
Item {
    id: card
    property string theme: "ps3"
    property bool open: false
    enabled: open
    property string tab: "uscl"
    property var person: ({})   // {id, name, avatar, status, level, session, me}
    property var uscl: ({})     // Profile::summary, or a friend's profile (Profile::readPresence)
    property var steam: ({})    // Online::steamProfiles[id]
    property string dateFormat: "MM/dd/yy"
    property var gameIcon: function(game) { return "" }   // a USCL game's icon (Main.qml knows the library)
    property int focusIndex: 0  // 0 the tabs, then the game rows, then the buttons
    property int buttonIndex: 0
    signal sound(string name)
    signal openGame(var game)   // ✕ on one of the player's own games: its trophies
    signal join()
    signal openSteam()

    ThemePalette { id: style; theme: card.theme }
    readonly property bool ps3: theme === "ps3"
    readonly property color ink: ps3 ? "white" : style.text
    readonly property color soft: ps3 ? "#b4ffffff" : style.muted
    readonly property color line: ps3 ? "#40ffffff" : style.divider
    readonly property color tile: ps3 ? "#1effffff" : style.lightSwitch || style.metro || style.blades ? "#14000000" : "#1affffff"

    function playTime(seconds) {
        var h = Math.floor(seconds / 3600), m = Math.floor(seconds % 3600 / 60)
        return h ? qsTr("%1 h %2 min").arg(h).arg(m) : qsTr("%1 min").arg(m)
    }
    function dayText(secs) { return secs ? Qt.formatDate(new Date(secs * 1000), dateFormat) : "" }

    readonly property var usclRows: (uscl.games || []).map(function(g) {
        var bits = []
        if (g.total) bits.push(qsTr("Trophies %1 / %2").arg(g.earned).arg(g.total))
        if (g.last) bits.push(qsTr("Last played %1").arg(card.dayText(g.last)))
        return { game: g, icon: card.gameIcon(g), title: g.title, sub: bits.join("  ·  "), value: g.seconds ? card.playTime(g.seconds) : "",
                 progress: g.total ? g.earned / g.total : -1 }
    })
    readonly property var steamRows: (steam.games || []).map(function(g) {
        return { game: g, icon: g.icon || "", title: g.name, sub: g.hours2wk ? qsTr("%1 h in the past 2 weeks").arg(g.hours2wk) : "",
                 value: g.hoursTotal ? qsTr("%1 h on record").arg(g.hoursTotal) : "", progress: -1 }
    })
    readonly property var rows: tab === "uscl" ? usclRows : steamRows
    readonly property var buttons: {
        var b = []
        if (person.session && !person.me) b.push({ key: "join", title: qsTr("Join Session") })
        b.push({ key: "steam", title: qsTr("Open in Steam") }, { key: "close", title: qsTr("Close") })
        return b
    }
    readonly property int buttonsStart: rows.length + 1
    readonly property bool atButtons: focusIndex >= buttonsStart

    function show(p, which) {
        person = p || {}
        tab = which === "steam" ? "steam" : "uscl"
        focusIndex = 0; buttonIndex = 0
        content.contentY = 0
        open = true
    }
    function close() { open = false }
    function setTab(which) {
        if (which === tab) return
        tab = which; focusIndex = 0; content.contentY = 0
        tabSwitch.restart()
        sound("category")
    }
    function reveal() {
        if (focusIndex === 0) { content.contentY = 0; return }
        if (atButtons) { content.contentY = Math.max(0, content.contentHeight - content.height); return }
        column.forceLayout()   // rows just created (the card opened this frame) have no position yet
        var item = rowRepeater.itemAt(focusIndex - 1)
        if (!item) return
        // one assignment: contentY animates, so a second one would start from the old value
        var y = item.mapToItem(column, 0, 0).y, target = content.contentY
        if (y < target) target = y - 16
        else if (y + item.height > target + content.height) target = y + item.height - content.height + 16
        content.contentY = Math.max(0, Math.min(content.contentHeight - content.height, target))
    }
    function move(to) {
        to = Math.max(0, Math.min(buttonsStart, to))
        if (to === focusIndex) return
        focusIndex = to
        if (atButtons) buttonIndex = Math.min(buttonIndex, buttons.length - 1)
        sound("cursor")
        reveal()
    }
    function activate() {
        if (focusIndex === 0) { setTab(tab === "uscl" ? "steam" : "uscl"); return }
        if (atButtons) {
            var key = buttons[buttonIndex].key
            sound(key === "close" ? "back" : "select")
            if (key === "join") { close(); join() }
            else if (key === "steam") openSteam()
            else close()
            return
        }
        if (tab === "uscl" && person.me) { sound("select"); var g = rows[focusIndex - 1].game; close(); openGame(g) }
    }
    function input(b) {
        if (b === "back") { sound("back"); close() }
        else if (b === "l1" || b === "r1") setTab(b === "l1" ? "uscl" : "steam")
        else if (b === "up") move(atButtons ? rows.length : focusIndex - 1)
        else if (b === "down") move(focusIndex + 1)
        else if (b === "left" || b === "right") {
            if (atButtons) {
                var next = Math.max(0, Math.min(buttons.length - 1, buttonIndex + (b === "left" ? -1 : 1)))
                if (next !== buttonIndex) { buttonIndex = next; sound("cursor") }
            } else setTab(b === "left" ? "uscl" : "steam")
        }
        else if (b === "accept") activate()
    }

    anchors.fill: parent
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    Rectangle { anchors.fill: parent; color: "#c8000000" }
    MouseArea { anchors.fill: parent; onClicked: card.close(); onWheel: function(wheel) { wheel.accepted = true } }

    Rectangle {
        id: panel
        x: 260; width: parent.width - 520
        y: 70; height: 940
        scale: card.open ? 1 : .96
        Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        color: card.ps3 ? "#ee0c1424" : style.panel
        radius: style.radius
        border.width: card.ps3 ? 0 : 1; border.color: style.accent
        Rectangle { visible: card.ps3; width: parent.width; height: 2; color: "#50ffffff" }
        Rectangle { visible: card.ps3; y: parent.height - 2; width: parent.width; height: 2; color: "#50ffffff" }
        MouseArea { anchors.fill: parent; onWheel: function(wheel) { wheel.accepted = false } }   // clicks on the card don't close it

        // ---- who ----
        Rectangle {
            id: picture
            x: 44; y: 40; width: 168; height: 168
            radius: style.lightSwitch || style.nintendo ? 84 : style.radius; clip: true
            color: card.tile
            Image {
                anchors.fill: parent
                // the USCL picture on the USCL tab, the Steam one (full size) on the Steam tab
                source: card.tab === "steam" && card.steam.avatar ? card.steam.avatar : card.person.avatar || card.steam.avatar || ""
                fillMode: Image.PreserveAspectCrop; asynchronous: true; mipmap: true
            }
        }
        Rectangle {
            x: picture.x + picture.width - 30; y: picture.y + picture.height - 30; width: 26; height: 26; radius: 13
            visible: !!card.person.state
            color: card.person.state === "online" ? "#3ec43e" : card.person.state === "away" ? "#f0a30a" : "#8a8a8a"
            border.width: 4; border.color: panel.color
        }
        Text {
            x: 244; y: 44; width: panel.width - x - 44
            text: card.person.name || card.steam.name || ""
            color: card.ink; font.pixelSize: 50; font.weight: Font.Light; elide: Text.ElideRight
        }
        Text {
            x: 246; y: 112; width: panel.width - x - 44
            text: card.person.status || card.steam.status || ""
            color: card.soft; font.pixelSize: 27; elide: Text.ElideRight
        }
        Row {
            x: 246; y: 162; spacing: 22; height: 40
            Repeater {
                model: ["platinum", "gold", "silver", "bronze"]
                delegate: Row {
                    required property string modelData
                    spacing: 6
                    Image { width: 36; height: 36; source: "qrc:/qt/qml/Uscl/xmb/trophies/" + parent.modelData + ".png"; mipmap: true }
                    Text { height: 36; verticalAlignment: Text.AlignVCenter; text: String(card.uscl[parent.modelData] || 0); color: card.ink; font.pixelSize: 26 }
                }
            }
            Rectangle { width: 2; height: 32; y: 2; color: card.line; visible: card.person.level > 0 }
            Text { visible: card.person.level > 0; height: 36; verticalAlignment: Text.AlignVCenter; text: qsTr("Steam Level %1").arg(card.person.level); color: card.ink; font.pixelSize: 26 }
        }

        // ---- tabs ----
        Row {
            x: 44; y: 236; spacing: 12
            Repeater {
                model: [["uscl", qsTr("USCL Profile")], ["steam", qsTr("Steam Profile")]]
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool current: card.tab === modelData[0]
                    width: 300; height: 58; radius: style.radius
                    color: card.focusIndex === 0 && current ? (card.ps3 ? "#30ffffff" : style.focusFill) : "transparent"
                    border.width: card.focusIndex === 0 && current ? 2 : 0; border.color: card.ps3 ? "white" : style.accent
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Text {
                        anchors.centerIn: parent; text: parent.modelData[1]
                        color: card.focusIndex === 0 && parent.current && !card.ps3 ? style.focusText : parent.current ? card.ink : card.soft
                        font.pixelSize: 28
                    }
                    Rectangle { visible: parent.current; anchors.bottom: parent.bottom; width: parent.width; height: 4; color: card.ps3 ? "white" : style.accent }
                    MouseArea { anchors.fill: parent; onClicked: { card.focusIndex = 0; card.setTab(parent.modelData[0]) } }
                }
            }
        }
        Rectangle { x: 44; y: 302; width: panel.width - 88; height: 2; color: card.line }

        // ---- the tab's page ----
        Flickable {
            id: content
            objectName: "profileContent"
            x: 44; y: 318; width: panel.width - 88; height: 476
            clip: true; boundsBehavior: Flickable.StopAtBounds
            contentWidth: width; contentHeight: column.height + 12
            Behavior on contentY { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            transform: Translate { id: tabShift }
            NumberAnimation { id: tabSwitch; target: tabShift; property: "x"; from: card.tab === "steam" ? 60 : -60; to: 0; duration: 220; easing.type: Easing.OutCubic }
            Column {
                id: column
                width: content.width - 20
                spacing: 18

                // what there is, or why there's nothing
                Text {
                    width: parent.width
                    visible: text !== ""
                    text: card.tab === "uscl"
                          ? (!card.uscl.games ? qsTr("%1's USCL profile shows here once they open USCL while Steam is running.").arg(card.person.name || "")
                             : !card.uscl.games.length ? (card.person.me ? qsTr("Play a game from USCL to fill your profile: play time and trophies show here.")
                                                                         : qsTr("%1 hasn't played a game from USCL yet.").arg(card.person.name || ""))
                             : !card.person.me && card.uscl.live === false ? qsTr("As of %1, the last time USCL saw them.").arg(card.dayText(card.uscl.seen)) : "")
                          : card.steam.loading && !card.steam.name ? qsTr("Loading the Steam profile...")
                          : card.steam.error ? card.steam.error
                          : card.steam.privacy && card.steam.privacy !== "public" ? qsTr("This Steam profile is %1: Steam shares only the basics.").arg(card.steam.privacy === "friendsonly" ? qsTr("friends only") : qsTr("private"))
                          : ""
                    color: card.soft; font.pixelSize: 26; wrapMode: Text.Wrap
                }
                Row {
                    spacing: 18
                    visible: card.tab === "uscl" ? !!card.uscl.games : !!card.steam.name
                    Repeater {
                        model: card.tab === "uscl"
                               ? [[qsTr("Trophies"), String(card.uscl.earned || 0)],
                                  [qsTr("Play Time"), card.playTime(card.uscl.seconds || 0)],
                                  [qsTr("Games Played"), String(card.uscl.gameCount !== undefined ? card.uscl.gameCount : (card.uscl.games || []).length)]]
                               : [[qsTr("Member Since"), card.steam.memberSince || "—"],
                                  [qsTr("Location"), card.steam.location || card.steam.realName || "—"],
                                  [qsTr("Status"), card.steam.status || "—"]]
                        delegate: Rectangle {
                            required property var modelData
                            width: (column.width - 36) / 3; height: 112; radius: style.radius; color: card.tile
                            Text { x: 22; y: 16; width: parent.width - 44; text: parent.modelData[0]; color: card.soft; font.pixelSize: 22; elide: Text.ElideRight }
                            Text { x: 22; y: 50; width: parent.width - 44; text: parent.modelData[1]; color: card.ink; font.pixelSize: 34; elide: Text.ElideRight }
                        }
                    }
                }
                Text {
                    width: parent.width
                    visible: card.tab === "steam" && !!(card.steam.headline || card.steam.summary)
                    text: [card.steam.headline || "", card.steam.summary || ""].filter(Boolean).join("\n")
                    color: card.ink; font.pixelSize: 25; wrapMode: Text.Wrap; maximumLineCount: 8; elide: Text.ElideRight; lineHeight: 1.15
                }
                Text {
                    visible: card.rows.length > 0
                    text: card.tab === "uscl" ? qsTr("Recently Played") : qsTr("Recent Games")
                    color: card.soft; font.pixelSize: 24
                }
                Repeater {
                    id: rowRepeater
                    model: card.rows
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        readonly property bool selected: card.focusIndex === index + 1
                        width: column.width; height: 100; radius: style.radius
                        color: selected ? (card.ps3 ? "#30ffffff" : style.focusFill) : card.tile
                        border.width: selected ? 2 : 0; border.color: card.ps3 ? "white" : style.accent
                        Behavior on color { ColorAnimation { duration: 110 } }
                        readonly property color fg: selected && !card.ps3 ? style.focusText : card.ink
                        // white menu glyphs (a game without art) on a dark tile, so light themes don't swallow them
                        Rectangle {
                            visible: !card.ps3 && String(row.modelData.icon || "").indexOf("xmb/icons/") >= 0
                            x: 10; y: 10; width: 80; height: 80; radius: style.radius
                            color: style.xbox ? "#107c10" : "#2c3440"
                        }
                        Image {
                            x: 14; y: 14; width: 72; height: 72
                            source: row.modelData.icon || ""
                            fillMode: Image.PreserveAspectFit; asynchronous: true; mipmap: true
                        }
                        Text { x: 104; y: 14; width: parent.width - x - 300; text: row.modelData.title || ""; color: row.fg; font.pixelSize: 29; elide: Text.ElideRight }
                        Text { x: 104; y: 54; width: parent.width - x - 300; text: row.modelData.sub || ""; color: row.selected && !card.ps3 ? style.focusText : card.soft; font.pixelSize: 21; elide: Text.ElideRight }
                        Text { anchors.right: parent.right; anchors.rightMargin: 22; y: 14; width: 270; horizontalAlignment: Text.AlignRight; text: row.modelData.value || ""; color: row.fg; font.pixelSize: 25; elide: Text.ElideRight }
                        Rectangle {
                            visible: row.modelData.progress >= 0
                            anchors.right: parent.right; anchors.rightMargin: 22; y: 62; width: 220; height: 10; radius: 5; color: card.line
                            Rectangle { width: parent.width * Math.max(0, row.modelData.progress); height: parent.height; radius: 5; color: card.ps3 ? "white" : row.selected ? style.focusText : style.accent }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (row.selected) card.activate()
                                else { card.focusIndex = row.index + 1; card.sound("cursor"); card.reveal() }
                            }
                        }
                    }
                }
                Text {
                    width: parent.width
                    visible: card.tab === "steam" && (card.steam.groups || []).length > 0
                    text: qsTr("Groups: %1").arg((card.steam.groups || []).join(", "))
                    color: card.soft; font.pixelSize: 23; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
                }
            }
        }
        Rectangle {
            visible: content.contentHeight > content.height
            x: content.x + content.width - 6; y: content.y; width: 6; height: content.height; radius: 3; color: card.line
            Rectangle {
                width: 6; radius: 3; color: card.ps3 ? "white" : style.accent
                height: Math.max(30, parent.height * content.height / content.contentHeight)
                y: (parent.height - height) * content.contentY / Math.max(1, content.contentHeight - content.height)
            }
        }

        // ---- buttons ----
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 820; spacing: 30
            Repeater {
                model: card.buttons
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool selected: card.atButtons && card.buttonIndex === index
                    width: 300; height: 66; radius: style.radius
                    color: selected ? (card.ps3 ? "#40ffffff" : style.focusFill) : card.tile
                    border.width: selected ? 2 : 1; border.color: card.ps3 ? (selected ? "white" : "#40ffffff") : style.accent
                    Behavior on color { ColorAnimation { duration: 110 } }
                    Text { anchors.centerIn: parent; text: parent.modelData.title; color: parent.selected && !card.ps3 ? style.focusText : card.ink; font.pixelSize: 27 }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: { card.focusIndex = card.buttonsStart; card.buttonIndex = parent.index; card.activate() }
                    }
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 896
            text: qsTr("%1 Open    %2 Close    %3 or ← →: USCL / Steam    ↑ ↓ / wheel: Scroll").arg(style.confirmGlyph).arg(style.backGlyph)
                  .arg(style.xbox ? "LB / RB" : style.nintendo ? "L / R" : "L1 / R1")
            color: card.soft; font.pixelSize: 21
        }
    }
}
