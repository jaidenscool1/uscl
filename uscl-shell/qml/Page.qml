import QtQuick

// An inner XMB screen (kodi-xmb gen_menus.py settings page, 1920x1080 stage): the breadcrumb column on the
// left (category icon above, the opened item with an arrow below) and the page's rows in an XMB column,
// values in a grey column on the right, the focused row's help under its name.
Item {
    id: page
    property string catIcon: ""
    property string parentIcon: ""
    property var rows: []           // [{ title, value, help, icon, disabled, action }]
    property int current: 0
    property bool fromHome: true
    signal sound(string name)
    signal back()

    readonly property int colLeft: 460      // 200 + SHIFT
    readonly property int focTop: 440
    readonly property int rowH: 84
    readonly property int focH: 150
    readonly property var cur: rows[current]

    function input(b) {
        if ((b === "back" || b === "left")) { sound("back"); back() }
        else if (b === "up" && current > 0) { current--; sound("cursor") }
        else if (b === "down" && current < rows.length - 1) { current++; sound("cursor") }
        else if (b === "l1" && current > 0) { current = Math.max(0, current - 5); sound("cursor") }
        else if (b === "r1" && current < rows.length - 1) { current = Math.min(rows.length - 1, current + 5); sound("cursor") }
        else if ((b === "accept" || b === "right" || b === "options") && cur) {
            if (cur.disabled) { if (b === "accept") sound("error"); return }
            if (b === "right" && !cur.choices) return
            sound("select")
            if (cur.action) cur.action()
        }
    }

    // ---- breadcrumb ----
    Item {
        id: crumb
        width: page.width; height: page.height
        transform: Translate { id: crumbShift }
        Image {
            id: catImg
            x: 225; y: 228; width: 104; height: 104
            source: page.catIcon; opacity: 0.85; mipmap: true; fillMode: Image.PreserveAspectFit
        }
        Image {
            id: parentImg
            x: 220; y: 440; width: 110; height: 110
            source: page.parentIcon; opacity: 0.85; mipmap: true; fillMode: Image.PreserveAspectFit
        }
        Image { id: arrow; x: 356; y: 479; width: 24; height: 32; source: "qrc:/qt/qml/Uscl/xmb/ui/arrow_left.png"; opacity: 0.75 }
    }

    // ---- rows ----
    Item {
        id: list
        width: page.width; height: page.height
        transform: Translate { id: listShift }

        Image {
            x: page.colLeft + 60; y: page.focTop - 60; width: 240; height: 240
            source: "qrc:/qt/qml/Uscl/xmb/halo.png"
            visible: page.rows.length > 0
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { from: 0.09; to: 0.25; duration: 1700; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.09; duration: 1700; easing.type: Easing.InOutSine }
            }
        }
        Repeater {
            model: page.rows
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool sel: index === page.current
                x: page.colLeft
                y: sel ? page.focTop
                   : index < page.current ? page.focTop - (page.current - index) * page.rowH
                   : page.focTop + page.focH + (index - page.current - 1) * page.rowH
                width: page.width - page.colLeft; height: sel ? page.focH : page.rowH
                visible: y > -page.rowH && y < page.height
                opacity: modelData.disabled ? 0.45 : 1
                Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutSine } }

                Image {
                    x: row.sel ? 150 : 158; y: row.sel ? 32 : 21
                    width: row.sel ? 58 : 42; height: width
                    source: row.modelData.icon || "qrc:/qt/qml/Uscl/xmb/ui/setting_dot.png"
                    opacity: row.sel ? 1 : 0.63; mipmap: true; fillMode: Image.PreserveAspectFit
                }
                Text {
                    x: 230; y: row.sel ? 32 : 0
                    width: 560; height: row.sel ? 58 : page.rowH
                    verticalAlignment: Text.AlignVCenter
                    text: row.modelData.title
                    color: row.sel ? "white" : "#a8ffffff"
                    font.pixelSize: row.sel ? 40 : 29
                    elide: Text.ElideRight
                    style: Text.Raised; styleColor: row.sel ? "#90000000" : "#70000000"
                }
                Text {
                    x: 820; y: row.sel ? 32 : 0
                    width: 520; height: row.sel ? 58 : page.rowH
                    verticalAlignment: Text.AlignVCenter
                    text: row.modelData.value || ""
                    color: row.sel ? "#f0ffffff" : "#80ffffff"
                    font.pixelSize: row.sel ? 29 : 25
                    elide: Text.ElideRight
                    style: Text.Raised; styleColor: "#70000000"
                }
                Text {
                    visible: row.sel && !!row.modelData.help
                    x: 232; y: 92; width: 1100; height: 38
                    verticalAlignment: Text.AlignVCenter
                    text: row.modelData.help || ""
                    color: "#a0ffffff"; font.pixelSize: 25
                    elide: Text.ElideRight
                    style: Text.Raised; styleColor: "#70000000"
                }
            }
        }
    }

    // ---- open / close: rows slide in from the right; from the crossbar the breadcrumb shrinks into place ----
    function open() {
        enter.restart()
    }
    ParallelAnimation {
        id: enter
        NumberAnimation { target: page; property: "opacity"; from: 0; to: 1; duration: 260 }
        NumberAnimation { target: listShift; property: "x"; from: 120; to: 0; duration: 260; easing.type: Easing.OutSine }
        NumberAnimation { target: crumbShift; property: "x"; from: page.fromHome ? 145 : 60; to: 0; duration: 300; easing.type: Easing.OutSine }
        NumberAnimation { target: catImg; property: "scale"; from: page.fromHome ? 1.44 : 1; to: 1; duration: 300; easing.type: Easing.OutSine }
        NumberAnimation { target: parentImg; property: "scale"; from: page.fromHome ? 1.36 : 1; to: 1; duration: 300; easing.type: Easing.OutSine }
        SequentialAnimation {
            PropertyAction { target: arrow; property: "opacity"; value: 0 }
            PauseAnimation { duration: 180 }
            NumberAnimation { target: arrow; property: "opacity"; to: 0.75; duration: 200 }
        }
    }
}
