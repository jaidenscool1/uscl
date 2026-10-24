import QtQuick
import QtQuick.Window
import QtMultimedia
import QtCore

Window {
    id: root
    width: 1280
    height: 720
    visible: true
    visibility: windowed ? Window.Windowed : Window.FullScreen
    color: "black"
    title: "USCL"

    readonly property real u: height / 1080
    readonly property bool xboxPad: gamepad.name.indexOf("Xbox") >= 0

    // the JStationX XMB colour themes
    readonly property var tints: [
        ["Violet", "#5B1C80"], ["Midnight", "#11375C"], ["Blue", "#0E597E"], ["Teal", "#00665C"],
        ["Green", "#1C6E45"], ["Chartreuse", "#3B5E12"], ["Gold", "#966300"], ["Orange", "#A74012"],
        ["Brown", "#563529"], ["Maroon", "#6B1414"], ["Rose", "#A22A66"], ["Pink", "#A61558"],
        ["Concrete", "#4B5F69"], ["Charcoal", "#666666"]
    ]

    Settings {
        id: prefs
        property int tint: 0
        property int xmbCategory: 2
    }

    function glyph(cp) { return String.fromCodePoint(cp) }

    function classic() {
        if (!launcher.coreFound()) { coreMissing(); return }
        launcher.openClassic()
    }
    function coreMissing() {
        sound("error")
        side.show(qsTr("Can't start"), [], qsTr("The PS3 emulator (rpcs3) wasn't found next to USCL. Reinstalling USCL puts it back."))
    }
    function play(g) {
        if (!launcher.coreFound()) { coreMissing(); return }
        launcher.startGame(g.path, g.title)
    }
    function gameInfo(g) {
        return qsTr("Serial: %1\nConsole: %2\nLocation: %3").arg(g.serial).arg(g.console).arg(g.path)
    }

    // rebuilt whenever games, settings or controllers change
    property var cats: {
        games.count; system.onlineName; system.firmwareInstalled; gamepad.count; prefs.tint
        return buildCats()
    }

    function buildCats() {
        var gameItems = []
        for (var i = 0; i < games.count; i++) {
            let g = games.get(i)
            gameItems.push({
                title: g.title, subtitle: g.serial, image: g.icon, glyph: glyph(0xF0297),
                action: function() { play(g) },
                options: function() {
                    return [{ title: qsTr("Start"), action: function() { play(g) } },
                            { title: qsTr("Information"), action: function() { side.show(g.title, [], gameInfo(g)) } }]
                },
                info: gameInfo(g)
            })
        }
        gameItems.push({ title: qsTr("Add Games"), glyph: glyph(0xF0257), action: classic,
                         subtitle: qsTr("Opens All Settings: press Open and pick the folder your games are in") })

        var tintOptions = tints.map(function(t, i) {
            return { title: t[0], action: function() { prefs.tint = i } }
        })

        return [
            { name: qsTr("Users"), glyph: glyph(0xF0004), items: [
                { title: system.onlineName || qsTr("Set up online play"), glyph: glyph(0xF0004), action: classic,
                  subtitle: system.onlineName ? qsTr("Your online name") : qsTr("All Settings → USCL → Online") },
                { title: qsTr("Quit USCL"), glyph: glyph(0xF0425), action: Qt.quit, subtitle: qsTr("Back to the desktop") } ] },
            { name: qsTr("Settings"), glyph: glyph(0xF0493), items: [
                { title: qsTr("Theme"), glyph: glyph(0xF03D8), subtitle: tints[prefs.tint][0],
                  action: function() { side.show(qsTr("Theme"), tintOptions, "", prefs.tint) } },
                { title: qsTr("Controllers"), glyph: glyph(0xF0297), action: classic,
                  subtitle: gamepad.count ? gamepad.name : qsTr("No controller found: connect one, or use the keyboard") },
                { title: qsTr("System Software"), glyph: glyph(0xF06B0), action: classic,
                  subtitle: system.firmwareInstalled ? qsTr("PS3 system software is installed")
                                                     : qsTr("Not installed: All Settings → File → Install Firmware") },
                { title: qsTr("All Settings"), glyph: glyph(0xF062E), action: classic,
                  subtitle: qsTr("Every setting, in the classic window") } ] },
            { name: qsTr("Game"), glyph: glyph(0xF0297), items: gameItems },
            { name: qsTr("Network"), glyph: glyph(0xF01E7), items: [
                { title: qsTr("Online Play"), glyph: glyph(0xF01E7), action: classic,
                  subtitle: qsTr("Host or join with Steam friends: All Settings → USCL → Online") },
                { title: qsTr("Test Connection"), glyph: glyph(0xF0318), disabled: true, subtitle: qsTr("Coming soon") } ] },
            { name: qsTr("Friends"), glyph: glyph(0xF000E), items: [
                { title: qsTr("Steam Friends"), glyph: glyph(0xF000E), disabled: true, subtitle: qsTr("Coming soon") } ] }
        ]
    }

    // sounds
    readonly property var sounds: ({ cursor: sCursor, category: sCategory, select: sSelect, back: sBack, error: sError, notify: sNotify })
    function sound(name) { if (sounds[name]) sounds[name].play() }
    SoundEffect { id: sCursor; source: "qrc:/qt/qml/Uscl/sounds/xmb/cursor.wav" }
    SoundEffect { id: sCategory; source: "qrc:/qt/qml/Uscl/sounds/xmb/category.wav" }
    SoundEffect { id: sSelect; source: "qrc:/qt/qml/Uscl/sounds/xmb/select.wav" }
    SoundEffect { id: sBack; source: "qrc:/qt/qml/Uscl/sounds/xmb/back.wav" }
    SoundEffect { id: sError; source: "qrc:/qt/qml/Uscl/sounds/xmb/error.wav" }
    SoundEffect { id: sNotify; source: "qrc:/qt/qml/Uscl/sounds/xmb/notify.wav" }
    SoundEffect { id: sStartup; source: "qrc:/qt/qml/Uscl/sounds/xmb/startup.wav" }

    function input(b) {
        if (launcher.running || intro.running) return
        if (side.open) side.input(b)
        else xmb.input(b)
    }

    Connections {
        target: gamepad
        function onPressed(b) { if (root.active) root.input(b) }
        function onHomeHeld() { if (launcher.running) launcher.stop() }
    }
    Connections {
        target: launcher
        function onFinished(crashed, details) {
            // the window manager may have taken us out of full screen when the game's window appeared
            if (!windowed) { root.showFullScreen(); root.requestActivate() }
            games.reload()
            system.refresh()
            if (crashed) {
                sound("error")
                side.show(qsTr("The game stopped unexpectedly"), [],
                          details.split("\n").slice(-8).join("\n") || qsTr("No details were reported."))
            }
        }
    }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: function(e) {
            var map = {}
            map[Qt.Key_Up] = "up"; map[Qt.Key_Down] = "down"; map[Qt.Key_Left] = "left"; map[Qt.Key_Right] = "right"
            map[Qt.Key_Return] = "accept"; map[Qt.Key_Enter] = "accept"; map[Qt.Key_Escape] = "back"; map[Qt.Key_Backspace] = "back"
            map[Qt.Key_Space] = "options"; map[Qt.Key_Menu] = "options"; map[Qt.Key_I] = "info"
            map[Qt.Key_PageUp] = "l1"; map[Qt.Key_PageDown] = "r1"
            if (map[e.key]) { root.input(map[e.key]); e.accepted = true }
        }

        Background {
            anchors.fill: parent
            tint: root.tints[prefs.tint][1]
            running: !launcher.running
        }

        Xmb {
            id: xmb
            anchors.fill: parent
            u: root.u
            cats: root.cats
            cat: prefs.xmbCategory
            onCatChanged: prefs.xmbCategory = cat
            onSound: function(name) { root.sound(name) }
            onShowOptions: function(e) { side.show(e.title, e.options(), "") }
            onShowInfo: function(e) { side.show(e.title, [], e.info) }
        }

        StatusPill {
            anchors.right: parent.right; anchors.top: parent.top
            anchors.rightMargin: 60 * root.u; anchors.topMargin: 44 * root.u
            u: root.u
            name: system.onlineName
            padConnected: gamepad.count > 0
        }

        Text {   // button hints
            anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.rightMargin: 60 * root.u; anchors.bottomMargin: 40 * root.u
            text: root.xboxPad ? qsTr("Ⓐ Enter    Ⓑ Back    Ⓨ Options") : qsTr("✕ Enter    ○ Back    △ Options")
            color: "#b0ffffff"
            font.pixelSize: 24 * root.u
        }

        SidePanel {
            id: side
            u: root.u
            onSound: function(name) { root.sound(name) }
        }

        // while a game (or the classic window) runs
        Rectangle {
            anchors.fill: parent
            color: "black"
            opacity: launcher.running ? 0.92 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 300 } }
            Column {
                anchors.centerIn: parent
                spacing: 16 * root.u
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: launcher.title
                    color: "white"; font.pixelSize: 44 * root.u; font.weight: Font.Light
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Hold the PS / Guide button to quit and come back here")
                    color: "#b0ffffff"; font.pixelSize: 24 * root.u
                }
            }
        }

        // startup: fade in from black with the chime
        Rectangle {
            id: curtain
            anchors.fill: parent
            color: "black"
            NumberAnimation on opacity { id: intro; from: 1; to: 0; duration: 1600; easing.type: Easing.InOutQuad }
        }
    }

    Component.onCompleted: sStartup.play()
}
