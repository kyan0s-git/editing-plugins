// Screen Refilm - host independent CPU renderer.
#pragma once

#include <cstddef>

#include "refilm_params.h"

namespace refilm {

// RGBA float image. Row 0 is the BOTTOM row (OpenFX convention).
// `stride` is the distance between rows in floats (may be negative).
struct ConstImage {
    const float* data;
    int width, height;
    std::ptrdiff_t stride;
};

struct Image {
    float* data;
    int width, height;
    std::ptrdiff_t stride;
};

// Renders `src` as if it were shown on a physical screen and filmed by a
// camera. `dst` defines the output frame. `timeSeconds` drives all of the
// automatic motion (push-in, pan, drift, focus pull, flicker, moire crawl).
// threads <= 0 uses all hardware threads.
void render(const ConstImage& src, const Image& dst, double timeSeconds, const Params& params,
            int threads = 0);

} // namespace refilm
