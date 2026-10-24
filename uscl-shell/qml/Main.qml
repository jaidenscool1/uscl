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
        property bool sounds: true
        property bool setupDone: false
    }

    function glyph(cp) { return String.fromCodePoint(cp) }

    // ---- actions ----

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
        if (!system.firmwareInstalled) {
            sound("error")
            side.show(qsTr("System software needed"), [{ title: qsTr("Install it now"), action: firmwarePanel }])
            return
        }
        launcher.startGame(g.path, g.title)
    }
    function gameInfo(g) {
        return qsTr("Serial: %1\nConsole: %2\nLocation: %3").arg(g.serial).arg(g.console).arg(g.path)
    }

    // Folder browser for Add Games: drives first, then folders; ✕ on "Search this folder" picks it
    function browseFolders(path, pick, roots) {
        var top = roots || games.roots()
        if (!path) {
            side.show(qsTr("Where are your games?"), top.map(function(r) {
                return { title: r.name, subtitle: r.path, keepOpen: true, action: function() { browseFolders(r.path, pick, top) } }
            }))
            return
        }
        var opts = [{ title: qsTr("Search this folder"), subtitle: qsTr("Finds games in every folder inside it"),
                      action: function() { pick(path) } }]
        opts = opts.concat(games.folders(path).map(function(f) {
            return { title: f.name, keepOpen: true, action: function() { browseFolders(f.path, pick, top) } }
        }))
        var isRoot = top.some(function(r) { return r.path === path })
        var parent = path.substring(0, path.lastIndexOf("/")) || "/"
        side.show(path, opts, "", 0, { back: function() { browseFolders(isRoot ? "" : parent, pick, top) } })
    }
    function addGames() {
        browseFolders("", function(p) { games.scanFolder(p); toast.show(qsTr("Looking for games...")) })
    }

    function typeName(after) {
        prompt.show(qsTr("Online name"), system.onlineName || online.steamName, function(t) {
            if (!online.validName(t)) return qsTr("Use 3 to 16 letters, numbers, - or _.")
            if (!online.setup(t)) return qsTr("Couldn't save it.")
            system.refresh()
            toast.show(qsTr("Online name: %1").arg(t))
            if (after) after()
            return ""
        })
    }
    function nameChooser(after) {
        var o = []
        if (online.steamName)
            o.push({ title: qsTr("Use my Steam name"), subtitle: online.steamName, action: function() {
                if (online.setup(online.steamName)) { system.refresh(); toast.show(qsTr("Online name: %1").arg(online.steamName)); if (after) after() }
            } })
        o.push({ title: qsTr("Type a name"), subtitle: qsTr("With the keyboard"), action: function() { typeName(after) } })
        side.show(qsTr("Online Name"), o)
    }
    function needsName(then) {
        if (system.onlineName) { then(); return }
        nameChooser(then)
    }

    function firmwarePanel() {
        var o = system.findFirmware().map(function(p) {
            return { title: qsTr("Install from this file"), subtitle: p, action: function() { system.installFirmware(p); firmwareProgress() } }
        })
        o.push({ title: qsTr("Download from Sony"), subtitle: qsTr("About 200 MB, from PlayStation's own update server"),
                 action: function() { system.downloadFirmware(); firmwareProgress() } })
        side.show(system.firmwareInstalled ? qsTr("Reinstall System Software") : qsTr("Install System Software"), o)
    }
    function firmwareProgress() {
        side.show(qsTr("System Software"), [], "", 0, {
            live: function() { return system.firmwareStatus },
            progress: function() { return system.firmwareBusy ? system.firmwareProgress : -1 }
        })
    }

    function hostPicker() {
        needsName(function() {
            var o = []
            for (var i = 0; i < games.count; i++) {
                let g = games.get(i)
                o.push({ title: g.title, subtitle: g.serial, action: function() { online.host(g.serial, g.title) } })
            }
            o.push({ title: qsTr("Another game"), subtitle: qsTr("Any game that plays online through PSN"),
                     action: function() { online.host("", "") } })
            side.show(qsTr("Host a session for..."), o)
        })
    }
    function testConnection() {
        online.testConnection()
        side.show(qsTr("Test Connection"), [], "", 0, { live: function() {
            return online.tests.map(function(t) {
                var mark = t.result === "ok" ? "✓" : t.result === "fail" ? "✗" : "…"
                return mark + "  " + t.name + (t.detail ? "\n     " + t.detail : "")
            }).join("\n\n")
        } })
    }
    function sessionText() {
        switch (online.state) {
        case "hosting": return qsTr("Hosting session %1").arg(online.session)
        case "joined": return qsTr("In session %1").arg(online.session)
        case "waiting": return qsTr("Waiting for a Steam invite...")
        case "starting": return qsTr("Starting the session...")
        default: return qsTr("Not in a session")
        }
    }

    // ---- the XMB, rebuilt whenever what it shows changes ----

    readonly property int friendsCat: 4
    property var cats: {
        games.count; games.scanning; system.onlineName; system.firmwareInstalled; gamepad.count; prefs.tint; prefs.sounds
        online.state; online.session; online.message; online.friends; online.friendsLoading; online.friendsError
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
                            { title: qsTr("Host an online session"), action: function() { needsName(function() { online.host(g.serial, g.title) }) } },
                            { title: qsTr("Information"), action: function() { side.show(g.title, [], gameInfo(g)) } }]
                },
                info: gameInfo(g)
            })
        }
        gameItems.push({ title: qsTr("Add Games"), glyph: glyph(0xF0257), action: addGames,
                         subtitle: games.scanning ? qsTr("Looking for games...") : qsTr("Find the games in a folder (and every folder inside it)") })

        var tintOptions = tints.map(function(t, i) { return { title: t[0], action: function() { prefs.tint = i } } })
        var active = online.state !== "off"

        var network = [{ title: sessionText(), glyph: glyph(0xF01E7),
                         subtitle: online.message || (active ? (online.game || qsTr("Steam friends can join from their Friends list")) : qsTr("Host one, or join a friend's")),
                         action: active ? function() { side.show(sessionText(), [{ title: qsTr("Leave the session"), action: online.stop }]) } : hostPicker }]
        if (active)
            network.push({ title: qsTr("Leave the Session"), glyph: glyph(0xF05FC), subtitle: qsTr("Stops the session on this PC"), action: online.stop })
        else
            network.push({ title: qsTr("Host a Session"), glyph: glyph(0xF0849), subtitle: qsTr("Friends join you over Steam"), action: hostPicker },
                         { title: qsTr("Join by Steam Invite"), glyph: glyph(0xF000E), subtitle: qsTr("Accept an invite in Steam, or join the one session a friend is in"),
                           action: function() { needsName(function() { online.join("") }) } })
        network.push({ title: qsTr("Test Connection"), glyph: glyph(0xF0318), subtitle: qsTr("Checks USCL, the network and Steam"), action: testConnection })

        var friends = []
        if (online.friendsError)
            friends.push({ title: qsTr("Steam isn't available"), glyph: glyph(0xF0159), subtitle: online.friendsError, action: online.refreshFriends })
        else if (online.friendsLoading && !online.friends.length)
            friends.push({ title: qsTr("Loading friends..."), glyph: glyph(0xF000E), disabled: true })
        else if (!online.friends.length)
            friends.push({ title: qsTr("No friends online"), glyph: glyph(0xF000E), subtitle: qsTr("✕ to check again"), action: online.refreshFriends })
        online.friends.forEach(function(f) {
            friends.push({
                title: f.name, glyph: glyph(f.session ? 0xF0297 : 0xF0004),
                subtitle: f.session ? qsTr("In a USCL session: ✕ to join") : f.state,
                disabled: !f.session,
                action: function() { needsName(function() { online.join(f.session) }) }
            })
        })

        return [
            { name: qsTr("Users"), glyph: glyph(0xF0004), items: [
                { title: system.onlineName || qsTr("Choose an online name"), glyph: glyph(0xF0004), action: function() { nameChooser() },
                  subtitle: system.onlineName ? qsTr("Your online name (✕ to change)") : qsTr("Use your Steam name, or type one") },
                { title: qsTr("Quit USCL"), glyph: glyph(0xF0425), action: Qt.quit, subtitle: qsTr("Back to the desktop") } ] },
            { name: qsTr("Settings"), glyph: glyph(0xF0493), items: [
                { title: qsTr("Online Name"), glyph: glyph(0xF0004), subtitle: system.onlineName || qsTr("Not set"), action: function() { nameChooser() } },
                { title: qsTr("Theme"), glyph: glyph(0xF03D8), subtitle: tints[prefs.tint][0],
                  action: function() { side.show(qsTr("Theme"), tintOptions, "", prefs.tint) } },
                { title: qsTr("Menu Sounds"), glyph: glyph(0xF057E), subtitle: prefs.sounds ? qsTr("On") : qsTr("Off"),
                  action: function() { prefs.sounds = !prefs.sounds } },
                { title: qsTr("Controllers"), glyph: glyph(0xF0297), action: classic,
                  subtitle: gamepad.count ? qsTr("%1 (✕ for button setup)").arg(gamepad.name) : qsTr("No controller found: connect one, or use the keyboard") },
                { title: qsTr("System Software"), glyph: glyph(0xF06B0), action: firmwarePanel,
                  subtitle: system.firmwareInstalled ? qsTr("Installed") : qsTr("Not installed: ✕ to install") },
                { title: qsTr("Run Setup Again"), glyph: glyph(0xF02D7), subtitle: qsTr("Name, system software and games"), action: function() { setup.start() } },
                { title: qsTr("All Settings"), glyph: glyph(0xF062E), action: classic,
                  subtitle: qsTr("Every setting, in the classic window") } ] },
            { name: qsTr("Game"), glyph: glyph(0xF0297), items: gameItems },
            { name: qsTr("Network"), glyph: glyph(0xF01E7), items: network },
            { name: qsTr("Friends"), glyph: glyph(0xF000E), items: friends }
        ]
    }

    // ---- sounds ----

    readonly property var sounds: ({ cursor: sCursor, category: sCategory, select: sSelect, back: sBack, error: sError, notify: sNotify })
    function sound(name) { if (prefs.sounds && sounds[name]) sounds[name].play() }
    SoundEffect { id: sCursor; source: "qrc:/qt/qml/Uscl/sounds/xmb/cursor.wav" }
    SoundEffect { id: sCategory; source: "qrc:/qt/qml/Uscl/sounds/xmb/category.wav" }
    SoundEffect { id: sSelect; source: "qrc:/qt/qml/Uscl/sounds/xmb/select.wav" }
    SoundEffect { id: sBack; source: "qrc:/qt/qml/Uscl/sounds/xmb/back.wav" }
    SoundEffect { id: sError; source: "qrc:/qt/qml/Uscl/sounds/xmb/error.wav" }
    SoundEffect { id: sNotify; source: "qrc:/qt/qml/Uscl/sounds/xmb/notify.wav" }
    SoundEffect { id: sStartup; source: "qrc:/qt/qml/Uscl/sounds/xmb/startup.wav" }

    // ---- input: whatever is on top gets it ----

    function input(b) {
        if (launcher.running || intro.running) return
        if (prompt.open) prompt.pad(b)
        else if (side.open) side.input(b)
        else if (setup.open) setup.input(b)
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
    Connections {
        target: games
        function onScanFinished(added) {
            sound("notify")
            toast.show(added ? qsTr("Added %n game(s)", "", added) : qsTr("No new games found there"))
        }
    }
    Connections {
        target: online
        function onStateChanged() {
            if (online.state === "hosting" || online.state === "joined") { sound("notify"); toast.show(sessionText()) }
            else if (online.state === "off" && online.message) { sound("error"); toast.show(online.message) }
        }
    }
    Connections {
        target: system
        function onFirmwareChanged() { if (!system.firmwareBusy && system.firmwareStatus) sound(system.firmwareInstalled ? "notify" : "error") }
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
            visible: !setup.open
            cats: root.cats
            cat: prefs.xmbCategory
            onCatChanged: {
                prefs.xmbCategory = cat
                if (cat === root.friendsCat) online.refreshFriends()
            }
            onSound: function(name) { root.sound(name) }
            onShowOptions: function(e) { side.show(e.title, e.options(), "") }
            onShowInfo: function(e) { side.show(e.title, [], e.info) }
        }

        Setup {
            id: setup
            u: root.u
            onSound: function(name) { root.sound(name) }
            onFinished: prefs.setupDone = true
            onTypeName: root.typeName(function() { setup.next() })
            onBrowse: root.browseFolders("", function(p) { games.scanFolder(p) })
        }

        StatusPill {
            anchors.right: parent.right; anchors.top: parent.top
            anchors.rightMargin: 60 * root.u; anchors.topMargin: 44 * root.u
            u: root.u
            name: system.onlineName
            padConnected: gamepad.count > 0
        }

        Toast {
            id: toast
            u: root.u
            anchors.right: parent.right; anchors.top: parent.top
            anchors.rightMargin: 60 * root.u; anchors.topMargin: 120 * root.u
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

        TextPrompt {
            id: prompt
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
            anchors.fill: parent
            color: "black"
            NumberAnimation on opacity {
                id: intro; from: 1; to: 0; duration: 1600; easing.type: Easing.InOutQuad
                onFinished: if (!prefs.setupDone) setup.start()
            }
        }
    }

    Component.onCompleted: if (prefs.sounds) sStartup.play()
}
