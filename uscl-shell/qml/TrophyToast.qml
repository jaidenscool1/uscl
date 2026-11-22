pragma ComponentBehavior: Bound
import QtQuick

// The trophy notification, drawn like each console's own (1920x1080 stage units):
// PS3 the card top right, PS4 a grey bar sliding in top left, PS5 a rounded card dropping in top right,
// Xbox 360 the "Achievement unlocked" pill growing from its logo at the bottom, Xbox One the green tile bar
// opening from the middle at the bottom, Switch / Switch 2 a rounded card sliding down top right.
// show() queues: one trophy at a time, like the consoles.
Item {
    id: toast
    property string theme: "ps3"
    property var trophy: ({})
    property var queue: []
    readonly property bool active: running.running || toast.queue.length > 0
    signal shown(var trophy)   // one is on screen now (the shell plays the notify sound)
    ThemePalette { id: style; theme: toast.theme }

    // where it sits on the 1920x1080 stage
    readonly property string corner: style.ps4 ? "topLeft" : style.xbox ? "bottom" : "topRight"
    readonly property real cardWidth: style.blades || style.metro ? 620 : style.one ? 700 : style.ps4 ? 680 : style.ps5 ? 600 : 620
    readonly property real cardHeight: style.blades || style.metro ? 96 : style.one ? 104 : style.ps5 ? 124 : 116
    readonly property real stageX: corner === "topLeft" ? 0 : corner === "bottom" ? (1920 - cardWidth) / 2 : 1920 - cardWidth - (style.ps5 ? 56 : 40)
    readonly property real stageY: corner === "bottom" ? 1080 - cardHeight - 90 : style.ps4 ? 64 : style.ps5 ? 110 : style.nintendo ? 150 : 124
    width: cardWidth
    height: cardHeight
    visible: active

    readonly property string grade: trophy.grade || "bronze"
    readonly property int gamerscore: ({bronze: 15, silver: 30, gold: 90, platinum: 180})[grade] || 15
    readonly property string gradeName: ({bronze: qsTr("Bronze"), silver: qsTr("Silver"), gold: qsTr("Gold"), platinum: qsTr("Platinum")})[grade] || qsTr("Bronze")
    readonly property url cup: "qrc:/qt/qml/Uscl/xmb/trophies/" + grade + ".png"
    readonly property url art: trophy.icon || cup

    function show(t) {
        queue = queue.concat([t])
        if (!running.running) next()
    }
    function next() {
        if (!queue.length) return
        trophy = queue[0]
        queue = queue.slice(1)
        shown(trophy)
        running.restart()
    }

    // 0 hidden .. 1 fully in; each theme maps it to its own motion
    property real reveal: 0
    SequentialAnimation {
        id: running
        NumberAnimation { target: toast; property: "reveal"; from: 0; to: 1; duration: style.xbox ? 520 : 320; easing.type: style.xbox ? Easing.OutQuint : Easing.OutCubic }
        PauseAnimation { duration: 4600 }
        NumberAnimation { target: toast; property: "reveal"; to: 0; duration: 300; easing.type: Easing.InCubic }
        ScriptAction { script: toast.next() }
    }

    // ---- PS3: the notification card (same art as other PS3 notices), fades in ----
    Item {
        visible: style.ps3
        anchors.fill: parent
        opacity: toast.reveal
        Image { anchors.fill: parent; source: "qrc:/qt/qml/Uscl/xmb/ui/notify_card.png" }
        Image { x: 22; y: 18; width: 80; height: 80; source: toast.art; fillMode: Image.PreserveAspectFit; mipmap: true }
        Column {
            x: 120; y: 20; width: parent.width - 140; spacing: 6
            Text { width: parent.width; text: qsTr("You have earned a trophy."); color: "white"; font.pixelSize: 24; elide: Text.ElideRight }
            Row {
                spacing: 8
                Image { width: 34; height: 34; source: toast.cup; mipmap: true }
                Text { width: 430; text: toast.trophy.name || ""; color: "white"; font.pixelSize: 27; elide: Text.ElideRight; height: 34; verticalAlignment: Text.AlignVCenter }
            }
        }
    }

    // ---- PS4: a grey bar sliding in from the left edge ----
    Rectangle {
        visible: style.ps4
        width: parent.width; height: parent.height
        x: -width * (1 - toast.reveal)
        color: "#e62d2f33"
        Rectangle { width: parent.width; height: 2; anchors.bottom: parent.bottom; color: "#4f7fd0" }
        Image { x: 18; y: 14; width: 88; height: 88; source: toast.art; fillMode: Image.PreserveAspectFit; mipmap: true }
        Column {
            x: 126; y: 18; width: parent.width - 146; spacing: 8
            Text { text: qsTr("You've earned a trophy."); color: "#c7cbd1"; font.pixelSize: 23 }
            Row {
                spacing: 10
                Image { width: 36; height: 36; source: toast.cup; mipmap: true }
                Text { width: 470; height: 36; text: toast.trophy.name || ""; color: "white"; font.pixelSize: 29; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
            }
        }
    }

    // ---- PS5: a rounded card dropping in from above ----
    Rectangle {
        visible: style.ps5
        width: parent.width; height: parent.height
        y: -30 * (1 - toast.reveal)
        opacity: toast.reveal
        radius: 16
        color: "#f2232529"
        border.width: 1; border.color: "#33ffffff"
        Rectangle {
            x: 18; y: 18; width: 88; height: 88; radius: 10; color: "#30363d"; clip: true
            Image { anchors.fill: parent; source: toast.art; fillMode: Image.PreserveAspectCrop; mipmap: true }
        }
        Row {
            x: 126; y: 22; spacing: 8
            Image { width: 26; height: 26; source: toast.cup; mipmap: true }
            Text { height: 26; text: qsTr("%1 Trophy Earned").arg(toast.gradeName); color: "#d6d8dc"; font.pixelSize: 21; verticalAlignment: Text.AlignVCenter }
        }
        Text { x: 126; y: 56; width: parent.width - 146; text: toast.trophy.name || ""; color: "white"; font.pixelSize: 30; font.weight: Font.Medium; elide: Text.ElideRight }
        Text { x: 126; y: 92; width: parent.width - 146; text: toast.trophy.game || ""; color: "#9aa0a8"; font.pixelSize: 19; elide: Text.ElideRight }
    }

    // ---- Xbox 360 (both): the logo circle first, then the pill opens to the right ----
    Item {
        visible: style.blades || style.metro
        anchors.fill: parent
        opacity: Math.min(1, toast.reveal * 3)
        Rectangle {
            id: pill
            height: parent.height
            width: parent.height + (parent.width - parent.height) * Math.max(0, (toast.reveal - .25) / .75)
            anchors.horizontalCenter: parent.horizontalCenter
            radius: height / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#f04a4d50" }
                GradientStop { position: 1; color: "#f01b1c1e" }
            }
            border.width: 2; border.color: "#80c8ccd0"
            clip: true
            Rectangle {
                x: 8; y: 8; width: parent.height - 16; height: width; radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: "#7fe05b" }
                    GradientStop { position: 1; color: "#1f8a10" }
                }
                Text { anchors.centerIn: parent; text: "✕"; color: "white"; font.pixelSize: 44; font.bold: true }   // an original X mark, not the Xbox logo
            }
            Column {
                x: parent.height + 10; anchors.verticalCenter: parent.verticalCenter; width: toast.width - x - 34; spacing: 2
                opacity: toast.reveal > .6 ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 160 } }
                Text { text: qsTr("Achievement unlocked"); color: "#d8dcdf"; font.pixelSize: 22 }
                Text { width: parent.width; text: qsTr("%1G - %2").arg(toast.gamerscore).arg(toast.trophy.name || ""); color: "white"; font.pixelSize: 28; elide: Text.ElideRight }
            }
        }
    }

    // ---- Xbox One: a dark bar opening from the middle, green trophy tile on the left ----
    Rectangle {
        visible: style.one
        height: parent.height
        width: parent.width * Math.min(1, toast.reveal * 1.25)
        anchors.horizontalCenter: parent.horizontalCenter
        color: "#f01c1c1c"
        clip: true
        Rectangle {
            width: parent.height; height: parent.height; color: "#107c10"
            Image { anchors.centerIn: parent; width: 64; height: 64; source: toast.cup; mipmap: true }
        }
        Column {
            x: toast.cardHeight + 22; anchors.verticalCenter: parent.verticalCenter; width: toast.width - x - 24; spacing: 4
            opacity: toast.reveal > .7 ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
            Text { text: qsTr("Achievement unlocked"); color: "#b8b8b8"; font.pixelSize: 21 }
            Text { width: parent.width; text: qsTr("%1G - %2").arg(toast.gamerscore).arg(toast.trophy.name || ""); color: "white"; font.pixelSize: 28; elide: Text.ElideRight }
        }
    }

    // ---- Switch / Switch 2: a rounded card sliding down ----
    Rectangle {
        visible: style.nintendo
        width: parent.width; height: parent.height
        y: -40 * (1 - toast.reveal)
        opacity: toast.reveal
        radius: style.lightSwitch ? 20 : 12
        color: style.lightSwitch ? "#fafafa" : "#f0464646"
        border.width: style.lightSwitch ? 1 : 0; border.color: "#22000000"
        Rectangle {
            x: 16; y: 16; width: parent.height - 32; height: width; radius: style.lightSwitch ? 14 : 6; clip: true
            color: style.lightSwitch ? "#e6e6e6" : "#2d2d2d"
            Image { anchors.fill: parent; anchors.margins: toast.trophy.icon ? 0 : 8; source: toast.art; fillMode: Image.PreserveAspectFit; mipmap: true }
        }
        Column {
            x: parent.height; anchors.verticalCenter: parent.verticalCenter; width: parent.width - x - 24; spacing: 4
            Row {
                spacing: 8
                Image { width: 26; height: 26; source: toast.cup; mipmap: true }
                Text { height: 26; text: qsTr("Trophy earned"); color: style.lightSwitch ? "#5b5b5b" : "#cfcfcf"; font.pixelSize: 21; verticalAlignment: Text.AlignVCenter }
            }
            Text { width: parent.width; text: toast.trophy.name || ""; color: style.lightSwitch ? "#202020" : "white"; font.pixelSize: 28; elide: Text.ElideRight }
            Text { width: parent.width; text: toast.trophy.game || ""; color: style.lightSwitch ? "#707070" : "#a8a8a8"; font.pixelSize: 19; elide: Text.ElideRight }
        }
    }
}
