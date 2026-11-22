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
    ConsoleBackdrop { id: backdrop; width: 400; height: 240; visible: false }
    Page { id: page; anchors.fill: parent; visible: false }
    OptionsPanel { id: menu; anchors.fill: parent }
    Dialog { id: dialog; anchors.fill: parent }
    TextPrompt { id: prompt; anchors.fill: parent }
    GameLaunch { id: launchScreen; anchors.fill: parent; shown: false }
    TrophyToast { id: trophyToast; x: stageX; y: stageY }
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
            home.visible = true; navigation.visible = false; home.animate = true
            page.rows = []; page.setSort("library"); page.resetNavigation()
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
        function test_interrupted_motion() {
            home.animate = true; home.theme = "ps4"; home.reset()
            for (var i = 0; i < 10; ++i) { home.input("right"); wait(12) }
            home.input("left")
            var tile = findChild(home, "homeTile9")
            tryCompare(tile, "width", 342)
            tryCompare(tile, "height", 412)
            var oldTile = findChild(home,"homeTile10")
            tryCompare(oldTile,"height",200,200)
            compare(oldTile.height,oldTile.width)
            tryCompare(findChild(home, "softwareTitle"), "opacity", 1)
            home.input("home"); wait(40); home.input("back"); wait(40); home.input("home")
            var quick = findChild(home, "consoleQuickMenu")
            tryCompare(quick, "opacity", 1)
            verify(quick.enabled)
            home.input("back")
            verify(!quick.enabled) // Closing pixels cannot consume the next click.
            tryCompare(quick, "visible", false)
        }
        function test_page_entrance_interrupted() {
            page.theme = "ps5"; page.visible = true; page.rows = [{title: "Settings"}]
            page.open(); wait(35); page.open(); wait(35); page.open()
            compare(page.opacity, 1)
            wait(220)
            page.theme = "ps3"; page.open(); wait(30)
            page.theme = "switch2"; page.open()
            compare(page.opacity, 1)
            wait(220)
            compare(page.current, 0)
        }
        function test_motion_disabled() {
            home.animate = false; home.theme = "ps5"; home.reset()
            home.input("right")
            compare(findChild(home, "homeTile1").width, 180)
            home.input("home")
            compare(findChild(home, "consoleQuickMenu").opacity, 1)
            home.input("back")
            compare(findChild(home, "consoleQuickMenu").visible, false)
            home.animate = true
        }
        function test_backdrop_latest_request() {
            backdrop.animate = false
            backdrop.source = "qrc:/qt/qml/Uscl/xmb/icons/cat_ps3.png"
            backdrop.source = "qrc:/qt/qml/Uscl/xmb/icons/cat_ps4.png"
            tryVerify(function() { return backdrop.front >= 0 })
            var image = findChild(backdrop, backdrop.front === 0 ? "backdropFirst" : "backdropSecond")
            tryCompare(image, "status", Image.Ready)
            verify(image.source.toString().endsWith("/cat_ps4.png"))
            backdrop.source = ""
            compare(backdrop.front, -1)
        }
        function test_reference_geometry() {
            home.animate = false; home.theme = "ps5"; home.reset()
            var ps5Tile = findChild(home,"homeTile0")
            var origin = ps5Tile.mapToItem(home,0,0)
            compare(origin.x,168); compare(origin.y,123); compare(ps5Tile.width,180)
            compare(findChild(home,"homeTile1").width,108)
            home.theme = "switch2"; home.reset()
            var switchTile = findChild(home,"homeTile0")
            origin = switchTile.mapToItem(home,0,0)
            compare(origin.x,159); compare(origin.y,300); compare(switchTile.width,384)
            verify(findChild(switchTile,"softwareFocusRing").visible)
            home.animate = true
        }
        function test_nintendo_title_travel_data() { return [{tag:"switch",theme:"switch"},{tag:"switch2",theme:"switch2"}] }
        function test_nintendo_title_travel(data) {
            home.theme = data.theme; home.reset()
            var rows = navigation.cats[0].items.slice()
            rows[0] = Object.assign({}, rows[0], {title:"A long Nintendo software title that continues beyond the visible title region with its full ending"})
            navigation.cats = [{id:"games",name:"Games",items:rows}]
            var title = findChild(home,"nintendoSoftwareTitle")
            verify(title.visible); verify(title.overflow > 0); verify(title.scrolling)
            wait(1100); compare(title.offset,0)
            wait(400); verify(title.offset < 0); verify(title.offset >= -title.overflow)
            home.input("home"); compare(title.offset,0); verify(!title.scrolling)
            home.input("back"); verify(title.scrolling)
            home.input("down"); verify(!title.visible); verify(!title.scrolling); compare(title.offset,0)
            home.input("up"); verify(title.scrolling)
            home.animate = false; verify(!title.scrolling); compare(title.offset,0)
            home.animate = true; home.input("right"); compare(title.text,"Game 1"); compare(title.offset,0); verify(!title.scrolling)
        }
        function test_ps5_card_geometry_and_empty_library() {
            home.animate = false; home.theme = "ps5"; home.reset(); home.input("home")
            wait(30) // Row positions settle during the next polish pass.
            verify(!findChild(home,"softwareTitle").visible)
            verify(!findChild(home,"homeGameDescription").visible)
            var controls = findChild(home,"controlCenterControls")
            for (var i=0; i<3; ++i) {
                var card = findChild(home,"controlCenterCard"+i), p = card.mapToItem(home,0,0)
                compare(card.width,438); compare(card.height,528)
                compare(p.x,84+i*462); compare(p.y,366)
                verify(p.y+card.height < controls.y); verify(p.x+card.width <= home.width)
            }
            for (var controlIndex=0; controlIndex<home.quickEntries.length; ++controlIndex) {
                var control = findChild(home,"controlCenterControl"+controlIndex), cp = control.mapToItem(home,0,0)
                compare(cp.x+control.width/2,457+controlIndex*144)
                verify(cp.y+control.height <= home.height)
            }
            navigation.cats = [{id:"games",name:"Games",items:[]}]
            home.input("up"); home.input("accept")
            compare(sectionSpy.signalArguments[0][0],"library"); compare(launches,0)
        }
        function test_ps5_card_previews_survive_closing_fade() {
            home.theme = "ps5"; home.reset(); home.input("home"); wait(270)
            var previews = findChild(findChild(home,"controlCenterCard0"),"controlCenterThumbnails")
            compare(previews.count,4)
            for (var i=1; i<3; ++i) compare(findChild(findChild(home,"controlCenterCard"+i),"controlCenterThumbnails").count,0)
            home.input("back"); wait(40); compare(previews.count,4)
            tryCompare(findChild(home,"consoleQuickMenu"),"visible",false)
            compare(previews.count,0)
        }
        function test_ps5_options_action() {
            home.theme = "ps5"; home.reset(); home.input("down"); home.input("right")
            compare(home.detailsAction,1); home.input("accept")
            compare(optionsSpy.count,1); compare(launches,0)
            home.input("left"); home.input("accept"); compare(launches,1)
        }
        function test_ps5_control_center_cards() {
            home.theme = "ps5"; home.reset(); home.input("home")
            home.input("up"); compare(home.guideRegion,"cards")
            home.input("right"); compare(home.guideCardIndex,1)
            home.input("accept"); compare(sectionSpy.signalArguments[0][0],"updates")
            compare(launches,0); verify(!home.guideOpen)
            home.input("home"); home.input("up"); home.input("down")
            compare(home.guideRegion,"controls")
            home.input("accept"); compare(sectionSpy.signalArguments[1][0],"library")
            home.input("home"); wait(260)
            var card = findChild(home,"controlCenterCard2")
            mouseClick(card,card.width/2,card.height/2)
            compare(sectionSpy.signalArguments[2][0],"changelog"); compare(launches,0)
        }
        function test_pivot_interruptions_data() {
            return ["x360classic","x360","xboxone"].map(function(t) { return {tag:t,theme:t} })
        }
        function test_pivot_interruptions(data) {
            home.theme = data.theme; home.reset()
            home.input("r1"); wait(35); home.input("l1"); wait(35); home.input("r1")
            tryCompare(home,"pivotOffset",0); tryCompare(home,"pivotOpacity",1)
            verify(home.tab >= 0 && home.tab < home.tabs.length)
            var before = home.tab; home.input("accept")
            compare(home.tab,before)
        }
        function libraryFixture(count, theme) {
            var rows = []
            for (var i = 0; i < count; i++) rows.push({gameId:"AUDIT"+i, title:"Long game title with distinct ending "+i,console:i%2 ? "PS4":"PS3",action:function() { launches++ },options:function() { return [] }})
            home.visible = false; page.theme = theme || "ps5"; page.presentation = "library";
            page.rows = rows; page.visible = true; page.resetNavigation(); page.open()
            wait(140)
            return findChild(page,"libraryGrid")
        }
        function test_library_sorting_data() { return test_themes_data() }
        function test_library_sorting(data) {
            libraryFixture(3,data.theme)
            page.rows = [{gameId:"z",title:"Zebra",console:"PS3"},{gameId:"a",title:"Alpha",console:"PS4"},{gameId:"b",title:"Alpha",console:"PS3"}]
            page.current = 0; page.setSort("title")
            compare(page.displayRows.map(function(e){return e.gameId}).join(","),"a,b,z"); compare(page.cur.gameId,"z")
            page.setSort("titleDescending"); compare(page.current,0)
            page.setSort("console"); compare(page.displayRows.map(function(e){return e.gameId}).join(","),"b,z,a"); compare(page.cur.gameId,"z")
            compare(page.rows[0].gameId,"z") // Sorting must not mutate the live source model.
            page.selectFilter(1); compare(page.displayRows.length,2)
            page.current = 1; page.setSort("title"); compare(page.cur.gameId,"z")
            var saved = page.saveNavigation(); page.resetNavigation(); page.restoreNavigation(saved)
            compare(page.cur.gameId,"z"); compare(page.filters[page.filterIndex],"PS3")
            page.setSort("invalid"); compare(page.sortMode,"library"); compare(page.cur.gameId,"z")
        }
        function test_library_sort_focus_data() { return test_themes_data() }
        function test_library_sort_focus(data) {
            libraryFixture(20,data.theme)
            var spy = Qt.createQmlObject('import QtTest; SignalSpy { signalName:"sortRequested" }',page); spy.target=page
            page.input("up"); verify(page.sortFocus)
            page.input("accept"); compare(spy.count,1); compare(launches,0)
            var button = findChild(page,"librarySort")
            verify(button.visible); verify(button.border.width > 1)
            mouseClick(button,button.width/2,button.height/2); compare(spy.count,2)
            var game = findChild(page,"libraryGame1")
            mouseClick(game,game.width/2,game.height/2); verify(!page.sortFocus); compare(page.current,1); compare(launches,0)
            page.current=0; page.sortFocus=true
            page.input("down"); verify(!page.sortFocus)
            page.input("up"); page.input("left"); verify(page.filterFocus); verify(!page.sortFocus)
            page.input("right"); verify(!page.filterFocus)
            page.input("up"); var state = page.saveNavigation(); page.resetNavigation(); page.restoreNavigation(state); verify(page.sortFocus)
            page.rows=[]; page.input("accept"); compare(spy.count,3); compare(launches,0)
            spy.destroy()
        }
        function test_library_last_row_and_real_layout_data() { return test_themes_data() }
        function test_library_last_row_and_real_layout(data) {
            var grid = libraryFixture(53,data.theme)
            page.current = page.gridColumns === 1 ? 51 : 49; page.input("down"); compare(page.current,52)
            tryVerify(function() { return grid.currentItem && grid.currentItem.y >= grid.contentY && grid.currentItem.y+grid.currentItem.height <= grid.contentY+grid.height+1 })
            page.input("right"); compare(page.current,52)
            page.current = page.gridColumns-1; page.input("right"); compare(page.current,page.gridColumns-1)
            page.input("down"); compare(page.current,2*page.gridColumns-1)
            tryVerify(function() { return grid.currentItem && grid.currentItem.y >= grid.contentY && grid.currentItem.y+grid.currentItem.height <= grid.contentY+grid.height+1 })
            compare(grid.currentItem.x,(page.gridColumns-1)*grid.cellWidth)
            page.input("options"); compare(optionsSpy.count,0) // Page has its own signal.
            var spy = Qt.createQmlObject('import QtTest; SignalSpy { signalName:"showOptions" }',page); spy.target = page
            page.input("options"); compare(spy.count,1); compare(spy.signalArguments[0][0].gameId,"AUDIT"+(2*page.gridColumns-1)); spy.destroy()
        }
        function test_library_filter_relayout_and_offscreen_activation() {
            var grid = libraryFixture(73)
            page.current = 72; wait(30); page.selectFilter(1); page.selectFilter(2)
            tryVerify(function() { return grid.currentItem && grid.currentItem.y >= grid.contentY && grid.currentItem.y+grid.currentItem.height <= grid.contentY+grid.height+1 })
            compare(page.cur.gameId,"AUDIT1")
            grid.contentY = grid.contentHeight-grid.height
            page.input("accept"); compare(launches,0)
            tryVerify(function() { return grid.currentItem && grid.currentItem.y >= grid.contentY && grid.currentItem.y+grid.currentItem.height <= grid.contentY+grid.height+1 })
            page.input("accept"); compare(launches,1)
        }
        function test_library_saved_state_and_persistent_tools() {
            libraryFixture(73); page.selectFilter(2); page.current = 12; wait(30)
            var state = page.saveNavigation(), gameId = page.cur.gameId
            page.presentation = ""; page.rows = [{title:"Sound settings"}]; page.resetNavigation()
            page.presentation = "library";
            var rows = []; for (var i = 0; i < 73; i++) rows.push({gameId:"AUDIT"+i, title:"Game "+i,console:i%2 ? "PS4":"PS3"})
            page.rows = rows; page.restoreNavigation(state)
            compare(page.filters[page.filterIndex],"PS4"); compare(page.cur.gameId,gameId)
            for (var tool of ["add","updates","help"]) {
                var item = findChild(page,"libraryTool_"+tool)
                verify(item.visible); var pos = item.mapToItem(page,0,0)
                verify(pos.y >= 0 && pos.y+item.height < 920)
            }
        }
        function test_xbox_tiles_do_not_overlap() {
            home.theme = "x360"; home.reset()
            for (var selected = 0; selected < home.panelEntries.length; selected++) {
                home.tileIndex = selected; wait(130)
                for (var a = 0; a < home.panelEntries.length; a++) for (var b = a+1; b < home.panelEntries.length; b++) {
                    var one = findChild(home,"dashboardTile"+a), two = findChild(home,"dashboardTile"+b)
                    var p = one.mapToItem(home,0,0), q = two.mapToItem(home,0,0)
                    verify(p.x+one.width*one.scale <= q.x || q.x+two.width*two.scale <= p.x || p.y+one.height*one.scale <= q.y || q.y+two.height*two.scale <= p.y,"Xbox focus must not cover adjacent tiles")
                }
            }
        }
        function test_long_game_titles_do_not_cover_actions_data() { return [{tag:"ps4",theme:"ps4"},{tag:"ps5",theme:"ps5"}] }
        function test_long_game_titles_do_not_cover_actions(data) {
            home.theme = data.theme; home.reset()
            var rows = navigation.cats[0].items.slice(); rows[0] = Object.assign({},rows[0],{title:Array(12).fill("A long subtitle with more words").join(" ")})
            navigation.cats = [{id:"games",name:"Games",items:rows}]
            wait(240)
            var title = findChild(home,"softwareTitle"), description = findChild(home,"homeGameDescription")
            verify(title.contentHeight <= title.height+1)
            verify(title.mapToItem(home,0,title.height).y <= description.mapToItem(home,0,0).y)
        }
        function test_library_mouse_and_wheel_data() { return test_themes_data() }
        function test_library_mouse_and_wheel(data) {
            var grid = libraryFixture(73,data.theme)
            var index = page.gridColumns === 1 ? 2 : 6
            var game = findChild(page,"libraryGame"+index)
            verify(game !== null)
            mouseClick(game,game.width/2,game.height/2); compare(page.current,index); compare(launches,0)
            wait(30)
            mouseClick(game,game.width/2,game.height/2); compare(launches,1)
            var spy = Qt.createQmlObject('import QtTest; SignalSpy { signalName:"showOptions" }',page); spy.target = page
            mouseClick(game,game.width/2,game.height/2,Qt.RightButton)
            compare(spy.count,1); compare(spy.signalArguments[0][0].gameId,"AUDIT"+index); spy.destroy()
            mouseWheel(grid,grid.width/2,grid.height/2,0,-720)
            wait(160)
            verify(grid.contentY > 0)
            var before = launches; page.input("accept")
            compare(launches,before)
            wait(30); page.input("accept"); compare(launches,before+1)
        }
        function test_theme_switch_drops_closing_overlays() {
            home.theme = "ps5"; home.reset(); home.input("home"); wait(40)
            home.theme = "ps4"
            compare(findChild(home,"consoleQuickMenu").visible,false)
            menu.theme = "ps5"; menu.show([{title:"Setting"}]); wait(40); menu.close(); menu.theme = "switch2"
            compare(menu.visible,false)
        }
        function test_ps4_function_transition_has_no_icon_collisions() {
            home.theme = "ps4"; home.reset(); wait(280); home.input("up")
            var previous = 0
            for (var sample of [40,100,160,240,300]) {
                wait(sample-previous); previous = sample
                var glyphs = []
                for (var shortcut of home.shortcuts) {
                    var fn = findChild(home,"homeShortcut_"+shortcut.key)
                    if (fn.visible && fn.opacity > .1) glyphs.push(findChild(fn,"functionGlyph"))
                }
                for (var i = 0; i < glyphs.length; ++i) for (var j = i+1; j < glyphs.length; ++j) {
                    var p = glyphs[i].mapToItem(home,0,0), q = glyphs[j].mapToItem(home,0,0)
                    verify(p.x+glyphs[i].width <= q.x || q.x+glyphs[j].width <= p.x || p.y+glyphs[i].height <= q.y || q.y+glyphs[j].height <= p.y,"Function icons collided at "+sample+" ms")
                }
                if (sample >= 160) compare(findChild(home,"consoleProfile").opacity,0)
            }
        }
        function test_backdrop_reused_buffer_is_not_partially_visible() {
            backdrop.animate = true; backdrop.source = "qrc:/qt/qml/Uscl/xmb/icons/cat_ps3.png"
            wait(250); backdrop.source = "qrc:/qt/qml/Uscl/xmb/icons/cat_ps4.png"
            tryVerify(function() { return backdrop.front >= 0 && findChild(backdrop,backdrop.front === 0 ? "backdropFirst":"backdropSecond").source.toString().endsWith("cat_ps4.png") })
            wait(35)
            var inactive = findChild(backdrop,backdrop.front === 0 ? "backdropSecond":"backdropFirst")
            backdrop.source = "qrc:/qt/qml/Uscl/xmb/icons/cat_switch.png"
            compare(inactive.opacity,0)
            wait(260)
            var active = findChild(backdrop,backdrop.front === 0 ? "backdropFirst":"backdropSecond")
            verify(active.source.toString().endsWith("cat_switch.png")); compare(active.opacity,1)
            backdrop.source = ""
        }
        function test_themes_data() {
            return ["ps4", "ps5", "x360classic", "x360", "xboxone", "switch", "switch2"].map(function(t) { return {tag: t, theme: t} })
        }
        function test_themes(data) {
            home.theme = data.theme; home.reset()
            compare(home.entries.length, data.theme === "ps5" ? 11 : 13) // Compact PS5 row, with a deliberate library entry.
            verify(home.entries.slice(0,-1).every(function(e) { return !!e.console }))
            if (!home.grid && data.theme !== "x360classic") {
                home.input("right"); compare(home.selected, 1)
                home.input("accept"); compare(launches, 1)
                home.input("options"); compare(optionsSpy.count, 1)
                home.input("down")
                if (home.nintendo) verify(home.categoryFocus)
                else compare(home.region, "details")
                home.input("back"); compare(home.region, "software")
                for (var i = 0; i < 30; i++) home.input("right")
                compare(home.selected, home.entries.length - 1)
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
        function test_switch2_opaque_cover_corners() {
            home.theme = "switch2"; home.reset(); wait(180)
            var tile = findChild(home,"homeTile0")
            var opaqueCover = Qt.createQmlObject('import QtQuick; Rectangle { anchors.fill: parent; color: "red"; z: 0.5 }',tile)
            wait(60)
            var capture = grabImage(home)
            var origin = tile.mapToItem(home,0,0)
            verify(capture.red(origin.x+2,origin.y+2) > 200 && capture.green(origin.x+2,origin.y+2) > 200,"An opaque cover must not square off the rounded corner")
            compare(capture.red(origin.x+100,origin.y+100),255); compare(capture.green(origin.x+100,origin.y+100),0)
            opaqueCover.destroy()
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
            var games = navigation.cats[0].items.slice()
            for (var i = 13; i < 25; ++i) games.push({gameId:"GAME"+i, console:"PS4", title:"Game "+i, action:function() { launches++ }})
            navigation.cats = [{id:"games", name:"Games", items:games},navigation.cats[1]]
            home.pinnedIds = games.map(function(g) { return g.gameId })
            home.tab = 0
            compare(home.panelEntries.length,26)
            compare(home.groupSize,15)
            home.tileIndex = 14; home.input("right")
            compare(home.tileIndex,15)
            home.input("accept"); compare(launches,1)
            home.pinnedIds = []
            compare(home.tileIndex,0); compare(home.panelEntries.length,1)
        }
        function test_library_tools_and_scroll() {
            page.theme = "switch2"; page.presentation = "library"; page.visible = true
            var rows = []
            for (var i = 0; i < 200; i++) rows.push({title:"Game "+i,console:"Console "+i%10})
            page.rows = rows; page.current = 0; page.input("left")
            for (i = 0; i < page.filters.length+3; i++) page.input("down")
            compare(page.libraryNavIndex,page.filters.length+3)
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
                for (var i = 0; i < home.entries.length; i++) {
                    home.choose(i); wait(180)
                    var game = findChild(home,"homeTile"+i)
                    verify(game !== null && game.visible)
                    verify(game.x >= 0 && game.y >= 0)
                    verify(game.x+game.width <= shelf.width+1)
                    verify(game.y+game.height <= shelf.height+1)
                    verify(game.border.width > 0 || (data.theme === "switch2" && findChild(game,"softwareFocusRing").visible))
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
        function test_trophy_toast_data() {
            return [{tag: "ps3", corner: "topRight"}, {tag: "ps4", corner: "topLeft"}, {tag: "ps5", corner: "topRight"},
                    {tag: "x360classic", corner: "bottom"}, {tag: "x360", corner: "bottom"}, {tag: "xboxone", corner: "bottom"},
                    {tag: "switch", corner: "topRight"}, {tag: "switch2", corner: "topRight"}]
        }
        function test_trophy_toast(data) {
            trophyToast.theme = data.tag
            compare(trophyToast.corner, data.corner)
            verify(trophyToast.stageX >= 0 && trophyToast.stageX + trophyToast.width <= 1920)
            verify(trophyToast.stageY >= 0 && trophyToast.stageY + trophyToast.height <= 1080)
            var shown = Qt.createQmlObject('import QtTest; SignalSpy { signalName: "shown" }', trophyToast)
            shown.target = trophyToast
            trophyToast.show({name: "First", grade: "gold"})
            trophyToast.show({name: "Second", grade: "platinum"})
            verify(trophyToast.active)
            compare(shown.count, 1); compare(trophyToast.trophy.name, "First"); compare(trophyToast.gamerscore, 90)
            tryCompare(shown, "count", 2, 8000)   // one at a time, in order
            compare(trophyToast.trophy.name, "Second"); compare(trophyToast.gradeName, "Platinum")
            tryCompare(trophyToast, "active", false, 8000)
            shown.destroy()
        }
        function test_friend_rows_and_buttons() {
            page.theme = "xboxone"; page.visible = true
            page.rows = [{title: "RiverFox", status: "online", icon: "", help: "✕ for their USCL or Steam profile", options: function() { return [] }}]
            var modern = findChild(page, "settingsRow0")
            verify(modern)
            var helpText = page.children.filter(function(c) { return c.controller === page })[0]
            compare(helpText.buttons("✕ to join, △ for options"), "A to join, ☰ for options")
            page.theme = "ps4"
            compare(helpText.buttons("✕ to join"), "✕ to join")
            var opened = Qt.createQmlObject('import QtTest; SignalSpy { signalName: "showOptions" }', page)
            opened.target = page
            page.input("options"); compare(opened.count, 1)   // △ on a friend: Join / USCL profile / Steam profile
            opened.destroy()
        }
        function test_ps3() {
            navigation.input("down"); compare(navigation.item, 1)
            navigation.input("accept"); compare(launches, 1)
            navigation.input("right"); compare(navigation.cat, 1)
            navigation.input("accept"); compare(launches, 2)
        }
    }
}
