import QtQuick

// The XMB side menu (options, choices) and the info card; slides in from the right
Item {
    id: panel
    property real u: 1
    property bool open: false
    property string heading: ""
    property var options: []      // [{ title, action }]
    property string text: ""      // info card when there are no options
    property int current: 0
    signal sound(string name)

    function show(h, opts, txt, start) {
        heading = h; options = opts || []; text = txt || ""; current = start || 0; open = true
    }
    function close() { open = false }
    function input(b) {
        if (b === "back" || b === "options" || b === "info") { sound("back"); close() }
        else if (b === "up" && current > 0) { current--; sound("cursor") }
        else if (b === "down" && current < options.length - 1) { current++; sound("cursor") }
        else if (b === "accept") {
            if (options.length) { sound("select"); var a = options[current].action; close(); if (a) a() }
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
        width: panel.options.length ? parent.width * 0.32 : parent.width * 0.5
        height: panel.options.length ? parent.height : body.implicitHeight + 160 * u
        x: panel.options.length ? parent.width - width + (panel.open ? 0 : 60 * u) : (parent.width - width) / 2
        y: panel.options.length ? 0 : (parent.height - height) / 2
        color: "#e6101018"
        border.color: panel.options.length ? "transparent" : "#40ffffff"
        radius: panel.options.length ? 0 : 12 * u
        Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        Text {
            id: head
            x: 48 * u; y: (panel.options.length ? 130 : 48) * u   // clear of the status pill
            width: parent.width - 96 * u
            text: panel.heading
            color: "white"
            font.pixelSize: 34 * u
            elide: Text.ElideRight
        }
        ListView {
            visible: panel.options.length > 0
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
                required property var modelData
                required property int index
                width: ListView.view.width; height: 64 * u
                radius: 6 * u
                color: index === panel.current ? "#33ffffff" : "transparent"
                border.color: index === panel.current ? "#80ffffff" : "transparent"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 20 * u
                    text: parent.modelData.title
                    color: "white"
                    font.pixelSize: 28 * u
                }
            }
        }
        Text {
            id: body
            visible: panel.options.length === 0
            x: 48 * u; y: head.y + head.height + 30 * u
            width: parent.width - 96 * u
            text: panel.text
            color: "#e0ffffff"
            font.pixelSize: 24 * u
            wrapMode: Text.Wrap
            lineHeight: 1.2
        }
    }
}
