// TimingShiftCore.cpp
// -----------------------------------------------------------------------------
// CPU implementation of the Timing Shift FX look.
//
//   1. Linearise:  the source (and the optional ghost frame) are converted from
//                  the chosen Input Gamma to linear light.
//   2. Ghost:      a time-shifted copy, moved by Ghost Distance / Direction,
//                  is mixed in with Ghost Amount.
//   3. Streak:     a directional box blur of length Blur Strength along Blur
//                  Angle (one-sided like film transport, or symmetric). The
//                  result keeps part of the sharp image, like a camera whose
//                  shutter is partly open while the film moves.
//   4. Back:       converted back to the Input Gamma.
//   5. Cleanup:    dark areas (Cleanup Low..High) get the clean image back.
//
// Shake varies the blur strength randomly between Min and Max over time.
// Everything is deterministic: same settings + same frame = same image.
// -----------------------------------------------------------------------------
#include "TimingShiftCore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace tsfx {

float toLinearExact(float v, Gamma g);
float fromLinearExact(float v, Gamma g);

namespace {

constexpr double kPi = 3.14159265358979323846;
// Share of the sharp image in the streaked result. The rest is the smear.
constexpr float kSharpWeight = 0.55f;

inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline double smoothstep(double e0, double e1, double x) {
    const double t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0 - 2.0 * t);
}

inline int edgeIndex(int i, int lo, int hi, Edges mode) {  // hi exclusive
    if (mode == Edges::Replicate) return std::clamp(i, lo, hi - 1);
    const int n = hi - lo;
    int r = (i - lo) % n;
    if (r < 0) r += n;
    return r + lo;
}

// Full-frame float RGBA working buffer.
struct Buffer {
    int x1 = 0, y1 = 0, w = 0, h = 0;
    std::vector<float> px;
    void init(int x1_, int y1_, int w_, int h_) {
        x1 = x1_; y1 = y1_; w = w_; h = h_;
        px.assign(static_cast<size_t>(w) * h * 4, 0.0f);
    }
    float* at(int x, int y) { return &px[(static_cast<size_t>(y - y1) * w + (x - x1)) * 4]; }
    const float* at(int x, int y) const { return &px[(static_cast<size_t>(y - y1) * w + (x - x1)) * 4]; }
};

// Bilinear read with edge handling. Works for ImageView and Buffer.
template <class Img, class PixelFn>
inline void sampleBilinear(const Img& img, int ix1, int iy1, int ix2, int iy2,
                           PixelFn pixel, double fx, double fy, Edges mode,
                           float out[4]) {
    const double flx = std::floor(fx), fly = std::floor(fy);
    const float tx = static_cast<float>(fx - flx);
    const float ty = static_cast<float>(fy - fly);
    const int xa = edgeIndex(static_cast<int>(flx), ix1, ix2, mode);
    const int xb = edgeIndex(static_cast<int>(flx) + 1, ix1, ix2, mode);
    const int ya = edgeIndex(static_cast<int>(fly), iy1, iy2, mode);
    const int yb = edgeIndex(static_cast<int>(fly) + 1, iy1, iy2, mode);
    const float* a = pixel(img, xa, ya);
    const float* b = pixel(img, xb, ya);
    const float* d = pixel(img, xa, yb);
    const float* e = pixel(img, xb, yb);
    for (int c = 0; c < 4; ++c)
        out[c] = lerpf(lerpf(a[c], b[c], tx), lerpf(d[c], e[c], tx), ty);
}

inline void sampleBuf(const Buffer& b, double fx, double fy, Edges mode, float out[4]) {
    sampleBilinear(b, b.x1, b.y1, b.x1 + b.w, b.y1 + b.h,
                   [](const Buffer& im, int x, int y) { return im.at(x, y); },
                   fx, fy, mode, out);
}
inline void sampleView(const ImageView& v, double fx, double fy, Edges mode, float out[4]) {
    sampleBilinear(v, v.x1, v.y1, v.x2, v.y2,
                   [](const ImageView& im, int x, int y) { return im.pixel(x, y); },
                   fx, fy, mode, out);
}

// Runs fn(begin, end) over [begin, end) split across threads.
void parallelFor(int begin, int end, int threads,
                 const std::function<void(int, int)>& fn) {
    const int n = end - begin;
    if (n <= 0) return;
    threads = std::clamp(threads, 1, std::max(1, n / 8));
    if (threads == 1) { fn(begin, end); return; }
    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(threads - 1));
    const int chunk = (n + threads - 1) / threads;
    for (int t = 1; t < threads; ++t) {
        const int b = begin + t * chunk, e = std::min(end, b + chunk);
        if (b < e) pool.emplace_back(fn, b, e);
    }
    fn(begin, std::min(end, begin + chunk));
    for (auto& th : pool) th.join();
}

// Exact box blur along a vertical or horizontal line with running sums.
// Window: offsets s in [a, b] (pixels), reading src at p - sign * s.
void boxBlurAxis(const Buffer& src, Buffer& dst, bool vertical, int sign, int a,
                 int b, Edges mode, int threads) {
    const double inv = 1.0 / static_cast<double>(b - a + 1);
    const int X1 = src.x1, Y1 = src.y1, X2 = src.x1 + src.w, Y2 = src.y1 + src.h;
    if (vertical) {
        // Threads own column ranges; rows are walked top to bottom.
        parallelFor(X1, X2, threads, [&](int xb, int xe) {
            const int cols = xe - xb;
            std::vector<double> acc(static_cast<size_t>(cols) * 4, 0.0);
            // Unwrapped row range for output row y: [y - b, y - a] (sign>0)
            // or [y + a, y + b] (sign<0). Both shift by +1 per row.
            int lo = sign > 0 ? Y1 - b : Y1 + a;
            int hi = sign > 0 ? Y1 - a : Y1 + b;
            for (int r = lo; r <= hi; ++r) {
                const int yy = edgeIndex(r, Y1, Y2, mode);
                for (int i = 0; i < cols; ++i) {
                    const float* p = src.at(xb + i, yy);
                    for (int c = 0; c < 4; ++c) acc[i * 4 + c] += p[c];
                }
            }
            for (int y = Y1; y < Y2; ++y) {
                for (int i = 0; i < cols; ++i) {
                    float* o = dst.at(xb + i, y);
                    for (int c = 0; c < 4; ++c) o[c] = static_cast<float>(acc[i * 4 + c] * inv);
                }
                const int yin = edgeIndex(hi + 1, Y1, Y2, mode);
                const int yout = edgeIndex(lo, Y1, Y2, mode);
                for (int i = 0; i < cols; ++i) {
                    const float* pin = src.at(xb + i, yin);
                    const float* pout = src.at(xb + i, yout);
                    for (int c = 0; c < 4; ++c) acc[i * 4 + c] += pin[c] - pout[c];
                }
                ++lo; ++hi;
            }
        });
    } else {
        parallelFor(Y1, Y2, threads, [&](int yb, int ye) {
            for (int y = yb; y < ye; ++y) {
                double acc[4] = {0, 0, 0, 0};
                int lo = sign > 0 ? X1 - b : X1 + a;
                int hi = sign > 0 ? X1 - a : X1 + b;
                for (int r = lo; r <= hi; ++r) {
                    const float* p = src.at(edgeIndex(r, X1, X2, mode), y);
                    for (int c = 0; c < 4; ++c) acc[c] += p[c];
                }
                for (int x = X1; x < X2; ++x) {
                    float* o = dst.at(x, y);
                    for (int c = 0; c < 4; ++c) o[c] = static_cast<float>(acc[c] * inv);
                    const float* pin = src.at(edgeIndex(hi + 1, X1, X2, mode), y);
                    const float* pout = src.at(edgeIndex(lo, X1, X2, mode), y);
                    for (int c = 0; c < 4; ++c) acc[c] += pin[c] - pout[c];
                    ++lo; ++hi;
                }
            }
        });
    }
}

// Box blur along any angle: averages N = 2^n evenly spaced samples between
// start and start + length, built by n doubling passes (each pass averages the
// previous result with a shifted copy of itself).
void boxBlurAngle(const Buffer& src, Buffer& dst, double dx, double dy,
                  double start, double length, Edges mode, int threads) {
    int n = 1;
    // Sample spacing <= 1.5 px is enough: bilinear reads fill the gaps.
    while (((1 << n) - 1) * 1.5 < length && n < 11) ++n;
    const int N = 1 << n;
    const double step = length / (N - 1);
    Buffer tmp;
    tmp.init(src.x1, src.y1, src.w, src.h);
    const Buffer* in = &src;
    Buffer* bufs[2] = {&dst, &tmp};
    // Make sure the final pass lands in dst.
    int target = (n % 2 == 1) ? 0 : 1;
    const int Y1 = src.y1, Y2 = src.y1 + src.h, X1 = src.x1, X2 = src.x1 + src.w;
    for (int k = 0; k < n; ++k) {
        Buffer* out = bufs[target];
        const double m = static_cast<double>(1 << k) * step;  // shift of this pass
        const double s0 = (k == 0) ? start : 0.0;
        parallelFor(Y1, Y2, threads, [&](int yb, int ye) {
            for (int y = yb; y < ye; ++y)
                for (int x = X1; x < X2; ++x) {
                    float p[4], q[4];
                    if (k == 0) sampleBuf(*in, x - dx * s0, y - dy * s0, mode, p);
                    else { const float* c = in->at(x, y); p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = c[3]; }
                    sampleBuf(*in, x - dx * (s0 + m), y - dy * (s0 + m), mode, q);
                    float* o = out->at(x, y);
                    for (int c = 0; c < 4; ++c) o[c] = 0.5f * (p[c] + q[c]);
                }
        });
        in = out;
        target ^= 1;
    }
}

// Fast transfer functions via lookup tables (built per render, ~0.1 ms).
// Decode: uniform table over the encoded range. Encode: table over the 4th
// root of the linear value, which keeps the curves smooth near black.
// Values outside the tables fall back to the exact formulas.
struct GammaLut {
    static constexpr int N = 4096;
    static constexpr float kLo = -0.125f, kHi = 1.375f;
    Gamma g = Gamma::Linear;
    float sMax = 1.0f;
    std::vector<float> dec, enc;
    explicit GammaLut(Gamma gamma) : g(gamma) {
        if (g == Gamma::Linear) return;
        dec.resize(N + 1);
        for (int i = 0; i <= N; ++i) dec[i] = toLinearExact(kLo + (kHi - kLo) * i / N, g);
        sMax = std::sqrt(std::sqrt(std::max(1e-6f, dec[N])));
        enc.resize(N + 1);
        for (int i = 0; i <= N; ++i) {
            const double sv = static_cast<double>(sMax) * i / N;
            enc[i] = fromLinearExact(static_cast<float>(sv * sv * sv * sv), g);
        }
    }
    float toLin(float v) const {
        if (g == Gamma::Linear) return v;
        const float f = (v - kLo) * (N / (kHi - kLo));
        if (!(f >= 0.0f) || f >= static_cast<float>(N)) return toLinearExact(v, g);
        const int i = static_cast<int>(f);
        return lerpf(dec[i], dec[i + 1], f - static_cast<float>(i));
    }
    float fromLin(float x) const {
        if (g == Gamma::Linear) return x;
        if (!(x >= 0.0f)) return fromLinearExact(x, g);
        const float f = std::sqrt(std::sqrt(x)) * (N / sMax);
        if (f >= static_cast<float>(N)) return fromLinearExact(x, g);
        const int i = static_cast<int>(f);
        return lerpf(enc[i], enc[i + 1], f - static_cast<float>(i));
    }
};

double streakLengthPx(const Settings& s, double time, double ps) {
    return std::max(0.0, effectiveStrength(s, time)) * kStreakLengthAtStrength1 * ps;
}

}  // namespace

// ----------------------------------------------------------------- gamma
float toLinearExact(float vf, Gamma g) {
    const double v = vf;
    switch (g) {
        case Gamma::Linear: return vf;
        case Gamma::Rec709:
            return static_cast<float>(v >= 0 ? std::pow(v, 2.4) : -std::pow(-v, 2.4));
        case Gamma::SonySLog3:
            return static_cast<float>(v >= 171.2102946929 / 1023.0
                ? std::pow(10.0, (v * 1023.0 - 420.0) / 261.5) * (0.18 + 0.01) - 0.01
                : (v * 1023.0 - 95.0) * 0.01125 / (171.2102946929 - 95.0));
        case Gamma::ArriLogC3: {
            const double cut = 0.010591, a = 5.555556, b = 0.052272, c = 0.247190,
                         d = 0.385537, e = 5.367655, f = 0.092809;
            return static_cast<float>(v > e * cut + f ? (std::pow(10.0, (v - d) / c) - b) / a
                                                      : (v - f) / e);
        }
        case Gamma::ArriLogC4: {
            const double a = (std::pow(2.0, 18) - 16.0) / 117.45, b = (1023.0 - 95.0) / 1023.0,
                         c = 95.0 / 1023.0;
            const double s = (7.0 * std::log(2.0) * std::pow(2.0, 7.0 - 14.0 * c / b)) / (a * b);
            const double t = (std::pow(2.0, 14.0 * (-c / b) + 6.0) - 64.0) / a;
            return static_cast<float>(v >= 0 ? (std::pow(2.0, 14.0 * (v - c) / b + 6.0) - 64.0) / a
                                             : v * s + t);
        }
        case Gamma::DaVinciIntermediate: {
            const double A = 0.0075, B = 7.0, C = 0.07329248, M = 10.44426855, LOG_CUT = 0.02740668;
            return static_cast<float>(v <= LOG_CUT ? v / M : std::pow(2.0, v / C - B) - A);
        }
    }
    return vf;
}

float fromLinearExact(float xf, Gamma g) {
    const double x = xf;
    switch (g) {
        case Gamma::Linear: return xf;
        case Gamma::Rec709:
            return static_cast<float>(x >= 0 ? std::pow(x, 1.0 / 2.4) : -std::pow(-x, 1.0 / 2.4));
        case Gamma::SonySLog3:
            return static_cast<float>(x >= 0.01125
                ? (420.0 + std::log10((x + 0.01) / (0.18 + 0.01)) * 261.5) / 1023.0
                : (x * (171.2102946929 - 95.0) / 0.01125 + 95.0) / 1023.0);
        case Gamma::ArriLogC3: {
            const double cut = 0.010591, a = 5.555556, b = 0.052272, c = 0.247190,
                         d = 0.385537, e = 5.367655, f = 0.092809;
            return static_cast<float>(x > cut ? c * std::log10(a * x + b) + d : e * x + f);
        }
        case Gamma::ArriLogC4: {
            const double a = (std::pow(2.0, 18) - 16.0) / 117.45, b = (1023.0 - 95.0) / 1023.0,
                         c = 95.0 / 1023.0;
            const double s = (7.0 * std::log(2.0) * std::pow(2.0, 7.0 - 14.0 * c / b)) / (a * b);
            const double t = (std::pow(2.0, 14.0 * (-c / b) + 6.0) - 64.0) / a;
            return static_cast<float>(x >= t ? (std::log2(a * x + 64.0) - 6.0) / 14.0 * b + c
                                             : (x - t) / s);
        }
        case Gamma::DaVinciIntermediate: {
            const double A = 0.0075, B = 7.0, C = 0.07329248, M = 10.44426855, LIN_CUT = 0.00262409;
            return static_cast<float>(x <= LIN_CUT ? x * M : (std::log2(x + A) + B) * C);
        }
    }
    return xf;
}

float toLinear(float v, Gamma g) { return toLinearExact(v, g); }
float fromLinear(float v, Gamma g) { return fromLinearExact(v, g); }

// ----------------------------------------------------------------- helpers
double hashNoise(unsigned seed, long long a) {
    std::uint64_t h = 0x9E3779B97F4A7C15ull ^ seed;
    h ^= static_cast<std::uint64_t>(a) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

double effectiveStrength(const Settings& s, double time) {
    if (!s.shake) return s.blurStrength;
    const double period = std::max(1.0, s.shakeSmoothness);
    const double u = time / period;
    const double k = std::floor(u);
    const double f = u - k;
    const double a = hashNoise(static_cast<unsigned>(s.seed), static_cast<long long>(k));
    const double b = hashNoise(static_cast<unsigned>(s.seed), static_cast<long long>(k) + 1);
    const double t = f * f * (3.0 - 2.0 * f);  // smooth transition between random values
    const double n = a + (b - a) * t;
    return s.strengthMin + (s.strengthMax - s.strengthMin) * n;
}

bool needsGhostFrame(const Settings& s) {
    return s.ghostAmount > 0.0 && s.timingOffset != 0;
}

bool isIdentity(const Settings& s, double time) {
    const bool ghost = s.ghostAmount > 0.0 && (s.timingOffset != 0 || s.ghostDistance != 0.0);
    const bool streak = effectiveStrength(s, time) * kStreakLengthAtStrength1 >= 0.5;
    return !ghost && !streak;
}

// ----------------------------------------------------------------- render
bool render(const Settings& s, const FrameInputs& in, const MutableImageView& dst,
            int winX1, int winY1, int winX2, int winY2, double time,
            double pixelScale, int numThreads) {
    const ImageView& cur = in.current;
    if (!cur.valid() || dst.data == nullptr) return false;
    winX1 = std::max(winX1, dst.x1); winX2 = std::min(winX2, dst.x2);
    winY1 = std::max(winY1, dst.y1); winY2 = std::min(winY2, dst.y2);
    if (winX2 <= winX1 || winY2 <= winY1) return true;

    const int threads = numThreads > 0 ? numThreads
                                       : static_cast<int>(std::thread::hardware_concurrency());
    const double ps = pixelScale > 0.0 ? pixelScale : 1.0;
    const double len = streakLengthPx(s, time, ps);
    const bool streak = len >= 0.5;
    const float ghostMix = static_cast<float>(clamp01(s.ghostAmount));
    const bool ghost = ghostMix > 0.0f;

    // Nothing to do: exact copy (no gamma round trip).
    if (!streak && !ghost) {
        parallelFor(winY1, winY2, threads, [&](int yb, int ye) {
            for (int y = yb; y < ye; ++y)
                for (int x = winX1; x < winX2; ++x) {
                    const float* p = cur.pixel(std::clamp(x, cur.x1, cur.x2 - 1),
                                               std::clamp(y, cur.y1, cur.y2 - 1));
                    float* o = dst.pixel(x, y);
                    o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = p[3];
                }
        });
        return true;
    }

    // --- 1 + 2: linear working copy, ghost mixed in --------------------
    Buffer lin;
    lin.init(cur.x1, cur.y1, cur.x2 - cur.x1, cur.y2 - cur.y1);
    const ImageView& gsrc = in.ghost.valid() ? in.ghost : cur;
    const double grad = s.ghostDirection * kPi / 180.0;
    const double gdx = std::cos(grad) * s.ghostDistance * ps;
    const double gdy = std::sin(grad) * s.ghostDistance * ps;
    const GammaLut lut(s.inputGamma);
    parallelFor(cur.y1, cur.y2, threads, [&](int yb, int ye) {
        for (int y = yb; y < ye; ++y)
            for (int x = cur.x1; x < cur.x2; ++x) {
                const float* p = cur.pixel(x, y);
                float* o = lin.at(x, y);
                for (int c = 0; c < 3; ++c) o[c] = lut.toLin(p[c]);
                o[3] = p[3];
                if (ghost) {
                    float q[4];
                    sampleView(gsrc, x - gdx, y - gdy, s.edges, q);
                    for (int c = 0; c < 3; ++c) q[c] = lut.toLin(q[c]);
                    for (int c = 0; c < 4; ++c) o[c] = lerpf(o[c], q[c], ghostMix);
                }
            }
    });

    // --- 3: streak -------------------------------------------------------
    Buffer blurred;
    if (streak) {
        blurred.init(lin.x1, lin.y1, lin.w, lin.h);
        const double rad = s.blurAngle * kPi / 180.0;
        double dx = std::cos(rad), dy = std::sin(rad);
        if (std::fabs(dx) < 1e-9) dx = 0.0;
        if (std::fabs(dy) < 1e-9) dy = 0.0;
        if (dx == 0.0 || dy == 0.0) {
            const bool vertical = dx == 0.0;
            const int sign = vertical ? (dy > 0 ? 1 : -1) : (dx > 0 ? 1 : -1);
            const int L = std::max(1, static_cast<int>(std::lround(len)));
            boxBlurAxis(lin, blurred, vertical, sign, s.symmetricBlur ? -L : 0, L,
                        s.edges, threads);
        } else {
            const double total = s.symmetricBlur ? 2.0 * len : len;
            const double start = s.symmetricBlur ? -len : 0.0;
            boxBlurAngle(lin, blurred, dx, dy, start, total, s.edges, threads);
        }
    }

    // --- 4 + 5: back to the input gamma, cleanup, write -----------------
    const bool cleanup = s.cleanupHigh > 0.0 || s.cleanupLow > 0.0;
    parallelFor(winY1, winY2, threads, [&](int yb, int ye) {
        for (int y = yb; y < ye; ++y) {
            const int cy = std::clamp(y, cur.y1, cur.y2 - 1);
            for (int x = winX1; x < winX2; ++x) {
                const int cx = std::clamp(x, cur.x1, cur.x2 - 1);
                const float* l = lin.at(cx, cy);
                float v[4] = {l[0], l[1], l[2], l[3]};
                if (streak) {
                    const float* b = blurred.at(cx, cy);
                    for (int c = 0; c < 4; ++c) v[c] = kSharpWeight * v[c] + (1.0f - kSharpWeight) * b[c];
                }
                for (int c = 0; c < 3; ++c) v[c] = lut.fromLin(v[c]);
                const float* orig = cur.pixel(cx, cy);
                if (cleanup) {
                    const double Y = clamp01(0.2126 * orig[0] + 0.7152 * orig[1] + 0.0722 * orig[2]);
                    double m;
                    if (s.cleanupHigh > s.cleanupLow) m = 1.0 - smoothstep(s.cleanupLow, s.cleanupHigh, Y);
                    else m = Y < s.cleanupLow ? 1.0 : 0.0;
                    const float mf = static_cast<float>(m);
                    for (int c = 0; c < 4; ++c) v[c] = lerpf(v[c], orig[c], mf);
                }
                float* o = dst.pixel(x, y);
                o[0] = v[0]; o[1] = v[1]; o[2] = v[2]; o[3] = v[3];
            }
        }
    });
    return true;
}

}  // namespace tsfx
