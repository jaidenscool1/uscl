import QtQuick

// Rectangle radius does not clip child Images. Mask only the outer corners,
// preserving opaque cover art on both the GPU and the software renderer.
Canvas {
    id: mask
    property real radius: 24
    property color outsideColor: "transparent"
    onOutsideColorChanged: requestPaint()
    onRadiusChanged: requestPaint()
    onVisibleChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        var c = getContext("2d"), r = Math.min(radius,width/2,height/2)
        c.reset(); c.fillStyle = outsideColor
        for (var corner = 0; corner < 4; corner++) {
            c.save()
            c.translate(corner === 1 || corner === 2 ? width : 0, corner >= 2 ? height : 0)
            c.rotate(corner*Math.PI/2)
            c.beginPath(); c.moveTo(0,0); c.lineTo(r,0)
            c.arc(r,r,r,-Math.PI/2,-Math.PI,true)
            c.closePath(); c.fill(); c.restore()
        }
    }
}
