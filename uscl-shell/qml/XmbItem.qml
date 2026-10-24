import QtQuick

// One row of an XMB column: icon, title, and the subtitle while selected
Item {
    id: row
    property var entry: ({})
    property bool selected: false
    property real u: 1

    width: parent ? parent.width : 0
    height: 110 * u
    opacity: entry.disabled ? (selected ? 0.55 : 0.35) : (selected ? 1 : 0.6)
    Behavior on opacity { NumberAnimation { duration: 150 } }

    Icon {
        id: ic
        anchors.verticalCenter: parent.verticalCenter
        x: (96 * u - (entry.image ? 96 * u * 1.82 : 96 * u)) / 2 + (entry.image ? 48 * u : 0)
        size: (row.selected ? 96 : 76) * row.u
        glyph: entry.glyph || ""
        source: entry.image || ""
        glow: row.selected
        Behavior on size { NumberAnimation { duration: 150 } }
    }
    Column {
        anchors.verticalCenter: parent.verticalCenter
        x: (entry.image ? 96 * 1.82 + 28 : 128) * u
        spacing: 4 * u
        Text {
            text: entry.title || ""
            color: "white"
            font.pixelSize: (row.selected ? 40 : 34) * row.u
            font.weight: Font.Light
            style: Text.Raised; styleColor: "#40000000"
        }
        Text {
            visible: row.selected && !!entry.subtitle
            text: entry.subtitle || ""
            color: "#d0ffffff"
            font.pixelSize: 24 * row.u
        }
    }
}
