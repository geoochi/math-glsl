// ============================================================================
//  wave.glsl  --  the whole drawing, in one fragment shader
//
//  Draws the graph of   y = A * sin(k * x + w * t)   (default y = sin(x + t))
//  on a 2D coordinate system with grid, axes and a tracer dot.
//
//  This file contains NO #version line on purpose: it is the shared body that
//  gets prepended with the right header by each host.
//    * WebGL2  (web/index.html)      -> #version 300 es   + precision + out
//    * Desktop (render_glsl.py)      -> #version 410 core + out
//
//  Required from the host:
//    uniform vec2  uResolution;
//    uniform float uTime;
//    out vec4 fragColor;
//
//  Everything is computed per *pixel*, in parallel, with no loops over pixels
//  and no CPU work per frame -- this is the whole point of GLSL.
// ============================================================================

const float PI = 3.14159265359;

// ---- tunables --------------------------------------------------------------
const float UNITS_X   = 9.0;    // how many maths units the viewport is wide
const float AMPLITUDE = 1.0;    // A : wave amplitude
const float FREQ      = 1.0;    // k : spatial frequency
const float SPEED     = 1.0;    // w : temporal frequency (rad / s)
const float LINE_W    = 1.6;    // curve half-thickness, in pixels
// ---------------------------------------------------------------------------

// ★★★ CHANGE THIS ONE FUNCTION ★★★
// Whatever curveValue() returns is what gets drawn; the axes, grid, anti-aliasing
// and tracer dot all adapt automatically. Swap the return expression for e.g.
//
//   wave packet    : AMPLITUDE * exp(-0.15 * x * x) * sin(FREQ * x + SPEED * t)
//   damped         : AMPLITUDE * exp(-0.08 * abs(x)) * sin(FREQ * x + SPEED * t)
//   square (Fourier, first 4 odd harmonics):
//                    float s = 0.0;
//                    for (int n = 1; n <= 7; n += 2) s += sin(float(n) * FREQ * x + SPEED * t) / float(n);
//                    return AMPLITUDE * s * 4.0 / PI;
//   beats          : AMPLITUDE * sin(FREQ * x + SPEED * t) * cos(0.15 * FREQ * x)
//
// If you change it, update the matching derivative `c` in main() (it is the
// analytic f'(x) used to keep the stroke width constant on steep slopes), or set
// c = 0.0 to trade a little width accuracy for forgetfulness.
float curveValue(float x, float t) {
    return AMPLITUDE * sin(FREQ * x + SPEED * t);
}

// Colour by height: teal in the troughs, violet on the crests.
vec3 waveColor(float y) {
    float h = clamp(y / max(AMPLITUDE, 1e-4) * 0.5 + 0.5, 0.0, 1.0);
    return mix(vec3(0.24, 0.80, 0.72), vec3(0.72, 0.55, 1.00), h);
}

// 1-pixel-wide coordinate grid. fwidth() turns "distance in maths units" into
// "distance in pixels", so the grid stays crisp at any zoom or resolution.
float gridMask(vec2 p) {
    vec2 g = abs(fract(p - 0.5) - 0.5) / fwidth(p);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

float axisMask(float dist, float pixel) {
    return 1.0 - smoothstep(0.9 * pixel, 2.2 * pixel, abs(dist));
}

void main() {
    vec2  res   = uResolution;
    float pixel = UNITS_X / res.x;                         // maths units per pixel
    vec2  p     = (gl_FragCoord.xy - 0.5 * res) * pixel;   // maths coords, y up
    float t     = uTime;

    vec3 col = vec3(0.043, 0.055, 0.078);                  // deep navy background

    // ---- grid --------------------------------------------------------------
    float fade = 1.0 - smoothstep(3.0, 9.0, length(p * vec2(0.55, 1.0)));
    col = mix(col, vec3(0.16, 0.19, 0.26), gridMask(p) * 0.8 * fade);

    // ---- axes through the origin -------------------------------------------
    col = mix(col, vec3(0.44, 0.49, 0.60),
              max(axisMask(p.y, pixel), axisMask(p.x, pixel)) * 0.9);

    // ---- the curve ---------------------------------------------------------
    // g is the implicit form of the graph: zero exactly on the curve.
    // Dividing by |grad g| converts vertical distance -> perpendicular distance,
    // so the stroke keeps a constant width even where the curve is steep.
    float s  = curveValue(p.x, t);
    float c  = FREQ * AMPLITUDE * cos(FREQ * p.x + SPEED * t);
    float g  = (p.y - s) / sqrt(1.0 + c * c);
    float gw = fwidth(g);                                  // anti-alias width
    float hw = LINE_W * pixel;

    float glow = 1.0 - smoothstep(hw * 3.0, hw * 6.5 + gw, abs(g));
    float core = 1.0 - smoothstep(hw - gw, hw + gw, abs(g));

    vec3  cw = waveColor(s);
    col += cw * glow * 0.15;                               // soft halo
    col  = mix(col, mix(cw, vec3(1.0), 0.3 * core), core); // bright core

    // ---- tracer dot, riding the wave at x = 0 -------------------------------
    vec2  dpos = vec2(0.0, curveValue(0.0, t));
    float dr   = length(p - dpos) / pixel;                 // distance in pixels
    float disc = 1.0 - smoothstep(4.0, 6.0, dr);
    float ph   = fract(t * 0.8);
    float ring = (1.0 - smoothstep(0.0, 2.5, abs(dr - (8.0 + 46.0 * ph)))) * (1.0 - ph);
    col  = mix(col, vec3(1.0), disc);
    col += vec3(0.55, 0.85, 1.00) * ring * 0.30;

    // ---- gentle vignette ---------------------------------------------------
    vec2 uv = gl_FragCoord.xy / res;
    col *= 1.0 - 0.22 * dot(uv - 0.5, uv - 0.5);

    fragColor = vec4(col, 1.0);
}
