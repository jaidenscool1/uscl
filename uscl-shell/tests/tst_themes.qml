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
    TestCase {
        name: "ConsoleThemes"
        when: windowShown
        property int launches: 0
        SignalSpy { id: optionsSpy; target: navigation; signalName: "showOptions" }
        function init() {
            menu.close(); dialog.close(); prompt.close(); page.visible = false
            home.visible = true; navigation.visible = false
            home.categoryFocus = false
            launches = 0
            optionsSpy.clear()
            var entries = []
            for (var i = 0; i < 13; i++) entries.push({title: "Game " + i, action: function() { launches++ }, options: function() { return [] }})
            navigation.cats = [{name: "Games", items: entries}, {name: "Settings", items: [{title: "Update USCL", action: function() { launches++ }}]}]
            navigation.cat = 0
            navigation.itemIndex = []
        }
        function test_themes_data() {
            return ["ps4", "ps5", "x360classic", "x360", "xboxone", "switch", "switch2"].map(function(t) { return {tag: t, theme: t} })
        }
        function test_themes(data) {
            home.theme = data.theme
            home.input(data.theme === "x360classic" ? "down" : "right"); compare(navigation.item, 1)
            home.input("accept"); compare(launches, 1)
            home.input("options"); compare(optionsSpy.count, 1)
            home.input("r1"); compare(navigation.cat, 1)
            home.input("accept"); compare(launches, 2)
            home.input("l1"); compare(navigation.cat, 0); compare(navigation.item, 1)
            for (var i = 0; i < 20; i++) home.input(data.theme === "x360classic" ? "down" : "right")
            compare(navigation.item, 12)
            home.input(data.theme === "x360classic" ? "down" : "right"); compare(navigation.item, 12)
            if (home.grid) { home.input("up"); verify(navigation.item >= 0 && navigation.item < 13) }
            navigation.setItem(999); compare(navigation.item, 12)
            navigation.cats = [{name: "Empty", items: []}]
            navigation.cat = 0
            home.input("accept"); compare(launches, 2)
            compare(home.selected, 0)
        }
        function test_disabled_and_live_updates_data() { return test_themes_data().concat([{tag: "ps3", theme: "ps3"}]) }
        function test_disabled_and_live_updates(data) {
            home.theme = data.theme
            navigation.cats = [{name: "Games", items: [{title: "Disabled", disabled: true, action: function() { launches++ }}]}]
            navigation.cat = 0
            navigation.input("accept"); compare(launches, 0)
            navigation.cats = [{name: "Games", items: [{title: "Updated", badge: "Update", action: function() { launches++ }}]}]
            compare(home.current.badge, "Update")
            navigation.input("accept"); compare(launches, 1)
            navigation.cats = []
            navigation.input("accept"); compare(launches, 1)
            compare(home.entries.length, 0)
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
            if (data.theme !== "ps3") { page.input("right"); compare(page.current, 1); page.input("left"); compare(page.current, 0) }
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
            home.theme = data.theme
            var shelf = findChild(home, "softwareShelf")
            verify(shelf !== null)
            for (var i = 0; i < 13; i++) {
                navigation.setItem(i)
                wait(220)
                var tile = findChild(home, "homeTile" + i)
                verify(tile !== null)
                verify(tile.visible)
                verify(tile.x >= 0 && tile.y >= 0)
                verify(tile.x + tile.width <= shelf.width + 1)
                verify(tile.y + tile.height <= shelf.height + 1)
                verify(tile.border.width > 0)
                verify(tile.border.color.toString() !== tile.color.toString())
            }
        }
        function test_mouse_data() { return test_themes_data() }
        function test_mouse(data) {
            home.theme = data.theme
            wait(250)
            var tile = findChild(home, "homeTile1")
            verify(tile !== null)
            mouseClick(tile, tile.width / 2, tile.height / 2)
            compare(navigation.item, 1)
            wait(220)
            mouseClick(tile, tile.width / 2, tile.height / 2)
            compare(launches, 1)
            mouseClick(tile, tile.width / 2, tile.height / 2, Qt.RightButton)
            compare(optionsSpy.count, 1)
            compare(launches, 1) // A right-click must not also confirm/launch.
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
        function test_ps3() {
            navigation.input("down"); compare(navigation.item, 1)
            navigation.input("accept"); compare(launches, 1)
            navigation.input("right"); compare(navigation.cat, 1)
            navigation.input("accept"); compare(launches, 2)
        }
    }
}
