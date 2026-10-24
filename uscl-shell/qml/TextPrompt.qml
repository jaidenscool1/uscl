import QtQuick

// Typing a name with the keyboard: Enter saves, Esc (or ○) cancels
Item {
    id: prompt
    property real u: 1
    property bool open: false
    property string heading: ""
    property string error: ""
    property var onDone: null       // function(text) returning an error message, or "" when accepted
    signal sound(string name)

    function show(h, initial, done) {
        heading = h; error = ""; onDone = done; input.text = initial || ""; open = true
        input.forceActiveFocus(); input.selectAll()
    }
    function close() { open = false }
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
    Rectangle { anchors.fill: parent; color: "#88000000" }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.45; height: 330 * u
        radius: 12 * u
        color: "#f0101018"; border.color: "#40ffffff"
        Column {
            x: 48 * u; y: 44 * u; width: parent.width - 96 * u
            spacing: 22 * u
            Text { text: prompt.heading; color: "white"; font.pixelSize: 34 * u }
            Rectangle {
                width: parent.width; height: 70 * u; radius: 8 * u
                color: "#22ffffff"; border.color: "#80ffffff"
                TextInput {
                    id: input
                    anchors.fill: parent; anchors.margins: 16 * u
                    verticalAlignment: TextInput.AlignVCenter
                    color: "white"; font.pixelSize: 30 * u
                    selectionColor: "#80ffffff"
                    maximumLength: 16
                    onAccepted: prompt.submit()
                    Keys.onEscapePressed: { prompt.sound("back"); prompt.close() }
                }
            }
            Text { text: prompt.error; visible: text !== ""; color: "#ffb0b0"; font.pixelSize: 22 * u }
            Text { text: qsTr("Letters, numbers, - and _ (3 to 16).  Enter saves, Esc cancels."); color: "#b0ffffff"; font.pixelSize: 22 * u }
        }
    }
}
