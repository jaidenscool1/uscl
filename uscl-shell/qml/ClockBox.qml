import QtQuick

// The clock box, top right (kodi-xmb XMB_Clock): avatar, network and controller status with badges,
// laptop battery, date and time. 560x64 on the 1920x1080 stage.
Item {
    id: box
    property string avatar: ""
    property int internet: -1       // 1 online, 0 offline, -1 unknown
    property int pads: 0
    property int battery: -1
    property bool charging: false
    property string dateFormat: "MM/dd/yy"
    property bool clock24: false

    readonly property string ui: "qrc:/qt/qml/Uscl/xmb/ui/"
    width: 560; height: 64

    Image { anchors.fill: parent; source: "qrc:/qt/qml/Uscl/xmb/clockbox.png" }
    Image { x: 12; y: 7; width: 50; height: 50; source: box.avatar; smooth: true; mipmap: true }

    // network: green check online, red cross offline
    Image { x: 78; y: 12; width: 40; height: 40; source: box.ui + "status_net.png"; opacity: 0.88; mipmap: true }
    Image {
        x: 70; y: 6; width: 20; height: 20; visible: box.internet >= 0; mipmap: true
        source: box.ui + (box.internet === 1 ? "badge_ok.png" : "badge_err.png")
    }
    // controllers: check when one is connected
    Image { x: 132; y: 12; width: 40; height: 40; source: "qrc:/qt/qml/Uscl/xmb/icons/it_gamepad.png"; opacity: 0.88; mipmap: true }
    Image { x: 124; y: 6; width: 20; height: 20; source: box.ui + (box.pads > 0 ? "badge_ok.png" : "badge_err.png"); mipmap: true }
    // battery: fill in tenths, % inside, bolt while charging
    Item {
        visible: box.battery >= 0
        Image { x: 186; y: 18; width: 56; height: 28; mipmap: true; source: box.ui + "battery_" + Math.max(0, Math.min(10, Math.round(box.battery / 10))) + ".png" }
        Text {
            x: 186; y: 18; width: 52; height: 28
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            text: box.battery + "%"; color: "white"; font.pixelSize: 16
            style: Text.Outline; styleColor: "#c0000000"
        }
        Image { x: 178; y: 6; width: 20; height: 20; visible: box.charging; source: box.ui + "badge_bolt.png"; mipmap: true }
    }
    Text {
        id: clock
        anchors.right: parent.right; anchors.rightMargin: 28
        height: 64; verticalAlignment: Text.AlignVCenter
        color: "#f0ffffff"; font.pixelSize: 30
        style: Text.Raised; styleColor: "#80000000"
        function tick() {
            var now = new Date()
            text = Qt.formatDate(now, box.dateFormat) + "  " + Qt.formatTime(now, box.clock24 ? "H:mm" : "h:mm AP")
        }
        Component.onCompleted: tick()
        Connections { target: box; function onDateFormatChanged() { clock.tick() } function onClock24Changed() { clock.tick() } }
    }
    Timer { interval: 1000; running: true; repeat: true; onTriggered: clock.tick() }
}
