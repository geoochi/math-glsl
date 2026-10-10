// ============================================================================
//  wave.glsl -- the video this demo is about.
//  Produced from the prompt in demo/wave/prompt.md, rendered to demo/wave/wave.mp4.
//
//  Built on shaders/axes.glsl: same empty coordinate system, plus the wave.
//  Only the block between the markers is "the user's logic" -- everything else
//  is the reusable base.
//
//  Render it with:
//      SHADER=demo/wave/wave.glsl ./render_glsl demo/wave/wave.mp4 3840 2160 60 5
// ============================================================================

#include "../../shaders/lib.glsl"

const float UNITS_X   = 9.0;    // viewport width in maths units (equal aspect)
const float AMPLITUDE = 1.0;    // A
const float FREQ      = 1.0;    // k, spatial frequency
const float SPEED     = 1.0;    // w, temporal frequency (rad/s)
const float LINE_W    = 1.6;    // stroke half-width, in pixels

// ★ CHANGE THIS ONE LINE to draw something else -- the axes adapt automatically.
//   wave packet : AMPLITUDE * exp(-0.15*x*x) * sin(FREQ*x + SPEED*t)
//   damped      : AMPLITUDE * exp(-0.08*abs(x)) * sin(FREQ*x + SPEED*t)
//   beats       : AMPLITUDE * sin(FREQ*x + SPEED*t) * cos(0.15*FREQ*x)
float curveValue(float x, float t) {
    return AMPLITUDE * sin(FREQ * x + SPEED * t);
}

void main() {
    vec2  res   = uResolution;
    float pixel = UNITS_X / res.x;
    vec2  p     = toMath(gl_FragCoord.xy, res, UNITS_X);
    float t     = uTime;

    vec3 col = coordinateSystem(p, pixel);

    // ---------------- the user's logic --------------------------------------
    // Implicit form of y = f(x). |g| is the perpendicular distance to the curve
    // once divided by |grad g|, which is what keeps the stroke width constant
    // even where the slope is steep.
    float s  = curveValue(p.x, t);
    float fp = FREQ * AMPLITUDE * cos(FREQ * p.x + SPEED * t);   // analytic f'(x)
    float g  = (p.y - s) / sqrt(1.0 + fp * fp);
    float gw = fwidth(g);
    float hw = LINE_W * pixel;

    float glow = 1.0 - smoothstep(hw * 3.0, hw * 6.5 + gw, abs(g));
    float core = strokeMask(g, hw, gw);

    vec3 cw = heightRamp(s, AMPLITUDE);
    col += cw * glow * 0.15;                                     // halo
    col  = mix(col, mix(cw, vec3(1.0), 0.3 * core), core);       // bright core

    // tracer dot riding the wave at x = 0, with an expanding ring
    vec2  dpos = vec2(0.0, curveValue(0.0, t));
    float dr   = length(p - dpos) / pixel;
    float disc = 1.0 - smoothstep(4.0, 6.0, dr);
    float ph   = fract(t * 0.8);
    float ring = (1.0 - smoothstep(0.0, 2.5, abs(dr - (8.0 + 46.0 * ph)))) * (1.0 - ph);
    col  = mix(col, vec3(1.0), disc);
    col += vec3(0.55, 0.85, 1.00) * ring * 0.30;
    // ------------------------------------------------------------------------

    fragColor = vec4(vignette(col, gl_FragCoord.xy, res), 1.0);
}
