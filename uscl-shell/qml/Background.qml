import QtQuick

// The JStationX XMB background: month-coloured base, bloom, three silk layers that sway and breathe on
// unrelated periods (so the ribbon undulates in place), drifting sparkles, and a darker shade at night.
// Drawn on the 1920x1080 stage.
Item {
    id: bg
    property color tint: "#955CC8"
    property real shade: 0          // 0..1 black on top (evening / night)
    property bool running: true

    readonly property string media: "qrc:/qt/qml/Uscl/xmb/"

    Image { id: base; source: bg.media + "bg_base.png"; visible: false }
    ShaderEffect {
        anchors.fill: parent
        property var source: base
        property color tint: bg.tint
        fragmentShader: "qrc:/qt/qml/Uscl/shaders/tint.frag.qsb"
        Behavior on tint { ColorAnimation { duration: 900 } }
    }
    Image { anchors.fill: parent; source: bg.media + "bg_bloom.png" }

    // (texture, sway px, sway ms, breathe %, breathe ms) from kodi-xmb gen_skin.py
    Repeater {
        model: [["silk_c.png", -150, 17000, 114, 19000], ["silk_b.png", 170, 31000, 89, 11000], ["silk_a.png", -210, 23000, 110, 13000]]
        delegate: Item {
            id: silk
            required property var modelData
            anchors.fill: parent
            transform: Scale { id: breathe; origin.x: 960; origin.y: 560 }
            SequentialAnimation {
                running: bg.running; loops: Animation.Infinite
                NumberAnimation { target: breathe; property: "yScale"; from: 1; to: silk.modelData[3] / 100; duration: silk.modelData[4]; easing.type: Easing.InOutSine }
                NumberAnimation { target: breathe; property: "yScale"; to: 1; duration: silk.modelData[4]; easing.type: Easing.InOutSine }
            }
            Image {
                id: layer
                x: -240; width: 2400; height: 1080
                source: bg.media + silk.modelData[0]
                SequentialAnimation on x {
                    running: bg.running; loops: Animation.Infinite
                    NumberAnimation { from: -240; to: -240 + silk.modelData[1]; duration: silk.modelData[2]; easing.type: Easing.InOutSine }
                    NumberAnimation { to: -240; duration: silk.modelData[2]; easing.type: Easing.InOutSine }
                }
            }
        }
    }
    // (texture, drift px, drift ms, twinkle ms)
    Repeater {
        model: [["sparkle_b.png", 60, 41000, 5300], ["sparkle_a.png", -80, 29000, 3700]]
        delegate: Image {
            id: sparkle
            required property var modelData
            x: -240; width: 2400; height: 1080
            source: bg.media + modelData[0]
            SequentialAnimation on x {
                running: bg.running; loops: Animation.Infinite
                NumberAnimation { from: -240; to: -240 + sparkle.modelData[1]; duration: sparkle.modelData[2]; easing.type: Easing.InOutSine }
                NumberAnimation { to: -240; duration: sparkle.modelData[2]; easing.type: Easing.InOutSine }
            }
            SequentialAnimation on opacity {
                running: bg.running; loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.35; duration: sparkle.modelData[3]; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1; duration: sparkle.modelData[3]; easing.type: Easing.InOutSine }
            }
        }
    }
    Rectangle { anchors.fill: parent; color: "black"; opacity: bg.shade; Behavior on opacity { NumberAnimation { duration: 2000 } } }
}
