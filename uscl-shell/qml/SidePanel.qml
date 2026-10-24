import QtQuick

// The XMB side menu (options, choices) and the info card; slides in from the right
Item {
    id: panel
    property real u: 1
    property bool open: false
    property string heading: ""
    property var options: []      // [{ title, subtitle, action, disabled }]
    property string text: ""      // info card when there are no options
    property var live: null       // function returning the card text, re-read as what it uses changes
    property var progress: null   // function returning 0..1 for a progress bar, or -1 for none
    property var backAction: null // what ○ does instead of closing (e.g. up one folder)
    property int current: 0
    signal sound(string name)

    // opts: { back: fn, live: fn, progress: fn }
    function show(h, opts, txt, start, extra) {
        heading = h; options = opts || []; text = txt || ""; current = start || 0
        backAction = extra && extra.back ? extra.back : null
        live = extra && extra.live ? extra.live : null
        progress = extra && extra.progress ? extra.progress : null
        open = true
    }
    function close() { open = false }
    function input(b) {
        if (b === "back") { sound("back"); if (backAction) backAction(); else close() }
        else if (b === "options" || b === "info") { sound("back"); close() }
        else if (b === "up" && current > 0) { current--; sound("cursor") }
        else if (b === "down" && current < options.length - 1) { current++; sound("cursor") }
        else if (b === "l1" && current > 0) { current = Math.max(0, current - 8); sound("cursor") }
        else if (b === "r1" && current < options.length - 1) { current = Math.min(options.length - 1, current + 8); sound("cursor") }
        else if (b === "accept") {
            if (options.length) {
                var o = options[current]
                if (o.disabled) { sound("error"); return }
                sound("select")
                if (!o.keepOpen) close()
                if (o.action) o.action()
            }
            else { sound("back"); close() }
        }
    }

    anchors.fill: parent
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 180 } }

    Rectangle { anchors.fill: parent; color: "#66000000" }   // dims the XMB behind

    Rectangle {
        id: card
        readonly property bool list: panel.options.length > 0
        width: list ? parent.width * 0.36 : parent.width * 0.5
        height: list ? parent.height : Math.min(parent.height * 0.8, body.implicitHeight + bar.height + 170 * u)
        x: list ? parent.width - width + (panel.open ? 0 : 60 * u) : (parent.width - width) / 2
        y: list ? 0 : (parent.height - height) / 2
        color: "#e6101018"
        border.color: list ? "transparent" : "#40ffffff"
        radius: list ? 0 : 12 * u
        Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        Text {
            id: head
            x: 48 * u; y: (card.list ? 130 : 48) * u   // clear of the status pill
            width: parent.width - 96 * u
            text: panel.heading
            color: "white"
            font.pixelSize: 34 * u
            elide: Text.ElideMiddle
        }
        ListView {
            visible: card.list
            x: 48 * u; y: head.y + head.height + 40 * u
            width: parent.width - 96 * u
            height: parent.height - y - 60 * u
            clip: true
            spacing: 6 * u
            model: panel.options
            currentIndex: panel.current
            highlightMoveDuration: 120
            preferredHighlightBegin: height * 0.3
            preferredHighlightEnd: height * 0.7
            highlightRangeMode: ListView.ApplyRange
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool sel: index === panel.current
                width: ListView.view.width
                height: (modelData.subtitle ? 84 : 64) * u
                radius: 6 * u
                color: sel ? "#33ffffff" : "transparent"
                border.color: sel ? "#80ffffff" : "transparent"
                opacity: modelData.disabled ? 0.45 : 1
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 20 * u
                    width: parent.width - 40 * u
                    Text {
                        width: parent.width
                        text: row.modelData.title
                        color: "white"
                        font.pixelSize: 28 * u
                        elide: Text.ElideMiddle
                    }
                    Text {
                        visible: !!row.modelData.subtitle
                        width: parent.width
                        text: row.modelData.subtitle || ""
                        color: "#b0ffffff"
                        font.pixelSize: 20 * u
                        elide: Text.ElideMiddle
                    }
                }
            }
        }
        Text {
            id: body
            visible: !card.list
            x: 48 * u; y: head.y + head.height + 30 * u
            width: parent.width - 96 * u
            text: panel.live ? panel.live() : panel.text
            color: "#e0ffffff"
            font.pixelSize: 24 * u
            wrapMode: Text.Wrap
            lineHeight: 1.25
        }
        Rectangle {
            id: bar
            readonly property real value: panel.progress ? panel.progress() : -1
            visible: !card.list && value >= 0
            x: 48 * u; y: body.y + body.height + 24 * u
            width: parent.width - 96 * u; height: visible ? 10 * u : 0
            radius: height / 2
            color: "#33ffffff"
            Rectangle { width: parent.width * Math.max(0, Math.min(1, bar.value)); height: parent.height; radius: parent.radius; color: "white" }
        }
    }
}
