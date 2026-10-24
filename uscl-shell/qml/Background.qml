import QtQuick

// Animated XMB background: the tint is the player's colour theme
ShaderEffect {
    id: bg
    property color tint: "#5B1C80"
    property real time: 0
    property real aspect: width / Math.max(1, height)
    property bool running: true

    fragmentShader: "qrc:/qt/qml/Uscl/shaders/wave.frag.qsb"

    NumberAnimation on time {
        from: 0; to: 6283.0   // long loop; the shader only uses sin()
        duration: 6283.0 * 1000
        loops: Animation.Infinite
        running: bg.running
    }
    Behavior on tint { ColorAnimation { duration: 600 } }
}
