// Screen Refilm - CPU renderer.
//
// Model: the source frame is shown on a flat physical screen made of RGB
// sub-pixels. A virtual pinhole camera orbits a "look at" point on that
// screen and films it. Rendering happens in two passes:
//
//   1. Ray-cast every output pixel onto the screen plane, fetch the colour of
//      the screen pixel that is hit, multiply by the sub-pixel mask of the
//      chosen display type (anti-aliased by the pixel's footprint), and add
//      optional moire bands. Result: a sharp, linear-light "plate".
//   2. Build a box-filtered mip pyramid of the plate and gather a variable
//      radius disc blur from it (depth-of-field band), with per-channel
//      offsets (RGB split / chromatic aberration), then glow, flicker,
//      vignette and a highlight shoulder.

#include "refilm_render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace refilm {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

struct V2 { double x, y; };
struct V3 { double x, y, z; };
struct C3 { float r, g, b; };

inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 normalize(V3 a) {
    double l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1.0 / l) : a;
}
// Rodrigues rotation of v around unit axis k.
inline V3 rotate(V3 v, V3 k, double ang) {
    double c = std::cos(ang), s = std::sin(ang);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1 - c));
}

inline C3 operator+(C3 a, C3 b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline C3 operator*(C3 a, float s) { return {a.r * s, a.g * s, a.b * s}; }
inline C3 lerp(C3 a, C3 b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

inline double clamp(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline double smoothstep(double e0, double e1, double x) {
    double t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
// Soft-edged box: 1 inside [a,b], 0 outside, edge width ~2s.
inline double band(double x, double a, double b, double s) {
    return smoothstep(a - s, a + s, x) * (1.0 - smoothstep(b - s, b + s, x));
}

inline double hashInt(int64_t i, uint32_t seed) {
    uint32_t x = uint32_t(i) * 0x9E3779B1u ^ (seed * 0x85EBCA77u + 0x27d4eb2fu);
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return double(x) / 4294967295.0 * 2.0 - 1.0;
}
// Smooth 1D value noise in [-1, 1].
inline double valueNoise(double t, uint32_t seed) {
    double f = std::floor(t);
    double u = t - f;
    u = u * u * (3 - 2 * u);
    double a = hashInt(int64_t(f), seed), b = hashInt(int64_t(f) + 1, seed);
    return a + (b - a) * u;
}
// Organic wobble in roughly [-1, 1].
inline double wobble(double t, double s) {
    return 0.55 * std::sin(1.3 * t + s) + 0.3 * std::sin(2.9 * t + 2.1 * s) + 0.15 * valueNoise(t * 1.7, uint32_t(s * 1000));
}

template <class F>
void parallelRows(int rows, int threads, F fn) {
    if (threads <= 0) threads = int(std::max(1u, std::thread::hardware_concurrency()));
    threads = std::min(threads, std::max(1, rows / 8));
    if (threads <= 1) { fn(0, rows); return; }
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (int i = 0; i < threads; ++i) {
        int a = rows * i / threads, b = rows * (i + 1) / threads;
        pool.emplace_back([=, &fn] { fn(a, b); });
    }
    for (auto& t : pool) t.join();
}

// ---------------------------------------------------------------------------
// Sub-pixel masks, prefiltered analytically.
//
// Each emitter shape is a box or a Gaussian, so convolving it with a Gaussian
// approximation of the output pixel's footprint (sx, sy, in screen pixels)
// has a closed form. That removes aliasing between the sub-pixel grid and the
// output grid without supersampling; deliberate moire is a separate control.
// Shapes are normalised so each channel averages to exactly 1 over an area,
// and filtering preserves that average, so brightness never pumps with zoom.

constexpr double kSqrt2 = 1.4142135623730951;

// Periodic (period 1) box [a, b] blurred by a Gaussian of std-dev s.
inline double boxPeriodic(double x, double a, double b, double s) {
    const double inv = 1.0 / (kSqrt2 * s);
    // Only periodic copies within 4 sigma of x contribute.
    const int k0 = int(std::ceil(a - 4 * s - x)), k1 = int(std::floor(b + 4 * s - x));
    double v = 0;
    for (int k = k0; k <= k1; ++k) v += std::erf(float((x - a + k) * inv)) - std::erf(float((x - b + k) * inv));
    return 0.5 * v;
}

// Periodic (period 1) Gaussian lobe of std-dev s0 at c, blurred to std-dev s.
// Normalised to unit mean.
inline double lobePeriodic(double x, double c, double s) {
    const int k0 = int(std::ceil(c - 4 * s - x)), k1 = int(std::floor(c + 4 * s - x));
    double v = 0;
    const double k2 = -0.5 / (s * s);
    for (int k = k0; k <= k1; ++k) { double d = x - c + k; v += std::exp(float(d * d * k2)); }
    return v / (s * std::sqrt(2 * kPi));
}

// q: position in screen pixels. sx, sy: footprint std-dev in screen pixels.
// lum: brightness of the pixel (CRT beam width depends on it).
C3 maskAt(int type, V2 q, double sx, double sy, double lum) {
    switch (type) {
    case kScreenCRT: {
        // Aperture grille phosphor stripes x electron-beam scanlines.
        const double s0 = 0.085, sl = std::sqrt(s0 * s0 + sx * sx);
        const double fx = q.x - std::floor(q.x), fy = q.y - std::floor(q.y);
        const double sb0 = 0.15 + 0.11 * clamp(lum, 0.0, 1.5);
        const double beam = lobePeriodic(fy, 0.5, std::sqrt(sb0 * sb0 + sy * sy));
        return {float(lobePeriodic(fx, 1.0 / 6, sl) * beam), float(lobePeriodic(fx, 0.5, sl) * beam),
                float(lobePeriodic(fx, 5.0 / 6, sl) * beam)};
    }
    case kScreenOLED: {
        // Diamond/PenTile style: a green dot in every pixel, red and blue dots
        // alternate in a checkerboard. Small emitters on black.
        const double sG = 0.085, sRB = 0.11;
        const double gx = std::sqrt(sG * sG + sx * sx), gy = std::sqrt(sG * sG + sy * sy);
        const double bx = std::sqrt(sRB * sRB + sx * sx), by = std::sqrt(sRB * sRB + sy * sy);
        const int64_t ix = int64_t(std::floor(q.x)), iy = int64_t(std::floor(q.y));
        const int R = std::min(3, 1 + int(3 * std::max(bx, by)));
        double r = 0, g = 0, b = 0;
        for (int64_t cy = iy - R; cy <= iy + R; ++cy)
            for (int64_t cx = ix - R; cx <= ix + R; ++cx) {
                double dgx = q.x - (cx + 0.74), dgy = q.y - (cy + 0.5);
                g += std::exp(-0.5 * (dgx * dgx / (gx * gx) + dgy * dgy / (gy * gy)));
                double dbx = q.x - (cx + 0.28);
                double e = std::exp(-0.5 * (dbx * dbx / (bx * bx) + dgy * dgy / (by * by)));
                if (((cx + cy) & 1) == 0) r += e; else b += e;
            }
        // Unit mean: green covers every cell, red/blue every other cell.
        return {float(r / (kPi * bx * by)), float(g / (2 * kPi * gx * gy)), float(b / (kPi * bx * by))};
    }
    case kScreenLCD:
    default: {
        // RGB vertical stripes with a thin black matrix between rows.
        const double e = 0.03;  // native edge softness
        const double bx = std::sqrt(e * e + sx * sx), by = std::sqrt(e * e + sy * sy);
        const double fx = q.x - std::floor(q.x), fy = q.y - std::floor(q.y);
        const double rowH = 0.84, colW = 0.28;
        const double row = boxPeriodic(fy, 0.08, 0.08 + rowH, by) / rowH;
        const double k = row / colW;
        return {float(boxPeriodic(fx, 0.03, 0.31, bx) * k), float(boxPeriodic(fx, 0.36, 0.64, bx) * k),
                float(boxPeriodic(fx, 0.69, 0.97, bx) * k)};
    }
    }
}

// ---------------------------------------------------------------------------
// Image access

inline C3 fetchSrc(const ConstImage& im, int x, int y) {
    x = std::min(std::max(x, 0), im.width - 1);
    y = std::min(std::max(y, 0), im.height - 1);
    const float* p = im.data + std::ptrdiff_t(y) * im.stride + std::ptrdiff_t(x) * 4;
    return {p[0], p[1], p[2]};
}

// Bilinear sample, coordinates in pixels with pixel centres at +0.5.
inline C3 sampleSrc(const ConstImage& im, double x, double y) {
    x -= 0.5; y -= 0.5;
    double fx = std::floor(x), fy = std::floor(y);
    int x0 = int(fx), y0 = int(fy);
    float tx = float(x - fx), ty = float(y - fy);
    if (x0 >= 0 && y0 >= 0 && x0 < im.width - 1 && y0 < im.height - 1) {  // fast interior path
        const float* p = im.data + std::ptrdiff_t(y0) * im.stride + std::ptrdiff_t(x0) * 4;
        const float* q = p + im.stride;
        auto L = [&](int c) { float a = p[c] + (p[c + 4] - p[c]) * tx, b = q[c] + (q[c + 4] - q[c]) * tx; return a + (b - a) * ty; };
        return {L(0), L(1), L(2)};
    }
    C3 a = lerp(fetchSrc(im, x0, y0), fetchSrc(im, x0 + 1, y0), tx);
    C3 b = lerp(fetchSrc(im, x0, y0 + 1), fetchSrc(im, x0 + 1, y0 + 1), tx);
    return lerp(a, b, ty);
}

struct Plane {
    int w = 0, h = 0;
    std::vector<C3> px;
    C3 at(int x, int y) const {
        x = std::min(std::max(x, 0), w - 1);
        y = std::min(std::max(y, 0), h - 1);
        return px[size_t(y) * w + x];
    }
    C3 sample(double x, double y) const {
        x -= 0.5; y -= 0.5;
        double fx = std::floor(x), fy = std::floor(y);
        int x0 = int(fx), y0 = int(fy);
        float tx = float(x - fx), ty = float(y - fy);
        if (x0 >= 0 && y0 >= 0 && x0 < w - 1 && y0 < h - 1) {  // fast interior path
            const C3* p = &px[size_t(y0) * w + x0];
            return lerp(lerp(p[0], p[1], tx), lerp(p[w], p[w + 1], tx), ty);
        }
        C3 a = lerp(at(x0, y0), at(x0 + 1, y0), tx);
        C3 b = lerp(at(x0, y0 + 1), at(x0 + 1, y0 + 1), tx);
        return lerp(a, b, ty);
    }
};

struct Pyramid {
    std::vector<Plane> levels;
    // x, y in level-0 pixel units.
    C3 sample(double x, double y, double lod) const {
        lod = clamp(lod, 0.0, double(levels.size() - 1));
        int l0 = int(lod);
        float t = float(lod - l0);
        double s0 = 1.0 / double(1 << l0);
        C3 a = levels[l0].sample(x * s0, y * s0);
        if (t <= 0.001f || l0 + 1 >= int(levels.size())) return a;
        C3 b = levels[l0 + 1].sample(x * s0 * 0.5, y * s0 * 0.5);
        return lerp(a, b, t);
    }
};

void buildPyramid(Pyramid& pyr, int threads) {
    while (pyr.levels.back().w > 2 && pyr.levels.back().h > 2 && pyr.levels.size() < 9) {
        const Plane& s = pyr.levels.back();
        Plane d;
        d.w = (s.w + 1) / 2;
        d.h = (s.h + 1) / 2;
        d.px.resize(size_t(d.w) * d.h);
        parallelRows(d.h, threads, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < d.w; ++x) {
                    C3 c = s.at(2 * x, 2 * y) + s.at(2 * x + 1, 2 * y) + s.at(2 * x, 2 * y + 1) + s.at(2 * x + 1, 2 * y + 1);
                    d.px[size_t(y) * d.w + x] = c * 0.25f;
                }
        });
        pyr.levels.push_back(std::move(d));
    }
}

inline float shoulder(float x) {
    const float k = 0.8f;
    if (x <= k) return std::max(x, 0.f);
    return k + (1 - k) * (1 - std::exp(-(x - k) / (1 - k)));
}

// Highlight handling like an over-exposed sensor: a channel driven past the
// top desaturates towards white (energy moves into the other channels)
// instead of clipping on its own, which would shift hue and lose brightness
// wherever sub-pixels are resolved.
inline C3 desaturateOver(C3 c) {
    c = {std::max(c.r, 0.f), std::max(c.g, 0.f), std::max(c.b, 0.f)};
    const float m = std::max(c.r, std::max(c.g, c.b));
    const float limit = 1.15f;
    if (m > limit) {
        const float L = (c.r + c.g + c.b) / 3.f;
        if (L >= limit) {
            c = {L, L, L};
        } else {
            const float a = (limit - L) / (m - L);
            c = {L + (c.r - L) * a, L + (c.g - L) * a, L + (c.b - L) * a};
        }
    }
    return c;
}

inline C3 compressHighlights(C3 c) {
    c = desaturateOver(c);
    return {shoulder(c.r), shoulder(c.g), shoulder(c.b)};
}

// ---------------------------------------------------------------------------
// Camera

struct Camera {
    V3 pos, f, r, u;
    double tanHalf;
    double bulge;
    double halfW, halfH;  // output half size in pixels
    double invHalfH;

    // Output pixel -> normalised coords (y in [-1, 1]).
    V2 ndc(double px, double py) const { return {(px - halfW) * invHalfH, (py - halfH) * invHalfH}; }

    // Returns false if the ray misses the screen plane.
    bool cast(double px, double py, V2& hit) const {
        V2 n = ndc(px, py);
        if (bulge != 0) {
            double r2 = (n.x * n.x + n.y * n.y) / (halfW * halfW * invHalfH * invHalfH + 1.0);
            double s = std::max(0.2, 1.0 + bulge * r2);
            n.x *= s; n.y *= s;
        }
        V3 d = f + r * (n.x * tanHalf) + u * (n.y * tanHalf);
        if (d.z > -1e-9) return false;
        double t = -pos.z / d.z;
        hit = {pos.x + d.x * t, pos.y + d.y * t};
        return true;
    }
};

double shapeAspect(int shape, double timelineAspect) {
    switch (shape) {
    case kShape16x9: return 16.0 / 9.0;
    case kShape4x3: return 4.0 / 3.0;
    case kShape9x16: return 9.0 / 16.0;
    case kShape1x1: return 1.0;
    default: return timelineAspect;
    }
}

} // namespace

void render(const ConstImage& src, const Image& dst, double t, const Params& P, int threads) {
    const int W = dst.width, H = dst.height;
    if (W <= 0 || H <= 0) return;
    if (threads <= 0) threads = int(std::max(1u, std::thread::hardware_concurrency()));

    const int type = int(clamp(std::round(P.screen), 0, 2));
    const double aspect = double(W) / H;

    // --- Screen geometry (units: screen heights, origin at screen centre) ---
    const double A = shapeAspect(int(std::round(P.recordingShape)), aspect);
    const double halfA = A * 0.5;
    const double srcW = std::max(1, src.width), srcH = std::max(1, src.height);
    const double srcAspect = srcW / srcH;
    double imgW, imgH;  // picture fitted inside the screen
    if (srcAspect >= A) { imgW = A; imgH = A / srcAspect; }
    else { imgH = 1.0; imgW = srcAspect; }
    const double scale = P.sourceScale > 0 ? P.sourceScale : 1.0;
    const double pixelSize = std::max(1.0, std::round(P.pixelSize));
    const double srcPerScreenPx = pixelSize * scale;            // actual src pixels per screen pixel
    const double pitch = imgH / (srcH / scale) * pixelSize;      // screen units per screen pixel
    const V2 gridOrigin = {-imgW * 0.5, -imgH * 0.5};
    const double blackLevel = type == kScreenLCD ? 0.02 : (type == kScreenCRT ? 0.008 : 0.0);

    // --- Camera ---
    const double zoomEff = std::max(0.05, P.zoom * std::max(0.05, 1.0 + P.pushIn * t / 10.0) *
                                              (1.0 + P.drift * 0.02 * wobble(t * 0.5, 5.7)));
    const double vfov = 40.0 * kDeg;
    Camera cam;
    cam.tanHalf = std::tan(vfov * 0.5);
    const double dist = 0.5 / (cam.tanHalf * zoomEff);
    const V3 look = {(P.lookX - 0.5) * A + P.panLR * t / 20.0, (P.lookY - 0.5) + P.panUD * t / 20.0, 0};
    const double yaw = clamp(P.tiltLR, -85, 85) * kDeg, pitchA = clamp(P.tiltUD, -85, 85) * kDeg;
    cam.pos = look + V3{std::sin(yaw) * std::cos(pitchA), std::sin(pitchA), std::cos(yaw) * std::cos(pitchA)} * dist;
    cam.f = normalize(look - cam.pos);
    cam.r = normalize(cross(cam.f, V3{0, 1, 0}));
    cam.u = cross(cam.r, cam.f);
    if (P.drift != 0) {
        double dy = P.drift * kDeg * (1.4 * wobble(t * 0.8, 1.0) + 0.2 * valueNoise(t * 6.0, 7));
        double dp = P.drift * kDeg * (1.1 * wobble(t * 0.8, 2.3) + 0.2 * valueNoise(t * 6.0, 9));
        double dr = P.drift * kDeg * 0.9 * wobble(t * 0.6, 4.1);
        cam.f = rotate(cam.f, cam.u, dy); cam.r = rotate(cam.r, cam.u, dy);
        cam.f = rotate(cam.f, cam.r, dp); cam.u = rotate(cam.u, cam.r, dp);
        cam.r = rotate(cam.r, cam.f, dr); cam.u = rotate(cam.u, cam.f, dr);
    }
    cam.bulge = P.lensBulge * 0.35;
    cam.halfW = W * 0.5;
    cam.halfH = H * 0.5;
    cam.invHalfH = 2.0 / H;

    // --- Pass 1: the sharp plate ---
    Pyramid pyr;
    pyr.levels.resize(1);
    Plane& plate = pyr.levels[0];
    plate.w = W; plate.h = H;
    plate.px.resize(size_t(W) * H);

    const double gridStrength = clamp(P.gridStrength, 0, 1);
    const double moireAmt = P.moire * 0.45;
    const float moirePhase = float(2.0944 * clamp(P.moireColour, 0, 1));
    const double mc = std::cos(0.35), ms = std::sin(0.35);
    const C3 bezel = {0.012f, 0.012f, 0.014f};

    parallelRows(H, threads, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < W; ++x) {
                C3& out = plate.px[size_t(y) * W + x];
                V2 s, sx, sy;
                if (!cam.cast(x + 0.5, y + 0.5, s) || std::fabs(s.x) > halfA || std::fabs(s.y) > 0.5) {
                    out = bezel;
                    continue;
                }
                V2 q = {(s.x - gridOrigin.x) / pitch, (s.y - gridOrigin.y) / pitch};
                // Footprint of this output pixel, in screen pixels.
                V2 jx = {1e3, 0}, jy = {0, 1e3};
                if (cam.cast(x + 1.5, y + 0.5, sx)) jx = {(sx.x - s.x) / pitch, (sx.y - s.y) / pitch};
                if (cam.cast(x + 0.5, y + 1.5, sy)) jy = {(sy.x - s.x) / pitch, (sy.y - s.y) / pitch};
                double fp = std::max(std::hypot(jx.x, jx.y), std::hypot(jy.x, jy.y));

                // Colour of the screen pixel (blocky when magnified, smooth when not).
                V2 cell = {std::floor(q.x) + 0.5, std::floor(q.y) + 0.5};
                double cxs = cell.x * srcPerScreenPx, cys = cell.y * srcPerScreenPx;
                C3 col = {0, 0, 0};
                if (cxs >= 0 && cys >= 0 && cxs < srcW && cys < srcH) {
                    col = sampleSrc(src, cxs, cys);
                    double blend = smoothstep(0.6, 1.5, fp);
                    if (blend > 0) col = lerp(col, sampleSrc(src, q.x * srcPerScreenPx, q.y * srcPerScreenPx), float(blend));
                }
                col = {float(blackLevel + col.r * (1 - blackLevel)), float(blackLevel + col.g * (1 - blackLevel)),
                       float(blackLevel + col.b * (1 - blackLevel))};

                // Sub-pixel mask, prefiltered by this pixel's footprint and faded
                // out entirely once it is far below the output resolution.
                double fade = smoothstep(1.5, 3.0, fp);
                if (gridStrength > 0 && fade < 1) {
                    const double sxp = 0.45 * std::hypot(jx.x, jy.x), syp = 0.45 * std::hypot(jx.y, jy.y);
                    const double lum = 0.2126 * col.r + 0.7152 * col.g + 0.0722 * col.b;
                    C3 m = maskAt(type, q, sxp, syp, lum);
                    float k = float((1 - fade) * gridStrength);
                    col = {col.r * (1 + (m.r - 1) * k), col.g * (1 + (m.g - 1) * k), col.b * (1 + (m.b - 1) * k)};
                }

                if (moireAmt != 0) {
                    double uu = s.x * mc + s.y * ms;
                    double warp = 0.035 * std::sin(s.y * 5.0 + s.x * 2.0) + 0.02 * std::sin(s.x * 11.0 - t * 0.3);
                    float ph = float(2 * kPi * P.moireBands * (uu + warp) + t * P.moireSpeed * 2.0);
                    col.r *= float(1 + moireAmt * std::sin(ph - moirePhase));
                    col.g *= float(1 + moireAmt * std::sin(ph));
                    col.b *= float(1 + moireAmt * std::sin(ph + moirePhase));
                }
                // Balance resolved sub-pixel peaks now (luminance preserving)
                // so later per-channel lens offsets cannot tint them.
                out = desaturateOver(col);
            }
        }
    });

    buildPyramid(pyr, threads);

    // --- Pass 2: lens ---
    // Sharp band orientation follows the camera tilt (that is the direction in
    // which distance to the screen changes), then Focus Angle rotates it.
    V2 fdir = {std::tan(yaw), std::tan(pitchA)};
    double fl = std::hypot(fdir.x, fdir.y);
    fdir = fl > 1e-3 ? V2{fdir.x / fl, fdir.y / fl} : V2{0, 1};
    {
        double a = P.focusAngle * kDeg, c = std::cos(a), s = std::sin(a);
        fdir = {fdir.x * c - fdir.y * s, fdir.x * s + fdir.y * c};
    }
    const double shift = P.focusShift + P.focusPull * 0.8 * std::sin(t * 0.9);
    const V2 fcenter = {fdir.x * shift, fdir.y * shift};
    const double blurScale = std::max(0.0, P.focusBlur) * 0.018 * H;

    const double splitPx = P.rgbSplit * 0.004 * H;
    const double caK = P.chromaticAberration * 0.008;

    const double hz = 0.4 * std::pow(60.0, clamp(P.flickerSpeed, 0, 1));
    const double flick = 1.0 + P.flicker * (0.3 * valueNoise(t * hz, 11) + 0.12 * valueNoise(t * hz * 2.7, 23));
    const bool glowOnly = P.flickerGlowOnly >= 0.5;
    const float picGain = float(P.brightness * (glowOnly ? 1.0 : flick));
    const float glowGain = float(P.brightness * flick * P.glow * 0.9);
    const double glowLod0 = std::min(3.0, double(pyr.levels.size() - 1));
    const double cornerR2 = aspect * aspect + 1.0;
    const double vig = clamp(P.vignette, 0, 2) * 0.9;

    parallelRows(H, threads, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* row = dst.data + std::ptrdiff_t(y) * dst.stride;
            for (int x = 0; x < W; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                const V2 n = cam.ndc(px, py);
                const double d = (n.x - fcenter.x) * fdir.x + (n.y - fcenter.y) * fdir.y;
                const double radius = blurScale * std::max(0.0, std::fabs(d) - 0.1);

                // Per-channel offsets in pixels.
                V2 offR = {splitPx + (px - cam.halfW) * caK, (py - cam.halfH) * caK};
                V2 offB = {-offR.x, -offR.y};
                const bool split = std::fabs(offR.x) + std::fabs(offR.y) > 0.01;

                auto tap = [&](double sx, double sy, double lod) -> C3 {
                    if (!split) return pyr.sample(sx, sy, lod);
                    C3 c;
                    c.r = pyr.sample(sx + offR.x, sy + offR.y, lod).r;
                    c.g = pyr.sample(sx, sy, lod).g;
                    c.b = pyr.sample(sx + offB.x, sy + offB.y, lod).b;
                    return c;
                };

                C3 col;
                if (radius < 0.25) {
                    col = tap(px, py, 0);
                } else {
                    // Taps read from a pre-filtered mip level matched to their
                    // spacing so the sub-pixel grid never aliases in the blur.
                    int N = int(clamp(4 + radius * 0.8, 6, 16));
                    double lod = std::log2(1.0 + radius * std::sqrt(kPi / N));
                    col = {0, 0, 0};
                    for (int i = 0; i < N; ++i) {
                        double rr = radius * std::sqrt((i + 0.5) / N);
                        double a = i * 2.399963229728653;
                        col = col + tap(px + rr * std::cos(a), py + rr * std::sin(a), lod);
                    }
                    col = col * (1.0f / N);
                }

                C3 outc = col * picGain;
                if (glowGain > 0) {
                    C3 g = pyr.sample(px, py, glowLod0) + pyr.sample(px, py, glowLod0 + 1.5) +
                           pyr.sample(px, py, glowLod0 + 3);
                    outc = outc + g * (glowGain / 3.0f);
                }
                if (vig > 0) {
                    double r2 = (n.x * n.x + n.y * n.y) / cornerR2;
                    outc = outc * float(std::max(0.0, 1.0 - vig * std::pow(r2, 1.5)));
                }
                float* o = row + std::ptrdiff_t(x) * 4;
                outc = compressHighlights(outc);
                o[0] = outc.r;
                o[1] = outc.g;
                o[2] = outc.b;
                o[3] = 1.0f;
            }
        }
    });
}

} // namespace refilm
