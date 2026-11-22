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
    ClockBox { theme: home.theme; x: gallery.width-width-(home.theme === "ps5" ? 56 : 40); y: home.theme === "ps5" ? 28 : home.nintendo ? 80 : 44; avatar: home.avatar; internet: 1; pads: 1; battery: 80; clock24: true }
    TrophyToast { id: toast; theme: home.theme; x: stageX; y: stageY }
    ProfileCard { id: profileCard; theme: home.theme; gameIcon: function(g) { return Qt.resolvedUrl("../xmb/icons/" + ({PS3: "cat_ps3", Switch: "cat_switch", Wii: "cat_wii"})[g.console] + ".png") } }
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
            // made-up friends, a friend's USCL profile and a trophy list (Main.qml builds the same rows)
            var avatar = function(n) { return Qt.resolvedUrl("../xmb/avatars/" + n + ".png") }
            var cup = function(g) { return "qrc:/qt/qml/Uscl/xmb/trophies/" + g + ".png" }
            var friendRows = [
                {title: "RiverFox", icon: avatar("star"), friend: true, status: "online", cardColor: "#3c6cd8", level: 12, value: "Session", desc: "In a USCL session", help: "In a USCL session  ·  ✕ to join or view their profile"},
                {title: "MapleKnight", icon: avatar("rocket"), friend: true, status: "online", cardColor: "#4ca834", level: 31, value: "Playing", desc: "Playing Circuit Legends", help: "Playing Circuit Legends  ·  ✕ for their USCL or Steam profile"},
                {title: "PixelPilot", icon: avatar("moon"), friend: true, status: "away", cardColor: "#b058c8", level: 5, value: "Away", desc: "Away", help: "Away  ·  ✕ for their USCL or Steam profile"},
                {title: "QuietOwl", icon: avatar("music"), friend: true, status: "offline", cardColor: "#202022", level: 2, value: "Offline", desc: "Offline", help: "Offline  ·  ✕ for their USCL or Steam profile"}]
            var profileRows = [
                {title: "MapleKnight", icon: avatar("rocket"), value: "Playing", help: "Circuit Legends"},
                {title: "Trophies", icon: cup("platinum"), value: "57", help: "Platinum 1  ·  Gold 6  ·  Silver 14  ·  Bronze 36"},
                {title: "Play Time", icon: Qt.resolvedUrl("../xmb/icons/it_clock.png"), value: "86 h 12 min", help: "All games started from USCL"},
                {title: "Circuit Legends", icon: Qt.resolvedUrl("../xmb/icons/cat_ps3.png"), value: "41 h 3 min", help: "Trophies 31 / 48  ·  Last played 11/21/26"},
                {title: "Skyward Journey", icon: Qt.resolvedUrl("../xmb/icons/cat_switch.png"), value: "12 h 40 min", help: "Trophies 5 / 6  ·  Last played 11/18/26"},
                {title: "View Steam Profile", icon: Qt.resolvedUrl("../xmb/icons/it_person.png"), help: "Opens MapleKnight's profile in Steam"}]
            var trophyRows = [{title: "Progress", icon: cup("gold"), value: "64%", help: "31 of 48 earned  ·  Platinum 0  ·  Gold 2  ·  Silver 7  ·  Bronze 22"}]
            var grades = ["bronze", "silver", "gold", "platinum"]
            for (var r = 0; r < 8; r++) trophyRows.push({title: ["Green Flag", "Photo Finish", "Clean Sweep", "Legend of the Circuit"][r % 4], icon: cup(grades[r % 4]),
                                                         disabled: r > 4, value: r > 4 ? ["Bronze", "Silver", "Gold"][r % 3] : "11/2" + r + "/26", help: "Win a race without hitting a wall"})
            for (var t = 0; t < themes.length; t++) {
                home.theme = themes[t]; home.reset()
                wait(400)
                grabImage(gallery).save(directory + "/uscl-preview-" + themes[t] + ".png")
                if (themes[t] === "ps3") {
                    var withFriends = nav.cats.slice(); withFriends[5] = Object.assign({}, withFriends[5], {items: friendRows})
                    nav.cats = withFriends; nav.cat = 5
                    wait(400); grabImage(gallery).save(directory + "/uscl-friends-ps3.png")
                    nav.cat = 3
                }
                var social = [["friends", "Friends", friendRows], ["profile", "MapleKnight", profileRows], ["trophies", "Circuit Legends", trophyRows]]
                for (var v = 0; v < social.length; v++) {
                    if (themes[t] === "ps3" && v === 0) continue
                    page.presentation = ""; page.heading = social[v][1]; page.rows = social[v][2]; page.current = 1; page.visible = true
                    wait(300); grabImage(gallery).save(directory + "/uscl-" + social[v][0] + "-" + themes[t] + ".png")
                    page.visible = false
                }
                // the profile card: a friend's USCL tab, then their Steam tab (made-up profile)
                profileCard.uscl = {earned: 57, platinum: 1, gold: 6, silver: 14, bronze: 36, seconds: 310320, gameCount: 9, live: true, games: [
                    {title: "Circuit Legends", console: "PS3", seconds: 147780, earned: 31, total: 48, last: 1795300000},
                    {title: "Skyward Journey", console: "Switch", seconds: 45600, earned: 5, total: 6, last: 1795000000},
                    {title: "Meadow Stories", console: "Wii", seconds: 7300, earned: 2, total: 6, last: 1794000000},
                    {title: "Ocean Trails", console: "PS3", seconds: 3600, earned: 4, total: 51, last: 1793000000}]}
                profileCard.steam = {name: "MapleKnight", status: "In-Game Circuit Legends", privacy: "public", memberSince: "March 3, 2012", location: "Somewhere Nice",
                    summary: "Racing games, co-op and the occasional puzzle.\nAdd me if we've played together!", groups: ["Racers", "USCL Testers"],
                    games: [{name: "Circuit Legends", icon: Qt.resolvedUrl("../xmb/icons/cat_ps3.png"), hours2wk: "6.5", hoursTotal: "412"},
                            {name: "World Explorers", icon: Qt.resolvedUrl("../xmb/icons/cat_switch.png"), hours2wk: "1.2", hoursTotal: "38"}]}
                profileCard.show({id: "1", name: "MapleKnight", avatar: avatar("rocket"), level: 31, session: "123", state: "online", status: "Playing Circuit Legends"}, "uscl")
                profileCard.input("down")
                wait(400); grabImage(gallery).save(directory + "/uscl-card-" + themes[t] + ".png")
                profileCard.input("r1")
                wait(400); grabImage(gallery).save(directory + "/uscl-cardsteam-" + themes[t] + ".png")
                profileCard.close(); wait(300)
                toast.show({name: "Legend of the Circuit", grade: "platinum", game: "Circuit Legends", icon: ""})
                wait(700); grabImage(gallery).save(directory + "/uscl-trophy-" + themes[t] + ".png")
                wait(5400)
                if (themes[t] === "xboxone") {
                    home.pinnedIds = games.slice(0,10).map(function(g) { return g.gameId }); home.tab = 0
                    wait(240); grabImage(gallery).save(directory+"/uscl-pins-xboxone.png")
                    home.reset()
                }
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
