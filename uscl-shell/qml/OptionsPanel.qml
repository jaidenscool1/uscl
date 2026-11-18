import QtQuick

// The triangle options panel (kodi-xmb DialogContextMenu): dims the screen, a dark panel slides in from the
// right edge, options are centred on it; the focused one sits on the focus bar. Also used for choosing a
// setting's value (the current one is marked with a dot).
Item {
    id: panel
    property string theme: "ps3"
    ThemePalette { id: themeStyle; theme: panel.theme }
    property bool open: false
    property string openingTheme: theme
    onThemeChanged: { if (!open) { dimFade.complete(); sideSlide.complete(); } }
    enabled: open
    property var options: []       // [{ title, action, checked, disabled }]
    property int current: 0
    signal sound(string name)

    function show(opts, start) {
        options = opts || []
        current = Math.max(0, Math.min(options.length - 1, start !== undefined ? start : Math.max(0, options.findIndex(function(o) { return o.checked }))))
        openingTheme = theme
        open = true
        list.positionViewAtIndex(current, ListView.Contain)
    }
    function close() { open = false }
    function input(b) {
        if (b === "back" || b === "options" || b === "left") { sound("back"); close() }
        else if (b === "up" && current > 0) { current--; sound("cursor") }
        else if (b === "down" && current < options.length - 1) { current++; sound("cursor") }
        else if (b === "accept" && options[current]) {
            var o = options[current]
            if (o.disabled) { sound("error"); return }
            sound("select"); close()
            if (o.action) o.action()
        }
    }

    anchors.fill: parent
    visible: dim.opacity > 0 && openingTheme === theme

    Rectangle {
        id: dim
        anchors.fill: parent; color: "black"
        opacity: panel.open ? 0.55 : 0
        Behavior on opacity { NumberAnimation { id: dimFade; duration: panel.open ? 220 : 160 } }
    }
    MouseArea { anchors.fill: parent; onClicked: panel.input("back"); onWheel: function(wheel) { wheel.accepted = true } }
    Item {
        id: side
        width: 700; height: parent.height
        x: parent.width - 700 + (panel.open ? 0 : 640)
        Behavior on x { NumberAnimation { id: sideSlide; duration: panel.open ? 240 : 180; easing.type: panel.open ? Easing.OutSine : Easing.InSine } }
        MouseArea { anchors.fill: parent }
        Rectangle { anchors.fill: parent; visible: panel.theme !== "ps3"; color: themeStyle.panel }
        Image { visible: panel.theme === "ps3"; anchors.fill: parent; source: "qrc:/qt/qml/Uscl/xmb/ui/options_panel.png" }
        ListView {
            id: list
            x: 110; width: 560
            height: Math.min(contentHeight, 780)
            y: (parent.height - height) / 2
            clip: true
            model: panel.options
            currentIndex: panel.current
            interactive: true
            highlightMoveDuration: 120
            preferredHighlightBegin: height / 2 - 32
            preferredHighlightEnd: height / 2 + 32
            highlightRangeMode: contentHeight > height ? ListView.ApplyRange : ListView.NoHighlightRange
            delegate: Item {
                id: opt
                required property var modelData
                required property int index
                readonly property bool sel: index === panel.current
                width: 560; height: 64
                opacity: modelData.disabled ? 0.45 : 1
                BorderImage {
                    anchors.fill: parent; visible: opt.sel && panel.theme === "ps3"
                    source: "qrc:/qt/qml/Uscl/xmb/ui/focus_bar.png"
                    border { left: 40; right: 120 }
                }
                Rectangle {
                    anchors.fill: parent; visible: opt.sel && panel.theme !== "ps3"
                    color: themeStyle.focusFill; border.width: themeStyle.nintendo ? 3 : 1; border.color: themeStyle.accent
                }
                Image {
                    visible: panel.theme === "ps3" && !!opt.modelData.checked
                    x: 18; y: 18; width: 28; height: 28
                    source: "qrc:/qt/qml/Uscl/xmb/ui/setting_dot.png"; mipmap: true
                }
                Text { visible: panel.theme !== "ps3" && !!opt.modelData.checked; x: 22; height: 64; verticalAlignment: Text.AlignVCenter; text: "●"; color: opt.sel ? themeStyle.focusText : themeStyle.text; font.pixelSize: 23 }
                Text {
                    x: 60; width: 480; height: 64
                    verticalAlignment: Text.AlignVCenter
                    text: opt.modelData.title
                    color: panel.theme === "ps3" ? (opt.sel ? "white" : "#b4ffffff") : opt.sel ? themeStyle.focusText : themeStyle.text
                    font.pixelSize: 29
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { panel.current = index; panel.input("accept") }
                }
            }
        }
    }
}
