import QtQuick

// First start: online name (Steam's by default), PS3 system software, games. Controller only.
Item {
    id: setup
    property bool open: false
    property int step: 0
    property int current: 0
    property string note: ""
    signal sound(string name)
    signal finished()
    signal typeName()
    signal browse()

    readonly property var steps: ["welcome", "name", "firmware", "games", "done"]
    readonly property string stepName: steps[step]

    function start() { step = 0; current = 0; note = ""; open = true }
    function next() { if (step < steps.length - 1) { step++; current = 0; note = "" } }
    function pickName(n) {
        if (online.setup(n)) { system.refresh(); next() }
        else { note = qsTr("Couldn't save the online name."); sound("error") }
    }

    // re-evaluated as the firmware install, the game scan or the name change
    readonly property var opts: {
        system.firmwareInstalled; system.firmwareBusy; games.scanning; games.count; system.onlineName
        switch (stepName) {
        case "welcome":
            return [{ title: qsTr("Start"), action: next }]
        case "name": {
            var o = []
            if (online.steamName)
                o.push({ title: qsTr("Use my Steam name"), subtitle: online.steamName, action: function() { pickName(online.steamName) } })
            if (system.onlineName && system.onlineName !== online.steamName)
                o.push({ title: qsTr("Keep my current name"), subtitle: system.onlineName, action: next })
            o.push({ title: qsTr("Type a name"), subtitle: qsTr("With the keyboard"), action: function() { typeName() } })
            return o
        }
        case "firmware": {
            if (system.firmwareBusy) return []
            if (system.firmwareInstalled) return [{ title: qsTr("Continue"), action: next }]
            var f = system.findFirmware().map(function(p) {
                return { title: qsTr("Install from this file"), subtitle: p, action: function() { system.installFirmware(p) } }
            })
            f.push({ title: qsTr("Download from Sony"), subtitle: qsTr("About 200 MB, from PlayStation's own update server"), action: system.downloadFirmware })
            f.push({ title: qsTr("Skip for now"), subtitle: qsTr("Games won't start until it's installed"), action: next })
            return f
        }
        case "games":
            if (games.scanning) return []
            return [{ title: qsTr("Find my games"), subtitle: qsTr("Pick the folder your PS3 games are in"), action: function() { browse() } },
                    { title: games.count ? qsTr("Continue") : qsTr("Skip for now"), action: next }]
        default:
            return [{ title: qsTr("Go to the home screen"), action: function() { open = false; finished() } }]
        }
    }

    readonly property string heading: ({
        welcome: qsTr("Welcome to USCL"), name: qsTr("Your online name"), firmware: qsTr("PS3 system software"),
        games: qsTr("Your games"), done: qsTr("You're all set")
    })[stepName]

    readonly property string body: {
        switch (stepName) {
        case "welcome":
            return qsTr("Play your PS3 games, and play them online with your Steam friends.\n\n") +
                   (gamepad.count ? qsTr("Controller: %1").arg(gamepad.name) : qsTr("No controller found. The keyboard works too: arrows, Enter, Esc."))
        case "name":
            return qsTr("This is the name other players see in games. You can change it later in Settings.")
        case "firmware":
            if (system.firmwareBusy || system.firmwareStatus) return system.firmwareStatus
            return system.firmwareInstalled ? qsTr("Already installed.") : qsTr("Games need the PS3 system software. USCL installs it for you.")
        case "games":
            if (games.scanning) return qsTr("Looking for games...")
            return games.count ? qsTr("%n game(s) in your library.", "", games.count) : qsTr("Point USCL at the folder your games are in. Disc folders, ISOs and installed games are all found, in any subfolder.")
        default:
            return qsTr("Everything else is in Settings. Online play is under Network, and your Steam friends are under Friends.")
        }
    }

    function input(b) {
        if (b === "up" && current > 0) { current--; sound("cursor") }
        else if (b === "down" && current < opts.length - 1) { current++; sound("cursor") }
        else if (b === "back" && step > 0 && !system.firmwareBusy && !games.scanning) { step--; current = 0; note = ""; sound("back") }
        else if (b === "accept" && opts[current]) { sound("select"); opts[current].action() }
    }
    onOptsChanged: current = Math.min(current, Math.max(0, opts.length - 1))

    anchors.fill: parent
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 300 } }

    // PS3 setup screens: the background stays, framed by two thin lines, text and choices centred
    Rectangle { anchors.fill: parent; color: "#66000000" }
    Rectangle { y: 250; width: parent.width; height: 2; color: "#50ffffff" }
    Rectangle { y: 890; width: parent.width; height: 2; color: "#50ffffff" }

    Column {
        id: content
        width: 1200
        x: (parent.width - width) / 2; y: 290
        spacing: 22
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: setup.steps.map(function(s, i) { return i === setup.step ? "●" : "○" }).join("  ")
            color: "#90ffffff"; font.pixelSize: 18
        }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: setup.heading; color: "white"; font.pixelSize: 40 }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: setup.body; color: "#d8ffffff"; font.pixelSize: 27; wrapMode: Text.Wrap; lineHeight: 1.2
        }
        Rectangle {   // download / install progress
            visible: setup.stepName === "firmware" && system.firmwareBusy
            anchors.horizontalCenter: parent.horizontalCenter
            width: 800; height: 14; radius: 7; color: "#33ffffff"
            Rectangle { width: parent.width * system.firmwareProgress; height: parent.height; radius: 7; color: "white" }
        }
        Text { visible: setup.note !== ""; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: setup.note; color: "#ffb0b0"; font.pixelSize: 25 }
        Item { width: 1; height: 6 }
        Repeater {
            model: setup.opts
            delegate: Item {
                id: opt
                required property var modelData
                required property int index
                readonly property bool sel: index === setup.current
                anchors.horizontalCenter: parent.horizontalCenter
                width: 760; height: modelData.subtitle ? 84 : 64
                BorderImage {
                    anchors.fill: parent; visible: opt.sel
                    source: "qrc:/qt/qml/Uscl/xmb/ui/focus_bar.png"
                    border { left: 40; right: 120 }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 40; width: parent.width - 80
                    Text { width: parent.width; text: opt.modelData.title; color: opt.sel ? "white" : "#b4ffffff"; font.pixelSize: 29; elide: Text.ElideRight }
                    Text {
                        visible: !!opt.modelData.subtitle
                        width: parent.width; text: opt.modelData.subtitle || ""
                        color: "#a0ffffff"; font.pixelSize: 21; elide: Text.ElideMiddle
                    }
                }
            }
        }
    }
    Hints { y: 906; anchors.horizontalCenter: parent.horizontalCenter }
}
