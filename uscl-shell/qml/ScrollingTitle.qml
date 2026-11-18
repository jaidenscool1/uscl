import QtQuick

// Nintendo-style delayed title travel. Short, hidden and motion-disabled labels stay still.
Item {
    id: title
    property alias text: label.text
    property alias color: label.color
    property int pixelSize: 34
    property bool active: true
    property real offset: 0
    readonly property real overflow: Math.max(0, label.implicitWidth - width)
    readonly property bool scrolling: travel.running
    clip: true
    function resetTravel() {
        travel.stop(); offset = 0;
        if (active && visible && overflow > 1) travel.start();
    }
    onTextChanged: resetTravel()
    onOverflowChanged: resetTravel()
    onActiveChanged: resetTravel()
    onVisibleChanged: resetTravel()
    Component.onCompleted: resetTravel()
    Text {
        id: label
        objectName: "scrollingTitleText"
        x: title.offset
        height: title.height
        font.pixelSize: title.pixelSize
        font.weight: Font.Light
        verticalAlignment: Text.AlignVCenter
    }
    SequentialAnimation {
        id: travel
        loops: Animation.Infinite
        PauseAnimation { duration: 1200 }
        NumberAnimation { target: title; property: "offset"; from: 0; to: -title.overflow; duration: Math.max(600,title.overflow / 60 * 1000) }
        PauseAnimation { duration: 1000 }
        PropertyAction { target: title; property: "offset"; value: 0 }
    }
}
