pragma ComponentBehavior: Bound
import QtQuick

// All dashboard variants use the same selection, actions, and live models.
Item {
    id: home
    required property string theme
    required property var navigation
    property string userName: qsTr("User")
    property string avatar: ""
    signal navigate(string button)
    ThemePalette { id: themeStyle; theme: home.theme }
    readonly property bool nintendo: themeStyle.nintendo
    readonly property bool grid: themeStyle.metro || themeStyle.one
    readonly property var entries: navigation.items
    readonly property int selected: navigation.item
    readonly property var current: navigation.current
    readonly property real margin: Math.max(72, (width - 1760) / 2)
    property bool categoryFocus: false
    onThemeChanged: categoryFocus = false

    // Tile geometry follows the original asymmetric Metro / Xbox One dashboards.
    readonly property int groupSize: themeStyle.one ? 8 : 7
    function xboxRect(index) {
        var slot = index % groupSize
        var unit = shelf.width / (themeStyle.one ? 1368 : 1760)
        var shapes = themeStyle.one
            ? [[0,0,760,430], [774,0,220,430], [1008,0,360,132], [1008,149,360,132],
               [1008,298,360,132], [0,444,446,155], [460,444,446,155], [920,444,448,155]]
            : [[0,0,700,482], [714,0,336,234], [1064,0,336,234], [1414,0,336,234],
               [714,248,336,234], [1064,248,336,234], [1414,248,336,234]]
        var r = shapes[slot]
        return Qt.rect(r[0] * unit, r[1], r[2] * unit, r[3])
    }
    function verticalMove(direction) {
        var start = Math.floor(selected / groupSize) * groupSize
        var rect = xboxRect(selected)
        var best = -1, score = Number.MAX_VALUE
        for (var i = start; i < Math.min(entries.length, start + groupSize); i++) {
            var candidate = xboxRect(i)
            var dy = candidate.y + candidate.height / 2 - rect.y - rect.height / 2
            if (dy * direction <= 1) continue
            var dx = Math.abs(candidate.x + candidate.width / 2 - rect.x - rect.width / 2)
            var distance = Math.abs(dy) + dx * 3
            if (distance < score) { score = distance; best = i }
        }
        if (best >= 0) choose(best)
        else if (direction < 0) categoryFocus = true
    }

    function choose(index) {
        if (index >= 0 && index < entries.length && index !== selected) {
            navigation.setItem(index); navigation.sound("cursor")
        }
    }
    function input(button) {
        if (button === "l1" || button === "r1") {
            navigation.input(button === "l1" ? "left" : "right")
        } else if (button === "back") {
            categoryFocus = !categoryFocus; navigation.sound("back")
        } else if (categoryFocus) {
            if (button === "accept" || button === "down") categoryFocus = false
            else if (button === "left" || button === "right") navigation.input(button)
        } else if (themeStyle.blades) navigation.input(button)
        else if (button === "left" || button === "right") {
            choose(selected + (button === "right" ? 1 : -1))
        } else if (grid && (button === "up" || button === "down")) {
            verticalMove(button === "down" ? 1 : -1)
        } else if (button === "up") categoryFocus = true
        else if (button === "down") categoryFocus = true
        else navigation.input(button)
    }

    Rectangle { anchors.fill: parent; color: themeStyle.background }
    // PS4's blue field and PS5's game backdrop stay behind the quiet navigation row.
    Rectangle {
        anchors.fill: parent; visible: themeStyle.ps4
        gradient: Gradient {
            GradientStop { position: 0; color: "#062974" }
            GradientStop { position: .65; color: "#0955c0" }
            GradientStop { position: 1; color: "#062168" }
        }
    }
    Image {
        anchors.fill: parent; visible: themeStyle.ps4 || themeStyle.ps5 || themeStyle.one
        source: home.current ? (home.current.pic || "") : ""
        fillMode: Image.PreserveAspectCrop; asynchronous: true
        opacity: themeStyle.ps5 ? .65 : .22
    }
    Rectangle {
        anchors.fill: parent; visible: themeStyle.ps5
        gradient: Gradient {
            GradientStop { position: 0; color: "#20061020" }
            GradientStop { position: .5; color: "#40061020" }
            GradientStop { position: 1; color: "#ed030711" }
        }
    }
    // Original 360: curved silver surround and a green active blade.
    Rectangle {
        visible: themeStyle.blades; x: home.margin + 120; y: 174
        width: home.width - 2 * home.margin - 240; height: 746; radius: 90
        color: "#ced4c8"; border.width: 4; border.color: "#fafff5"
        Rectangle {
            anchors.fill: parent; anchors.margins: 10; radius: 82
            gradient: Gradient {
                GradientStop { position: 0; color: "#f8faf5" }
                GradientStop { position: .6; color: "#e6eadf" }
                GradientStop { position: 1; color: "#b8c5a9" }
            }
        }
    }
    // Switch system shortcuts are circles below the software strip.
    Item {
        id: categories
        x: home.margin
        y: home.nintendo ? 810 : themeStyle.ps5 ? 100 : 165
        width: home.width - 2 * home.margin
        height: home.nintendo ? 118 : themeStyle.blades ? 746 : 74
        Repeater {
            model: home.navigation.cats
            delegate: Rectangle {
                id: category
                objectName: "homeCategory" + index
                required property var modelData
                required property int index
                readonly property bool selected: index === home.navigation.cat
                readonly property real slot: categories.width / Math.max(1, home.navigation.cats.length)
                x: themeStyle.blades ? (index <= home.navigation.cat ? index * 48 : categories.width - (home.navigation.cats.length - index) * 48) : index * slot
                y: 0
                width: themeStyle.blades ? (selected ? categories.width - (home.navigation.cats.length - 1) * 48 : 48) : slot - 12
                height: categories.height
                color: themeStyle.blades ? (selected ? "transparent" : ["#b3bdab", "#9baa88", "#849d71", "#739853", "#a6b59b", "#bdc4b8"][index % 6]) : "transparent"
                radius: themeStyle.blades ? 24 : 0
                border.width: themeStyle.blades && !selected ? 2 : 0; border.color: "#f0f4e9"
                Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                Image {
                    visible: themeStyle.ps4 || themeStyle.ps5
                    anchors.horizontalCenter: parent.horizontalCenter; y: 0; width: 42; height: 42
                    source: category.modelData.icon || ""; fillMode: Image.PreserveAspectFit; mipmap: true
                    opacity: category.selected ? 1 : .65
                }
                Rectangle {
                    visible: !themeStyle.blades && !home.nintendo && !themeStyle.ps4 && !themeStyle.ps5
                    x: 0; y: parent.height - 3; width: parent.width; height: home.categoryFocus && category.selected ? 4 : 2
                    color: category.selected ? themeStyle.accent : "transparent"
                }
                Rectangle {
                    visible: home.nintendo; anchors.horizontalCenter: parent.horizontalCenter
                    width: 84; height: 84; radius: 42
                    color: themeStyle.lightSwitch ? "#777777" : "#414141"; border.width: category.selected ? 4 : 0; border.color: themeStyle.accent
                    Image { anchors.centerIn: parent; width: 48; height: 48; source: category.modelData.icon || ""; fillMode: Image.PreserveAspectFit; mipmap: true }
                }
                Text {
                    anchors.centerIn: parent
                    width: themeStyle.blades ? 500 : parent.width - 8
                    rotation: themeStyle.blades && !category.selected ? -90 : 0
                    visible: !themeStyle.blades || !category.selected
                    text: category.modelData.name
                    color: category.selected ? themeStyle.text : themeStyle.muted
                    font.pixelSize: home.nintendo ? 23 : themeStyle.metro ? 34 : 28
                    y: home.nintendo ? 86 : 0
                    horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                    anchors.verticalCenterOffset: home.nintendo ? 49 : themeStyle.ps4 || themeStyle.ps5 ? 27 : 0
                }
                MouseArea {
                    enabled: !themeStyle.blades || !category.selected
                    anchors.fill: parent
                    onClicked: { home.navigation.cat = index; home.categoryFocus = false; home.navigation.sound("category") }
                }
            }
        }
    }
    Text {
        x: themeStyle.blades ? home.margin + 330 : themeStyle.one ? home.margin + 230 : home.margin
        y: themeStyle.ps5 ? 630 : home.nintendo ? 245 : themeStyle.blades ? 245 : 255
        width: home.width - x - home.margin - 80; height: themeStyle.ps5 ? 156 : 76
        text: themeStyle.blades && home.navigation.cats[home.navigation.cat] ? home.navigation.cats[home.navigation.cat].name : home.current ? home.current.title : qsTr("No items available")
        color: themeStyle.text; font.pixelSize: themeStyle.ps5 ? 62 : themeStyle.blades ? 42 : 36
        font.weight: Font.Light; elide: Text.ElideRight
        maximumLineCount: themeStyle.ps5 ? 2 : 1; wrapMode: Text.Wrap
    }
    Item {
        id: shelf
        objectName: "softwareShelf"
        x: themeStyle.blades ? home.margin + 330 : themeStyle.one ? home.margin + 230 : home.margin
        y: themeStyle.ps5 ? 216 : home.nintendo ? 340 : 350
        width: themeStyle.blades ? 760 : home.width - x - home.margin
        height: themeStyle.ps5 ? 170 : home.nintendo ? 390 : themeStyle.one ? 605 : 530
        clip: true
        WheelHandler {
            onWheel: function(event) {
                home.categoryFocus = false
                home.choose(home.selected + (event.angleDelta.y < 0 ? 1 : -1))
                event.accepted = true
            }
        }
        Repeater {
            model: home.entries
            delegate: Rectangle {
                id: tile
                objectName: "homeTile" + index
                required property var modelData
                required property int index
                readonly property bool selected: index === home.selected
                readonly property rect metroRect: home.xboxRect(index)
                readonly property bool inView: home.grid ? Math.floor(index / home.groupSize) === Math.floor(home.selected / home.groupSize) : x + width > 0 && x < shelf.width && y + height > 0 && y < shelf.height
                visible: inView
                readonly property int delta: index - home.selected
                width: themeStyle.blades ? shelf.width : home.grid ? metroRect.width : home.nintendo ? 362 : themeStyle.ps5 ? (selected ? 150 : 112) : (selected ? 380 : 290)
                height: themeStyle.blades ? 92 : home.grid ? metroRect.height : home.nintendo ? 362 : themeStyle.ps5 ? width : width
                x: themeStyle.blades ? 0 : home.grid ? metroRect.x : themeStyle.ps5 ? (delta < 0 ? delta * 126 : delta === 0 ? 0 : 166 + (delta - 1) * 126) : home.nintendo ? delta * 386 : delta < 0 ? delta * 308 : delta === 0 ? 0 : 398 + (delta - 1) * 308
                y: themeStyle.blades ? delta * 100 : home.grid ? metroRect.y : 8
                radius: themeStyle.ps5 ? 18 : themeStyle.blades ? 9 : 0
                color: themeStyle.blades ? (selected ? "#5d9630" : "transparent") : themeStyle.xbox ? "#107c10" : home.nintendo ? (themeStyle.lightSwitch ? "#656565" : "#414141") : "#16223b"
                border.width: selected && !home.categoryFocus ? (home.nintendo ? 5 : 3) : 0
                border.color: themeStyle.blades ? "#b9ec8d" : themeStyle.xbox ? "#ffffff" : themeStyle.accent
                opacity: modelData.disabled ? .45 : 1
                Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                Rectangle { visible: themeStyle.blades; x: 17; y: 13; width: 66; height: 66; radius: 33; color: "#6b8457" }
                Image {
                    x: themeStyle.blades ? 20 : 8; y: 8
                    width: themeStyle.blades ? 66 : parent.width - 16
                    height: themeStyle.blades ? 66 : parent.height - (themeStyle.xbox ? 58 : 16)
                    source: tile.inView ? (tile.modelData.icon || "") : ""; fillMode: Image.PreserveAspectFit
                    asynchronous: true; mipmap: true
                }
                Text {
                    visible: themeStyle.xbox
                    x: themeStyle.blades ? 112 : 16; y: themeStyle.blades ? 0 : parent.height - 54
                    width: parent.width - x - 16; height: themeStyle.blades ? parent.height : 50
                    text: tile.modelData.title; color: themeStyle.blades && !tile.selected ? themeStyle.text : "white"
                    font.pixelSize: themeStyle.blades ? 34 : 25; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                }
                Rectangle {
                    visible: !!tile.modelData.badge; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                    width: badge.implicitWidth + 20; height: 34; radius: 17; color: "#f1b73d"
                    Text { id: badge; anchors.centerIn: parent; text: tile.modelData.badge || ""; color: "#111111"; font.pixelSize: 21 }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        home.categoryFocus = false
                        if (mouse.button === Qt.RightButton) { home.choose(index); home.navigate("options") }
                        else if (tile.selected) home.navigate("accept")
                        else home.choose(index)
                    }
                }
            }
        }
    }
    Item {
        visible: themeStyle.one
        x: home.margin; y: 350; width: 190; height: 430
        Image { anchors.horizontalCenter: parent.horizontalCenter; y: 20; width: 150; height: 150; source: home.avatar; mipmap: true; fillMode: Image.PreserveAspectFit }
        Text { y: 195; width: parent.width; text: home.userName; color: "white"; font.pixelSize: 29; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
        Text { y: 290; width: parent.width; text: qsTr("My profile"); color: "#bfc5bf"; font.pixelSize: 24 }
        MouseArea { anchors.fill: parent; onClicked: { home.navigation.cat = 0; home.navigation.setItem(0); home.navigate("accept") } }
    }
    Rectangle {
        visible: themeStyle.blades
        x: 1240; y: 350; width: 380; height: 480; radius: 16
        color: "#c6d4b8"; border.width: 2; border.color: "#eff7e7"
        Image { x: 55; y: 30; width: 270; height: 230; source: home.current ? (home.current.icon || "") : ""; fillMode: Image.PreserveAspectFit; mipmap: true }
        Text { x: 25; y: 278; width: parent.width - 50; height: 80; text: home.current ? home.current.title : ""; color: themeStyle.text; font.pixelSize: 29; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
        Text { x: 25; y: 375; width: parent.width - 50; height: 90; text: home.current ? (home.current.desc || "") : ""; color: themeStyle.muted; font.pixelSize: 23; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
    }
    Text {
        x: themeStyle.blades ? home.margin + 330 : home.margin
        visible: !themeStyle.blades
        y: themeStyle.ps5 ? 800 : home.nintendo ? 730 : themeStyle.one ? 960 : 904
        width: home.width - x - home.margin; height: themeStyle.one ? 30 : 70
        text: home.current ? (home.current.desc || "") : ""
        color: themeStyle.muted; font.pixelSize: 26; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
    }
    Rectangle {
        visible: themeStyle.ps5 || themeStyle.ps4
        x: home.margin; y: themeStyle.ps5 ? 910 : 780; width: themeStyle.ps5 ? 300 : 380; height: 66
        radius: themeStyle.ps5 ? 33 : 0
        color: themeStyle.ps5 ? "#ecffffff" : "#604ca0f0"
        Text { anchors.centerIn: parent; text: home.current && home.current.console ? qsTr("Play Game") : qsTr("Open"); color: themeStyle.ps5 ? "#111820" : "white"; font.pixelSize: 29 }
        MouseArea { anchors.fill: parent; enabled: !!home.current; onClicked: home.navigate("accept") }
    }
    Rectangle { x: home.margin; y: 990; width: home.width - home.margin * 2; height: 1; color: themeStyle.nintendo ? "#666666" : "#30757575" }
    Text {
        x: home.margin; y: 1010; width: home.width - 2 * home.margin
        text: themeStyle.blades ? qsTr("↑ ↓ Select    ← → Blades    A / Enter: Open    Y / Space: Options")
            : qsTr("%1 / Enter: Open    %2 / Esc: Categories    %3 / Space: Options    L1 / R1: Categories").arg(themeStyle.confirmGlyph).arg(themeStyle.backGlyph).arg(themeStyle.optionsGlyph)
        color: themeStyle.muted; font.pixelSize: 24; elide: Text.ElideRight
    }
}
