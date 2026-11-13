import QtQuick

// PS3 dialog (kodi-xmb DialogConfirm): the screen goes dark between two thin lines, a small heading,
// the message, optional progress bar and buttons, and the button hints at the bottom.
Item {
    id: dlg
    property bool open: false
    property string heading: ""
    property string text: ""
    property var live: null          // function returning the message, re-read as what it uses changes
    property var progress: null      // function returning 0..1, or -1 for no bar
    property var buttons: []         // [{ title, action }]; empty: ✕ / ○ just close
    property int current: 0
    signal sound(string name)

    // opts: { buttons, live, progress, start }
    function show(h, t, opts) {
        heading = h || ""; text = t || ""
        buttons = opts && opts.buttons ? opts.buttons : []
        live = opts && opts.live ? opts.live : null
        progress = opts && opts.progress ? opts.progress : null
        current = opts && opts.start !== undefined ? opts.start : 0
        open = true
    }
    function confirm(h, t, yes) {
        show(h, t, { buttons: [{ title: qsTr("Yes"), action: yes }, { title: qsTr("No") }], start: 1 })
    }
    function close() { open = false }
    function input(b) {
        if (b === "back") { sound("back"); close() }
        else if (b === "left" && current > 0) { current--; sound("cursor") }
        else if (b === "right" && current < buttons.length - 1) { current++; sound("cursor") }
        else if (b === "accept") {
            sound("select")
            var a = buttons.length ? buttons[current].action : null
            close()
            if (a) a()
        }
    }

    anchors.fill: parent
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 200 } }

    Rectangle { anchors.fill: parent; color: "#c8000000" }
    Rectangle { y: 250; width: parent.width; height: 2; color: "#50ffffff" }
    Rectangle { y: 890; width: parent.width; height: 2; color: "#50ffffff" }

    Item {
        x: (parent.width - 1400) / 2; y: 300; width: 1400; height: 480
        Text { width: 1400; height: 50; horizontalAlignment: Text.AlignHCenter; text: dlg.heading; color: "#b4ffffff"; font.pixelSize: 25 }
        Rectangle { x: 200; y: 58; width: 1000; height: 2; color: "#40ffffff" }
        Text {
            id: msg
            x: 100; y: 90; width: 1200; height: bar.visible || dlg.buttons.length ? 200 : 330
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            text: dlg.live ? dlg.live() : dlg.text
            color: "white"; font.pixelSize: 29
            wrapMode: Text.Wrap; lineHeight: 1.2
        }
        Rectangle {
            id: bar
            readonly property real value: dlg.progress ? dlg.progress() : -1
            visible: value >= 0
            x: 300; y: 300; width: 800; height: 14; radius: 7
            color: "#33ffffff"
            Rectangle { width: parent.width * Math.max(0, Math.min(1, bar.value)); height: parent.height; radius: 7; color: "white" }
        }
        Row {
            y: 350; height: 70
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 40
            Repeater {
                model: dlg.buttons
                delegate: BorderImage {
                    required property var modelData
                    required property int index
                    width: 260; height: 64
                    source: "qrc:/qt/qml/Uscl/xmb/ui/" + (index === dlg.current ? "button_focus.png" : "button_nofocus.png")
                    border { left: 14; right: 14; top: 14; bottom: 14 }
                    Text { anchors.centerIn: parent; text: parent.modelData.title; color: index === dlg.current ? "white" : "#b4ffffff"; font.pixelSize: 29 }
                }
            }
        }
    }
    Hints { y: 906; anchors.horizontalCenter: parent.horizontalCenter }
}
