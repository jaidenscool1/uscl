pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: screen
    required property var controller
    required property string theme
    property string heading: ""
    ThemePalette {
        id: style
        theme: screen.theme
    }
    readonly property bool bladeList: style.blades && !controller.modernSoftware
    readonly property bool software: controller.modernSoftware
    readonly property bool settingsSidebar: controller.presentation === "settings" && (style.nintendo
                                                                                       || style.ps5)
    readonly property real contentLeft: software ? 380 : bladeList ? 270 : settingsSidebar ? 110 : style.ps4 ? 286 :
                                                                                             style.nintendo
                                                                                             ? 600 : 330
    readonly property real listTop: style.ps4 || style.blades ? 162 : style.nintendo ? 190 : 240
    Rectangle {
        anchors.fill: parent
        color: style.blades ? "#a18acb" : style.background
    }
    Text {
        x: style.blades ? 270 : style.ps4 ? 107 : 96
        y: style.ps4 ? 48 : 65
        width: parent.width - 192
        height: 80
        text: screen.heading || qsTr("Settings")
        color: style.text
        font.pixelSize: style.ps4 ? 56 : 48
        font.weight: Font.Light
        elide: Text.ElideRight
    }
    Rectangle {
        x: style.ps4 ? 0 : 96
        y: style.ps4 ? 134 : 165
        width: parent.width - (style.ps4 ? 0 : 192)
        height: 2
        color: style.divider
    }
    // Library filters are a separate navigation region; no game is hidden behind a utility tile.
    ListView {
        id: librarySidebar
        objectName: "librarySidebar"
        visible: screen.software
        x: 96
        y: 230
        width: 245
        height: 690
        clip: true
        spacing: 8
        model: screen.controller.filters.map(function (title, index) {
            return {
                title: title,
                filter: index
            };
        }).concat(screen.controller.libraryTools)
        currentIndex: screen.controller.libraryNavIndex
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        delegate: Rectangle {
            required property var modelData
            required property int index
            width: 245
            height: 72
            readonly property bool selected: screen.controller.filterFocus && index
                                             === screen.controller.libraryNavIndex
            color: selected ? style.focusFill : "transparent"
            border.width: selected ? 3 : 0
            border.color: style.accent
            Rectangle {
                x: 0
                y: 18
                width: 4
                height: 36
                visible: parent.modelData.filter === screen.controller.filterIndex
                color: style.accent
            }
            Text {
                x: 15
                width: parent.width - 30
                height: parent.height
                text: parent.modelData.title
                color: parent.selected ? style.focusText : style.text
                font.pixelSize: 25
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (parent.modelData.filter !== undefined) {
                        screen.controller.selectFilter(parent.modelData.filter);
                        screen.controller.filterFocus = true;
                    } else
                        screen.controller.libraryAction(parent.modelData.key);
                }
            }
        }
    }
    GridView {
        id: library
        objectName: "libraryGrid"
        visible: screen.software
        x: screen.contentLeft
        y: 230
        width: screen.width - x - 96
        height: 690
        cellWidth: width / screen.controller.gridColumns
        cellHeight: style.blades ? 92 : style.nintendo ? 300 : 330
        clip: true
        model: screen.software ? screen.controller.displayRows : []
        currentIndex: screen.controller.current
        highlightMoveDuration: 140
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, GridView.Contain)
        delegate: Rectangle {
            id: game
            required property var modelData
            required property int index
            objectName: "libraryGame" + index
            width: library.cellWidth - 18
            height: library.cellHeight - (style.blades ? 4 : 18)
            radius: style.radius
            readonly property bool selected: index === screen.controller.current
            color: style.panel
            border.width: selected && !screen.controller.filterFocus ? 4 : 0
            border.color: style.accent
            opacity: modelData.disabled ? .45 : 1
            Rectangle {
                x: 8
                y: 8
                width: style.blades ? 66 : parent.width - 16
                height: style.blades ? 66 : parent.height - 80
                color: "#444444"
            }
            Image {
                x: 8
                y: 8
                width: style.blades ? 66 : parent.width - 16
                height: style.blades ? 66 : parent.height - 80
                source: game.modelData.icon || ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                mipmap: true
            }
            RoundedCoverMask {
                anchors.fill: parent
                visible: style.lightSwitch
                z: 1
                radius: game.radius
                outsideColor: style.background
            }
            ConsoleFocusRing {
                anchors.fill: parent; radius: game.radius; z: 2
                visible: style.lightSwitch && game.selected && !screen.controller.filterFocus
            }
            Text {
                x: style.blades ? 100 : 12
                y: style.blades ? 8 : parent.height - 67
                width: parent.width-x-24
                height: style.blades ? 70 : 60
                text: game.modelData.title
                color: style.text
                font.pixelSize: 23
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function (mouse) {
                    var was = game.selected;
                    screen.controller.filterFocus = false;
                    screen.controller.current = game.index;
                    if (mouse.button === Qt.RightButton)
                        screen.controller.input("options");
                    else if (was)
                        screen.controller.input("accept");
                    else
                        screen.controller.sound("cursor");
                }
            }
        }
        Text {
            visible: library.count === 0
            anchors.centerIn: parent
            width: parent.width - 80
            text: qsTr("No games here yet.\nChoose Add Games to find your games.")
            color: style.muted
            font.pixelSize: 32
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
    }
    ListView {
        id: systemSections
        objectName: "systemSections"
        visible: style.nintendo && screen.controller.settingsSection >= 0 && !screen.software
        x: 100
        y: screen.listTop
        width: 440
        height: 920 - y
        clip: true
        model: visible ? screen.controller.settingsSections : []
        currentIndex: screen.controller.sectionFocus ? screen.controller.sectionCursor :
                                                       screen.controller.settingsSection
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        delegate: Rectangle {
            required property var modelData
            required property int index
            width: 440
            height: 82
            color: index === systemSections.currentIndex ? style.panel : "transparent"
            border.width: index === systemSections.currentIndex && screen.controller.sectionFocus ? 4 : 0
            border.color: style.accent
            Text {
                x: 24
                width: parent.width - 48
                height: parent.height
                text: parent.modelData.title
                color: style.text
                font.pixelSize: 27
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            MouseArea {
                anchors.fill: parent
                onClicked: screen.controller.settingsSectionChosen(parent.index)
            }
        }
    }
    ListView {
        id: list
        objectName: "consoleSettingsList"
        visible: !screen.software
        x: screen.contentLeft
        y: screen.listTop
        width: screen.bladeList ? 750 : screen.settingsSidebar ? (style.nintendo ? 440 : 600) : screen.width - x - (style.ps4 ? 278 :
                                                                                                       96)
        height: 920 - y
        clip: true
        model: screen.software ? [] : screen.controller.displayRows
        currentIndex: screen.controller.current
        highlightMoveDuration: 140
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        delegate: Rectangle {
            id: entry
            required property var modelData
            required property int index
            objectName: "settingsRow" + index
            readonly property bool selected: index === screen.controller.current
            width: list.width
            height: style.blades ? 76 : style.ps4 ? (selected ? 124 : 84) : screen.settingsSidebar ? 82 : 100
            color: selected ? (style.blades ? "#50ffffff" : style.ps4 ? "transparent" : style.focusFill) : "transparent"
            border.width: selected && !screen.controller.sectionFocus ? (style.nintendo ? 4 : 2) : 0
            border.color: style.blades ? "#675385" : style.ps4 ? "#cdd8ed" : style.accent
            opacity: modelData.disabled ? .45 : 1
            Image {
                visible: !screen.settingsSidebar
                x: 22
                y: 18
                width: 48
                height: 48
                source: entry.modelData.icon || ""
                fillMode: Image.PreserveAspectFit
                mipmap: true
            }
            Text {
                x: screen.settingsSidebar ? 24 : entry.modelData.icon ? 99 : 26
                y: 0
                width: parent.width - x - 20 - (entry.modelData.value ? 360 : 0)
                height: style.ps4 ? 84 : parent.height
                text: entry.modelData.title
                color: selected && !style.ps4 && !style.blades ? style.focusText : style.text
                font.pixelSize: style.ps4 ? 34 : 29
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 26
                width: 340
                height: style.ps4 ? 84 : parent.height
                text: entry.modelData.value === undefined ? "" : String(entry.modelData.value)
                color: entry.selected && !style.ps4 && !style.blades ? style.focusText : style.muted
                font.pixelSize: 25
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
            }
            Text {
                visible: style.ps4 && entry.selected
                x: 99
                y: 78
                width: parent.width - 120
                height: 40
                text: entry.modelData.help || entry.modelData.desc || ""
                color: style.muted
                font.pixelSize: 23
                elide: Text.ElideRight
            }
            Rectangle {
                x: 88
                y: parent.height - 1
                width: parent.width - 88
                height: 1
                color: style.divider
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function (mouse) {
                    var was = entry.selected;
                    screen.controller.current = entry.index;
                    if (mouse.button === Qt.RightButton)
                        screen.controller.input("options");
                    else if (was)
                        screen.controller.input("accept");
                    else
                        screen.controller.sound("cursor");
                }
            }
        }
    }
    Item {
        visible: screen.settingsSidebar || screen.bladeList
        x: screen.bladeList ? 1100 : style.nintendo ? 625 : 850
        y: 250
        width: screen.width - x - 140
        height: 670
        Text {
            width: parent.width
            height: 120
            text: screen.controller.cur ? screen.controller.cur.title : ""
            color: style.text
            font.pixelSize: 38
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            y: 130
            width: parent.width
            height: 100
            text: screen.controller.cur ? screen.controller.cur.desc || "" : ""
            color: style.muted
            font.pixelSize: 27
            wrapMode: Text.Wrap
        }
        Column {
            y: 260
            width: parent.width
            spacing: 12
            Repeater {
                model: screen.controller.cur ? (screen.controller.cur.preview || []).slice(0, 6) : []
                delegate: Text {
                    required property string modelData
                    width: parent.width
                    height: 42
                    text: modelData
                    color: style.text
                    font.pixelSize: 27
                    elide: Text.ElideRight
                }
            }
        }
        Text {
            y: 615
            text: qsTr("Enter to configure")
            color: style.muted
            font.pixelSize: 25
        }
    }
    Text {
        x: screen.software ? 380 : screen.contentLeft
        y: 932
        width: parent.width - x - 96
        height: 48
        text: screen.controller.cur ? screen.controller.cur.help || screen.controller.cur.desc || "" : ""
        color: style.muted
        font.pixelSize: 23
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }
    Rectangle {
        x: 96
        y: 990
        width: parent.width - 192
        height: 1
        color: style.divider
    }
    Text {
        x: 96
        y: 1010
        width: parent.width - 192
        text: qsTr(
                  "%1 / Enter: Select    %2 / Esc: Back    %3 / Space: Options    Page Up / Down: Scroll").arg(
                  style.confirmGlyph).arg(style.backGlyph).arg(style.optionsGlyph)
        color: style.muted
        font.pixelSize: 24
        elide: Text.ElideRight
    }
}
