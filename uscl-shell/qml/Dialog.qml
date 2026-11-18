import QtQuick

// PS3 dialog (kodi-xmb DialogConfirm): the screen goes dark between two thin lines, a small heading,
// the message, optional progress bar and buttons, and the button hints at the bottom.
Item {
    id: dlg
    property string theme: "ps3"
    ThemePalette { id: themeStyle; theme: dlg.theme }
    property bool open: false
    enabled: open
    property string heading: ""
    property string text: ""
    property bool markdown: false
    property var live: null          // function returning the message, re-read as what it uses changes
    property var progress: null      // function returning 0..1, or -1 for no bar
    property var buttons: []         // [{ title, action }]; empty: ✕ / ○ just close
    property int current: 0
    readonly property real scrollPosition: messageViewport.contentY
    readonly property bool scrollable: messageViewport.contentHeight > messageViewport.height
    signal sound(string name)

    // opts: { buttons, live, progress, start, markdown }
    function show(h, t, opts) {
        heading = h || ""; text = t || ""
        markdown = !!(opts && opts.markdown)
        buttons = opts && opts.buttons ? opts.buttons : []
        live = opts && opts.live ? opts.live : null
        progress = opts && opts.progress ? opts.progress : null
        current = Math.max(0, Math.min(buttons.length - 1, opts && opts.start !== undefined ? opts.start : 0))
        messageViewport.contentY = 0
        open = true
    }
    function confirm(h, t, yes) {
        show(h, t, { buttons: [{ title: qsTr("Yes"), action: yes }, { title: qsTr("No") }], start: 1 })
    }
    function close() { open = false }
    function input(b) {
        if (b === "up" || b === "down" || b === "l1" || b === "r1") {
            var distance = (b === "l1" || b === "r1" ? 300 : 100) * (b === "up" || b === "l1" ? -1 : 1)
            messageViewport.contentY = Math.max(0, Math.min(messageViewport.contentHeight - messageViewport.height, messageViewport.contentY + distance))
            return
        }
        if (b === "back") { sound("back"); close() }
        else if (b === "left" && current > 0) { current--; sound("cursor") }
        else if (b === "right" && current < buttons.length - 1) { current++; sound("cursor") }
        else if (b === "accept") {
            sound("select")
            var a = buttons.length && buttons[current] ? buttons[current].action : null
            close()
            if (a) a()
        }
    }

    anchors.fill: parent
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 200 } }

    Rectangle { anchors.fill: parent; color: "#c8000000" }
    MouseArea { anchors.fill: parent; onWheel: function(wheel) { wheel.accepted = true } }
    Rectangle {
        visible: dlg.theme !== "ps3"; x: 160; y: 230; width: parent.width - 320; height: 670
        color: themeStyle.panel; radius: themeStyle.radius
        border.width: 1; border.color: themeStyle.accent
    }
    Rectangle { visible: dlg.theme === "ps3"; y: 250; width: parent.width; height: 2; color: "#50ffffff" }
    Rectangle { visible: dlg.theme === "ps3"; y: 890; width: parent.width; height: 2; color: "#50ffffff" }

    Item {
        x: (parent.width - 1400) / 2; y: 300; width: 1400; height: 480
        Text { width: 1400; height: 50; horizontalAlignment: Text.AlignHCenter; text: dlg.heading; color: dlg.theme === "ps3" ? "#b4ffffff" : themeStyle.muted; font.pixelSize: 25 }
        Rectangle { x: 200; y: 58; width: 1000; height: 2; color: dlg.theme === "ps3" ? "#40ffffff" : themeStyle.divider }
        Flickable {
            id: messageViewport
            x: 100; y: 90; width: 1200; height: bar.visible || dlg.buttons.length ? 200 : 330
            clip: true; boundsBehavior: Flickable.StopAtBounds
            contentWidth: width; contentHeight: Math.max(height, msg.implicitHeight)
            Text {
                id: msg
                width: messageViewport.width - 28
                y: implicitHeight < messageViewport.height ? (messageViewport.height - implicitHeight) / 2 : 0
                horizontalAlignment: dlg.markdown ? Text.AlignLeft : Text.AlignHCenter
                text: dlg.live ? dlg.live() : dlg.text
                textFormat: dlg.markdown ? Text.MarkdownText : Text.AutoText
                color: dlg.theme === "ps3" ? "white" : themeStyle.text; font.pixelSize: 29
                wrapMode: Text.Wrap; lineHeight: 1.2
            }
        }
        Rectangle {
            visible: messageViewport.contentHeight > messageViewport.height
            x: 1290; y: 90; width: 5; height: messageViewport.height; radius: 2; color: "#50707070"
            Rectangle {
                width: parent.width; height: Math.max(20, parent.height * messageViewport.height / messageViewport.contentHeight)
                y: (parent.height - height) * messageViewport.contentY / Math.max(1, messageViewport.contentHeight - messageViewport.height)
                color: dlg.theme === "ps3" ? "white" : themeStyle.accent; radius: 2
            }
        }
        Rectangle {
            id: bar
            readonly property real value: dlg.progress ? dlg.progress() : -1
            visible: value >= 0
            x: 300; y: 300; width: 800; height: 14; radius: 7
            color: dlg.theme === "ps3" ? "#33ffffff" : themeStyle.divider
            Rectangle { width: parent.width * Math.max(0, Math.min(1, bar.value)); height: parent.height; radius: 7; color: dlg.theme === "ps3" ? "white" : themeStyle.accent }
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
                    source: dlg.theme === "ps3" ? "qrc:/qt/qml/Uscl/xmb/ui/" + (index === dlg.current ? "button_focus.png" : "button_nofocus.png") : ""
                    border { left: 14; right: 14; top: 14; bottom: 14 }
                    Rectangle { anchors.fill: parent; visible: dlg.theme !== "ps3"; radius: themeStyle.radius; color: index === dlg.current ? themeStyle.focusFill : themeStyle.panel; border.width: 2; border.color: themeStyle.accent }
                    MouseArea { anchors.fill: parent; onClicked: { dlg.current = index; dlg.input("accept") } }
                    Text { anchors.centerIn: parent; text: parent.modelData.title; color: dlg.theme === "ps3" ? (index === dlg.current ? "white" : "#b4ffffff") : index === dlg.current ? themeStyle.focusText : themeStyle.text; font.pixelSize: 29 }
                }
            }
        }
    }
    Text { visible: dlg.scrollable; x: 200; y: 840; text: qsTr("↑ ↓ / mouse wheel: Scroll"); color: dlg.theme === "ps3" ? "#c0ffffff" : themeStyle.muted; font.pixelSize: 22 }
    Hints { id: dialogHints; theme: dlg.theme; y: 906; anchors.horizontalCenter: parent.horizontalCenter }
    MouseArea { x: dialogHints.x; y: dialogHints.y; width: dialogHints.width; height: dialogHints.height; onClicked: dlg.input("accept") }
}
