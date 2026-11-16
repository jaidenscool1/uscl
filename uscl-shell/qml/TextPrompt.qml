import QtQuick

// Typing a name with the keyboard: Enter saves, Esc (or ○) cancels
Item {
    id: prompt
    property string theme: "ps3"
    ThemePalette { id: themeStyle; theme: prompt.theme }
    signal closed()
    property bool open: false
    enabled: open
    property string heading: ""
    property string error: ""
    property var onDone: null       // function(text) returning an error message, or "" when accepted
    signal sound(string name)

    function show(h, initial, done) {
        heading = h; error = ""; onDone = done; input.text = initial || ""; open = true
        input.forceActiveFocus(); input.selectAll()
    }
    function close() { open = false; closed() }
    function submit() {
        var e = onDone ? onDone(input.text.trim()) : ""
        if (e) { error = e; sound("error") } else { sound("select"); close() }
    }
    function pad(b) {
        if (b === "accept") submit()
        else if (b === "back") { sound("back"); close() }
    }

    anchors.fill: parent
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 180 } }

    Rectangle { anchors.fill: parent; color: "#c8000000" }
    MouseArea { anchors.fill: parent; onWheel: function(wheel) { wheel.accepted = true } }
    Rectangle { visible: prompt.theme !== "ps3"; x: 160; y: 230; width: parent.width - 320; height: 670; color: themeStyle.panel; radius: themeStyle.radius; border.width: 1; border.color: themeStyle.accent }
    Rectangle { visible: prompt.theme === "ps3"; y: 250; width: parent.width; height: 2; color: "#50ffffff" }
    Rectangle { visible: prompt.theme === "ps3"; y: 890; width: parent.width; height: 2; color: "#50ffffff" }
    Column {
        width: 1000
        x: (parent.width - width) / 2; y: 300
        spacing: 26
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: prompt.heading; color: prompt.theme === "ps3" ? "#b4ffffff" : themeStyle.text; font.pixelSize: 25 }
        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 1000; height: 2; color: "#40ffffff" }
        BorderImage {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 700; height: 76
            source: prompt.theme === "ps3" ? "qrc:/qt/qml/Uscl/xmb/ui/button_focus.png" : ""
            border { left: 14; right: 14; top: 14; bottom: 14 }
            Rectangle { anchors.fill: parent; visible: prompt.theme !== "ps3"; color: themeStyle.background; border.width: 2; border.color: themeStyle.accent }
            TextInput {
                id: input
                anchors.fill: parent; anchors.margins: 18
                horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter
                color: prompt.theme === "ps3" ? "white" : themeStyle.text; font.pixelSize: 34
                selectionColor: "#80ffffff"
                maximumLength: 16
                onAccepted: prompt.submit()
                Keys.onEscapePressed: { prompt.sound("back"); prompt.close() }
            }
        }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: prompt.error; visible: text !== ""; color: prompt.theme === "ps3" ? "#ffb0b0" : themeStyle.danger; font.pixelSize: 25 }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: qsTr("Letters, numbers, - and _ (3 to 16). Type with the keyboard, Enter saves."); color: prompt.theme === "ps3" ? "#a0ffffff" : themeStyle.muted; font.pixelSize: 23 }
    }
    Hints { theme: prompt.theme; y: 906; anchors.horizontalCenter: parent.horizontalCenter; accept: qsTr("Save") }
}
