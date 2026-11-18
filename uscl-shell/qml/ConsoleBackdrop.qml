pragma ComponentBehavior: Bound
import QtQuick

// Decode offscreen, then crossfade. Never expose an unloaded image during rapid navigation.
Item {
    id: backdrop
    property url source
    property bool animate: true
    property int front: -1
    function request() {
        if (source.toString() === "") { firstFade.stop(); secondFade.stop(); first.opacity = 0; second.opacity = 0; front = -1; return; }
        // Reusing a buffer while it is fading out must not expose newly decoded art
        // at the old image's partial opacity. Settle that blend before replacing it.
        firstFade.stop(); secondFade.stop();
        first.opacity = front === 0 ? 1 : 0;
        second.opacity = front === 1 ? 1 : 0;
        var next = front === 0 ? second : first;
        next.source = source;
        if (next.status === Image.Ready) present(next);
    }
    function present(image) {
        if (image.status === Image.Ready && image.source.toString() === source.toString())
        {
            front = image === first ? 0 : 1;
            firstFade.stop(); secondFade.stop();
            firstFade.to = front === 0 ? 1 : 0;
            secondFade.to = front === 1 ? 1 : 0;
            firstFade.start(); secondFade.start();
        }
    }
    NumberAnimation { id: firstFade; target: first; property: "opacity"; duration: backdrop.animate ? 220 : 0; easing.type: Easing.OutCubic }
    NumberAnimation { id: secondFade; target: second; property: "opacity"; duration: backdrop.animate ? 220 : 0; easing.type: Easing.OutCubic }
    onSourceChanged: request()
    Image {
        id: first
        objectName: "backdropFirst"
        anchors.fill: parent
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        opacity: 0
        onStatusChanged: backdrop.present(first)

    }
    Image {
        id: second
        objectName: "backdropSecond"
        anchors.fill: parent
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        opacity: 0
        onStatusChanged: backdrop.present(second)

    }
}
