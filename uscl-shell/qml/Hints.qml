import QtQuick

// "✕ Enter   ○ Back" with the button glyphs, as under PS3 dialogs
Row {
    property string accept: qsTr("Enter")
    property string back: qsTr("Back")
    height: 44; spacing: 12
    Image { y: 8; width: 28; height: 28; source: "qrc:/qt/qml/Uscl/xmb/ui/btn_cross.png" }
    Text { height: 44; verticalAlignment: Text.AlignVCenter; text: parent.accept; color: "#d0ffffff"; font.pixelSize: 25 }
    Item { width: 148; height: 1 }
    Image { y: 8; width: 28; height: 28; source: "qrc:/qt/qml/Uscl/xmb/ui/btn_circle.png" }
    Text { height: 44; verticalAlignment: Text.AlignVCenter; text: parent.back; color: "#d0ffffff"; font.pixelSize: 25 }
}
