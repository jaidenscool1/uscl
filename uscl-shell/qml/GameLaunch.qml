pragma ComponentBehavior: Bound
import QtQuick

// A shell-side handoff for every core. The emulator owns progress and focus; no guessed completion timer.
Item {
    id: screen
    property string theme: "ps3"
    property bool shown: false
    property bool starting: false
    property bool windowActive: true
    property bool xboxPad: false
    property bool homeSupported: false
    property string title: ""
    property url gameIcon: ""
    readonly property bool animating: shown && windowActive && visible
    ThemePalette { id: launchStyle; theme: screen.theme }

    visible: opacity > 0
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    Rectangle { anchors.fill: parent; color: launchStyle.background }
    Rectangle {
        visible: launchStyle.ps4 || launchStyle.ps5
        x: -parent.width * .1; y: parent.height * .66; width: parent.width * 1.2; height: 2
        rotation: -8; color: launchStyle.accent; opacity: .24
    }
    Repeater {
        model: launchStyle.blades ? 5 : 0
        Rectangle { required property int index; x: 18 + index * 20; width: 8; height: parent.height; color: index === 4 ? launchStyle.accent : launchStyle.divider }
    }
    Rectangle { visible: launchStyle.nintendo; x: 90; y: 145; width: parent.width - 180; height: 2; color: launchStyle.divider }
    Text {
        x: launchStyle.blades ? 150 : 100; y: 77; width: parent.width - 200
        text: "USCL"; color: launchStyle.text; font.pixelSize: 32; font.letterSpacing: 5
    }
    Column {
        anchors.centerIn: parent
        width: parent.width - 360
        spacing: 26
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: launchStyle.xbox ? 240 : 260; height: width
            radius: launchStyle.radius
            color: launchStyle.blades || launchStyle.metro || launchStyle.lightSwitch ? "#263240" : launchStyle.panel; border.width: launchStyle.nintendo ? 3 : 1; border.color: launchStyle.accent
            Image {
                id: art
                anchors.fill: parent; anchors.margins: 8
                source: screen.shown ? screen.gameIcon : ""
                sourceSize: Qt.size(480, 480); fillMode: Image.PreserveAspectFit; asynchronous: true
            }
            Text {
                anchors.centerIn: parent; visible: art.status !== Image.Ready
                text: "USCL"; color: launchStyle.text; font.pixelSize: 36
            }
        }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: screen.title; textFormat: Text.PlainText; color: launchStyle.text
            font.pixelSize: 46; maximumLineCount: 2; elide: Text.ElideRight; wrapMode: Text.Wrap
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter; spacing: 16
            Item {
                width: 30; height: 30
                visible: screen.starting
                Rectangle {
                    anchors.centerIn: parent; width: 22; height: 22
                    radius: launchStyle.nintendo ? 3 : launchStyle.xbox ? 0 : 11
                    color: "transparent"; border.width: 3; border.color: launchStyle.accent
                    RotationAnimator on rotation { from: 0; to: 360; duration: 1300; loops: Animation.Infinite; running: screen.animating && screen.starting }
                }
            }
            Text {
                text: screen.starting ? qsTr("Starting emulator…") : qsTr("Emulator running")
                color: launchStyle.muted; font.pixelSize: 27
            }
        }
    }
    Text {
        x: 180; y: parent.height - 155; width: parent.width - 360
        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
        text: !screen.homeSupported ? qsTr("Close the emulator window to return to USCL.")
              : screen.xboxPad ? qsTr("Press Guide to return to USCL. The game keeps running.") : qsTr("Press the PS button to return to USCL. The game keeps running.")
        color: launchStyle.muted; font.pixelSize: 24
    }
}
