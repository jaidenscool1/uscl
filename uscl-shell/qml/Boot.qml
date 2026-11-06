import QtQuick

// The boot screen (kodi-xmb Custom_1199_JaxBoot + service.xmb.startup timeline, timed like a PS3 cold boot):
// black, a silk wave drifts in with the chime, the USCL logo, the screen lifts to the background, then the
// health warning, then the crossbar. ✕ skips it.
Item {
    id: boot
    property int stage: 0
    property bool running: false
    signal chime()
    signal finished()

    function start() { stage = 0; running = true; timeline.restart() }
    // straight to the crossbar with a short fade (boot screen off, or skipped)
    function quick() { timeline.stop(); stage = 3; running = true; done.restart() }
    function skip() { if (running && stage < 7) quick() }

    anchors.fill: parent
    visible: running || black.opacity > 0

    SequentialAnimation {
        id: timeline
        PauseAnimation { duration: 300 }
        ScriptAction { script: { boot.stage = 1; boot.chime() } }
        PauseAnimation { duration: 1300 }
        PropertyAction { target: boot; property: "stage"; value: 2 }
        PauseAnimation { duration: 1200 }
        PropertyAction { target: boot; property: "stage"; value: 3 }
        PauseAnimation { duration: 2200 }
        PropertyAction { target: boot; property: "stage"; value: 4 }
        PauseAnimation { duration: 800 }
        PropertyAction { target: boot; property: "stage"; value: 5 }
        PauseAnimation { duration: 4200 }
        PropertyAction { target: boot; property: "stage"; value: 6 }
        PauseAnimation { duration: 800 }
        ScriptAction { script: { boot.stage = 7; boot.running = false; boot.finished() } }
    }
    SequentialAnimation {
        id: done
        PauseAnimation { duration: 600 }
        ScriptAction { script: { boot.stage = 7; boot.running = false; boot.finished() } }
    }

    // black until stage 3, then it lifts to the background over two seconds
    Rectangle {
        id: black
        anchors.fill: parent
        color: "black"
        opacity: boot.stage < 3 ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: boot.stage < 3 ? 300 : 2000; easing.type: Easing.InOutSine } }
    }

    // the silk wave drifting in
    Image {
        id: silk
        x: -240; width: 2400; height: 1080
        source: "qrc:/qt/qml/Uscl/xmb/silk_a.png"
        readonly property bool on: boot.stage >= 1 && boot.stage < 4
        opacity: on ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: silk.on ? 1500 : 900; easing.type: Easing.InOutSine } }
        NumberAnimation on x { running: boot.stage === 1; from: -240; to: -360; duration: 6000; easing.type: Easing.OutSine }
    }

    // the logo, lower right like the PS3's, with the name under it
    Item {
        id: logo
        readonly property bool on: boot.stage >= 2 && boot.stage < 4
        x: 1742 - 684; y: 458; width: 684; height: 200
        opacity: on ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: logo.on ? 1000 : 600; easing.type: Easing.InOutSine } }
        NumberAnimation on scale { running: boot.stage === 2; from: 0.97; to: 1; duration: 3400; easing.type: Easing.OutSine }
        Image { width: 684; height: 158; source: "qrc:/qt/qml/Uscl/xmb/ui/uscl_logo.png" }
        Text {
            y: 150; width: 684
            horizontalAlignment: Text.AlignHCenter
            text: "Ultimate Steam Compatibility Layer"
            color: "#c8ffffff"; font.pixelSize: 22; font.letterSpacing: 3
        }
    }

    // the health warning
    Column {
        readonly property bool on: boot.stage === 5
        x: 330; y: 478; width: 1300
        spacing: 6
        opacity: on ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 600; easing.type: Easing.InOutSine } }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: qsTr("PHOTOSENSITIVE EPILEPSY"); color: "white"; font.pixelSize: 25; font.letterSpacing: 2 }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            text: qsTr("If you have a history of epilepsy or seizures, consult a doctor before use. Certain flashing patterns may trigger seizures, even in people with no prior history. Take regular breaks while playing.")
            color: "#d0ffffff"; font.pixelSize: 24; lineHeight: 1.2
        }
    }
}
