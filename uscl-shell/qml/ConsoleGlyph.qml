import QtQuick

// Original vector glyphs. They keep system controls legible on both Nintendo palettes
// without borrowing console artwork or tinting the PS3's baked-in glow textures.
Canvas {
    id: glyph
    property string kind: "library"
    property color ink: "white"
    onKindChanged: requestPaint()
    onInkChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        var c = getContext("2d");
        c.reset();
        c.scale(width / 64, height / 64);
        c.strokeStyle = ink;
        c.fillStyle = ink;
        c.lineWidth = 4;
        c.lineCap = "round";
        c.lineJoin = "round";
        function line(x1, y1, x2, y2) {
            c.beginPath();
            c.moveTo(x1, y1);
            c.lineTo(x2, y2);
            c.stroke();
        }
        function circle(x, y, r, fill) {
            c.beginPath();
            c.arc(x, y, r, 0, Math.PI * 2);
            if (fill)
                c.fill();
            else
                c.stroke();
        }
        if (kind === "wifi") {
            for (var w = 0; w < 3; w++) {
                c.beginPath(); c.arc(32,54,14+w*13,Math.PI*1.2,Math.PI*1.8); c.stroke();
            }
            circle(32,53,3,true);
        } else if (kind === "power") {
            c.beginPath();
            c.arc(32, 34, 21, -Math.PI * .32, Math.PI * 1.32);
            c.stroke();
            line(32, 5, 32, 31);
        } else if (kind === "settings" || kind === "controllers") {
            if (kind === "settings") {
                c.beginPath();
                for (var i = 0; i < 48; i++) {
                    var a = i * Math.PI / 24, r = i % 6 < 3 ? 25 : 20;
                    var x = 32 + Math.cos(a) * r, y = 32 + Math.sin(a) * r;
                    if (i === 0)
                        c.moveTo(x, y);
                    else
                        c.lineTo(x, y);
                }
                c.closePath();
                c.stroke();
                circle(32, 32, 9, false);
            } else {
                c.beginPath();
                c.moveTo(17, 18);
                c.lineTo(47, 18);
                c.quadraticCurveTo(55, 18, 58, 46);
                c.quadraticCurveTo(58, 57, 47, 49);
                c.lineTo(40, 41);
                c.lineTo(24, 41);
                c.lineTo(17, 49);
                c.quadraticCurveTo(6, 57, 6, 46);
                c.quadraticCurveTo(9, 18, 17, 18);
                c.stroke();
                line(16, 30, 28, 30);
                line(22, 24, 22, 36);
                circle(44, 28, 2, true);
                circle(50, 34, 2, true);
            }
        } else if (kind === "add" || kind === "updates") {
            c.strokeRect(12, 8, 40, 48);
            if (kind === "add") {
                line(22, 32, 42, 32);
                line(32, 22, 32, 42);
            } else {
                line(32, 17, 32, 43);
                line(23, 35, 32, 44);
                line(41, 35, 32, 44);
            }
        } else if (kind === "profile" || kind === "friends") {
            circle(kind === "friends" ? 23 : 32, 20, 10, false);
            c.beginPath();
            c.arc(kind === "friends" ? 23 : 32, 53, 18, Math.PI, Math.PI * 2);
            c.stroke();
            if (kind === "friends") {
                circle(46, 23, 7, false);
                c.beginPath();
                c.arc(47, 52, 12, -Math.PI / 2, 0);
                c.stroke();
            }
        } else if (kind === "party" || kind === "sound") {
            c.beginPath();
            c.arc(32, 30, 21, Math.PI, Math.PI * 2);
            c.stroke();
            c.strokeRect(9, 29, 8, 18);
            c.strokeRect(47, 29, 8, 18);
            c.beginPath();
            c.moveTo(51, 47);
            c.quadraticCurveTo(51, 57, 34, 55);
            c.stroke();
        } else if (kind === "online") {
            circle(32, 32, 25, false);
            c.beginPath();
            c.ellipse(21, 7, 22, 50);
            c.stroke();
            line(7, 32, 57, 32);
        } else if (kind === "changelog" || kind === "help") {
            c.strokeRect(12, 8, 40, 48);
            line(22, 21, 42, 21);
            line(22, 32, 42, 32);
            line(22, 43, 35, 43);
        } else {
            for (var j = 0; j < 4; j++)
                c.strokeRect(9 + (j % 2) * 27, 9 + Math.floor(j / 2) * 27, 19, 19);
        }
    }
}
