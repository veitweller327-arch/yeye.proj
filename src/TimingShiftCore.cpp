// TimingShiftCore.cpp
// -----------------------------------------------------------------------------
// CPU implementation of the Timing Shift FX look.
//
// Per output row the algorithm does:
//   1. Analog jitter     - a small, seed-driven offset of the whole frame.
//   2. Band displacement - irregular horizontal bands are pushed sideways and
//                          show the *time-shifted* frame (the "timing error").
//   3. RGB misalignment  - red/green/blue are sampled at different x offsets.
//   4. Ghosting / trails - up to 4 earlier (or later) frames are blended in.
//   5. Streaks           - optional horizontal smear of bright pixels.
//   6. Blend             - mix with the untouched original.
//
// Everything is deterministic: the random pattern only depends on the seed,
// the timeline time and the pixel row, never on thread order or wall clock.
// -----------------------------------------------------------------------------
#include "TimingShiftCore.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace tsfx {

namespace {

inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline double smoothstep(double t) { return t * t * (3.0 - 2.0 * t); }

// Reads one channel at a fractional position, clamping to the image edges.
// Clamping (instead of returning black) is what prevents black borders when
// the image is shifted.
inline float sampleChannel(const ImageView& img, double fx, double fy, int c,
                           bool bilinear) {
    if (!bilinear) {
        int ix = static_cast<int>(std::floor(fx + 0.5));
        int iy = static_cast<int>(std::floor(fy + 0.5));
        ix = std::clamp(ix, img.x1, img.x2 - 1);
        iy = std::clamp(iy, img.y1, img.y2 - 1);
        return img.pixel(ix, iy)[c];
    }
    const double flx = std::floor(fx), fly = std::floor(fy);
    const float tx = static_cast<float>(fx - flx);
    const float ty = static_cast<float>(fy - fly);
    const int x0 = std::clamp(static_cast<int>(flx), img.x1, img.x2 - 1);
    const int x1 = std::clamp(static_cast<int>(flx) + 1, img.x1, img.x2 - 1);
    const int y0 = std::clamp(static_cast<int>(fly), img.y1, img.y2 - 1);
    const int y1 = std::clamp(static_cast<int>(fly) + 1, img.y1, img.y2 - 1);
    const float a = img.pixel(x0, y0)[c], b = img.pixel(x1, y0)[c];
    const float d = img.pixel(x0, y1)[c], e = img.pixel(x1, y1)[c];
    return lerpf(lerpf(a, b, tx), lerpf(d, e, tx), ty);
}

inline void sampleRGBA(const ImageView& img, double fx, double fy,
                       bool bilinear, float out[4]) {
    for (int c = 0; c < 4; ++c) out[c] = sampleChannel(img, fx, fy, c, bilinear);
}

inline std::int64_t floorToInt(double v) {
    return static_cast<std::int64_t>(std::floor(v));
}

// Everything that is constant for one frame. Computed once, shared by threads.
struct FrameState {
    const Settings* s;
    const FrameInputs* in;
    MutableImageView dst;
    bool bilinear;
    double I;          // master intensity
    double ps;         // pixel scale
    std::int64_t key;  // pattern clock (changes with jitter speed)
    double jx, jy;     // whole-frame jitter in pixels
    double bandH, bandPhase;
    double ghostMix;
    double streakStrength;
    double streakDecay;
    bool streaksOn;
    const ImageView* bandSource;  // time-shifted frame used inside bands
};

void renderRows(const FrameState& f, int winX1, int winX2, int yBegin,
                int yEnd) {
    const Settings& s = *f.s;
    const FrameInputs& in = *f.in;
    const ImageView& cur = in.current;
    const std::uint32_t seed = static_cast<std::uint32_t>(s.seed);

    const double rgbPx = s.rgbSplit * f.I * f.ps;
    const double rOff = rgbPx * s.redShift;
    const double gOff = rgbPx * s.greenShift;
    const double bOff = rgbPx * s.blueShift;
    const double dispPx = s.hDisplacement * f.I * f.ps;
    const double density = clamp01(s.bandDensity);
    const double ghostStep = s.ghostShift * f.I * f.ps;
    const float blend = static_cast<float>(clamp01(s.blend));

    for (int y = yBegin; y < yEnd; ++y) {
        // --- 2. which band is this row in, and is it displaced? ----------
        const double bandPos = (y + f.bandPhase) / f.bandH;
        const std::int64_t band = floorToInt(bandPos);
        const double inBand = bandPos - static_cast<double>(band);
        // Each band is split at a random height into two sub-bands. That
        // gives irregular band heights instead of a regular grid.
        const int sub = inBand < hashNoise(seed, f.key, band, 3) ? 0 : 1;
        const std::int64_t bandId = band * 2 + sub;
        const bool active =
            dispPx != 0.0 && hashNoise(seed, f.key, bandId, 1) < density;
        double rowShift = 0.0;
        if (active) {
            const double mag = 0.35 + 0.65 * hashNoise(seed, f.key, bandId, 2);
            const double wobble = 0.92 + 0.16 * hashNoise(seed, f.key, y, 4);
            rowShift = dispPx * mag * wobble;
        }
        const ImageView& src = active ? *f.bandSource : cur;

        // Sample positions (pixel index space). Positive shift moves the
        // picture to the right, so we read from the left.
        const double sy = y - f.jy;

        float* outRow = f.dst.pixel(winX1, y);
        for (int x = winX1; x < winX2; ++x) {
            const double sx = x - f.jx - rowShift;
            float px[4];
            // --- 3. RGB misalignment (alpha follows the geometry) --------
            px[0] = sampleChannel(src, sx - rOff, sy, 0, f.bilinear);
            px[1] = sampleChannel(src, sx - gOff, sy, 1, f.bilinear);
            px[2] = sampleChannel(src, sx - bOff, sy, 2, f.bilinear);
            px[3] = sampleChannel(src, sx, sy, 3, f.bilinear);

            // --- 4. ghosting / trails ------------------------------------
            if (f.ghostMix > 0.0) {
                float g[4] = {0, 0, 0, 0};
                float wsum = 0.0f;
                const int n = std::max(in.numEchoes, 1);
                float w = 1.0f;
                for (int k = 0; k < n; ++k) {
                    const ImageView& e = in.numEchoes > 0 ? in.echoes[k] : cur;
                    float t[4];
                    sampleRGBA(e, sx - ghostStep * (k + 1), sy, f.bilinear, t);
                    for (int c = 0; c < 4; ++c) g[c] += t[c] * w;
                    wsum += w;
                    w *= 0.6f;
                }
                const float gm = static_cast<float>(f.ghostMix);
                for (int c = 0; c < 4; ++c) px[c] = lerpf(px[c], g[c] / wsum, gm);
            }

            float* o = outRow + (x - winX1) * 4;
            o[0] = px[0]; o[1] = px[1]; o[2] = px[2]; o[3] = px[3];
        }

        // --- 5. horizontal streaks (in-place along the row) --------------
        if (f.streaksOn) {
            const std::int64_t group =
                floorToInt(y / std::max(1.0, 3.0 * f.ps));
            const double m = hashNoise(seed, f.key, group, 5);
            double strength = 0.0;
            if (m < 0.18) strength = f.streakStrength * (1.0 - m / 0.18 * 0.5);
            else if (s.quality != Quality::Draft) strength = f.streakStrength * 0.15;
            if (strength > 0.0) {
                const float st = static_cast<float>(clamp01(strength));
                const float d = static_cast<float>(f.streakDecay);
                float acc[3] = {outRow[0], outRow[1], outRow[2]};
                for (int x = winX1; x < winX2; ++x) {
                    float* o = outRow + (x - winX1) * 4;
                    for (int c = 0; c < 3; ++c) {
                        acc[c] = acc[c] * d + o[c] * (1.0f - d);
                        const float smear = std::max(o[c], acc[c]);
                        o[c] = lerpf(o[c], smear, st);
                    }
                }
            }
        }

        // --- 6. blend with the original ---------------------------------
        if (blend < 1.0f) {
            const int cy = std::clamp(y, cur.y1, cur.y2 - 1);
            for (int x = winX1; x < winX2; ++x) {
                const float* orig = cur.pixel(std::clamp(x, cur.x1, cur.x2 - 1), cy);
                float* o = outRow + (x - winX1) * 4;
                for (int c = 0; c < 4; ++c) o[c] = lerpf(orig[c], o[c], blend);
            }
        }
    }
}

}  // namespace

float hashNoise(std::uint32_t seed, std::int64_t a, std::int64_t b,
                std::uint32_t channel) {
    // Integer-only mixing (based on the "lowbias32" / murmur finalizer).
    // Identical results on every CPU, compiler and thread.
    std::uint64_t h = 0x9E3779B97F4A7C15ull ^ seed;
    auto mix = [&h](std::uint64_t v) {
        h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h ^= h >> 33;
        h *= 0xFF51AFD7ED558CCDull;
        h ^= h >> 33;
        h *= 0xC4CEB9FE1A85EC53ull;
        h ^= h >> 33;
    };
    mix(static_cast<std::uint64_t>(a));
    mix(static_cast<std::uint64_t>(b));
    mix(channel);
    return static_cast<float>((h >> 40) * (1.0 / 16777216.0));  // 24 bit
}

Settings presetSubtle() { return Settings{}; }

Settings presetMusicVideo() {
    Settings s;
    s.timingOffset = 4;
    s.hDisplacement = 140.0;
    s.bandSize = 70.0;
    s.bandDensity = 0.45;
    s.rgbSplit = 12.0;
    s.ghostAmount = 0.55;
    s.trailLength = 3;
    s.ghostShift = 40.0;
    s.jitterAmount = 8.0;
    s.streakEnable = true;
    s.streakAmount = 0.6;
    s.streakLength = 220.0;
    s.quality = Quality::Best;
    return s;
}

int echoFramesNeeded(const Settings& s) {
    if (s.timingOffset == 0) return 0;
    int n = 1;  // bands always use the first time-shifted frame
    if (s.ghostAmount > 0.0) n = std::clamp(s.trailLength, 1, kMaxTrailFrames);
    if (s.quality == Quality::Draft) n = std::min(n, 1);
    else if (s.quality == Quality::Normal) n = std::min(n, 2);
    return n;
}

bool isIdentity(const Settings& s) {
    if (!s.enabled || s.blend <= 0.0 || s.masterIntensity <= 0.0) return true;
    const bool anything = s.hDisplacement != 0.0 || s.rgbSplit != 0.0 ||
                          s.ghostAmount > 0.0 || s.jitterAmount != 0.0 ||
                          (s.streakEnable && s.streakAmount > 0.0);
    return !anything;
}

bool render(const Settings& s, const FrameInputs& in,
            const MutableImageView& dst, int winX1, int winY1, int winX2,
            int winY2, double time, double pixelScale, int numThreads) {
    if (!in.current.valid() || dst.data == nullptr) return false;
    // Never write outside the destination image.
    winX1 = std::max(winX1, dst.x1); winX2 = std::min(winX2, dst.x2);
    winY1 = std::max(winY1, dst.y1); winY2 = std::min(winY2, dst.y2);
    if (winX2 <= winX1 || winY2 <= winY1) return true;

    FrameInputs inputs = in;
    inputs.numEchoes = std::clamp(inputs.numEchoes, 0, kMaxTrailFrames);
    for (int k = 0; k < inputs.numEchoes; ++k)
        if (!inputs.echoes[k].valid()) inputs.echoes[k] = inputs.current;

    // Disabled: plain copy (the host normally skips us via isIdentity).
    Settings eff = s;
    if (!s.enabled) {
        eff.masterIntensity = 0.0;
        eff.blend = 0.0;
    }

    FrameState f;
    f.s = &eff;
    f.in = &inputs;
    f.dst = dst;
    f.bilinear = eff.quality != Quality::Draft;
    f.I = std::max(0.0, eff.masterIntensity);
    f.ps = pixelScale > 0.0 ? pixelScale : 1.0;
    const std::uint32_t seed = static_cast<std::uint32_t>(eff.seed);

    // --- 1. pattern clock + whole-frame jitter --------------------------
    const double clock = eff.jitterSpeed > 0.0 ? time * eff.jitterSpeed : 0.0;
    f.key = floorToInt(clock + 1e-9);
    const double frac = smoothstep(clamp01(clock - static_cast<double>(f.key)));
    auto jitterAxis = [&](std::uint32_t ch) {
        const double a = hashNoise(seed, f.key, 0, ch);
        const double b = hashNoise(seed, f.key + 1, 0, ch);
        return (a + (b - a) * frac) * 2.0 - 1.0;
    };
    const double jit = eff.jitterAmount * f.I * f.ps;
    f.jx = jitterAxis(10) * jit;
    f.jy = jitterAxis(11) * jit * 0.35;

    f.bandH = std::max(1.0, eff.bandSize * f.ps);
    f.bandPhase = hashNoise(seed, f.key, 0, 12) * f.bandH * 4.0;
    f.ghostMix = clamp01(eff.ghostAmount * f.I);
    f.streaksOn = eff.streakEnable && eff.streakAmount > 0.0 && f.I > 0.0;
    f.streakStrength = eff.streakAmount * f.I;
    f.streakDecay = std::exp(-1.0 / std::max(1.0, eff.streakLength * f.ps));
    f.bandSource = inputs.numEchoes > 0 ? &inputs.echoes[0] : &inputs.current;

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
