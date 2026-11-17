import QtQuick
import QtTest
import "../qml"

Item {
    width: 1920; height: 1080
    Xmb {
        id: navigation
        anchors.fill: parent
        visible: false
        cats: [{name: "Games", items: []}, {name: "Settings", items: []}]
    }
    ConsoleHome { id: home; anchors.fill: parent; navigation: navigation; theme: "ps5"; onNavigate: function(button) { input(button) } }
    Page { id: page; anchors.fill: parent; visible: false }
    OptionsPanel { id: menu; anchors.fill: parent }
    Dialog { id: dialog; anchors.fill: parent }
    TextPrompt { id: prompt; anchors.fill: parent }
    GameLaunch { id: launchScreen; anchors.fill: parent; shown: false }
    TestCase {
        name: "ConsoleThemes"
        when: windowShown
        property int launches: 0
        property int inspected: 0
        SignalSpy { id: sectionSpy; target: home; signalName: "sectionRequested" }
        SignalSpy { id: optionsSpy; target: navigation; signalName: "showOptions" }
        function init() {
            failOnWarning(/(ReferenceError|TypeError|Unable to assign|Binding loop|Cannot open)/)
            menu.close(); dialog.close(); prompt.close(); page.visible = false
            home.visible = true; navigation.visible = false
            home.reset()
            page.presentation = ""; page.filterIndex = 0; page.filterFocus = false; page.settingsSection = -1; page.sectionFocus = false; home.pinnedIds = []
            sectionSpy.clear()
            launches = 0; inspected = 0
            optionsSpy.clear()
            var entries = []
            for (var i = 0; i < 13; i++) entries.push({gameId: "GAME"+i, console: i % 2 ? "PS4" : "PS3", title: "Game " + i, action: function() { launches++ }, info: function() { inspected++ }, options: function() { return [] }})
            navigation.cats = [{id: "games", name: "Games", items: entries}, {id: "settings", name: "Settings", items: [{title: "Update USCL", action: function() { launches++ }}]}]
            navigation.cat = 0
            navigation.itemIndex = []
        }
        function test_themes_data() {
            return ["ps4", "ps5", "x360classic", "x360", "xboxone", "switch", "switch2"].map(function(t) { return {tag: t, theme: t} })
        }
        function test_themes(data) {
            home.theme = data.theme; home.reset()
            compare(home.entries.length, 13) // 12 games and a deliberate library entry.
            verify(home.entries.slice(0,12).every(function(e) { return !!e.console }))
            if (!home.grid && data.theme !== "x360classic") {
                home.input("right"); compare(home.selected, 1)
                home.input("accept"); compare(launches, 1)
                home.input("options"); compare(optionsSpy.count, 1)
                home.input("down")
                if (home.nintendo) verify(home.categoryFocus)
                else compare(home.region, "details")
                home.input("back"); compare(home.region, "software")
                for (var i = 0; i < 30; i++) home.input("right")
                compare(home.selected, 12)
                home.input("accept"); compare(sectionSpy.signalArguments[0][0], "library")
                home.input("options"); compare(sectionSpy.count, 1)
            } else {
                if (data.theme === "x360classic") {
                    home.input("accept"); compare(sectionSpy.signalArguments[0][0], "library")
                    home.input("down"); home.input("accept"); compare(sectionSpy.signalArguments[1][0], "add")
                } else { home.input("accept"); compare(launches, 1) }
                home.input("r1"); verify(home.tab > 0)
                home.input("l1"); verify(home.tab >= 0)
                home.input("back"); compare(home.region, "software")
            }
            home.input("home"); verify(home.guideOpen)
            home.input("back"); verify(!home.guideOpen)
            home.input("home"); home.shortcutIndex = home.quickEntries.findIndex(function(e) { return e.key === "settings" })
            home.input("accept"); verify(!home.guideOpen)
            compare(sectionSpy.signalArguments[sectionSpy.count-1][0], "settings")
        }
        function test_reachable_shortcuts_data() { return test_themes_data() }
        function test_reachable_shortcuts(data) {
            home.theme = data.theme; home.reset()
            for (var i = 0; i < home.quickEntries.length; i++) {
                home.input("home"); home.shortcutIndex = i
                home.input("accept"); compare(sectionSpy.signalArguments[i][0],home.quickEntries[i].key)
            }
            if (!home.grid && data.theme !== "x360classic") {
                home.input(home.nintendo ? "down" : "up")
                verify(home.categoryFocus)
                for (i = 0; i < 15; i++) {
                    verify(home.functionIndices.indexOf(home.shortcutIndex) >= 0)
                    home.input("right")
                }
                home.input(home.nintendo ? "up" : "down"); compare(home.region,"software")
            }
        }
        function test_empty_and_large_libraries_data() { return test_themes_data() }
        function test_empty_and_large_libraries(data) {
            home.theme = data.theme; home.reset()
            navigation.cats = [{id:"games",name:"Games",items:[]}]
            compare(home.entries.length,1); compare(home.current.key,"library")
            if (!home.grid && data.theme !== "x360classic") {
                home.input("right"); compare(home.selected,0)
                home.input("accept"); compare(sectionSpy.signalArguments[0][0],"library")
            }
            var rows = []
            for (var i = 0; i < 240; i++) rows.push({title:"Title "+i,console:i%2 ? "PS4":"PS3",action:function(){launches++}})
            page.theme = data.theme; page.presentation = "library"; page.rows = rows; page.current = 0
            compare(page.displayRows.length,240)
            page.input("down"); compare(page.current,page.gridColumns)
            page.current = 239; page.input("down"); compare(page.current,239)
            page.input("accept"); compare(launches,1)
            page.current = 0; page.input("left"); verify(page.filterFocus)
            page.input("down"); compare(page.filterIndex,1); compare(page.displayRows.length,120)
            page.input("right"); verify(!page.filterFocus)
            page.input("accept"); compare(launches,2)
            page.rows = []; compare(page.filterIndex,0); compare(page.displayRows.length,0)
            page.input("accept"); compare(launches,2)
        }
        function test_xbox_spatial_reachability_data() { return [{tag:"x360",theme:"x360"},{tag:"xboxone",theme:"xboxone"}] }
        function test_xbox_spatial_reachability(data) {
            home.theme = data.theme; home.reset()
            for (var tab = 0; tab < home.tabs.length; tab++) {
                home.tab = tab
                var pending = [0], visited = ({0:true})
                while (pending.length) {
                    var from = pending.shift()
                    for (var d = 0; d < 4; d++) {
                        home.tab = tab; home.region = "software"; home.tileIndex = from
                        home.input(["left","right","up","down"][d])
                        if (home.tab !== tab || home.region !== "software") continue
                        if (!visited[home.tileIndex]) { visited[home.tileIndex] = true; pending.push(home.tileIndex) }
                    }
                }
                compare(Object.keys(visited).length,home.panelEntries.length,"Every tile in pivot "+tab+" must be reachable")
            }
        }
        function test_ps4_information_does_not_launch() {
            home.theme = "ps4"; home.reset()
            home.input("down"); home.input("accept")
            compare(inspected,1); compare(launches,0)
            home.input("right"); home.input("accept")
            compare(optionsSpy.count,1); compare(launches,0)
            home.input("back"); home.input("accept"); compare(launches,1)
        }
        function test_blades_game_bar() {
            home.theme = "x360classic"; home.reset()
            for (var i = 0; i < home.panelEntries.length; i++) home.input("down")
            compare(home.tileIndex,home.panelEntries.length)
            home.input("accept"); compare(launches,1)
        }
        function test_xbox_pins_pages() {
            home.theme = "xboxone"; home.reset()
            home.pinnedIds = navigation.items.map(function(g) { return g.gameId })
            home.tab = 0
            compare(home.panelEntries.length,14)
            home.tileIndex = 7; home.input("right")
            verify(home.tileIndex >= 8)
            home.tileIndex = 8; home.input("accept"); compare(launches,1)
            home.pinnedIds = []
            compare(home.tileIndex,0); compare(home.panelEntries.length,1)
        }
        function test_library_tools_and_scroll() {
            page.theme = "switch2"; page.presentation = "library"; page.visible = true
            var rows = []
            for (var i = 0; i < 200; i++) rows.push({title:"Game "+i,console:"Console "+i%10})
            page.rows = rows; page.current = 0; page.input("left")
            for (i = 0; i < page.filters.length+2; i++) page.input("down")
            compare(page.libraryNavIndex,page.filters.length+2)
            var spy = Qt.createQmlObject('import QtTest; SignalSpy { signalName: "libraryAction" }',page)
            spy.target = page
            page.input("accept"); compare(spy.signalArguments[0][0],"help")
            var sidebar = findChild(page,"librarySidebar")
            wait(200)
            verify(sidebar.contentY > 0)
            spy.destroy()
        }
        function test_nintendo_settings_sections() {
            page.theme = "switch2"; page.settingsSections = [{title:"Sound"},{title:"Controllers"}]
            page.settingsSection = 0; page.rows = [{title:"Volume",value:"50%"}]; page.current = 0
            page.input("left"); verify(page.sectionFocus)
            page.input("down"); compare(page.sectionCursor,1)
            var spy = Qt.createQmlObject('import QtTest; SignalSpy { signalName: "settingsSectionChosen" }',page)
            spy.target = page
            page.input("right"); compare(spy.signalArguments[0][0],1)
            spy.destroy()
            page.settingsSection = -1; page.sectionFocus = false
        }
        function test_disabled_and_live_updates_data() { return test_themes_data().concat([{tag: "ps3", theme: "ps3"}]) }
        function test_disabled_and_live_updates(data) {
            home.theme = data.theme
            navigation.cats = [{id:"games", name: "Games", items: [{console:"PS3", title: "Disabled", disabled: true, action: function() { launches++ }}]}]
            navigation.cat = 0
            navigation.input("accept"); compare(launches, 0)
            navigation.cats = [{id:"games", name: "Games", items: [{console:"PS3", title: "Updated", badge: "Update", action: function() { launches++ }}]}]
            compare(home.current.badge, "Update")
            navigation.input("accept"); compare(launches, 1)
            navigation.cats = []
            navigation.input("accept"); compare(launches, 1)
            compare(home.games.length, 0); compare(home.entries.length, 1)
        }
        function test_pages_and_modals_data() { return test_disabled_and_live_updates_data() }
        function test_pages_and_modals(data) {
            page.theme = data.theme
            menu.theme = data.theme
            dialog.theme = data.theme
            var rows = []
            for (var i = 0; i < 40; i++) rows.push({title: "Setting " + i, choices: true, value: "Enabled", action: function() { launches++ }})
            page.rows = rows; page.current = 0
            page.input("down"); compare(page.current, 1)
            page.input("r1"); compare(page.current, 6)
            page.input("right"); compare(launches, 1)
            page.current = 39
            page.rows = [{title: "Update USCL", action: function() { launches++ }}]
            compare(page.current, 0)
            page.input("accept"); compare(launches, 2)
            page.rows = [{title: "Game", console: "PS3", info: function() { launches++ }}, {title: "Other", console: "PS3"}]
            page.input("info"); compare(launches, 3)
            if (data.theme === "x360classic") { page.input("down"); compare(page.current,1); page.input("up"); compare(page.current,0) }
            else if (data.theme !== "ps3") { page.input("right"); compare(page.current, 1); page.input("left"); compare(page.current, 0) }
            menu.show([{title: "Disabled", disabled: true, action: function() { launches++ }}, {title: "Update", checked: true, action: function() { launches++ }}])
            compare(menu.current, 1)
            menu.input("up"); menu.input("accept"); verify(menu.open); compare(launches, 3)
            menu.input("down"); menu.input("accept"); verify(!menu.open); compare(launches, 4)
            menu.show([]); menu.input("accept"); menu.input("back"); verify(!menu.open)
            dialog.confirm("Update", "Install the update?", function() { launches++ })
            compare(dialog.current, 1)
            dialog.input("accept"); compare(launches, 4)
            dialog.confirm("Update", "Install the update?", function() { launches++ })
            dialog.input("left"); dialog.input("accept"); compare(launches, 5)
            dialog.show("Done", "Update complete"); dialog.input("accept"); verify(!dialog.open)
            dialog.show("Long release notes", new Array(80).join("An update fixes game launching and improves controllers.\n"))
            wait(0); verify(dialog.scrollable)
            dialog.input("down"); verify(dialog.scrollPosition > 0)
            dialog.input("up"); compare(dialog.scrollPosition, 0)
            dialog.input("back"); verify(!dialog.open)
        }
        function test_layout_data() { return test_themes_data() }
        function test_layout(data) {
            home.theme = data.theme; home.reset()
            if (home.grid) {
                for (var t = 0; t < home.tabs.length; t++) {
                    home.tab = t; home.tileIndex = 0; wait(0)
                    for (var j = 0; j < home.panelEntries.length; j++) {
                        var tile = findChild(home,"dashboardTile"+j)
                        verify(tile !== null); verify(tile.width > 0 && tile.height > 0)
                        verify(tile.x >= 0 && tile.y >= 0)
                        verify(tile.x+tile.width <= tile.parent.width+1)
                        verify(tile.y+tile.height <= tile.parent.height+1)
                    }
                }
            } else if (data.theme !== "x360classic") {
                var shelf = findChild(home, "softwareShelf")
                for (var i = 0; i < 13; i++) {
                    home.choose(i); wait(180)
                    var game = findChild(home,"homeTile"+i)
                    verify(game !== null && game.visible)
                    verify(game.x >= 0 && game.y >= 0)
                    verify(game.x+game.width <= shelf.width+1)
                    verify(game.y+game.height <= shelf.height+1)
                    verify(game.border.width > 0)
                }
            }
        }
        function test_mouse_data() { return test_themes_data() }
        function test_mouse(data) {
            home.theme = data.theme; home.reset(); wait(200)
            if (data.theme === "x360classic") {
                var shortcut = home.panelEntries[0]
                home.activate(shortcut); compare(sectionSpy.signalArguments[0][0], "library")
                return
            }
            var tile = findChild(home,home.grid ? "dashboardTile0" : "homeTile1")
            verify(tile !== null)
            if (!home.grid) {
                mouseClick(tile,tile.width/2,tile.height/2); compare(home.selected,1)
                wait(180)
            }
            mouseClick(tile,tile.width/2,tile.height/2); compare(launches,1)
            mouseClick(tile,tile.width/2,tile.height/2,Qt.RightButton)
            compare(optionsSpy.count,1); compare(launches,1)
        }
        function test_mouse_ps3() {
            home.visible = false; navigation.visible = true
            wait(250)
            var other = findChild(navigation, "xmbEntryMouse1")
            verify(other !== null)
            mouseClick(other, 500, other.height / 2)
            compare(navigation.item, 1)
            wait(220)
            mouseClick(other, 500, other.height / 2)
            compare(launches, 1)
            mouseClick(other, 500, other.height / 2, Qt.RightButton)
            compare(optionsSpy.count, 1); compare(launches, 1)
        }
        function test_name_prompt_data() { return test_disabled_and_live_updates_data() }
        function test_name_prompt(data) {
            prompt.theme = data.theme
            var saved = ""
            prompt.show("Online name", "  Player1  ", function(value) { saved = value; return "" })
            keyClick(Qt.Key_Return)
            compare(saved, "Player1"); verify(!prompt.open)
            prompt.show("Online name", "x", function(value) { return "Too short" })
            keyClick(Qt.Key_Return)
            verify(prompt.open); compare(prompt.error, "Too short")
            keyClick(Qt.Key_Escape)
            verify(!prompt.open)
        }
        function test_launchScreen_data() {
            return ["ps3", "ps4", "ps5", "x360classic", "x360", "xboxone", "switch", "switch2"].map(function(theme) { return {tag: theme, theme: theme} })
        }
        function test_launchScreen(data) {
            failOnWarning(/Unable to assign|ReferenceError|TypeError/)
            launchScreen.theme = data.theme
            launchScreen.title = "A game title with <markup> shown literally"
            launchScreen.shown = true
            launchScreen.starting = true
            launchScreen.windowActive = true
            tryCompare(launchScreen, "opacity", 1)
            verify(launchScreen.animating)
            compare(launchScreen.theme, data.theme)
            launchScreen.windowActive = false
            verify(!launchScreen.animating) // no launch animation behind the emulator window
            launchScreen.windowActive = true
            launchScreen.starting = false
            launchScreen.shown = false
            tryCompare(launchScreen, "visible", false)
            compare(launchScreen.opacity, 0)
            verify(!launchScreen.animating)
        }
        function test_ps3() {
            navigation.input("down"); compare(navigation.item, 1)
            navigation.input("accept"); compare(launches, 1)
            navigation.input("right"); compare(navigation.cat, 1)
            navigation.input("accept"); compare(launches, 2)
        }
    }
}
