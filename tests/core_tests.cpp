// core_tests.cpp - unit tests for the host-independent image core.
// Build + run:  cmake -S . -B build && cmake --build build && ctest --test-dir build
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "TimingShiftCore.h"

using namespace tsfx;

static int gFailures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("  FAILED: %s (line %d)\n", #cond, __LINE__);    \
            ++gFailures;                                                  \
        }                                                                 \
    } while (0)

// Bottom-up image like OpenFX: row 0 is the bottom row.
struct Image {
    int w, h;
    std::vector<float> px;
    Image(int w_, int h_, float r, float g, float b, float a) : w(w_), h(h_), px(size_t(w_) * h_ * 4) {
        for (size_t i = 0; i < px.size(); i += 4) { px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = a; }
    }
    float* at(int x, int y) { return &px[(size_t(y) * w + x) * 4]; }
    ImageView view() const { return ImageView{px.data(), 0, 0, w, h, std::ptrdiff_t(w) * 16}; }
    MutableImageView mview() { return MutableImageView{px.data(), 0, 0, w, h, std::ptrdiff_t(w) * 16}; }
};

static Image pattern(int w, int h) {
    Image img(w, h, 0, 0, 0, 1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float* p = img.at(x, y);
            p[0] = float((x * 7 + y * 3) % 255) / 255.0f;
            p[1] = float((x / 8 + y / 8) % 2);
            p[2] = float(y) / float(h);
            p[3] = (x > w / 3) ? 1.0f : 0.5f;
        }
    return img;
}

static Image run(const Settings& s, const FrameInputs& in, int w, int h, double t = 1,
                 double ps = 1, int threads = 2) {
    Image out(w, h, -1, -1, -1, -1);
    render(s, in, out.mview(), 0, 0, w, h, t, ps, threads);
    return out;
}

static bool same(const Image& a, const Image& b) {
    return a.px.size() == b.px.size() &&
           std::memcmp(a.px.data(), b.px.data(), a.px.size() * sizeof(float)) == 0;
}

static double meanAbsDiff(const Image& a, const Image& b) {
    double d = 0;
    for (size_t i = 0; i < a.px.size(); ++i) d += std::fabs(a.px[i] - b.px[i]);
    return d / a.px.size();
}

// Streak length in px for a given strength at pixelScale 1.
static double strengthFor(double px) { return px / kStreakLengthAtStrength1; }

static void testGammaRoundTrip() {
    std::puts("input gamma: decode + encode is lossless for every curve");
    for (int g = 0; g < kNumGammas; ++g) {
        double maxErr = 0;
        for (float v = -0.05f; v <= 1.2f; v += 0.001f) {
            const float r = fromLinear(toLinear(v, Gamma(g)), Gamma(g));
            maxErr = std::fmax(maxErr, std::fabs(r - v));
        }
        std::printf("  gamma %d max round-trip error %.2g\n", g, maxErr);
        CHECK(maxErr < 1e-4);
    }
    // Known points: 18% grey
    CHECK(std::fabs(fromLinear(0.18f, Gamma::SonySLog3) - 420.0f / 1023.0f) < 1e-4);
    CHECK(std::fabs(fromLinear(0.18f, Gamma::ArriLogC3) - 0.391007f) < 1e-3);
    CHECK(std::fabs(fromLinear(0.18f, Gamma::ArriLogC4) - 0.278396f) < 1e-3);
    CHECK(std::fabs(fromLinear(0.18f, Gamma::DaVinciIntermediate) - 0.336043f) < 1e-3);
}

static void testLutAccuracy() {
    std::puts("fast gamma tables match the exact curves in the render path");
    const int w = 1500;
    Image ramp(w, 1, 0, 0, 0, 1);
    for (int x = 0; x < w; ++x) { float* p = ramp.at(x, 0); p[0] = -0.1f + 1.4f * x / (w - 1); p[1] = p[0] * 0.5f; p[2] = p[0] * 0.9f; }
    FrameInputs in; in.current = ramp.view(); in.ghost = ramp.view();
    for (int g = 0; g < kNumGammas; ++g) {
        // Ghost of the same frame at distance 0 = pure decode/encode round trip.
        Settings s; s.inputGamma = Gamma(g); s.blurStrength = 0; s.ghostAmount = 1; s.ghostDistance = 0;
        Image o = run(s, in, w, 1);
        double maxErr = 0;
        for (size_t i = 0; i < o.px.size(); ++i) maxErr = std::fmax(maxErr, std::fabs(o.px[i] - ramp.px[i]));
        std::printf("  gamma %d max error %.2g\n", g, maxErr);
        CHECK(maxErr < 2e-4);
    }
}

static void testIdentity() {
    std::puts("strength 0 + no ghost -> exact copy");
    Image src = pattern(64, 48);
    FrameInputs in; in.current = src.view();
    Settings s; s.blurStrength = 0;
    CHECK(isIdentity(s, 1));
    CHECK(same(run(s, in, 64, 48), src));
    CHECK(!isIdentity(Settings{}, 1));
    s.ghostAmount = 0.5;
    CHECK(!isIdentity(s, 1));
    CHECK(!needsGhostFrame(Settings{}));  // ghost is off by default
    CHECK(needsGhostFrame(s));
}

static void testDeterminism() {
    std::puts("deterministic: same result twice and for 1 vs 8 threads (both blur paths)");
    Image src = pattern(160, 120);
    FrameInputs in; in.current = src.view();
    for (double angle : {90.0, 30.0}) {
        Settings s; s.blurAngle = angle; s.blurStrength = strengthFor(25);
        Image a = run(s, in, 160, 120, 3, 1, 1);
        CHECK(same(a, run(s, in, 160, 120, 3, 1, 1)));
        CHECK(same(a, run(s, in, 160, 120, 3, 1, 8)));
    }
}

static void testFlatAndAlpha() {
    std::puts("flat input stays flat (no black borders, no NaN, alpha kept)");
    for (int e = 0; e < 2; ++e)
        for (double angle : {90.0, 37.0}) {
            Image src(120, 80, 0.5f, 0.4f, 0.3f, 0.25f);
            FrameInputs in; in.current = src.view();
            Settings s; s.edges = Edges(e); s.blurAngle = angle; s.symmetricBlur = true;
            s.blurStrength = 2.0; s.ghostAmount = 0.5; s.ghostDistance = 200;
            Image out = run(s, in, 120, 80);
            double maxErr = 0;
            for (size_t i = 0; i < out.px.size(); ++i) {
                CHECK(std::isfinite(out.px[i]));
                maxErr = std::fmax(maxErr, std::fabs(out.px[i] - src.px[i]));
            }
            CHECK(maxErr < 1e-4);
        }
}

// Single bright dot on black, linear gamma so values are easy to reason about.
static Image dotImage(int w, int h, int x, int y) {
    Image img(w, h, 0, 0, 0, 1);
    img.at(x, y)[0] = 1;
    return img;
}

static void testStreakDirection() {
    std::puts("streak goes in the Blur Angle direction; symmetric goes both ways");
    Image dot = dotImage(100, 100, 50, 50);
    FrameInputs in; in.current = dot.view();
    Settings s; s.inputGamma = Gamma::Linear; s.blurStrength = strengthFor(20);
    s.edges = Edges::Replicate;
    Image up = run(s, in, 100, 100);
    CHECK(up.at(50, 60)[0] > 0.01f);    // above the dot
    CHECK(up.at(50, 70)[0] > 0.01f);    // end of the streak
    CHECK(up.at(50, 72)[0] == 0.0f);    // beyond the streak
    CHECK(up.at(50, 45)[0] == 0.0f);    // nothing below
    CHECK(up.at(50, 50)[0] > up.at(50, 60)[0]);  // sharp image stays strongest
    s.blurAngle = 0;
    Image right = run(s, in, 100, 100);
    CHECK(right.at(60, 50)[0] > 0.01f && right.at(40, 50)[0] == 0.0f);
    s.blurAngle = 90; s.symmetricBlur = true;
    Image sym = run(s, in, 100, 100);
    CHECK(sym.at(50, 60)[0] > 0.01f && sym.at(50, 40)[0] > 0.01f);
    // Energy is preserved (blur only redistributes light).
    double sum = 0;
    for (int y = 0; y < 100; ++y) for (int x = 0; x < 100; ++x) sum += up.at(x, y)[0];
    CHECK(std::fabs(sum - 1.0) < 1e-3);
}

static void testFastPathMatchesGeneral() {
    std::puts("vertical fast path matches the any-angle path");
    Image src = pattern(160, 120);
    FrameInputs in; in.current = src.view();
    Settings s; s.blurStrength = strengthFor(30);
    Image a = run(s, in, 160, 120);
    s.blurAngle = 90.0001;  // forces the general path
    Image b = run(s, in, 160, 120);
    const double d = meanAbsDiff(a, b);
    std::printf("  mean difference: %.4f\n", d);
    CHECK(d < 0.01);
}

static void testEdges() {
    std::puts("Wrap-Around continues at the opposite edge, Replicate does not");
    Image dot = dotImage(60, 60, 30, 57);
    FrameInputs in; in.current = dot.view();
    Settings s; s.inputGamma = Gamma::Linear; s.blurStrength = strengthFor(10);
    s.edges = Edges::WrapAround;
    Image wrap = run(s, in, 60, 60);
    CHECK(wrap.at(30, 2)[0] > 0.01f);   // came around from the top
    s.edges = Edges::Replicate;
    Image rep = run(s, in, 60, 60);
    CHECK(rep.at(30, 2)[0] == 0.0f);
}

static void testShake() {
    std::puts("shake: strength stays in [min,max], is smooth, deterministic, reseedable");
    Settings s; s.shake = true; s.strengthMin = 0.5; s.strengthMax = 1.0; s.shakeSmoothness = 5;
    double lo = 9, hi = -9, maxStep = 0, prev = effectiveStrength(s, 0);
    for (int t = 0; t < 500; ++t) {
        const double v = effectiveStrength(s, t);
        lo = std::fmin(lo, v); hi = std::fmax(hi, v);
        maxStep = std::fmax(maxStep, std::fabs(v - prev));
        prev = v;
        CHECK(v == effectiveStrength(s, t));
    }
    std::printf("  range %.3f..%.3f, biggest frame-to-frame step %.3f\n", lo, hi, maxStep);
    CHECK(lo >= 0.5 && hi <= 1.0 && hi - lo > 0.3);
    CHECK(maxStep < 0.3);
    Settings r = s; r.seed = 1;
    bool differs = false;
    for (int t = 0; t < 50; ++t) differs |= effectiveStrength(s, t) != effectiveStrength(r, t);
    CHECK(differs);
    s.shake = false; s.blurStrength = 0.7;
    CHECK(effectiveStrength(s, 3) == 0.7);
}

static void testCleanup() {
    std::puts("cleanup keeps dark areas clean");
    Image img(40, 40, 0.05f, 0.05f, 0.05f, 1);
    for (int y = 0; y < 10; ++y) for (int x = 0; x < 40; ++x) { float* p = img.at(x, y); p[0] = p[1] = p[2] = 1; }
    FrameInputs in; in.current = img.view();
    Settings s; s.blurStrength = strengthFor(20); s.edges = Edges::Replicate;
    Image dirty = run(s, in, 40, 40);
    CHECK(dirty.at(20, 15)[0] > 0.2f);  // bright streak into the dark area
    s.cleanupLow = 0.1; s.cleanupHigh = 0.2;
    Image clean = run(s, in, 40, 40);
    CHECK(std::fabs(clean.at(20, 15)[0] - 0.05f) < 1e-6);
}

static void testGhost() {
    std::puts("ghost: time-shifted copy, moved by distance in direction");
    Image cur(100, 100, 0, 0, 0, 1);
    Image old = dotImage(100, 100, 30, 50);
    FrameInputs in; in.current = cur.view(); in.ghost = old.view();
    Settings s; s.inputGamma = Gamma::Linear; s.blurStrength = 0;
    s.ghostAmount = 1.0; s.ghostDistance = 40; s.ghostDirection = 0;
    Image o = run(s, in, 100, 100);
    CHECK(std::fabs(o.at(70, 50)[0] - 1.0f) < 1e-5);
    CHECK(o.at(30, 50)[0] == 0.0f);
    // resolution independence: pixelScale 2 moves twice as far
    Image cur2(200, 200, 0, 0, 0, 1), old2 = dotImage(200, 200, 60, 100);
    FrameInputs in2; in2.current = cur2.view(); in2.ghost = old2.view();
    Image o2 = run(s, in2, 200, 200, 1, 2);
    CHECK(std::fabs(o2.at(140, 100)[0] - 1.0f) < 1e-5);
}

static void testRenderWindow() {
    std::puts("only pixels inside the render window are written");
    Image src = pattern(80, 60);
    FrameInputs in; in.current = src.view();
    Image out(80, 60, -7, -7, -7, -7);
    render(Settings{}, in, out.mview(), 10, 10, 30, 20, 1, 1, 3);
    CHECK(out.at(9, 15)[0] == -7 && out.at(30, 15)[0] == -7 && out.at(15, 20)[0] == -7);
    CHECK(out.at(10, 10)[0] != -7 && out.at(29, 19)[3] != -7);
}

static void benchmark() {
    std::puts("benchmark (CPU, all threads, Blur Strength 1):");
    for (int k = 1; k <= 2; ++k) {
        const int w = 1920 * k, h = 1080 * k;
        Image src = pattern(w, h);
        FrameInputs in; in.current = src.view(); in.ghost = src.view();
        struct { const char* name; double angle; double ghost; } cases[] = {
            {"vertical", 90, 0}, {"vertical + ghost", 90, 0.4}, {"angle 75", 75, 0}};
        for (auto& c : cases) {
            Settings s; s.blurAngle = c.angle; s.ghostAmount = c.ghost;
            Image out(w, h, 0, 0, 0, 0);
            render(s, in, out.mview(), 0, 0, w, h, 1, k, 0);  // warm-up
            auto t0 = std::chrono::steady_clock::now();
            const int reps = 3;
            for (int r = 0; r < reps; ++r) render(s, in, out.mview(), 0, 0, w, h, r, k, 0);
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - t0).count() / reps;
            std::printf("  %dx%d %-17s %.1f ms/frame\n", w, h, c.name, ms);
        }
    }
}

int main() {
    testGammaRoundTrip();
    testLutAccuracy();
    testIdentity();
    testDeterminism();
    testFlatAndAlpha();
    testStreakDirection();
    testFastPathMatchesGeneral();
    testEdges();
    testShake();
    testCleanup();
    testGhost();
    testRenderWindow();
    benchmark();
    if (gFailures) { std::printf("%d check(s) FAILED\n", gFailures); return 1; }
    std::puts("ALL CORE TESTS PASSED");
    return 0;
}
