import QtQuick

// A short notice under the status pill ("3 games added", "Hosting session ...")
Rectangle {
    id: toast
    property real u: 1
    function show(msg) { label.text = msg; opacity = 1; hide.restart() }

    width: label.implicitWidth + 48 * u
    height: 64 * u
    radius: 10 * u
    color: "#e6101018"
    border.color: "#40ffffff"
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 250 } }
    Text { id: label; anchors.centerIn: parent; color: "white"; font.pixelSize: 24 * toast.u }
    Timer { id: hide; interval: 4000; onTriggered: toast.opacity = 0 }
}
