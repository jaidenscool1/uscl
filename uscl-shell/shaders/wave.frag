#version 440
// The XMB background: a tinted glow with slow silk ribbons drawn across it
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float aspect;
    vec4 tint;
};

float curve(float x, float phase, float freq, float amp, float base)
{
    return base + amp * sin(x * freq + phase) + amp * 0.45 * sin(x * freq * 2.3 - phase * 1.3);
}

float line(float y, float at, float width)
{
    float d = (y - at) / width;
    return exp(-d * d);
}

void main()
{
    vec2 uv = qt_TexCoord0;
    float t = time;

    // glow up and to the left, darker towards the bottom
    float glow = 1.0 - smoothstep(0.0, 1.25, distance(vec2(uv.x * aspect, uv.y), vec2(0.36 * aspect, 0.22)));
    vec3 col = tint.rgb * (0.42 + 0.70 * glow);
    col *= mix(1.05, 0.55, uv.y);

    // two bright edges with a faint sheet of silk between them, plus two soft wide bands
    float a = curve(uv.x, t * 0.33, 5.6, 0.055, 0.57);
    float b = curve(uv.x, t * 0.27 + 1.9, 4.9, 0.065, 0.59);
    float sheet = smoothstep(0.0, 0.004, uv.y - min(a, b)) * smoothstep(0.0, 0.004, max(a, b) - uv.y);
    float w = 0.10 * sheet;
    w += 0.55 * line(uv.y, a, 0.0030) + 0.40 * line(uv.y, b, 0.0024);
    w += 0.16 * line(uv.y, curve(uv.x, t * 0.21 + 0.7, 4.1, 0.05, 0.61), 0.045);
    w += 0.10 * line(uv.y, curve(uv.x, t * 0.29 + 2.6, 3.6, 0.08, 0.64), 0.080);
    col += w * mix(vec3(1.0), normalize(tint.rgb + 0.2) * 1.4, 0.25);

    // a little noise so the smooth gradient doesn't show 8-bit bands
    col += (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;

    fragColor = vec4(col, 1.0) * qt_Opacity;
}
