// ============================================================================
//  mandelbrot.glsl -- an endless dive into the Mandelbrot set.
//
//  Companion to demo/mandelbrot/prompt.md, rendered to demo/mandelbrot/mandelbrot.mp4.
//
//  There is no coordinate grid here: the fractal IS the picture. The camera
//  flies to TARGET while its field of view shrinks *exponentially*, so the
//  perceived zoom rate is constant for the whole clip -- a linear zoom looks
//  slow at the start and unwatchably fast near the end.
//
//  Render it with:
//      HWENC=0 SHADER=demo/mandelbrot/mandelbrot.glsl \
//          ./render_glsl demo/mandelbrot/mandelbrot.mp4 3840 2160 60 12
// ============================================================================

#include "../../shaders/lib.glsl"

const float DURATION  = 12.0;    // seconds the dive is designed for
const float BAIL      = 256.0;   // escape once |z|^2 exceeds this
const float HALF_0    = 1.45;    // view half-height at t = 0 (whole set visible)
const float HALF_1    = 4.0e-4;  // view half-height at t = DURATION (~3600x)
const float ARRIVE    = 0.22;    // fraction of the clip spent flying to TARGET
const float PAL_SPEED = 0.020;   // palette drift, cycles per second
const float PAL_SCALE = 0.45;    // palette cycles per unit of SQRT(escape time)
const float ROT_TOTAL = 0.0;     // total camera roll over the clip, radians

// A classic deep-zoom destination: the tip of a seahorse valley filament.
// The 16 digits matter -- shortening them makes the deep end drift off centre.
const vec2 TARGET = vec2(-0.7436438870371587, 0.13182590420531197);

vec2 cmul(vec2 a, vec2 b) {
    return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

// Cyclic ramp in the same family as lib.glsl's heightRamp: deep navy -> teal ->
// violet -> a near-white highlight -> navy. It never exceeds 1.0, so the bright
// filaments keep their colour instead of clipping to flat white the way a
// saturated palette does.
vec3 ramp(float x) {
    x = fract(x);
    vec3 navy  = vec3(0.035, 0.055, 0.130);
    vec3 teal  = vec3(0.130, 0.640, 0.620);
    vec3 vilet = vec3(0.640, 0.420, 1.000);
    vec3 hlite = vec3(0.960, 0.980, 1.000);
    vec3 col = mix(navy, teal,  smoothstep(0.00, 0.30, x));
    col      = mix(col,  vilet, smoothstep(0.30, 0.62, x));
    col      = mix(col,  hlite, smoothstep(0.62, 0.84, x));
    col      = mix(col,  navy,  smoothstep(0.84, 1.00, x));
    return col;
}

// Smooth escape time. Using the raw integer iteration count would print hard
// concentric colour bands; mu is the continuous version of that count.
// Returns `cap` for points that never escape (inside the set).
float escapeTime(vec2 c, float cap) {
    vec2  z       = vec2(0.0);
    float n       = 0.0;
    bool  escaped = false;
    for (int i = 0; i < 520; ++i) {
        if (n >= cap) break;
        z = cmul(z, z) + c;
        if (dot(z, z) > BAIL) { escaped = true; break; }
        n += 1.0;
    }
    if (!escaped) return cap;
    float logZn = 0.5 * log(dot(z, z));
    float nu    = log(logZn / log(2.0)) / log(2.0);
    return n + 1.0 - nu;
}

void main() {
    vec2  res = uResolution;
    float t   = uTime;

    // ---- camera: fly to TARGET, then shrink the view exponentially ----------
    float u  = t / DURATION;
    float k  = smoothstep(0.0, ARRIVE, min(u, 1.0));
    vec2  O  = mix(vec2(-0.5, 0.0), TARGET, k);          // view centre
    float H  = exp(mix(log(HALF_0), log(HALF_1), u));    // view half-height
    float a  = ROT_TOTAL * min(u, 1.0);
    mat2  R  = mat2(vec2(cos(a), sin(a)), vec2(-sin(a), cos(a)));

    vec2 uv = (gl_FragCoord.xy - 0.5 * res) / res.y;     // y in [-0.5, 0.5]
    vec2 c  = O + R * (uv * (2.0 * H));

    // Deeper views need more iterations before a point counts as "inside".
    float cap = mix(220.0, 480.0, clamp(u, 0.0, 1.0));

    // ---------------- the user's logic --------------------------------------
    float mu     = escapeTime(c, cap);
    bool  inside = mu >= cap;

    // Colour by sqrt(mu), not mu: the far field escapes almost immediately so
    // its mu barely changes across huge areas, and a linear mapping leaves those
    // areas as one flat wash. sqrt compands that end -- band spacing grows with
    // mu, so the flat regions get texture while the filigree near the boundary
    // keeps from turning into per-pixel rainbow noise.
    float m   = PAL_SCALE * sqrt(mu) + PAL_SPEED * t;
    vec3  col = inside ? vec3(0.012, 0.020, 0.045) : ramp(m);

    // fwidth(mu) blows up exactly where orbits escape late, i.e. along the set
    // boundary. Lit with it, the edge glows at one pixel wide no matter how deep
    // the zoom goes -- an effect that would need a separate blur pass otherwise.
    float aa  = fwidth(mu);
    float rim = clamp((aa - 3.0) / 12.0, 0.0, 1.0);
    col += vec3(0.30, 0.62, 1.00) * rim * 0.6;
    // ------------------------------------------------------------------------

    fragColor = vec4(vignette(col, gl_FragCoord.xy, res), 1.0);
}
