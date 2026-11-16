import QtQuick
import QtCore
import QtTest
import "../qml"
Item {
    id: gallery
    Rectangle { anchors.fill: parent; color: "#233b58" }
    width: 1920; height: 1080
    Background { anchors.fill: parent; visible: home.theme === "ps3"; tint: "#3c6cd8"; running: false; shade: .15 }
    Xmb { id: nav; anchors.fill: parent; visible: home.theme === "ps3"; cat: 3 }
    ConsoleHome { visible: theme !== "ps3"; id: home; anchors.fill: parent; navigation: nav; theme: "ps4" }
    TestCase {
        name: "Capture"
        when: windowShown
        function test_capture() {
            var games = []
            var icons = ["cat_ps3", "cat_ps4", "cat_switch", "cat_switch2", "cat_wii", "cat_x360"]
            for (var i = 0; i < 14; i++) games.push({title: ["A New Adventure", "Circuit Legends", "World Explorers", "Game Library", "Game Updates", "Add Games"][i % 6], icon: Qt.resolvedUrl("../xmb/icons/" + icons[i % 6] + ".png"), desc: "Your games, ready to play", badge: i === 4 ? "Update" : "", console: "PS3"})
            var categories = []
            var names = ["Users", "Settings", "Network", "Games", "Party", "Friends"]
            for (var c = 0; c < names.length; c++) categories.push({name: names[c], icon: Qt.resolvedUrl("../xmb/icons/" + ["it_person", "it_system", "it_network", "it_games", "it_sound", "it_person"][c] + ".png"), items: games})
            nav.cats = categories
            var directory = decodeURIComponent(String(StandardPaths.writableLocation(StandardPaths.TempLocation))).replace(/^file:\/\//, "")
            if (Qt.platform.os === "windows" && /^\/[a-z]:/i.test(directory)) directory = directory.slice(1)
            var themes = ["ps3", "ps4", "ps5", "x360classic", "x360", "xboxone", "switch", "switch2"]
            for (var t = 0; t < themes.length; t++) {
                home.theme = themes[t]
                wait(400)
                grabImage(gallery).save(directory + "/uscl-preview-" + themes[t] + ".png")
            }
        }
    }
}
