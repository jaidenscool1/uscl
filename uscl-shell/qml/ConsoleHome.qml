pragma ComponentBehavior: Bound
import QtQuick

// Console structure is presentation only: all actions still use USCL's live models.
Item {
    id: home
    required property string theme
    required property var navigation
    property string userName: qsTr("User")
    property string avatar: "qrc:/qt/qml/Uscl/xmb/avatars/uscl.png"
    property bool animate: true
    property var pinnedIds: []
    signal navigate(string button)
    signal sectionRequested(string section)
    ThemePalette {
        id: style
        theme: home.theme
    }
    readonly property bool nintendo: style.nintendo
    readonly property bool grid: style.metro || style.one
    readonly property real margin: 96
    property string region: "software"
    property bool categoryFocus: region === "functions"
    property int shortcutIndex: 0
    property int tab: 0
    property int tileIndex: 0
    property int softwareIndex: 0
    property int detailsAction: 0
    property bool guideOpen: false
    readonly property int gamesCategory: {
        for (var i = 0; i < navigation.cats.length; i++)
            if (navigation.cats[i].id === "games")
                return i;
        return 0;
    }
    readonly property var games: navigation.cats[gamesCategory] ? navigation.cats[gamesCategory].items.filter(
                                                                      function (e) {
                                                                          return !!e.console;
                                                                      }) : []
    function shortcut(key, title, icon) {
        var source = navigation && navigation.cats && navigation.cats[gamesCategory] ? navigation.cats[gamesCategory].items.find(function(e) { return e.id === key }) : null
        return Object.assign({}, source || {}, {
            key: key, title: title, icon: "qrc:/qt/qml/Uscl/xmb/icons/"+icon+".png"
        })
    }
    readonly property var library: shortcut("library", nintendo ? qsTr("All Software") : qsTr("Game Library"),
                                            "cat_game")
    readonly property var entries: games.slice(0, 12).concat([library])
    readonly property int selected: Math.max(0, Math.min(softwareIndex, entries.length - 1))
    readonly property var current: entries[selected]
    readonly property var shortcuts: {
        var news = shortcut("changelog", qsTr("USCL News"), "it_system")
        var add = shortcut("add", qsTr("Add Games"), "it_addgames")
        var online = shortcut("online", qsTr("Online Play"), "cat_network")
        var party = shortcut("party", qsTr("Party"), "cat_party")
        var friends = shortcut("friends", qsTr("Friends"), "cat_friends")
        var profile = shortcut("profile", nintendo ? qsTr("My Page") : qsTr("Profile"), "cat_users")
        var controllers = shortcut("controllers", qsTr("Controllers"), "it_gamepad")
        var settings = shortcut("settings", qsTr("System Settings"), "cat_settings")
        var power = shortcut("power", qsTr("Power"), "it_power")
        var updates = shortcut("updates", qsTr("Downloads"), "it_update")
        if (style.lightSwitch)
            return [profile, online, party, news, add, library, friends, controllers, updates, settings, power]
        if (nintendo)
            return [profile, news, add, library, controllers, settings, power]
        if (style.ps4)
            return [updates, online, friends, shortcut("help", qsTr("How to Play"), "it_help"), news,
                    party, profile, library, controllers, settings, power]
        return [updates, friends, online, party, profile, settings, power]
    }
    readonly property var quickEntries: [library, shortcut("friends", qsTr("Friends"), "cat_friends"),
        shortcut("party", qsTr("Party"), "cat_party"), shortcut("online", qsTr("Online Play"), "cat_network"),
        shortcut("sound", qsTr("Sound"), "it_sound"), shortcut("controllers", qsTr("Controllers"),
                                                               "it_gamepad"), shortcut("settings", qsTr(
                                                                                           "Settings"),
                                                                                       "cat_settings"),
        shortcut("power", qsTr("Power"), "it_power")]
    readonly property var functionIndices: nintendo ? shortcuts.map(function (e, i) {
        return i;
    }).slice(1) : style.ps5 ? [0, 5] : shortcuts.map(function (e, i) {
        return i;
    })
    function focusFunctions(delta) {
        var position = functionIndices.indexOf(shortcutIndex);
        if (position < 0)
            position = 0;
        else
            position = Math.max(0, Math.min(functionIndices.length - 1, position + delta));
        shortcutIndex = functionIndices[position];
        region = "functions";
    }
    readonly property var tabs: style.blades ? [qsTr("online"), qsTr("games"), qsTr("library"), qsTr(
                                                    "system")] : style.one ? [qsTr("pins"), qsTr("home"), qsTr(
                                                                                  "library")] : [qsTr("home"),
                                                                                                 qsTr("social"),
                                                                                                 qsTr("games"),
                                                                                                 qsTr("settings")]
    readonly property var panelEntries: {
        var lib = library, add = shortcut("add", qsTr("Add Games"), "it_addgames"), upd = shortcut("updates",
                                                                                                   qsTr("Game Updates"),
                                                                                                   "it_update");
        var settings = shortcut("settings", qsTr("System Settings"), "cat_settings");
        var friends = shortcut("friends", qsTr("Friends"), "cat_friends"), party = shortcut("party", qsTr(
                                                                                                "Party"), "cat_party");
        var online = shortcut("online", qsTr("Online Play"), "cat_network"), profile = shortcut("profile",
                                                                                                qsTr("My Profile"),
                                                                                                "cat_users");
        if (style.blades)
            return tab === 0 ? [online, friends, party, profile] : tab === 1 ? [lib, add, upd, shortcut("help",
                                                                                                        qsTr("How to Play"),
                                                                                                        "it_help")] :
                                                                               tab === 2 ? [lib, add, upd] :
                                                                                           [settings, shortcut(
                                                                                                "controllers",
                                                                                                qsTr("Controllers"),
                                                                                                "it_gamepad"),
                                                                                            shortcut("sound",
                                                                                                     qsTr("Sound"),
                                                                                                     "it_sound"),
                                                                                            shortcut("power",
                                                                                                     qsTr("Power"),
                                                                                                     "it_power")];
        if (style.one)
            return tab === 0 ? games.filter(function (g) {
                return pinnedIds.indexOf(g.gameId) >= 0;
            }).concat([lib]) : tab === 2 ? [lib, add, upd, settings, online, profile] : [games.length
                                                                                         ? games[0] : add, lib,
                                                                                         friends, party, upd,
                                                                                         settings, add,
                                                                                         online];
        var news = shortcut("changelog", qsTr("What's New in USCL"), "it_system");
        var sound = shortcut("sound", qsTr("Sound"), "it_sound"), controls = shortcut("controllers", qsTr(
                                                                                          "Controllers"),
                                                                                      "it_gamepad");
        return tab === 1 ? [profile, friends, party, online, news, upd, lib, add, settings] : tab === 2 ? [lib,
                                                                                                           add, upd, games.length
                                                                                                           ? games[0] :
                                                                                                             news, news,
                                                                                                           online, friends,
                                                                                                           party, settings] :
                                                                                                          tab === 3
                                                                                                          ? [settings,
                                                                                                             controls, sound,
                                                                                                             profile, online,
                                                                                                             news, upd,
                                                                                                             shortcut("help",
                                                                                                                      qsTr("How to Play"),
                                                                                                                      "it_help"),
                                                                                                             shortcut("power",
                                                                                                                      qsTr("Power"),
                                                                                                                      "it_power")] :
                                                                                                            [games.length
                                                                                                             ? games[0] :
                                                                                                               add, lib, upd,
                                                                                                             news, add,
                                                                                                             online, friends,
                                                                                                             party, settings];
    }
    onPanelEntriesChanged: tileIndex = Math.max(0, Math.min(tileIndex, panelEntries.length - 1))
    function reset() {
        region = "software";
        guideOpen = false;
        shortcutIndex = 0;
        tileIndex = 0;
        softwareIndex = 0;
        tab = theme === "xboxone" || theme === "x360classic" ? 1 : 0;
    }
    onThemeChanged: reset()
    Component.onCompleted: reset()
    onGamesChanged: softwareIndex = Math.min(softwareIndex, Math.max(0, entries.length - 1))
    function choose(index) {
        if (index < 0 || index >= entries.length)
            return;
        softwareIndex = index;
        if (index < games.length) {
            navigation.cat = gamesCategory;
            navigation.setItem(index);
        }
        navigation.sound("cursor");
    }
    function activate(entry, button) {
        if (!entry)
            return;
        if (entry.key) {
            if (button && button !== "accept")
                return;
            navigation.sound("select");
            sectionRequested(entry.key);
            return;
        }
        var idx = games.findIndex(function (g) {
            return g === entry || (g.gameId && g.gameId === entry.gameId) || g.title === entry.title;
        });
        if (idx < 0)
            return;
        navigation.cat = gamesCategory;
        navigation.setItem(idx);
        navigation.input(button || "accept");
    }
    function changeTab(delta) {
        tab = Math.max(0, Math.min(tabs.length - 1, tab + delta));
        tileIndex = 0;
        navigation.sound("category");
    }
    readonly property int groupSize: style.one ? 8 : 9
    function xboxRect(index) {
        var shapes = style.one ? [[0, 0, 760, 430], [774, 0, 220, 430], [1008, 0, 360, 132], [1008, 149, 360,
                                                                                              132], [1008, 298,
                                                                                                     360, 132],
                                  [0, 444, 446, 155], [460, 444, 446, 155], [920, 444, 448, 155]] : [[0, 0,
                                                                                                      261, 195],
                                                                                                     [0, 201, 261,
                                                                                                      195], [0, 402,
                                                                                                             261, 195],
                                                                                                     [267, 0, 699,
                                                                                                      396], [267,
                                                                                                             402, 345, 195],
                                                                                                     [618, 402,
                                                                                                      348, 195],
                                                                                                     [972, 0, 261,
                                                                                                      195], [972,
                                                                                                             201, 261, 195],
                                                                                                     [972, 402,
                                                                                                      261, 195]];
        var r = shapes[index % shapes.length], unit = xboxShelf.width / (style.one ? 1368 : 1233);
        return Qt.rect(r[0] * unit, r[1], r[2] * unit, r[3]);
    }
    function moveTile(button) {
        var a = xboxRect(tileIndex), dx = button === "left" ? -1 : button === "right" ? 1 : 0, dy = button
                                                                                               === "up" ? -1 :
                                                                                                          button
                                                                                                          === "down"
                                                                                                          ? 1 : 0;
        var best = -1, score = Number.MAX_VALUE;
        for (var i = Math.floor(tileIndex / groupSize) * groupSize; i < Math.min(panelEntries.length, (Math.floor(
                                                                                                           tileIndex
                                                                                                           / groupSize)
                                                                                                       + 1) * groupSize); i++) {
            var b = xboxRect(i), x = b.x + b.width / 2 - a.x - a.width / 2, y = b.y + b.height / 2 - a.y
                                                                            - a.height / 2;
            if ((dx && x * dx <= 1) || (dy && y * dy <= 1))
                continue;
            if (dx && (b.y >= a.y+a.height || b.y+b.height <= a.y)) continue;
            if (dy && (b.x >= a.x+a.width || b.x+b.width <= a.x)) continue;
            var distance = dx ? Math.abs(x) + Math.abs(y) * 3 : Math.abs(y) + Math.abs(x) * 3;
            if (distance < score) {
                score = distance;
                best = i;
            }
        }
        if (best >= 0) {
            tileIndex = best;
            navigation.sound("cursor");
        } else if (button === "up")
            region = "functions";
        else if (dx && ((dx > 0 && (Math.floor(tileIndex / groupSize) + 1) * groupSize < panelEntries.length)
                        || (dx < 0 && tileIndex >= groupSize))) {
            tileIndex = dx > 0 ? (Math.floor(tileIndex / groupSize) + 1) * groupSize : (Math.floor(tileIndex
                                                                                                   / groupSize)
                                                                                        - 1) * groupSize;
        } else if (dx)
            changeTab(dx);
    }
    function input(button) {
        if (button === "home") {
            guideOpen = !guideOpen;
            shortcutIndex = 0;
            return;
        }
        if (guideOpen) {
            if (button === "back")
                guideOpen = false;
            else if (button === "left" || button === "up")
                shortcutIndex = Math.max(0, shortcutIndex - 1);
            else if (button === "right" || button === "down")
                shortcutIndex = Math.min(quickEntries.length - 1, shortcutIndex + 1);
            else if (button === "accept") {
                guideOpen = false;
                activate(quickEntries[shortcutIndex]);
            }
            return;
        }
        if (button === "back") {
            region = "software";
            navigation.sound("back");
            return;
        }
        if (style.xbox) {
            if (button === "l1" || button === "r1") {
                changeTab(button === "r1" ? 1 : -1);
                return;
            }
            if (style.blades) {
                if (button === "left" || button === "right")
                    changeTab(button === "right" ? 1 : -1);
                else if (button === "up" || button === "down")
                    tileIndex = Math.max(0, Math.min(panelEntries.length, tileIndex + (button === "down"
                                                                                           ? 1 : -1)));
                else if (button === "accept") {
                    if (tileIndex === panelEntries.length) {
                        if (games.length) activate(games[0]); else sectionRequested("add");
                    } else activate(panelEntries[tileIndex]);
                }
            } else if (region === "functions") {
                if (button === "left" || button === "right")
                    changeTab(button === "right" ? 1 : -1);
                else if (button === "down" || button === "accept")
                    region = "software";
            } else if (["left", "right", "up", "down"].indexOf(button) >= 0)
                moveTile(button);
            else if (button === "accept" || button === "options" || button === "info")
                activate(panelEntries[tileIndex], button);
            return;
        }
        if (button === "l1" || button === "r1") {
            focusFunctions(button === "r1" ? 1 : -1);
            return;
        }
        if (region === "functions") {
            if (button === "left" || button === "right")
                focusFunctions(button === "right" ? 1 : -1);
            else if (button === "accept")
                activate(shortcuts[shortcutIndex]);
            else if (button === (nintendo ? "up" : "down"))
                region = "software";
        } else if (region === "profile") {
            if (button === "down" || button === "back")
                region = "software";
            else if (button === "accept")
                sectionRequested("profile");
        } else if (region === "details") {
            if (button === "up")
                region = "software";
            else if (style.ps4 && (button === "left" || button === "right"))
                detailsAction = button === "right" ? 1 : 0;
            else if (button === "accept")
                activate(current, style.ps4 && !current.key ? (detailsAction ? "options" : "info") : "accept");
            else if (button === "options" || button === "info")
                activate(current, button);
        } else if (button === "left" || button === "right")
            choose(selected + (button === "right" ? 1 : -1));
        else if (button === "up") {
            if (nintendo)
                region = "profile";
            else
                focusFunctions(0);
        } else if (button === "down") {
            if (nintendo)
                focusFunctions(0);
            else {
                detailsAction = 0;
                region = "details";
            }
        } else if (button === "accept" || button === "options" || button === "info")
            activate(current, button);
    }
    Rectangle {
        anchors.fill: parent
        color: style.background
    }
    Rectangle {
        anchors.fill: parent
        visible: style.metro
        gradient: Gradient {
            GradientStop {
                position: 0
                color: "#626568"
            }
            GradientStop {
                position: .65
                color: "#bfc1c3"
            }
            GradientStop {
                position: 1
                color: "#e1e3e5"
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: style.ps4
        gradient: Gradient {
            GradientStop {
                position: 0
                color: "#1046a8"
            }
            GradientStop {
                position: 1
                color: "#031d77"
            }
        }
    }
    Image {
        anchors.fill: parent
        visible: style.ps5 || style.one
        source: home.current ? home.current.pic || "" : ""
        fillMode: Image.PreserveAspectCrop
        opacity: .5
        asynchronous: true
    }
    Rectangle {
        anchors.fill: parent
        visible: style.ps5
        gradient: Gradient {
            GradientStop {
                position: 0
                color: "#20071020"
            }
            GradientStop {
                position: 1
                color: "#ed030711"
            }
        }
    }
    // Profile lives where the console puts it, separate from the status clock.
    Item {
        x: home.nintendo ? 96 : style.one ? 96 : style.ps4 ? 850 : 1430
        y: home.nintendo ? 70 : style.one ? 78 : 78
        width: home.nintendo ? 100 : style.ps5 ? 80 : 390
        height: 80
        visible: !style.blades && !style.metro && !(style.ps4 && home.categoryFocus)
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: home.nintendo ? 40 : 0
            border.width: home.region === "profile" ? 4 : 0
            border.color: style.accent
        }
        Image {
            x: 8
            y: 8
            width: 64
            height: 64
            source: home.avatar
            fillMode: Image.PreserveAspectFit
            mipmap: true
        }
        Text {
            visible: !home.nintendo && !style.ps5
            x: 88
            width: 295
            height: 80
            text: home.userName
            color: style.text
            font.pixelSize: 28
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        MouseArea {
            anchors.fill: parent
            onClicked: home.sectionRequested("profile")
        }
    }
    // PS4 function area; Switch's lower system row; PS5 top utility shortcuts.
    Rectangle {
        visible: style.lightSwitch
        x: functions.x - 15
        y: functions.y - 17
        width: functions.width + 30
        height: 134
        radius: 67
        color: "#f5f5f5"
    }
    Item {
        id: functions
        visible: !style.xbox
        x: home.nintendo ? (home.width - (home.shortcuts.length - 1) * 140) / 2 : style.ps5 ? 1210 : 100
        y: home.nintendo ? 790 : style.ps4 ? (home.categoryFocus ? 670 : 76) : 80
        width: home.nintendo ? (home.shortcuts.length - 1) * 140 : style.ps5 ? 220 : home.width - 200
        height: 150
        Behavior on y {
            NumberAnimation {
                duration: home.animate ? 240 : 0
                easing.type: Easing.OutCubic
            }
        }
        Repeater {
            model: home.shortcuts
            delegate: Item {
                id: fn
                required property var modelData
                required property int index
                readonly property bool included: home.nintendo ? index > 0 : style.ps5 ? modelData.key
                                                                                         === "settings"
                                                                                         || modelData.key
                                                                                         === "updates" :
                                                                                         home.categoryFocus
                                                                                         || ["updates",
                                                                                             "friends",
                                                                                             "settings",
                                                                                             "power"].indexOf(
                                                                                             modelData.key)
                                                                                         >= 0
                readonly property int slot: home.nintendo ? index - 1 : style.ps5 ? (modelData.key === "updates"
                                                                                     ? 0 : 1) : index
                objectName: "homeShortcut_" + modelData.key
                visible: included
                x: style.ps4 && !home.categoryFocus ? ({
                                                           updates: 115,
                                                           friends: 644,
                                                           settings: 1490,
                                                           power: 1650
                                                       }[modelData.key] || 0) : slot * (home.nintendo ? 140 :
                                                                                                        style.ps5
                                                                                                        ? 110 : functions.width
                                                                                                          / home.shortcuts.length)
                width: home.nintendo ? 120 : style.ps5 ? 90 : 150
                height: 140
                readonly property bool selected: home.categoryFocus && home.shortcutIndex === index
                Rectangle {
                    x: 10
                    y: 0
                    width: 100
                    height: 100
                    radius: home.nintendo ? 50 : 0
                    color: home.nintendo && !style.lightSwitch ? "#414141" : "transparent"
                    border.width: fn.selected ? 4 : 0
                    border.color: style.accent
                }
                ConsoleGlyph {
                    x: 28
                    y: 18
                    width: fn.selected && !home.nintendo ? 80 : 64
                    height: width
                    kind: fn.modelData.key
                    ink: home.nintendo ? ({
                                              changelog: style.lightSwitch ? "#63a629" : "#e44c4f",
                                              add: style.lightSwitch ? "#e7473d" : "#e49b31",
                                              online: "#e60020",
                                              party: "#e49b31",
                                              library: "#477dd3",
                                              friends: "#09afba"
                                          }[kind] || (style.lightSwitch ? "#555555" : "#eeeeee")) : "white"
                    opacity: fn.selected || home.nintendo ? 1 : .7
                }
                Text {
                    x: -40
                    y: 110
                    width: 200
                    text: fn.modelData.title
                    visible: fn.selected
                    horizontalAlignment: Text.AlignHCenter
                    color: style.text
                    font.pixelSize: 25
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        home.shortcutIndex = fn.index;
                        home.region = "functions";
                        home.activate(fn.modelData);
                    }
                }
            }
        }
    }
    Text {
        visible: style.ps5
        x: 96
        y: 70
        text: qsTr("Games")
        font.pixelSize: 35
        color: "white"
    }
    Text {
        visible: style.ps5
        x: 280
        y: 70
        text: qsTr("Game Library")
        font.pixelSize: 35
        color: "#bbc2ca"
        MouseArea {
            anchors.fill: parent
            onClicked: home.sectionRequested("library")
        }
    }
    Item {
        id: shelf
        objectName: "softwareShelf"
        visible: !style.xbox
        x: home.nintendo ? 105 : style.ps5 ? 96 : 0
        y: home.nintendo ? 320 : style.ps5 ? 190 : home.categoryFocus ? 912 : 203
        width: home.width - x
        height: home.nintendo ? 400 : style.ps5 ? 180 : 420
        clip: true
        opacity: style.ps4 && home.categoryFocus ? .5 : 1
        Behavior on y {
            NumberAnimation {
                duration: home.animate ? 240 : 0
                easing.type: Easing.OutCubic
            }
        }
        WheelHandler {
            onWheel: function (event) {
                home.region = "software";
                home.choose(home.selected + (event.angleDelta.y < 0 ? 1 : -1));
                event.accepted = true;
            }
        }
        Repeater {
            model: home.entries
            delegate: Rectangle {
                id: tile
                required property var modelData
                required property int index
                objectName: "homeTile" + index
                readonly property bool selected: index === home.selected
                readonly property int delta: index - home.selected
                x: home.nintendo ? 8 + delta * 386 : style.ps5 ? 8 + (delta < 0 ? delta * 126 : delta === 0
                                                                                  ? 0 : 166 + (delta - 1)
                                                                                    * 126) : 282 + (delta < 0
                                                                                                    ? delta * 204 :
                                                                                                      delta === 0
                                                                                                      ? 0 : (home.categoryFocus
                                                                                                             ? 204 : 342)
                                                                                                        + (delta
                                                                                                           - 1) * 204)
                y: 6
                width: home.nintendo ? 368 : style.ps5 ? (selected ? 150 : 112) : selected &&
                                                         !home.categoryFocus ? 342 : 200
                height: style.ps4 && selected && !home.categoryFocus ? 412 : width
                radius: style.ps5 ? 16 : style.lightSwitch ? 24 : 0
                color: home.nintendo ? "#505050" : "#123979"
                border.width: selected && home.region === "software" ? (home.nintendo ? 5 : 4) : 0
                border.color: style.ps4 ? "#cdd8ed" : style.accent
                opacity: modelData.disabled ? .45 : 1
                Behavior on x {
                    NumberAnimation {
                        duration: home.animate ? 150 : 0
                        easing.type: Easing.OutCubic
                    }
                }
                Image {
                    visible: !tile.modelData.key
                    x: 6
                    y: 6
                    width: parent.width - 12
                    height: width
                    source: tile.modelData.icon || ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    mipmap: true
                }
                RoundedCoverMask {
                    anchors.fill: parent
                    visible: style.lightSwitch
                    z: 1
                    radius: tile.radius
                    outsideColor: style.background
                }
                ConsoleGlyph {
                    visible: !!tile.modelData.key
                    x: parent.width * .25
                    y: parent.width * .15
                    width: parent.width * .5
                    height: width
                    kind: "library"
                }
                Text {
                    visible: !!tile.modelData.key
                    y: parent.height * .7
                    width: parent.width - 20
                    text: tile.modelData.title
                    color: "white"
                    font.pixelSize: style.ps5 ? 20 : 30
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
                Text {
                    visible: style.ps4 && tile.selected && !home.categoryFocus
                    y: 342
                    width: parent.width
                    height: 70
                    text: tile.modelData.key ? qsTr("Open") : tile.modelData.running ? qsTr("Resume") : qsTr(
                                                                                           "Start")
                    color: "white"
                    font.pixelSize: 32
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                Rectangle {
                    visible: !!tile.modelData.badge
                    x: 12
                    y: 12
                    width: badge.implicitWidth + 20
                    height: 34
                    radius: 17
                    color: "#eebd46"
                    Text {
                        id: badge
                        anchors.centerIn: parent
                        text: tile.modelData.badge || ""
                        color: "#151515"
                        font.pixelSize: 21
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function (mouse) {
                        var was = tile.selected;
                        home.region = "software";
                        home.choose(tile.index);
                        if (mouse.button === Qt.RightButton)
                            home.activate(tile.modelData, "options");
                        else if (was)
                            home.activate(tile.modelData);
                    }
                }
            }
        }
    }
    Text {
        visible: !style.xbox && !(style.ps4 && home.categoryFocus)
        x: home.nintendo ? 105 : style.ps4 ? 638 : 96
        y: home.nintendo ? 255 : style.ps4 ? 538 : 580
        width: home.width - x - 96
        height: style.ps5 ? 150 : 80
        text: home.current ? home.current.title : ""
        color: style.text
        font.pixelSize: style.ps5 ? 62 : style.ps4 ? 56 : 32
        font.weight: Font.Light
        elide: Text.ElideRight
        maximumLineCount: 2
        wrapMode: Text.Wrap
    }
    Item {
        visible: !style.xbox && !home.nintendo && !home.categoryFocus
        x: style.ps4 ? 286 : 96
        y: style.ps4 ? 700 : 710
        width: home.width - x - 200
        height: 245
        Text {
            width: parent.width
            text: home.region === "details" ? qsTr("Game Information") : home.current && home.current.console
                                              ? home.current.console : qsTr("Your games, together")
            font.pixelSize: 30
            color: style.muted
        }
        Rectangle {
            y: 46
            width: parent.width
            height: 1
            color: style.divider
        }
        Text {
            y: 67
            width: parent.width
            height: 85
            text: home.current ? home.current.desc || qsTr("Browse every console in your game library.") : ""
            font.pixelSize: 27
            color: style.muted
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Rectangle {
            visible: style.ps5 || home.region === "details"
            y: 166
            width: 300
            height: 62
            radius: style.ps5 ? 31 : 0
            color: style.ps5 ? "white" : "#174fb3"
            border.width: home.region === "details" && (!style.ps4 || home.detailsAction === 0) ? 4 : 0
            border.color: style.accent
            Text {
                anchors.centerIn: parent
                text: home.current && home.current.key ? qsTr("Open Library") : style.ps4 ? qsTr("Information") : qsTr("Play Game")
                color: style.ps5 ? "#111111" : "white"
                font.pixelSize: 28
            }
            MouseArea {
                anchors.fill: parent
                onClicked: home.activate(home.current, style.ps4 && !home.current.key ? "info" : "accept")
            }
        }
        Rectangle {
            visible: style.ps4 && home.region === "details" && !home.current.key
            x: 325; y: 166; width: 300; height: 62; color: "#174fb3"
            border.width: home.detailsAction === 1 ? 4 : 0; border.color: style.accent
            Text { anchors.centerIn: parent; text: qsTr("Options"); color: "white"; font.pixelSize: 28 }
            MouseArea { anchors.fill: parent; onClicked: home.activate(home.current,"options") }
        }
    }
    // Xbox pivots have their own dashboard panels, rather than turning settings into game covers.
    Row {
        visible: style.xbox && !style.blades
        x: style.one ? 326 : 255
        y: style.one ? 195 : 164
        spacing: 48
        Repeater {
            model: home.tabs
            delegate: Text {
                required property string modelData
                required property int index
                text: modelData
                font.pixelSize: style.one ? 48 : 42
                font.weight: Font.Light
                color: style.metro ? "white" : index === home.tab ? style.text : style.muted
                opacity: index === home.tab ? 1 : .65
                Rectangle {
                    y: parent.height + 8
                    width: parent.width
                    height: 3
                    visible: home.region === "functions" && parent.index === home.tab
                    color: style.accent
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        home.tab = parent.index;
                        home.tileIndex = 0;
                        home.region = "software";
                    }
                }
            }
        }
    }
    Item {
        id: xboxShelf
        visible: home.grid
        x: style.one ? 326 : 345
        y: style.one ? 340 : 270
        width: style.one ? home.width - x - 96 : 1233
        height: 610
        Repeater {
            model: home.grid ? home.panelEntries : []
            delegate: Rectangle {
                id: xboxTile
                required property var modelData
                required property int index
                objectName: "dashboardTile" + index
                visible: Math.floor(index / home.groupSize) === Math.floor(home.tileIndex / home.groupSize)
                readonly property rect rect: home.xboxRect(index)
                x: rect.x
                y: rect.y
                width: rect.width
                height: rect.height
                color: xboxTile.modelData.key === "changelog" ? "#1c334a" : "#107c10"
                border.width: index === home.tileIndex && home.region === "software" ? (style.metro ? 3 : 5) :
                                                                                       0
                scale: style.metro && index === home.tileIndex && home.region === "software" ? 1.06 : 1
                z: index === home.tileIndex ? 2 : 0
                Behavior on scale {
                    NumberAnimation {
                        duration: 120
                    }
                }
                border.color: style.one ? "white" : "#555555"
                Image {
                    visible: !xboxTile.modelData.key
                    x: 12
                    y: 12
                    width: parent.width - 24
                    height: parent.height - 70
                    source: xboxTile.modelData.icon || ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    mipmap: true
                }
                ConsoleGlyph {
                    visible: !!xboxTile.modelData.key
                    x: (parent.width - width) / 2
                    y: (parent.height - 56 - height) / 2
                    width: Math.min(parent.width * .43, (parent.height - 80) * .65)
                    height: width
                    kind: xboxTile.modelData.key || "library"
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 56
                    color: "#b0000000"
                    Text {
                        x: 16
                        width: parent.width - 32
                        height: 56
                        text: xboxTile.modelData.title
                        color: "white"
                        font.pixelSize: 27
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function (mouse) {
                        var was = home.tileIndex === xboxTile.index;
                        home.region = "software";
                        home.tileIndex = xboxTile.index;
                        if (mouse.button === Qt.RightButton)
                            home.activate(xboxTile.modelData, "options");
                        else if (was)
                            home.activate(xboxTile.modelData);
                    }
                }
            }
        }
    }
    Item {
        visible: style.blades
        anchors.fill: parent
        readonly property color bladeColor: ["#a65924", "#568b28", "#2876a5", "#66568e"][home.tab]
        Rectangle {
            anchors.fill: parent
            color: "#a5a9aa"
        }
        Repeater {
            model: style.blades ? 4 : 0
            delegate: Rectangle {
                required property int index
                x: index <= home.tab ? index * 70 : home.width - (4 - index) * 70
                width: index === home.tab ? home.width - 210 : 70
                height: home.height
                radius: 140
                color: index === home.tab ? parent.bladeColor : "#d7dad9"
                border.width: 3
                border.color: "#858a89"
                Text {
                    x: -130
                    y: 190
                    width: 330
                    height: 60
                    rotation: 90
                    text: home.tabs[parent.index] || ""
                    color: parent.index === home.tab ? "white" : "#444444"
                    font.pixelSize: 36
                }
                MouseArea {
                    enabled: parent.index !== home.tab
                    anchors.fill: parent
                    onClicked: {
                        home.tab = parent.index;
                        home.tileIndex = 0;
                    }
                }
            }
        }
        Item {
            x: 405
            y: 60
            width: 1180
            height: 910
            Text {
                text: home.tabs[home.tab]
                color: "white"
                font.pixelSize: 45
            }
            Rectangle {
                y: 90
                width: 600
                height: 205
                radius: 16
                color: "#30ffffff"
                border.width: 2
                border.color: "#80ffffff"
                Image {
                    x: 20
                    y: 25
                    width: 130
                    height: 130
                    source: home.avatar
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    x: 175
                    y: 22
                    width: 405
                    text: home.userName
                    color: "white"
                    font.pixelSize: 34
                    elide: Text.ElideRight
                }
                Text {
                    x: 175
                    y: 85
                    text: qsTr("%1 games\nUSCL Profile").arg(home.games.length)
                    color: "#edffffff"
                    font.pixelSize: 28
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: home.sectionRequested("profile")
                }
            }
            Text {
                x: 730
                y: 130
                text: "USCL"
                color: "#e8eedf"
                font.pixelSize: 74
                font.weight: Font.Light
            }
            Column {
                y: 350
                width: 600
                spacing: 1
                Repeater {
                    model: style.blades ? home.panelEntries : []
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: 600
                        height: 76
                        color: index === home.tileIndex ? "#65ffffff" : "transparent"
                        Image {
                            x: 12
                            y: 15
                            width: 46
                            height: 46
                            source: parent.modelData.icon
                            fillMode: Image.PreserveAspectFit
                        }
                        Text {
                            x: 80
                            width: 500
                            height: 76
                            text: parent.modelData.title
                            color: "white"
                            font.pixelSize: 32
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            y: 74
                            width: parent.width
                            height: 2
                            color: "#40505050"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                home.tileIndex = parent.index;
                                home.activate(parent.modelData);
                            }
                        }
                    }
                }
            }
            Text {
                x: 665
                y: 355
                width: 470
                height: 270
                text: home.panelEntries[home.tileIndex] ? home.panelEntries[home.tileIndex].desc || qsTr(
                                                              "Open %1").arg(
                                                              home.panelEntries[home.tileIndex].title) : ""
                color: "white"
                font.pixelSize: 31
                wrapMode: Text.Wrap
            }
            Rectangle {
                y: 735
                width: 1140
                height: 74
                radius: 37
                color: "#25ffffff"
                border.width: home.tileIndex === home.panelEntries.length ? 4 : 2
                border.color: home.tileIndex === home.panelEntries.length ? "white" : "#40505050"
                Text {
                    x: 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: home.games.length ? qsTr("Play: %1").arg(home.games[0].title) : qsTr(
                                                  "Add games to your library")
                    width: parent.width - 56
                    color: "white"
                    font.pixelSize: 29
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: home.games.length ? home.activate(home.games[0]) : home.sectionRequested("add")
                }
            }
        }
    }
    Rectangle {
        visible: !(style.ps4 && home.categoryFocus)
        x: 96
        y: 990
        width: home.width - 192
        height: 1
        color: style.divider
    }
    Text {
        visible: !(style.ps4 && home.categoryFocus)
        x: 96
        y: 1010
        width: home.width - 192
        color: style.blades ? "white" : style.muted
        font.pixelSize: 24
        elide: Text.ElideRight
        text: qsTr("%1 / Enter: Select    %2 / Esc: Back    %3 / Space: Options    Home: %4").arg(
                  style.confirmGlyph).arg(style.backGlyph).arg(style.optionsGlyph).arg(style.ps5 ? qsTr(
                                                                                                       "Control Center") :
                                                                                                   qsTr("Quick Menu"))
    }
    // Home button opens console-style quick access; Esc always closes it first.
    Rectangle {
        objectName: "consoleQuickMenu"
        visible: home.guideOpen
        anchors.fill: parent
        color: "#99000000"
        z: 20
        MouseArea {
            anchors.fill: parent
            onClicked: home.guideOpen = false
        }
        Rectangle {
            x: style.ps5 ? 96 : 0
            y: style.ps5 ? 910 : 0
            width: style.ps5 ? home.width - 192 : 620
            height: style.ps5 ? 140 : home.height
            radius: style.ps5 ? 12 : 0
            color: style.ps5 ? "#e0141414" : style.panel
            Text {
                x: 40
                y: 25
                visible: !style.ps5
                text: qsTr("Quick Menu")
                color: style.text
                font.pixelSize: 38
            }
            Repeater {
                model: home.quickEntries
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    x: style.ps5 ? 30 + index * (parent.width - 60) / home.quickEntries.length : 25
                    y: style.ps5 ? 0 : 110 + index * 112
                    width: style.ps5 ? (parent.width - 60) / home.quickEntries.length - 10 : 570
                    height: style.ps5 ? 130 : 100
                    color: !style.ps5 && index === home.shortcutIndex ? style.focusFill : "transparent"
                    border.width: !style.ps5 && index === home.shortcutIndex ? 3 : 0
                    border.color: style.accent
                    Rectangle {
                        visible: style.ps5 && parent.index === home.shortcutIndex
                        x: (parent.width-72)/2; y: 4; width: 72; height: 72; radius: 36; color: "white"
                    }
                    ConsoleGlyph {
                        x: style.ps5 ? (parent.width-width)/2 : 20
                        y: 18; width: style.ps5 ? 42 : 52; height: width
                        kind: parent.modelData.key
                        ink: style.ps5 && parent.index === home.shortcutIndex ? "#141414" : style.text
                    }
                    Text {
                        x: style.ps5 ? 12 : 95
                        y: style.ps5 ? 78 : 0
                        width: parent.width - x - 12
                        height: style.ps5 ? 40 : 100
                        text: parent.modelData.title
                        color: style.ps5 ? "white" : parent.index === home.shortcutIndex ? style.focusText : style.text
                        font.pixelSize: style.ps5 ? 22 : 25
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            home.guideOpen = false;
                            home.activate(parent.modelData);
                        }
                    }
                }
            }
        }
    }
}
