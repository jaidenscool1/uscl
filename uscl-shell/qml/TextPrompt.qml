import QtQuick

// Typing a name with the keyboard: Enter saves, Esc (or ○) cancels
Item {
    id: prompt
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

    Rectangle { anchors.fill: parent; color: "#c8000000" }
    Rectangle { y: 250; width: parent.width; height: 2; color: "#50ffffff" }
    Rectangle { y: 890; width: parent.width; height: 2; color: "#50ffffff" }
    Column {
        width: 1000
        x: (parent.width - width) / 2; y: 300
        spacing: 26
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: prompt.heading; color: "#b4ffffff"; font.pixelSize: 25 }
        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 1000; height: 2; color: "#40ffffff" }
        BorderImage {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 700; height: 76
            source: "qrc:/qt/qml/Uscl/xmb/ui/button_focus.png"
            border { left: 14; right: 14; top: 14; bottom: 14 }
            TextInput {
                id: input
                anchors.fill: parent; anchors.margins: 18
                horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter
                color: "white"; font.pixelSize: 34
                selectionColor: "#80ffffff"
                maximumLength: 16
                onAccepted: prompt.submit()
                Keys.onEscapePressed: { prompt.sound("back"); prompt.close() }
            }
        }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: prompt.error; visible: text !== ""; color: "#ffb0b0"; font.pixelSize: 25 }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: qsTr("Letters, numbers, - and _ (3 to 16). Type with the keyboard, Enter saves."); color: "#a0ffffff"; font.pixelSize: 23 }
    }
    Hints { y: 906; anchors.horizontalCenter: parent.horizontalCenter; accept: qsTr("Save") }
}
