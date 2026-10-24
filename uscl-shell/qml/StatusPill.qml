import QtQuick

// Top right: who's signed in, controller, clock
Rectangle {
    id: pill
    property real u: 1
    property string name: ""
    property bool padConnected: false

    height: 56 * u
    width: row.implicitWidth + 40 * u
    radius: height / 2
    color: "#26ffffff"
    border.color: "#33ffffff"

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 22 * u
        Text {
            visible: pill.name !== ""
            text: pill.name
            color: "white"; font.pixelSize: 24 * pill.u
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: String.fromCodePoint(0xF0297)   // gamepad-variant
            font.family: "Material Design Icons Desktop"
            font.pixelSize: 30 * pill.u
            color: pill.padConnected ? "white" : "#60ffffff"
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: clock
            color: "white"; font.pixelSize: 24 * pill.u
            anchors.verticalCenter: parent.verticalCenter
            function tick() { text = Qt.formatDateTime(new Date(), "M/d  h:mm AP") }
            Component.onCompleted: tick()
        }
    }
    Timer { interval: 1000; running: true; repeat: true; onTriggered: clock.tick() }
}
