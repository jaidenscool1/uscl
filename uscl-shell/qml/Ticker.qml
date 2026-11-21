import QtQuick

// The USCL ticker next to a game: how well it plays online through USCL, in one word or two.
// level: "online" (green), "beta" (blue), "soon" (amber), "offline" (grey). See Online::capability.
Rectangle {
    id: ticker
    property string text: ""
    property string level: ""
    property bool dim: false
    width: label.implicitWidth + 22
    height: label.implicitHeight + 6
    radius: height / 2
    opacity: dim ? 0.8 : 1
    color: level === "online" ? "#e02e9e4f" : level === "beta" ? "#e02f6fd8" : level === "soon" ? "#e0c98a1c" : "#c0505660"
    border.color: "#60ffffff"; border.width: 1
    Text {
        id: label
        anchors.centerIn: parent
        text: (ticker.level === "online" ? "● " : ticker.level === "offline" ? "" : "◐ ") + ticker.text
        color: "white"
        font.pixelSize: 19
        font.bold: true
    }
}
