import QtQuick

QtObject {
    property string theme: "ps3"
    readonly property bool ps3: theme === "ps3"
    readonly property bool ps4: theme === "ps4"
    readonly property bool ps5: theme === "ps5"
    readonly property bool blades: theme === "x360classic"
    readonly property bool metro: theme === "x360"
    readonly property bool one: theme === "xboxone"
    readonly property bool xbox: blades || metro || one
    readonly property bool lightSwitch: theme === "switch2"
    readonly property bool nintendo: theme === "switch" || theme === "switch2"
    readonly property color background: blades ? "#dce1d7" : metro ? "#dedede" : one ? "#0c0c0c" : lightSwitch ? "#ebebeb" : nintendo ? "#2d2d2d" : ps4 ? "#073ba0" : "#071222"
    readonly property color panel: blades ? "#ecede8" : metro ? "#eeeeee" : one ? "#202020" : lightSwitch ? "#ffffff" : nintendo ? "#363636" : ps4 ? "#114ca9" : "#172336"
    readonly property color text: blades || metro || lightSwitch ? "#2a2d28" : "#ffffff"
    readonly property color muted: blades || metro || lightSwitch ? "#5b6059" : "#d0d3d8"
    readonly property color danger: blades || metro || lightSwitch ? "#b02020" : "#ffb0b0"
    readonly property color divider: blades || metro || lightSwitch ? "#44707070" : "#40ffffff"
    readonly property color accent: xbox ? "#107c10" : nintendo ? "#39e3d2" : ps5 ? "#ffffff" : "#79c9ff"
    readonly property color focusFill: xbox ? "#107c10" : lightSwitch ? "#ffffff" : nintendo ? "#464646" : ps5 ? "#25303e" : "#ffffff"
    readonly property color focusText: xbox ? "white" : lightSwitch ? "#272727" : nintendo || ps5 ? "white" : "#15213a"
    readonly property int radius: lightSwitch ? 24 : ps5 ? 16 : nintendo ? 3 : 0
    readonly property string confirmGlyph: xbox ? "A" : nintendo ? "A" : "✕"
    readonly property string backGlyph: xbox || nintendo ? "B" : "○"
    readonly property string optionsGlyph: xbox ? "☰" : nintendo ? "+" : "△"
}
