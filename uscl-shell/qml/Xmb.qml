import QtQuick

// The crossbar (kodi-xmb gen_skin.py geometry, 1920x1080 stage): categories left/right on a bar at y 190,
// the focused category's items up/down. The focused row sits under the bar, earlier rows above it.
Item {
    id: xmb
    property var cats: []          // [{ name, icon, items: [{ title, desc, icon, wide, disabled, action, options, preview }] }]
    property int cat: 0
    property var itemIndex: []     // remembered row per category
    signal sound(string name)
    signal showOptions(var entry)

    readonly property var items: cats.length ? cats[cat].items : []
    readonly property int item: Math.min(itemIndex[cat] || 0, Math.max(0, items.length - 1))
    readonly property var current: items[item]

    // geometry
    readonly property int catLeft: 120
    readonly property int catTop: 190
    readonly property int catW: 200
    readonly property int colX: 200          // item column left (COL_X - PAD)
    readonly property int focTop: 185        // focused row
    readonly property int rowH: 120
    readonly property int focH: 405

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
    }

    onCatChanged: { colShow.restart(); previewShow.restart() }
    onItemChanged: previewShow.restart()

    // ---- categories ----
    Repeater {
        model: xmb.cats
        delegate: Item {
            id: c
            required property var modelData
            required property int index
            readonly property bool sel: index === xmb.cat
            x: xmb.catLeft + (index - xmb.cat + 1) * xmb.catW
            y: xmb.catTop
            width: xmb.catW; height: 240
            visible: x > -xmb.catW && x < xmb.width
            Behavior on x { NumberAnimation { duration: 260; easing.type: Easing.OutSine } }

            Image {
                source: c.modelData.icon
                width: c.sel ? 150 : 100; height: width
                x: (200 - width) / 2; y: c.sel ? 15 : 40
                opacity: c.sel ? 1 : 0.63
                mipmap: true
                Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutSine } }
                Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutSine } }
            }
            Text {
                x: -150; y: 168; width: 500; height: 40
                horizontalAlignment: Text.AlignHCenter
                text: c.modelData.name
                color: "white"; font.pixelSize: 31
                style: Text.Raised; styleColor: "#90000000"
                opacity: c.sel ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 220 } }
            }
        }
    }

    // ---- the focused category's items ----
    Item {
        id: column
        width: xmb.width; height: xmb.height
        transform: Translate { id: colShift }
        ParallelAnimation {
            id: colShow
            NumberAnimation { target: column; property: "opacity"; from: 0; to: 1; duration: 220 }
            NumberAnimation { target: colShift; property: "y"; from: 40; to: 0; duration: 260; easing.type: Easing.OutSine }
        }

        // the pulsing halo behind the focused icon
        Image {
            x: xmb.colX + 60; y: xmb.focTop + 150; width: 320; height: 320
            source: "qrc:/qt/qml/Uscl/xmb/halo.png"
            visible: xmb.items.length > 0
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { from: 0.11; to: 0.31; duration: 1700; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.11; duration: 1700; easing.type: Easing.InOutSine }
            }
        }

        Repeater {
            model: xmb.items
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool sel: index === xmb.item
                readonly property bool wide: !!modelData.wide      // game icons (ICON0.PNG, 320x176)
                x: xmb.colX
                y: sel ? xmb.focTop
                   : index < xmb.item ? xmb.focTop - (xmb.item - index) * xmb.rowH
                   : xmb.focTop + xmb.focH + (index - xmb.item - 1) * xmb.rowH
                width: xmb.width - xmb.colX
                height: sel ? xmb.focH : xmb.rowH
                visible: y > -xmb.rowH && y < xmb.height
                opacity: modelData.disabled ? 0.45 : 1
                Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutSine } }

                // icon: 100 px in the list, 150 px focused (wide game icons keep their shape)
                Image {
                    id: icon
                    readonly property real h: row.sel ? 150 : 100
                    width: row.wide ? h * 1.82 : h; height: h
                    x: 220 - width / 2
                    y: row.sel ? 235 : 10
                    source: row.modelData.icon || ""
                    fillMode: Image.PreserveAspectFit
                    opacity: row.sel ? 1 : 0.71
                    asynchronous: true; mipmap: true
                    Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutSine } }
                }
                Text {
                    x: row.sel ? (row.wide ? 220 + icon.width / 2 + 40 : 325) : (row.wide ? 220 + icon.width / 2 + 40 : 310)
                    y: row.sel ? 262 : 10
                    height: row.sel ? 56 : 100
                    width: 1200
                    verticalAlignment: Text.AlignVCenter
                    text: row.modelData.title
                    color: row.sel ? "white" : "#b4ffffff"
                    font.pixelSize: row.sel ? 40 : 29
                    elide: Text.ElideRight
                    style: Text.Raised; styleColor: row.sel ? "#90000000" : "#70000000"
                }
                Text {
                    visible: row.sel && !!row.modelData.desc
                    x: row.wide ? 220 + icon.width / 2 + 42 : 327
                    y: 322; height: 40; width: 1200
                    verticalAlignment: Text.AlignVCenter
                    text: row.modelData.desc || ""
                    color: "#a8ffffff"; font.pixelSize: 25
                    elide: Text.ElideRight
                    style: Text.Raised; styleColor: "#70000000"
                }
            }
        }
    }

    // ---- what the focused item holds, faint on the right ----
    Column {
        id: preview
        x: 1200; y: 438
        width: 620
        opacity: 0
        transform: Translate { id: previewShift }
        readonly property var labels: xmb.current && xmb.current.preview ? xmb.current.preview.slice(0, 6) : []
        Repeater {
            model: preview.labels
            delegate: Row {
                required property var modelData
                height: 50; spacing: 16
                Image { y: 7; width: 36; height: 36; source: xmb.current ? xmb.current.icon : ""; opacity: 0.56; mipmap: true; fillMode: Image.PreserveAspectFit }
                Text { height: 50; verticalAlignment: Text.AlignVCenter; text: parent.modelData; color: "#b0ffffff"; font.pixelSize: 25; style: Text.Raised; styleColor: "#60000000" }
            }
        }
        SequentialAnimation {
            id: previewShow
            PropertyAction { target: preview; property: "opacity"; value: 0 }
            PauseAnimation { duration: 280 }
            ParallelAnimation {
                NumberAnimation { target: preview; property: "opacity"; from: 0; to: 1; duration: 260 }
                NumberAnimation { target: previewShift; property: "x"; from: 24; to: 0; duration: 260; easing.type: Easing.OutSine }
            }
        }
    }
}
