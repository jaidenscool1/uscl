import QtQuick
import QtQuick.Effects

// A Material Design glyph with the XMB glow, or a picture (a game's icon)
Item {
    id: icon
    property string glyph: ""
    property string source: ""
    property real size: 96
    property bool glow: true

    width: source !== "" ? size * 1.82 : size   // ICON0.PNG is 320x176
    height: size

    Text {
        anchors.centerIn: parent
        visible: icon.source === ""
        text: icon.glyph
        color: "white"
        font.family: "Material Design Icons Desktop"
        font.pixelSize: icon.size * 0.78
        layer.enabled: icon.glow
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "white"
            shadowBlur: 0.7
            shadowOpacity: 0.55
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 0
        }
    }
    Image {
        anchors.fill: parent
        visible: icon.source !== ""
        source: icon.source
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        smooth: true
        mipmap: true
    }
}
