// TimingShiftCore.cpp
// -----------------------------------------------------------------------------
// CPU implementation of the Timing Shift FX ghost effect.
//
// For every output pixel:
//   1. Read the time-shifted frame ("ghost frame") at a position moved by
//      Ghost Distance in Ghost Direction.
//   2. Ghost Stretch smears that read along the direction (several samples
//      between a shorter and the full distance) - this distorts / stretches
//      the ghost like an analog timing error.
//   3. Mix the ghost over the current frame with Ghost Amount.
//
// Pixels outside the image are clamped to the edge, so there are never black
// borders. The result only depends on the inputs - it is fully deterministic.
// -----------------------------------------------------------------------------
#include "TimingShiftCore.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace tsfx {

namespace {

constexpr int kMaxStretchSamples = 32;
constexpr double kPi = 3.14159265358979323846;

inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Bilinear RGBA read at a fractional position, clamped to the image edges.
inline void sampleRGBA(const ImageView& img, double fx, double fy, float out[4]) {
    const double flx = std::floor(fx), fly = std::floor(fy);
    const float tx = static_cast<float>(fx - flx);
    const float ty = static_cast<float>(fy - fly);
    const int x0 = std::clamp(static_cast<int>(flx), img.x1, img.x2 - 1);
    const int x1 = std::clamp(static_cast<int>(flx) + 1, img.x1, img.x2 - 1);
    const int y0 = std::clamp(static_cast<int>(fly), img.y1, img.y2 - 1);
    const int y1 = std::clamp(static_cast<int>(fly) + 1, img.y1, img.y2 - 1);
    const float* a = img.pixel(x0, y0);
    const float* b = img.pixel(x1, y0);
    const float* d = img.pixel(x0, y1);
    const float* e = img.pixel(x1, y1);
    for (int c = 0; c < 4; ++c)
        out[c] = lerpf(lerpf(a[c], b[c], tx), lerpf(d[c], e[c], tx), ty);
}

// Everything that is constant for one frame. Computed once, shared by threads.
struct FrameState {
    const ImageView* cur;
    const ImageView* ghost;
    MutableImageView dst;
    float amount;
    int samples;
    double offX[kMaxStretchSamples];  // where each smear sample reads from
    double offY[kMaxStretchSamples];
    int ioffX[kMaxStretchSamples];    // same, rounded (used when smearing)
    int ioffY[kMaxStretchSamples];
};

void renderRows(const FrameState& f, int winX1, int winX2, int yBegin, int yEnd) {
    const ImageView& cur = *f.cur;
    const float inv = 1.0f / static_cast<float>(f.samples);
    for (int y = yBegin; y < yEnd; ++y) {
        const int cy = std::clamp(y, cur.y1, cur.y2 - 1);
        float* outRow = f.dst.pixel(winX1, y);
        for (int x = winX1; x < winX2; ++x) {
            float g[4] = {0, 0, 0, 0};
            if (f.samples == 1) {
                // Sharp ghost: smooth sub-pixel (bilinear) read.
                // Moving the ghost by +offset means reading from -offset.
                sampleRGBA(*f.ghost, x - f.offX[0], y - f.offY[0], g);
            } else {
                // Smeared ghost: the averaging hides sub-pixel detail, so
                // whole-pixel reads are enough and ~4x faster.
                const ImageView& gi = *f.ghost;
                for (int k = 0; k < f.samples; ++k) {
                    const int sx = std::clamp(x - f.ioffX[k], gi.x1, gi.x2 - 1);
                    const int sy = std::clamp(y - f.ioffY[k], gi.y1, gi.y2 - 1);
                    const float* t = gi.pixel(sx, sy);
                    g[0] += t[0]; g[1] += t[1]; g[2] += t[2]; g[3] += t[3];
                }
            }
            const float* base = cur.pixel(std::clamp(x, cur.x1, cur.x2 - 1), cy);
            float* o = outRow + (x - winX1) * 4;
            for (int c = 0; c < 4; ++c) o[c] = lerpf(base[c], g[c] * inv, f.amount);
        }
    }
}

}  // namespace

bool needsGhostFrame(const Settings& s) {
    return s.timingOffset != 0 && !isIdentity(s);
}

bool isIdentity(const Settings& s) {
    if (s.ghostAmount <= 0.0) return true;
    // Ghost identical to the image: same frame, no movement, no smear.
    return s.timingOffset == 0 && s.ghostDistance == 0.0;
}

bool render(const Settings& s, const FrameInputs& in,
            const MutableImageView& dst, int winX1, int winY1, int winX2,
            int winY2, double pixelScale, int numThreads) {
    if (!in.current.valid() || dst.data == nullptr) return false;
    // Never write outside the destination image.
    winX1 = std::max(winX1, dst.x1); winX2 = std::min(winX2, dst.x2);
    winY1 = std::max(winY1, dst.y1); winY2 = std::min(winY2, dst.y2);
    if (winX2 <= winX1 || winY2 <= winY1) return true;

    FrameState f;
    f.cur = &in.current;
    f.ghost = in.ghost.valid() ? &in.ghost : &in.current;
    f.dst = dst;
    f.amount = static_cast<float>(clamp01(s.ghostAmount));

    // Ghost offset vector in pixels (OFX: y points up, so 90 deg = up).
    const double ps = pixelScale > 0.0 ? pixelScale : 1.0;
    const double dist = s.ghostDistance * ps;
    const double rad = s.ghostDirection * kPi / 180.0;
    const double dx = std::cos(rad), dy = std::sin(rad);
    // Stretch: samples spread from (1 - stretch) * distance to distance.
    const double stretch = clamp01(s.ghostStretch);
    const double len = stretch * std::fabs(dist);
    f.samples = std::clamp(static_cast<int>(std::ceil(len / 1.5)) + 1, 1, kMaxStretchSamples);
    const double start = dist * (1.0 - stretch);
    const double end = dist;
    for (int k = 0; k < f.samples; ++k) {
        const double t = f.samples == 1 ? 1.0 : static_cast<double>(k) / (f.samples - 1);
        const double d = start + (end - start) * t;
        f.offX[k] = dx * d;
        f.offY[k] = dy * d;
        f.ioffX[k] = static_cast<int>(std::lround(f.offX[k]));
        f.ioffY[k] = static_cast<int>(std::lround(f.offY[k]));
    }

    // --- split rows across CPU threads ----------------------------------
    int threads = numThreads > 0 ? numThreads
                                 : static_cast<int>(std::thread::hardware_concurrency());
    const int rows = winY2 - winY1;
    threads = std::clamp(threads, 1, std::max(1, rows / 16));
    if (threads == 1) {
        renderRows(f, winX1, winX2, winY1, winY2);
        return true;
    }
    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(threads - 1));
    const int chunk = (rows + threads - 1) / threads;
    for (int t = 1; t < threads; ++t) {
        const int b = winY1 + t * chunk;
        const int e = std::min(winY2, b + chunk);
        if (b < e) pool.emplace_back(renderRows, std::cref(f), winX1, winX2, b, e);
    }
    renderRows(f, winX1, winX2, winY1, std::min(winY2, winY1 + chunk));
    for (auto& th : pool) th.join();
    return true;
}

}  // namespace tsfx
