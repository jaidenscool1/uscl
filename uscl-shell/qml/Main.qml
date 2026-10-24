import QtQuick
import QtQuick.Window
import QtMultimedia
import QtCore

Window {
    id: root
    width: 1280
    height: 720
    visible: true
    visibility: windowed ? Window.Windowed : Window.FullScreen
    color: "black"
    title: "USCL"

    readonly property bool xboxPad: gamepad.name.indexOf("Xbox") >= 0

    // PS3 month colours (kodi-xmb gen_skin.py), Jan..Dec, then Black; they multiply bg_base.png
    readonly property var monthColors: ["#B8BEC8", "#D8B040", "#A8C840", "#E07AB8", "#4CA834", "#B058C8",
                                        "#38B0A4", "#48B4E8", "#3C6CD8", "#955CC8", "#B87038", "#C83C38", "#202022"]
    readonly property var colorNames: [qsTr("Silver"), qsTr("Gold"), qsTr("Lime"), qsTr("Pink"), qsTr("Green"), qsTr("Magenta"),
                                       qsTr("Teal"), qsTr("Sky Blue"), qsTr("Blue"), qsTr("Purple"), qsTr("Copper"), qsTr("Red"), qsTr("Black")]
    readonly property var avatars: [["uscl", "USCL"], ["controller", qsTr("Controller")], ["star", qsTr("Star")], ["music", qsTr("Music")],
                                    ["film", qsTr("Film")], ["bolt", qsTr("Bolt")], ["heart", qsTr("Heart")], ["moon", qsTr("Moon")],
                                    ["rocket", qsTr("Rocket")], ["pet", qsTr("Pet")]]

    Settings {
        id: prefs
        property int bgColor: 0          // 0 automatic (month), 1..13 fixed
        property bool nightShade: true
        property bool sounds: true
        property bool setupDone: false
        property int xmbCategory: 2
        property bool clock24: false
        property string dateFormat: "MM/dd/yy"
        property string avatar: "uscl"
        property bool autoScale: true        // Resolution Scale follows this PC's graphics card and screen
        property bool autoControllers: true  // every connected controller becomes a player
        property bool bootScreen: true
    }

    // re-read RPCS3's config after a change (the rows read it through this)
    property int cfgTick: 0
    property int hour: new Date().getHours()
    property int month: new Date().getMonth()
    Timer { interval: 60000; running: true; repeat: true; onTriggered: { var d = new Date(); root.hour = d.getHours(); root.month = d.getMonth() } }

    function ic(n) { return "qrc:/qt/qml/Uscl/xmb/icons/" + n + ".png" }
    function avatarUrl(n) { return "qrc:/qt/qml/Uscl/xmb/avatars/" + (n || "uscl") + ".png" }
    readonly property color tint: prefs.bgColor > 0 ? monthColors[prefs.bgColor - 1] : monthColors[month]
    // darker in the evening and at night, like the original crossbar
    readonly property real shade: !prefs.nightShade ? 0 : hour < 6 ? 0.56 : hour < 8 ? 0.25 : hour < 17 ? 0 : hour < 19 ? 0.16 : hour < 21 ? 0.31 : 0.47

    // ---------------------------------------------------------------- actions

    function classic() {
        if (!launcher.coreFound()) { coreMissing(); return }
        launcher.openClassic()
    }
    function coreMissing() {
        sound("error")
        dialog.show(qsTr("Error"), qsTr("The PS3 emulator (rpcs3) wasn't found next to USCL. Reinstalling USCL puts it back."))
    }
    function play(g) {
        if (!launcher.coreFound()) { coreMissing(); return }
        if (!system.firmwareInstalled) {
            sound("error")
            dialog.show(qsTr("System Update"), qsTr("Games need the PS3 system software. Install it now?"),
                        { buttons: [{ title: qsTr("Yes"), action: function() { openPage(systemUpdatePage, ic("it_update"), 1) } }, { title: qsTr("No") }] })
            return
        }
        if (prefs.autoControllers) launcher.prepareInput(gamepad.names)
        if (prefs.autoScale) system.setRpcs3Value("Video", "Resolution Scale", String(autoScaleValue))
        launcher.startGame(g.path, g.title)
    }
    readonly property int autoScaleValue: system.recommendedScale(Screen.height * Screen.devicePixelRatio)
    function gameInfo(g) {
        return qsTr("%1\n\nSerial: %2      Console: %3\n%4").arg(g.title).arg(g.serial).arg(g.console).arg(g.path)
    }

    function typeName(after) {
        prompt.show(qsTr("Online ID"), system.onlineName || online.steamName, function(t) {
            if (!online.validName(t)) return qsTr("Use 3 to 16 letters, numbers, - or _.")
            if (!online.setup(t)) return qsTr("Couldn't save it.")
            system.refresh()
            notice.show(qsTr("Online ID: %1").arg(t), avatarUrl(prefs.avatar))
            if (after) after()
            return ""
        })
    }
    function nameChooser(after) {
        var o = []
        if (online.steamName)
            o.push({ title: qsTr("Use Steam Name (%1)").arg(online.steamName), action: function() {
                if (online.setup(online.steamName)) { system.refresh(); notice.show(qsTr("Online ID: %1").arg(online.steamName), avatarUrl(prefs.avatar)); if (after) after() }
            } })
        o.push({ title: qsTr("Type a Name"), action: function() { typeName(after) } })
        options.show(o, 0)
    }
    function needsName(then) {
        if (system.onlineName) { then(); return }
        nameChooser(then)
    }

    function installFromStorage() {
        var files = system.findFirmware()
        if (!files.length) {
            dialog.show(qsTr("Update via Storage Media"), qsTr("No PS3UPDAT.PUP was found in Downloads, on the desktop or in your home folder."))
            return
        }
        options.show(files.map(function(p) { return { title: p, action: function() { system.installFirmware(p); firmwareProgress() } } }), 0)
    }
    function firmwareProgress() {
        dialog.show(qsTr("System Update"), "", {
            live: function() { return system.firmwareStatus },
            progress: function() { return system.firmwareBusy ? system.firmwareProgress : -1 }
        })
    }

    function hostPicker() {
        needsName(function() {
            var o = []
            for (var i = 0; i < games.count; i++) {
                let g = games.get(i)
                o.push({ title: g.title, action: function() { online.host(g.serial, g.title) } })
            }
            o.push({ title: qsTr("Another Game (PSN)"), action: function() { online.host("", "") } })
            options.show(o, 0)
        })
    }
    function testConnection() {
        online.testConnection()
        dialog.show(qsTr("Internet Connection Test"), "", { live: function() {
            return online.tests.map(function(t) {
                var mark = t.result === "ok" ? qsTr("Succeeded") : t.result === "fail" ? qsTr("Failed") : qsTr("Testing...")
                return t.name + ":  " + mark + (t.detail ? "  (" + t.detail + ")" : "")
            }).join("\n")
        } })
    }
    function sessionText() {
        switch (online.state) {
        case "hosting": return qsTr("Hosting Session %1").arg(online.session)
        case "joined": return qsTr("In Session %1").arg(online.session)
        case "waiting": return qsTr("Waiting for a Steam Invite...")
        case "starting": return qsTr("Starting the Session...")
        default: return qsTr("Online Play")
        }
    }
    function guide() {
        dialog.show(qsTr("User's Guide"), xboxPad
            ? qsTr("D-pad or left stick: move     A: select     B: back     Y: options\nLB / RB: jump through long lists\nHold the Guide button during a game to quit back here.\nOnline: Network → Host a Session, or accept a Steam invite.")
            : qsTr("D-pad or left stick: move     ✕: select     ○: back     △: options\nL1 / R1: jump through long lists\nHold the PS button during a game to quit back here.\nOnline: Network → Host a Session, or accept a Steam invite."))
    }

    // ---------------------------------------------------------------- settings rows

    // a row whose value comes from a list of choices; get/set read and write the stored value
    function choiceRow(title, help, choices, get, set, icon) {
        var v = get()
        var label = v
        for (var i = 0; i < choices.length; i++) if (String(choices[i][1]) === String(v)) label = choices[i][0]
        return { title: title, value: label, help: help, icon: icon, choices: true, action: function() {
            options.show(choices.map(function(c) {
                return { title: c[0], checked: String(c[1]) === String(v), action: function() { set(c[1]); cfgTick++ } }
            }))
        } }
    }
    function rpcs3Row(section, key, title, help, choices) {
        return choiceRow(title, help, choices, function() { cfgTick; return system.rpcs3Value(section, key) },
                         function(v) { system.setRpcs3Value(section, key, String(v)) })
    }
    function prefRow(prop, title, help, choices) {
        return choiceRow(title, help, choices, function() { return prefs[prop] }, function(v) { prefs[prop] = v })
    }
    function allSettingsRow() {
        return { title: qsTr("All Settings in This Category"), help: qsTr("Opens the classic window with every setting"), action: classic }
    }
    readonly property var onOff: [[qsTr("On"), "true"], [qsTr("Off"), "false"]]

    function gameSettingsPage() {
        return [
            rpcs3Row("Video", "Renderer", qsTr("Renderer"), qsTr("How games are drawn. Vulkan is fastest on most PCs."), [["Vulkan", "Vulkan"], ["OpenGL", "OpenGL"]]),
            rpcs3Row("Video", "Resolution", qsTr("Output Resolution"), qsTr("The resolution games think they run at. Most games look best at 720p."),
                     [["720p (1280x720)", "1280x720"], ["1080p (1920x1080)", "1920x1080"], ["480p (720x480)", "720x480"], ["576p (720x576)", "720x576"]]),
            choiceRow(qsTr("Resolution Scale"),
                      system.gpu ? qsTr("Automatic picks %1% for %2 on this screen").arg(autoScaleValue).arg(system.gpu) : qsTr("Renders games sharper than the PS3 could"),
                      [[qsTr("Automatic (this PC: %1%)").arg(autoScaleValue), "auto"], ["50%", "50"], ["100% (native)", "100"], ["150% (1080p)", "150"],
                       ["200% (1440p)", "200"], ["250%", "250"], ["300% (4K)", "300"]],
                      function() { cfgTick; return prefs.autoScale ? "auto" : system.rpcs3Value("Video", "Resolution Scale") },
                      function(v) { prefs.autoScale = v === "auto"; system.setRpcs3Value("Video", "Resolution Scale", v === "auto" ? String(autoScaleValue) : v) }),
            rpcs3Row("Video", "Frame limit", qsTr("Frame Limit"), qsTr("Caps the frame rate. Auto matches what the game expects."),
                     [[qsTr("Auto"), "Auto"], [qsTr("Off"), "Off"], ["30", "30"], ["50", "50"], ["60", "60"], ["120", "120"], [qsTr("Display rate"), "Display"], [qsTr("PS3 native"), "PS3 Native"]]),
            rpcs3Row("Video", "Anisotropic Filter Override", qsTr("Texture Filtering"), qsTr("Sharper textures at an angle. Auto lets each game decide."),
                     [[qsTr("Auto"), "0"], ["2x", "2"], ["4x", "4"], ["8x", "8"], ["16x", "16"]]),
            rpcs3Row("Video", "Output Scaling Mode", qsTr("Upscaling"), qsTr("How the picture is stretched to your screen."),
                     [[qsTr("Bilinear"), "Bilinear"], [qsTr("Nearest"), "Nearest"], ["FidelityFX Super Resolution", "FidelityFX Super Resolution"]]),
            rpcs3Row("Video", "Shader Mode", qsTr("Shader Compilation"), qsTr("How graphics code is prepared. The default avoids stutter."),
                     [[qsTr("Async with interpreter (recommended)"), "Async Recompiler with Shader Interpreter"], [qsTr("Async"), "Async Recompiler (multi-threaded)"],
                      [qsTr("Interpreter only"), "Shader Interpreter only"], [qsTr("Legacy"), "Legacy Recompiler (single-threaded)"]]),
            rpcs3Row("Video", "Shader Precision", qsTr("Shader Precision"), qsTr("Lower is faster, higher is more accurate."),
                     [[qsTr("Auto"), "Auto"], [qsTr("Low"), "Low"], [qsTr("High"), "High"], [qsTr("Ultra"), "Ultra"]]),
            allSettingsRow()
        ]
    }
    function videoSettingsPage() {
        return [
            rpcs3Row("Video", "Aspect ratio", qsTr("TV Type"), qsTr("Widescreen (16:9) or standard (4:3) for games that support both."), [["16:9", "16:9"], ["4:3", "4:3"]]),
            rpcs3Row("Video", "Stretch To Display Area", qsTr("Stretch to Screen"), qsTr("Fill the whole screen instead of keeping the game's shape."), onOff),
            allSettingsRow()
        ]
    }
    function soundSettingsPage() {
        return [
            rpcs3Row("Audio", "Master Volume", qsTr("Game Volume"), qsTr("How loud games play."),
                     [["100", "100"], ["90", "90"], ["80", "80"], ["70", "70"], ["60", "60"], ["50", "50"], ["40", "40"], ["30", "30"], ["20", "20"], ["10", "10"], [qsTr("Mute"), "0"]]),
            rpcs3Row("Audio", "Audio Format", qsTr("Audio Output Format"), qsTr("Stereo, or surround if you have a 5.1 / 7.1 system."),
                     [[qsTr("Stereo"), "Stereo"], [qsTr("Surround 5.1"), "Surround 5.1"], [qsTr("Surround 7.1"), "Surround 7.1"], [qsTr("Automatic"), "Automatic"]]),
            rpcs3Row("Audio", "Renderer", qsTr("Audio Output"), qsTr("The sound system games play through."),
                     Qt.platform.os === "windows" ? [["XAudio2", "XAudio2"], ["Cubeb", "Cubeb"], ["FAudio", "FAudio"]] : [["Cubeb", "Cubeb"], ["FAudio", "FAudio"]]),
            prefRow("sounds", qsTr("Menu Sounds"), qsTr("Sounds when you move through the menus."), [[qsTr("On"), true], [qsTr("Off"), false]]),
            allSettingsRow()
        ]
    }
    function themeSettingsPage() {
        var colors = [[qsTr("Automatic (changes every month)"), 0]].concat(colorNames.map(function(n, i) { return [n, i + 1] }))
        return [
            { title: qsTr("Theme"), value: "PlayStation 3 (XMB)", help: qsTr("More console looks (PS4, Xbox 360) are coming."), choices: true,
              action: function() { options.show([{ title: "PlayStation 3 (XMB)", checked: true }, { title: "PlayStation 4", disabled: true },
                                                 { title: "Xbox 360 Classic", disabled: true }, { title: "Xbox 360 (2012)", disabled: true }]) } },
            prefRow("bgColor", qsTr("Background Color"), qsTr("Automatic follows the month, like the PS3."), colors),
            prefRow("bootScreen", qsTr("Boot Screen"), qsTr("The USCL logo and health warning when USCL starts (✕ skips it)."), [[qsTr("On"), true], [qsTr("Off"), false]]),
            prefRow("nightShade", qsTr("Darker at Night"), qsTr("The background dims in the evening and at night."), [[qsTr("On"), true], [qsTr("Off"), false]])
        ]
    }
    function dateTimePage() {
        return [
            prefRow("clock24", qsTr("Time Format"), qsTr("How the clock shows the time."), [[qsTr("12-Hour Clock"), false], [qsTr("24-Hour Clock"), true]]),
            prefRow("dateFormat", qsTr("Date Format"), qsTr("How the date is shown."), [["MM/DD/YY", "MM/dd/yy"], ["DD/MM/YY", "dd/MM/yy"], ["YY/MM/DD", "yy/MM/dd"]])
        ]
    }
    function accessoryPage() {
        return [
            { title: qsTr("Controllers"), value: gamepad.count ? gamepad.name : qsTr("None connected"),
              help: gamepad.count > 1 ? qsTr("%1 controllers connected").arg(gamepad.count) : qsTr("Connect a controller by USB or Bluetooth"), disabled: !gamepad.count },
            prefRow("autoControllers", qsTr("Automatic Controller Setup"), qsTr("Each connected controller becomes a player when a game starts"), [[qsTr("On"), true], [qsTr("Off"), false]]),
            { title: qsTr("Button Assignments"), help: qsTr("Set up which buttons do what (classic window → Pads)"), action: classic },
            allSettingsRow()
        ]
    }
    function networkSettingsPage() {
        return [
            { title: qsTr("Online ID"), value: system.onlineName || qsTr("Not set"), help: qsTr("The name other players see"), choices: true, action: function() { nameChooser() } },
            { title: qsTr("Internet Connection Test"), help: qsTr("Checks USCL, the network and Steam"), action: testConnection },
            { title: qsTr("Online Server"), value: qsTr("Through Steam"), help: qsTr("Sessions run on the host's PC and reach friends over Steam; nothing to set up"), disabled: true }
        ]
    }
    function systemSettingsPage() {
        var langs = ["English (US)", "English (UK)", "Japanese", "French", "Spanish", "German", "Italian", "Dutch", "Portuguese (Portugal)", "Portuguese (Brazil)",
                     "Russian", "Korean", "Chinese (Traditional)", "Chinese (Simplified)", "Finnish", "Swedish", "Danish", "Norwegian", "Polish", "Turkish"]
        return [
            rpcs3Row("System", "Language", qsTr("System Language"), qsTr("The language games use, when they have it."), langs.map(function(l) { return [l, l] })),
            { title: qsTr("System Information"), help: qsTr("Versions, processor and memory"), action: function() { dialog.show(qsTr("System Information"), system.info()) } },
            { title: qsTr("Run Setup Again"), help: qsTr("Online ID, system software and games"), action: function() { setup.start() } },
            { title: qsTr("Restore Default Settings"), help: qsTr("Menu settings back to how they started (games and online ID stay)"),
              action: function() { dialog.confirm(qsTr("Restore Default Settings"), qsTr("Restore the menu settings to their defaults?"), function() {
                  prefs.bgColor = 0; prefs.nightShade = true; prefs.sounds = true; prefs.clock24 = false; prefs.dateFormat = "MM/dd/yy"
                  notice.show(qsTr("Settings restored"), ic("it_system"))
              }) } },
            allSettingsRow()
        ]
    }
    function systemUpdatePage() {
        return [
            { title: qsTr("Update via Internet"), help: qsTr("Downloads the latest PS3 system software from Sony and installs it"),
              action: function() { system.downloadFirmware(); firmwareProgress() } },
            { title: qsTr("Update via Storage Media"), help: qsTr("Installs a PS3UPDAT.PUP you already have"), action: installFromStorage },
            { title: qsTr("Installed Version"), value: system.firmwareVersion || qsTr("None"), disabled: true, help: qsTr("USCL %1").arg(system.version) }
        ]
    }
    function profilePage() {
        return [
            { title: qsTr("Online ID"), value: system.onlineName || qsTr("Not set"), help: qsTr("The name other players see"), choices: true, action: function() { nameChooser() } },
            { title: qsTr("Picture"), value: (avatars.find(function(a) { return a[0] === prefs.avatar }) || ["", "USCL"])[1], help: qsTr("Shown in the clock box and on your profile"),
              icon: avatarUrl(prefs.avatar), choices: true,
              action: function() { options.show(avatars.map(function(a) { return { title: a[1], checked: a[0] === prefs.avatar, action: function() { prefs.avatar = a[0] } } })) } },
            { title: qsTr("Steam Account"), value: online.steamName || qsTr("Not found"), help: qsTr("Sessions and invites use the Steam account signed in on this PC"), disabled: true }
        ]
    }

    // Add Games: an inner page listing folders; ○ goes up one folder
    property string browsePath: ""
    property var browseRoots: []
    function browsePage() {
        if (!browsePath)
            return browseRoots.map(function(r) { return { title: r.name, help: r.path, icon: ic(r.path === browseRoots[0].path ? "it_person" : "it_storage"), action: function() { browseTo(r.path) } } })
        var rows = [{ title: qsTr("Search This Folder"), help: browsePath, icon: ic("it_addgames"),
                      action: function() { games.scanFolder(browsePath); closePages(); notice.show(qsTr("Looking for games..."), ic("it_addgames")) } }]
        return rows.concat(games.folders(browsePath).map(function(f) { return { title: f.name, icon: ic("it_files"), action: function() { browseTo(f.path) } } }))
    }
    function browseTo(path) { browsePath = path; page.current = 0; page.open() }
    function browseBack() {
        if (!browsePath) { popPage(); return }
        var isRoot = browseRoots.some(function(r) { return r.path === browsePath })
        browsePath = isRoot ? "" : (browsePath.substring(0, browsePath.lastIndexOf("/")) || "/")
        page.current = 0
    }
    function addGames() {
        browseRoots = games.roots(); browsePath = ""
        openPage(browsePage, ic("it_addgames"), 2, browseBack)
    }

    // ---------------------------------------------------------------- pages

    property var pageStack: []        // [{ rows: fn, parentIcon, catIcon, back: fn }]
    readonly property var topPage: pageStack.length ? pageStack[pageStack.length - 1] : null
    function openPage(rowsFn, parentIcon, catIndex, backFn) {
        var catIcon = cats[catIndex !== undefined ? catIndex : xmb.cat].icon
        pageStack = pageStack.concat([{ rows: rowsFn, parentIcon: parentIcon, catIcon: catIcon, back: backFn || null }])
        page.current = 0
        page.fromHome = pageStack.length === 1
        page.open()
    }
    function popPage() { pageStack = pageStack.slice(0, -1); if (pageStack.length) page.open() }
    function closePages() { pageStack = [] }

    // ---------------------------------------------------------------- the crossbar

    readonly property int friendsCat: 4
    property var cats: {
        games.count; games.scanning; system.onlineName; system.firmwareInstalled; system.firmwareVersion; gamepad.count; prefs.avatar
        online.state; online.session; online.message; online.friends; online.friendsLoading; online.friendsError; cfgTick
        return buildCats()
    }

    function settingsItem(title, desc, icon, rowsFn) {
        return { title: title, desc: desc, icon: ic(icon), preview: rowsFn().map(function(r) { return r.title }),
                 action: function() { openPage(rowsFn, ic(icon), 1) } }
    }

    function buildCats() {
        var gameItems = []
        for (var i = 0; i < games.count; i++) {
            let g = games.get(i)
            gameItems.push({
                title: g.title, desc: g.serial, icon: g.icon || ic("it_games"), wide: !!g.icon,
                action: function() { play(g) },
                options: function() {
                    return [{ title: qsTr("Start"), action: function() { play(g) } },
                            { title: qsTr("Host Online Session"), action: function() { needsName(function() { online.host(g.serial, g.title) }) } },
                            { title: qsTr("Information"), action: function() { dialog.show(qsTr("Information"), gameInfo(g)) } },
                            { title: qsTr("Remove from List"), action: function() {
                                dialog.confirm(qsTr("Remove"), qsTr("Take %1 off the list? The game's files stay where they are.").arg(g.title), function() { games.remove(g.serial) })
                            } }]
                }
            })
        }
        gameItems.push({ title: qsTr("Add Games"), icon: ic("it_addgames"), action: addGames,
                         desc: games.scanning ? qsTr("Looking for games...") : qsTr("Find the games in a folder, and every folder inside it") })
        gameItems.push({ title: qsTr("How to Play"), icon: ic("it_help"), action: guide, desc: qsTr("Controls, quitting a game, and playing online") })

        var active = online.state !== "off"
        var network = [{ title: sessionText(), icon: ic(active ? "it_stream" : "it_host"),
                         desc: online.message || (active ? (online.game || qsTr("Friends join from their Steam friends list")) : qsTr("Host a session, or join a friend's")),
                         action: active ? function() { options.show([{ title: qsTr("Leave the Session"), action: online.stop }]) } : hostPicker }]
        if (active)
            network.push({ title: qsTr("Leave the Session"), icon: ic("it_logout"), desc: qsTr("Stops the session on this PC"), action: online.stop })
        else
            network.push({ title: qsTr("Host a Session"), icon: ic("it_host"), desc: qsTr("Friends join you over Steam"), action: hostPicker },
                         { title: qsTr("Join by Steam Invite"), icon: ic("it_join"), desc: qsTr("Accept an invite in Steam, or join the one session a friend is in"),
                           action: function() { needsName(function() { online.join("") }) } })
        network.push({ title: qsTr("Internet Connection Test"), icon: ic("it_nettest"), desc: qsTr("Check that this system can play online"), action: testConnection },
                     { title: qsTr("User's Guide"), icon: ic("it_help"), desc: qsTr("How to use this system"), action: guide })

        var friends = []
        if (online.friendsError)
            friends.push({ title: qsTr("Steam Isn't Available"), icon: ic("it_nettest"), desc: online.friendsError, action: online.refreshFriends })
        else if (online.friendsLoading && !online.friends.length)
            friends.push({ title: qsTr("Loading..."), icon: ic("cat_friends"), disabled: true })
        else if (!online.friends.length)
            friends.push({ title: qsTr("No Friends Online"), icon: ic("cat_friends"), desc: qsTr("✕ to check again"), action: online.refreshFriends })
        online.friends.forEach(function(f) {
            friends.push({
                title: f.name, icon: ic(f.session ? "cat_game" : "it_person"),
                desc: f.session ? qsTr("In a USCL session: ✕ to join") : f.state,
                disabled: !f.session,
                action: function() { needsName(function() { online.join(f.session) }) }
            })
        })

        return [
            { name: qsTr("Users"), icon: ic("cat_users"), items: [
                { title: system.onlineName || qsTr("User"), icon: avatarUrl(prefs.avatar), desc: qsTr("Your profile: change your name or picture"),
                  preview: [qsTr("Online ID"), qsTr("Picture"), qsTr("Steam Account")], action: function() { openPage(profilePage, avatarUrl(prefs.avatar), 0) } },
                { title: qsTr("Turn Off System"), icon: ic("it_power"), desc: qsTr("Quit USCL and go back to the desktop"),
                  action: function() { dialog.confirm(qsTr("Turn Off System"), qsTr("Quit USCL?"), function() { online.stop(); Qt.quit() }) } } ] },
            { name: qsTr("Settings"), icon: ic("cat_settings"), items: [
                settingsItem(qsTr("System Update"), system.firmwareInstalled ? qsTr("PS3 system software %1 installed").arg(system.firmwareVersion) : qsTr("Install the PS3 system software (games need it)"), "it_update", systemUpdatePage),
                settingsItem(qsTr("Game Settings"), qsTr("How games look and run: resolution, frame rate, upscaling"), "it_games", gameSettingsPage),
                settingsItem(qsTr("Video Settings"), qsTr("Screen shape and stretching"), "it_display", videoSettingsPage),
                settingsItem(qsTr("Sound Settings"), qsTr("Game volume, surround sound and menu sounds"), "it_sound", soundSettingsPage),
                settingsItem(qsTr("Theme Settings"), qsTr("Change the theme and background color"), "it_theme", themeSettingsPage),
                settingsItem(qsTr("Date and Time Settings"), qsTr("How the date and time are shown"), "it_clock", dateTimePage),
                settingsItem(qsTr("Accessory Settings"), qsTr("Controllers and button assignments"), "it_gamepad", accessoryPage),
                settingsItem(qsTr("Network Settings"), qsTr("Online ID and the connection test"), "it_network", networkSettingsPage),
                settingsItem(qsTr("System Settings"), qsTr("Language, system information and setup"), "it_system", systemSettingsPage) ] },
            { name: qsTr("Game"), icon: ic("cat_game"), items: gameItems },
            { name: qsTr("Network"), icon: ic("cat_network"), items: network },
            { name: qsTr("Friends"), icon: ic("cat_friends"), items: friends }
        ]
    }

    // ---------------------------------------------------------------- sounds

    readonly property var sounds: ({ cursor: sCursor, category: sCategory, select: sSelect, back: sBack, error: sError, notify: sNotify, startup: sStartup })
    function sound(name) { if (prefs.sounds && sounds[name]) sounds[name].play() }
    SoundEffect { id: sCursor; source: system.soundUrl("cursor") }
    SoundEffect { id: sCategory; source: system.soundUrl("category") }
    SoundEffect { id: sSelect; source: system.soundUrl("select") }
    SoundEffect { id: sBack; source: system.soundUrl("back") }
    SoundEffect { id: sError; source: system.soundUrl("error") }
    SoundEffect { id: sNotify; source: system.soundUrl("notify") }
    SoundEffect { id: sStartup; source: system.soundUrl("startup") }

    // ---------------------------------------------------------------- input: whatever is on top gets it

    function input(b) {
        if (launcher.running) return
        if (boot.running) { if (b === "accept") boot.skip(); return }
        if (prompt.open) prompt.pad(b)
        else if (dialog.open) dialog.input(b)
        else if (options.open) options.input(b)
        else if (topPage) page.input(b)
        else if (setup.open) setup.input(b)
        else xmb.input(b)
    }

    Connections {
        target: gamepad
        function onPressed(b) { if (root.active) root.input(b) }
        function onHomeHeld() { if (launcher.running) launcher.stop() }
    }
    Connections {
        target: launcher
        function onFinished(crashed, details) {
            // the window manager may have taken us out of full screen when the game's window appeared
            if (!windowed) { root.showFullScreen(); root.requestActivate() }
            games.reload()
            system.refresh()
            cfgTick++
            if (crashed) {
                sound("error")
                dialog.show(qsTr("Error"), qsTr("The game stopped unexpectedly.\n\n") + details.split("\n").slice(-4).join("\n"))
            }
        }
    }
    Connections {
        target: games
        function onScanFinished(added) {
            sound("notify")
            notice.show(added ? qsTr("%n game(s) added", "", added) : qsTr("No new games found there"), ic("it_addgames"))
        }
    }
    Connections {
        target: online
        function onStateChanged() {
            if (online.state === "hosting" || online.state === "joined") { sound("notify"); notice.show(sessionText(), ic("it_stream")) }
            else if (online.state === "off" && online.message) { sound("error"); notice.show(online.message, ic("it_nettest")) }
        }
    }
    Connections {
        target: system
        function onFirmwareChanged() { if (!system.firmwareBusy && system.firmwareStatus) sound(system.firmwareInstalled ? "notify" : "error") }
    }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: function(e) {
            var map = {}
            map[Qt.Key_Up] = "up"; map[Qt.Key_Down] = "down"; map[Qt.Key_Left] = "left"; map[Qt.Key_Right] = "right"
            map[Qt.Key_Return] = "accept"; map[Qt.Key_Enter] = "accept"; map[Qt.Key_Escape] = "back"; map[Qt.Key_Backspace] = "back"
            map[Qt.Key_Space] = "options"; map[Qt.Key_Menu] = "options"; map[Qt.Key_I] = "info"
            map[Qt.Key_PageUp] = "l1"; map[Qt.Key_PageDown] = "r1"
            if (map[e.key]) { root.input(map[e.key]); e.accepted = true }
        }

        // everything is laid out on a 1920x1080 stage (the kodi-xmb numbers), scaled to the window
        Item {
            id: stage
            readonly property real s: root.height / 1080
            width: root.width / s; height: 1080
            scale: s
            transformOrigin: Item.TopLeft

            Background {
                anchors.fill: parent
                tint: root.tint
                shade: root.shade
                running: !launcher.running
            }

            Xmb {
                id: xmb
                anchors.fill: parent
                cats: root.cats
                cat: prefs.xmbCategory
                // the crossbar slides out to the left while an inner screen is open
                opacity: root.topPage || setup.open || boot.running ? 0 : 1
                transform: Translate { x: root.topPage ? -120 : 0; Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutSine } } }
                Behavior on opacity { NumberAnimation { duration: 200 } }
                onCatChanged: {
                    prefs.xmbCategory = cat
                    if (cat === root.friendsCat) online.refreshFriends()
                }
                onSound: function(name) { root.sound(name) }
                onShowOptions: function(e) { options.show(e.options(), 0) }
            }

            Setup {
                id: setup
                visible: opacity > 0 && !root.topPage
                onSound: function(name) { root.sound(name) }
                onFinished: prefs.setupDone = true
                onTypeName: root.typeName(function() { setup.next() })
                onBrowse: root.addGames()
            }

            Page {
                id: page
                anchors.fill: parent
                visible: !!root.topPage
                rows: root.topPage ? root.topPage.rows() : []
                catIcon: root.topPage ? root.topPage.catIcon : ""
                parentIcon: root.topPage ? root.topPage.parentIcon : ""
                onSound: function(name) { root.sound(name) }
                onBack: { if (root.topPage && root.topPage.back) root.topPage.back(); else root.popPage() }
            }

            ClockBox {
                x: stage.width - 40 - width; y: 44
                avatar: root.avatarUrl(prefs.avatar)
                internet: system.internet
                pads: gamepad.count
                battery: system.battery
                charging: system.charging
                clock24: prefs.clock24
                dateFormat: prefs.dateFormat
                visible: !options.open
                opacity: boot.running ? 0 : 1
                Behavior on opacity { NumberAnimation { duration: 400 } }
            }

            Notice { id: notice; x: stage.width - 40 - width; y: 124 }

            OptionsPanel {
                id: options
                onSound: function(name) { root.sound(name) }
            }

            Dialog {
                id: dialog
                onSound: function(name) { root.sound(name) }
            }

            TextPrompt {
                id: prompt
                onSound: function(name) { root.sound(name) }
            }

            // while a game (or the classic window) runs
            Rectangle {
                anchors.fill: parent
                color: "black"
                opacity: launcher.running ? 0.92 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 300 } }
                Column {
                    anchors.centerIn: parent
                    spacing: 18
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: launcher.title; color: "white"; font.pixelSize: 44 }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.xboxPad ? qsTr("Hold the Guide button to quit and come back here") : qsTr("Hold the PS button to quit and come back here")
                        color: "#b0ffffff"; font.pixelSize: 25
                    }
                }
            }

            // startup: the boot screen, then the crossbar
            Boot {
                id: boot
                onChime: root.sound("startup")
                onFinished: if (!prefs.setupDone) setup.start()
            }
        }
    }

    Component.onCompleted: {
        if (prefs.bootScreen) boot.start()
        else { boot.quick(); sound("startup") }
    }
}
