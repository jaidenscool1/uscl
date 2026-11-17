pragma ComponentBehavior: Bound
import QtQuick

// Decode offscreen, then crossfade. Never expose an unloaded image during rapid navigation.
Item {
    id: backdrop
    property url source
    property bool animate: true
    property int front: -1
    function request() {
        if (source.toString() === "") { front = -1; return; }
        var next = front === 0 ? second : first;
        next.source = source;
        if (next.status === Image.Ready) present(next);
    }
    function present(image) {
        if (image.status === Image.Ready && image.source.toString() === source.toString())
            front = image === first ? 0 : 1;
    }
    onSourceChanged: request()
    Image {
        id: first
        objectName: "backdropFirst"
        anchors.fill: parent
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        opacity: backdrop.front === 0 ? 1 : 0
        onStatusChanged: backdrop.present(first)
        Behavior on opacity { NumberAnimation { duration: backdrop.animate ? 220 : 0; easing.type: Easing.OutCubic } }
    }
    Image {
        id: second
        objectName: "backdropSecond"
        anchors.fill: parent
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        opacity: backdrop.front === 1 ? 1 : 0
        onStatusChanged: backdrop.present(second)
        Behavior on opacity { NumberAnimation { duration: backdrop.animate ? 220 : 0; easing.type: Easing.OutCubic } }
    }
}
