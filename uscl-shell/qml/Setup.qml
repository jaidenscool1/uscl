import QtQuick

// First start: online name (Steam's by default), PS3 system software, games. Controller only.
Item {
    id: setup
    property real u: 1
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

    Rectangle { anchors.fill: parent; color: "#55000000" }

    Column {
        id: content
        x: parent.width * 0.2; y: parent.height * 0.2
        width: parent.width * 0.6
        spacing: 26 * setup.u

        Text {   // step dots
            text: setup.steps.map(function(s, i) { return i === setup.step ? "●" : "○" }).join("  ")
            color: "#b0ffffff"; font.pixelSize: 22 * setup.u
        }
        Text { text: setup.heading; color: "white"; font.pixelSize: 56 * setup.u; font.weight: Font.Light }
        Text { width: parent.width; text: setup.body; color: "#e0ffffff"; font.pixelSize: 28 * setup.u; wrapMode: Text.Wrap; lineHeight: 1.25 }
        Rectangle {   // download / install progress
            visible: setup.stepName === "firmware" && system.firmwareBusy
            width: parent.width; height: 10 * setup.u; radius: height / 2; color: "#33ffffff"
            Rectangle { width: parent.width * system.firmwareProgress; height: parent.height; radius: parent.radius; color: "white" }
        }
        Text { visible: setup.note !== ""; text: setup.note; color: "#ffb0b0"; font.pixelSize: 24 * setup.u }
        Item { width: 1; height: 10 * setup.u }
        Repeater {
            model: setup.opts
            delegate: Rectangle {
                id: opt
                required property var modelData
                required property int index
                readonly property bool sel: index === setup.current
                width: content.width; height: (modelData.subtitle ? 92 : 70) * setup.u
                radius: 8 * setup.u
                color: sel ? "#33ffffff" : "#14ffffff"
                border.color: sel ? "#a0ffffff" : "transparent"
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 26 * setup.u; width: parent.width - 52 * setup.u
                    Text { width: parent.width; text: opt.modelData.title; color: "white"; font.pixelSize: 30 * setup.u; elide: Text.ElideRight }
                    Text {
                        visible: !!opt.modelData.subtitle
                        width: parent.width; text: opt.modelData.subtitle || ""
                        color: "#b0ffffff"; font.pixelSize: 22 * setup.u; elide: Text.ElideMiddle
                    }
                }
            }
        }
    }
}
