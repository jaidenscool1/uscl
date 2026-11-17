import QtQuick

// Original vector outline based on the multicolor focus treatment in Nintendo's HOME walkthrough.
// No perpetual repaint: the canvas updates only when its geometry or visibility changes.
Canvas {
    id: ring

    property real radius: 24

    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onRadiusChanged: requestPaint()
    onVisibleChanged: requestPaint()
    onPaint: {
        var c = getContext("2d"), inset = 4;
        var w = width - inset * 2, h = height - inset * 2;
        var r = Math.max(0, Math.min(radius - inset, w / 2, h / 2));
        c.reset();
        c.translate(inset, inset);
        c.beginPath();
        c.moveTo(r, 0);
        c.lineTo(w - r, 0);
        c.arcTo(w, 0, w, r, r);
        c.lineTo(w, h - r);
        c.arcTo(w, h, w - r, h, r);
        c.lineTo(r, h);
        c.arcTo(0, h, 0, h - r, r);
        c.lineTo(0, r);
        c.arcTo(0, 0, r, 0, r);
        c.closePath();
        c.lineWidth = 8;
        c.strokeStyle = "#ffffff";
        c.stroke();
        var gradient = c.createLinearGradient(0, h, w, 0);
        gradient.addColorStop(0, "#7fe9ed");
        gradient.addColorStop(0.35, "#376ff0");
        gradient.addColorStop(0.7, "#b7a0ff");
        gradient.addColorStop(1, "#ffd5ee");
        c.lineWidth = 4;
        c.strokeStyle = gradient;
        c.stroke();
    }
}
