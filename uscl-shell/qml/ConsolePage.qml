pragma ComponentBehavior: Bound
import QtQuick

// Shared inner screens preserve every console action, while following each theme's
// list / software-shelf treatment rather than falling back to a PS3 screen.
Item {
    id: screen
    required property var controller
    required property string theme
    property string heading: ""
    ThemePalette { id: themeStyle; theme: screen.theme }
    readonly property bool software: controller.rows.some(function(row) { return !!row.console })
    readonly property real contentLeft: themeStyle.nintendo ? 590 : themeStyle.blades ? 400 : 240

    Rectangle { anchors.fill: parent; color: themeStyle.background; opacity: .96 }
    Text {
        x: 90; y: 160; width: parent.width - 180; height: 86
        text: screen.heading || qsTr("Settings")
        color: themeStyle.text; font.pixelSize: 48; font.weight: Font.Light; elide: Text.ElideRight
    }
    Rectangle { x: 90; y: 268; width: parent.width - 180; height: 1; color: themeStyle.muted; opacity: .35 }
    Item {
        visible: themeStyle.nintendo || themeStyle.blades
        x: 90; y: 320; width: screen.contentLeft - 150; height: 440
        Image { x: 50; y: 20; width: Math.min(230, parent.width - 70); height: width; source: screen.controller.parentIcon; fillMode: Image.PreserveAspectFit; mipmap: true }
        Text { x: 0; y: 310; width: parent.width; height: 120; text: screen.controller.cur ? (screen.controller.cur.help || screen.controller.cur.desc || "") : ""; color: themeStyle.muted; font.pixelSize: 25; wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight }
    }
    ListView {
        id: list
        x: screen.contentLeft; y: 310; width: parent.width - x - 90; height: 610
        clip: true; spacing: screen.software ? 22 : 0
        orientation: screen.software ? ListView.Horizontal : ListView.Vertical
        model: screen.controller.rows
        currentIndex: screen.controller.current
        highlightMoveDuration: 160
        preferredHighlightBegin: 0
        preferredHighlightEnd: screen.software ? 330 : height - 94
        highlightRangeMode: ListView.ApplyRange
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        delegate: Rectangle {
            id: entry
            required property var modelData
            required property int index
            readonly property bool selected: index === screen.controller.current
            width: screen.software ? 330 : list.width
            height: screen.software ? 390 : 94
            radius: themeStyle.radius
            color: selected ? themeStyle.focusFill : screen.software ? themeStyle.panel : "transparent"
            opacity: modelData.disabled ? .45 : 1
            border.width: selected ? (themeStyle.nintendo ? 4 : 2) : 0
            border.color: themeStyle.accent
            Rectangle {
                visible: screen.software || !!entry.modelData.icon
                x: screen.software ? 12 : 20; y: screen.software ? 12 : 21
                width: screen.software ? parent.width - 24 : 52; height: screen.software ? 290 : 52
                color: themeStyle.lightSwitch || themeStyle.blades || themeStyle.metro ? "#747474" : "transparent"
            }
            Image {
                x: screen.software ? 12 : 20; y: screen.software ? 12 : 21
                width: screen.software ? parent.width - 24 : 52
                height: screen.software ? 290 : 52
                source: entry.modelData.icon || ""
                visible: source.toString() !== ""
                fillMode: Image.PreserveAspectFit; asynchronous: true; mipmap: true
            }
            Text {
                x: screen.software ? 16 : (entry.modelData.icon ? 92 : 26)
                y: screen.software ? 312 : 0
                width: screen.software ? parent.width - 32 : parent.width * .57 - x
                height: screen.software ? 64 : parent.height
                text: entry.modelData.title
                color: entry.selected ? themeStyle.focusText : themeStyle.text
                font.pixelSize: 29; verticalAlignment: Text.AlignVCenter
                wrapMode: screen.software ? Text.Wrap : Text.NoWrap; maximumLineCount: 2; elide: Text.ElideRight
            }
            Text {
                visible: !screen.software; x: parent.width * .59; y: 0
                width: parent.width * .41 - 28; height: parent.height
                text: entry.modelData.value === undefined ? "" : String(entry.modelData.value)
                color: entry.selected ? themeStyle.focusText : themeStyle.muted; font.pixelSize: 25
                verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
            }
            Rectangle {
                visible: screen.software && !!entry.modelData.badge; x: 16; y: 20
                width: badge.implicitWidth + 24; height: 38; radius: 19; color: "#f1b73d"
                Text { id: badge; anchors.centerIn: parent; text: entry.modelData.badge || ""; color: "#151515"; font.pixelSize: 23 }
            }
            MouseArea {
                anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    var wasSelected = entry.selected
                    screen.controller.current = index
                    if (mouse.button === Qt.RightButton) screen.controller.input("options")
                    else if (wasSelected) screen.controller.input("accept")
                    else screen.controller.sound("cursor")
                }
            }
        }
    }
    Text {
        x: screen.contentLeft; y: 928; width: parent.width - x - 90; height: 52
        text: screen.controller.cur ? (screen.controller.cur.help || screen.controller.cur.desc || "") : ""
        color: themeStyle.muted; font.pixelSize: 23; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
    }
    Rectangle { x: 90; y: 983; width: parent.width - 180; height: 1; color: themeStyle.muted; opacity: .35 }
    Text {
        x: 90; y: 1008; width: parent.width - 180
        text: qsTr("%1 / Enter: Open    %2 / Esc: Back    %3 / Space: Options    Page Up / Down: Jump").arg(themeStyle.confirmGlyph).arg(themeStyle.backGlyph).arg(themeStyle.optionsGlyph)
        color: themeStyle.muted; font.pixelSize: 24; elide: Text.ElideRight
    }
}
