import QtQuick
import QtCore
import QtTest
import "../qml"
Item {
    id: gallery
    property var settingRows: []
    Rectangle { anchors.fill: parent; color: "#233b58" }
    width: 1920; height: 1080
    Background { anchors.fill: parent; visible: home.theme === "ps3"; tint: "#3c6cd8"; running: false; shade: .15 }
    Xmb { id: nav; anchors.fill: parent; visible: home.theme === "ps3"; cat: 3 }
    ConsoleHome { visible: theme !== "ps3" && !page.visible; id: home; anchors.fill: parent; navigation: nav; animate: false; theme: "ps4" }
    Page { id: page; anchors.fill: parent; visible: false; theme: home.theme; settingsSections: gallery.settingRows }
    ClockBox { theme: home.theme; x: gallery.width-width-40; y: 44; avatar: home.avatar; internet: 1; pads: 1; battery: 80; clock24: true }
    GameLaunch { id: launch; anchors.fill: parent; theme: home.theme; shown: false; starting: true; windowActive: false; title: "A New Adventure"; gameIcon: Qt.resolvedUrl("../xmb/icons/cat_ps3.png") }
    TestCase {
        name: "Capture"
        when: windowShown
        function test_capture() {
            failOnWarning(/(ReferenceError|TypeError|Unable to assign|Binding loop|Cannot open)/)
            var games = []
            var icons = ["cat_ps3", "cat_ps4", "cat_switch", "cat_switch2", "cat_wii", "cat_x360"]
            for (var i = 0; i < 14; i++) games.push({gameId:"PREVIEW"+i, title: ["A New Adventure", "Circuit Legends", "World Explorers", "Skyward Journey", "Ocean Trails", "Meadow Stories"][i % 6], icon: Qt.resolvedUrl("../xmb/icons/" + icons[i % 6] + ".png"), desc: "Your games, ready to play", badge: i === 4 ? "Update" : "", console: ["PS3","PS4","Switch","Switch2","Wii","Xbox360"][i%6]})
            var categories = []
            var names = ["Users", "Settings", "Network", "Games", "Party", "Friends"]
            for (var c = 0; c < names.length; c++) categories.push({id: ["users","settings","online","games","party","friends"][c], name: names[c], icon: Qt.resolvedUrl("../xmb/icons/" + ["it_person", "it_system", "it_network", "it_games", "it_sound", "it_person"][c] + ".png"), items: games})
            var labels = ["System Update","Game Settings","Switch Emulation","Video Settings","Sound Settings","Theme Settings","Date and Time Settings","Accessory Settings","Network Settings","System Settings","USCL Changelog"]
            gallery.settingRows = labels.map(function(title) { return {title:title,desc:"Configure "+title,preview:["Automatic","Output Device","Advanced Options"]} })
            nav.cats = categories
            var directory = decodeURIComponent(String(StandardPaths.writableLocation(StandardPaths.TempLocation))).replace(/^file:\/\//, "")
            if (Qt.platform.os === "windows" && /^\/[a-z]:/i.test(directory)) directory = directory.slice(1)
            var themes = ["ps3", "ps4", "ps5", "x360classic", "x360", "xboxone", "switch", "switch2"]
            for (var t = 0; t < themes.length; t++) {
                home.theme = themes[t]; home.reset()
                wait(400)
                grabImage(gallery).save(directory + "/uscl-preview-" + themes[t] + ".png")
                if (themes[t] !== "ps3") {
                    home.region = "functions"; home.focusFunctions(0); home.shortcutIndex = home.shortcuts.findIndex(function(e) { return e.key === "settings" })
                    wait(300); grabImage(gallery).save(directory+"/uscl-functions-"+themes[t]+".png")
                    home.region = "software"; home.input("home")
                    wait(100); grabImage(gallery).save(directory+"/uscl-quick-"+themes[t]+".png")
                    home.guideOpen = false
                    page.rows = gallery.settingRows; page.heading = "Settings"; page.presentation = "settings"; page.visible = true; page.current = 5
                    wait(200); grabImage(gallery).save(directory+"/uscl-settings-"+themes[t]+".png")
                    page.presentation = "library"; page.heading = "Game Library"; page.rows = games; page.current = 0
                    wait(200); grabImage(gallery).save(directory+"/uscl-library-"+themes[t]+".png")
                    page.visible = false
                }
                launch.shown = true
                tryCompare(launch, "opacity", 1)
                wait(100)
                grabImage(gallery).save(directory + "/uscl-loading-preview-" + themes[t] + ".png")
                launch.shown = false
                wait(260)
            }
        }
    }
}
