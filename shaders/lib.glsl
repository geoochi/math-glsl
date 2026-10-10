// ============================================================================
//  lib.glsl -- small GLSL helpers shared by the shaders in this repo.
//
//  Pure functions only: no globals except PI, no main(), no dependency on the
//  including file's constants. Anything can #include it and stay self-contained.
//  (The renderer resolves #include relative to the *including* file, since core
//  GLSL has no #include directive of its own.)
// ============================================================================

const float PI = 3.14159265359;

// Framebuffer pixel -> maths coordinate: origin at the centre, y up, same scale
// on both axes (so a circle in maths space is a circle on screen).
vec2 toMath(vec2 fragCoord, vec2 res, float unitsWide) {
    return (fragCoord - 0.5 * res) * (unitsWide / res.x);
}

// Anti-aliased stroke from an implicit function value g (zero exactly on the
// curve). fwidth() converts maths units into pixels, so the stroke keeps its
// width at any resolution. Divide g by |grad g| first when the curve can be
// steep, otherwise steep parts come out thinner.
float strokeMask(float g, float halfWidth, float aa) {
    return 1.0 - smoothstep(halfWidth - aa, halfWidth + aa, abs(g));
}

// Distance to the nearest integer grid line, in pixels. Dividing by fwidth(p)
// is what makes a 1-pixel grid stay 1 pixel at any zoom or resolution.
float gridMask(vec2 p) {
    vec2 g = abs(fract(p - 0.5) - 0.5) / fwidth(p);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

float axisMask(float dist, float pixel) {
    return 1.0 - smoothstep(0.9 * pixel, 2.2 * pixel, abs(dist));
}

// The empty coordinate system: grid fading with distance, axes through (0,0).
vec3 coordinateSystem(vec2 p, float pixel) {
    vec3 col = vec3(0.043, 0.055, 0.078);                        // background
    float fade = 1.0 - smoothstep(3.0, 9.0, length(p * vec2(0.55, 1.0)));
    col = mix(col, vec3(0.16, 0.19, 0.26), gridMask(p) * 0.8 * fade);
    col = mix(col, vec3(0.44, 0.49, 0.60),
              max(axisMask(p.y, pixel), axisMask(p.x, pixel)) * 0.9);
    return col;
}

// Colour ramp by height: teal in the troughs, violet on the crests.
vec3 heightRamp(float y, float amp) {
    float h = clamp(y / max(amp, 1e-4) * 0.5 + 0.5, 0.0, 1.0);
    return mix(vec3(0.24, 0.80, 0.72), vec3(0.72, 0.55, 1.00), h);
}

// Gentle corner falloff.
vec3 vignette(vec3 col, vec2 fragCoord, vec2 res) {
    vec2 uv = fragCoord / res;
    return col * (1.0 - 0.22 * dot(uv - 0.5, uv - 0.5));
}
