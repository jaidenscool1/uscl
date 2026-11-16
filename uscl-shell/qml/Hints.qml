import QtQuick

// "✕ Enter   ○ Back" with the button glyphs, as under PS3 dialogs
Row {
    id: hints
    property string theme: "ps3"
    ThemePalette { id: themeStyle; theme: hints.theme }
    property string accept: qsTr("Enter")
    property string back: qsTr("Back")
    height: 44; spacing: 12
    Image { visible: hints.theme === "ps3"; y: 8; width: 28; height: 28; source: "qrc:/qt/qml/Uscl/xmb/ui/btn_cross.png" }
    Text { visible: hints.theme !== "ps3"; height: 44; verticalAlignment: Text.AlignVCenter; text: themeStyle.confirmGlyph; color: themeStyle.xbox ? "#49d849" : "white"; font.pixelSize: 28 }
    Text { height: 44; verticalAlignment: Text.AlignVCenter; text: parent.accept; color: "#d0ffffff"; font.pixelSize: 25 }
    Item { width: 148; height: 1 }
    Image { visible: hints.theme === "ps3"; y: 8; width: 28; height: 28; source: "qrc:/qt/qml/Uscl/xmb/ui/btn_circle.png" }
    Text { visible: hints.theme !== "ps3"; height: 44; verticalAlignment: Text.AlignVCenter; text: themeStyle.backGlyph; color: themeStyle.xbox ? "#f66b61" : "white"; font.pixelSize: 28 }
    Text { height: 44; verticalAlignment: Text.AlignVCenter; text: parent.back; color: "#d0ffffff"; font.pixelSize: 25 }
}
