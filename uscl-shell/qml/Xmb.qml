import QtQuick

// The cross media bar: categories left/right, the selected category's items up/down
Item {
    id: xmb
    property real u: 1
    property var cats: []          // [{ name, glyph, items: [{ title, subtitle, glyph | image, disabled, action, options, info }] }]
    property int cat: 0
    property var itemIndex: []     // remembered row per category
    signal sound(string name)
    signal showOptions(var entry)
    signal showInfo(var entry)

    readonly property var items: cats.length ? cats[cat].items : []
    readonly property int item: Math.min(itemIndex[cat] || 0, Math.max(0, items.length - 1))
    readonly property var current: items[item]
    readonly property real catX: width * 0.22
    readonly property real barY: height * 0.25
    readonly property real step: 200 * u
    readonly property real rowH: 110 * u

    function setItem(i) {
        var a = itemIndex.slice()
        a[cat] = i
        itemIndex = a
    }

    function input(b) {
        if (b === "left" && cat > 0) { cat--; sound("category") }
        else if (b === "right" && cat < cats.length - 1) { cat++; sound("category") }
        else if (b === "up" && item > 0) { setItem(item - 1); sound("cursor") }
        else if (b === "down" && item < items.length - 1) { setItem(item + 1); sound("cursor") }
        else if (b === "l1" && item > 0) { setItem(Math.max(0, item - 5)); sound("cursor") }
        else if (b === "r1" && item < items.length - 1) { setItem(Math.min(items.length - 1, item + 5)); sound("cursor") }
        else if (b === "accept" && current) {
            if (current.disabled) sound("error")
            else { sound("select"); if (current.action) current.action() }
        }
        else if (b === "options" && current && current.options) { sound("select"); showOptions(current) }
        else if (b === "info" && current && current.info) { sound("select"); showInfo(current) }
    }

    onCatChanged: slide.restart()

    // categories
    Repeater {
        model: xmb.cats
        delegate: Item {
            id: catItem
            required property var modelData
            required property int index
            readonly property bool sel: index === xmb.cat
            width: 120 * xmb.u; height: 120 * xmb.u
            x: xmb.catX + (index - xmb.cat) * xmb.step - width / 2
            y: xmb.barY - height / 2
            opacity: sel ? 1 : 0.65
            Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            Behavior on opacity { NumberAnimation { duration: 220 } }

            Icon {
                anchors.centerIn: parent
                glyph: catItem.modelData.glyph
                size: (catItem.sel ? 104 : 82) * xmb.u
                glow: catItem.sel
                Behavior on size { NumberAnimation { duration: 220 } }
            }
            Text {
                visible: catItem.sel
                anchors.top: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                text: catItem.modelData.name
                color: "white"
                font.pixelSize: 30 * xmb.u
                style: Text.Raised; styleColor: "#40000000"
            }
        }
    }

    // the selected category's items
    Item {
        id: column
        x: xmb.catX - 48 * xmb.u
        width: xmb.width - x - 60 * xmb.u
        height: xmb.height
        transform: Translate { id: shift }

        ParallelAnimation {
            id: slide
            NumberAnimation { target: column; property: "opacity"; from: 0; to: 1; duration: 240 }
            NumberAnimation { target: shift; property: "x"; from: 50 * xmb.u; to: 0; duration: 240; easing.type: Easing.OutCubic }
        }

        Repeater {
            model: xmb.items
            delegate: XmbItem {
                required property var modelData
                required property int index
                entry: modelData
                selected: index === xmb.item
                u: xmb.u
                // the selected row sits under the category label; rows before it move up above the bar
                y: index === xmb.item ? xmb.barY + 105 * xmb.u
                   : index > xmb.item ? xmb.barY + 105 * xmb.u + 125 * xmb.u + (index - xmb.item - 1) * xmb.rowH
                   : xmb.barY - 75 * xmb.u - (xmb.item - index) * xmb.rowH
                visible: y > -height && y < xmb.height
                Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            }
        }
    }
}
