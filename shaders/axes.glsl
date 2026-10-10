// ============================================================================
//  axes.glsl -- THE DEFAULT SHADER
//
//  Draws an empty coordinate system and nothing else: grid + axes + vignette.
//  Put your own maths in the marked block below and it becomes your video.
//
//  There is no #version line here: the renderer prepends
//      #version 410 core
//      uniform vec2  uResolution;   // framebuffer size, pixels
//      uniform float uTime;         // seconds -- frame i is exactly i/fps
//      out vec4 fragColor;
//
//  Run:  ./render_glsl                                  4K 60fps 10-bit, 2 s
//        ./render_glsl out/mine.mp4 1920 1080 60 5       your own output spec
//        SHADER=shaders/axes.glsl ./render_glsl x.mp4    pick a shader
// ============================================================================

#include "lib.glsl"

const float UNITS_X = 9.0;   // how many maths units the viewport is wide.
                             // y is derived from it, so the aspect stays equal.

// Uncomment the next line to see the worked example that demo/wave/ grew from:
// #define DRAW_EXAMPLE

void main() {
    vec2  res   = uResolution;
    float pixel = UNITS_X / res.x;                       // maths units per pixel
    vec2  p     = toMath(gl_FragCoord.xy, res, UNITS_X); // maths coords, y up

    vec3 col = coordinateSystem(p, pixel);               // the empty axes

    // ======================= YOUR DRAWING GOES HERE =========================
    // `p`     this pixel in maths coordinates (origin centred, y up, equal aspect)
    // `pixel` one pixel measured in maths units -- multiply by it to keep a
    //         stroke exactly N pixels wide at any resolution
    // `uTime` seconds; the clock is virtual (frame i is rendered at i/fps), so
    //         the output is frame-exact and never drops frames
    //
    // A function graph y = f(x) as an anti-aliased stroke:
    //     float s  = sin(p.x + uTime);                  // f(x)
    //     float fp = cos(p.x + uTime);                  // f'(x), analytic
    //     float g  = (p.y - s) / sqrt(1.0 + fp * fp);   // |g| ~ perpendicular
    //     float m  = strokeMask(g, 1.6 * pixel, fwidth(g));
    //     col = mix(col, heightRamp(s, 1.0), m);
    //
    // Dividing by sqrt(1+f'^2) is what keeps the stroke width constant on steep
    // slopes. Unlike OpenCV's LINE_AA this is per-pixel analytics, so it stays
    // sharp at any resolution without touching the numbers.
    //
    // Want a 2D field (ripples, wave packets, interference) instead of a graph?
    // Replace p.x with length(p - centre) and keep everything else.
    // =======================================================================

#ifdef DRAW_EXAMPLE
    float s  = sin(p.x + uTime);
    float fp = cos(p.x + uTime);
    float g  = (p.y - s) / sqrt(1.0 + fp * fp);
    float gw = fwidth(g);
    float hw = 1.6 * pixel;
    vec3  cw = heightRamp(s, 1.0);
    col += cw * (1.0 - smoothstep(hw * 3.0, hw * 6.5 + gw, abs(g))) * 0.15;  // halo
    col  = mix(col, cw, strokeMask(g, hw, gw));                              // core
#endif

    // =========================== END OF YOUR DRAWING ========================

    fragColor = vec4(vignette(col, gl_FragCoord.xy, res), 1.0);
}
