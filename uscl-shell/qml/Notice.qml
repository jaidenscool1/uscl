import QtQuick

// PS3-style notification card, top right under the clock box
Item {
    id: notice
    property string icon: ""
    function show(msg, ic) { label.text = msg; icon = ic || ""; opacity = 1; hide.restart() }

    width: 600; height: 112
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 260 } }
    Image { anchors.fill: parent; source: "qrc:/qt/qml/Uscl/xmb/ui/notify_card.png" }
    Image { x: 24; y: 28; width: 56; height: 56; source: notice.icon; visible: notice.icon !== ""; mipmap: true }
    Text {
        id: label
        x: notice.icon ? 100 : 30; width: parent.width - x - 30; height: parent.height
        verticalAlignment: Text.AlignVCenter
        color: "white"; font.pixelSize: 25; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
    }
    Timer { id: hide; interval: 4000; onTriggered: notice.opacity = 0 }
}
