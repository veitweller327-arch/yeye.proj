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

// Deterministic test picture with structure in every channel.
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

static Settings strong() {
    Settings s;
    s.timingOffset = 3; s.hDisplacement = 120; s.bandDensity = 0.5; s.rgbSplit = 10;
    s.ghostAmount = 0.5; s.trailLength = 4; s.ghostShift = 30; s.jitterAmount = 6;
    s.streakEnable = true; s.streakAmount = 0.6; s.streakLength = 150;
    s.quality = Quality::Best;
    return s;
}

static Image run(const Settings& s, const FrameInputs& in, int w, int h, double t,
                 double ps, int threads) {
    Image out(w, h, -1, -1, -1, -1);
    render(s, in, out.mview(), 0, 0, w, h, t, ps, threads);
    return out;
}

static bool same(const Image& a, const Image& b) {
    return a.px.size() == b.px.size() &&
           std::memcmp(a.px.data(), b.px.data(), a.px.size() * sizeof(float)) == 0;
}

static void testHash() {
    std::puts("hash noise: deterministic, in [0,1)");
    for (int i = 0; i < 10000; ++i) {
        float v = hashNoise(42, i, -i, 3);
        CHECK(v >= 0.0f && v < 1.0f);
        CHECK(v == hashNoise(42, i, -i, 3));
    }
    CHECK(hashNoise(1, 5, 5, 0) != hashNoise(2, 5, 5, 0));
}

static void testIdentity() {
    std::puts("disabled / blend 0 / intensity 0 -> exact copy");
    Image src = pattern(64, 48);
    FrameInputs in; in.current = src.view();
    Settings s = strong();
    s.enabled = false;
    CHECK(isIdentity(s));
    CHECK(same(run(s, in, 64, 48, 10, 1, 1), src));
    s = strong(); s.blend = 0;
    CHECK(isIdentity(s));
    CHECK(same(run(s, in, 64, 48, 10, 1, 1), src));
    s = strong(); s.masterIntensity = 0;
    CHECK(isIdentity(s));
    CHECK(!isIdentity(strong()));
}

static void testDeterminism() {
    std::puts("deterministic: same result twice and for 1 vs 8 threads; seed matters");
    Image src = pattern(200, 120), e1 = pattern(200, 120);
    for (float& v : e1.px) v = 1.0f - v;
    FrameInputs in; in.current = src.view(); in.echoes[0] = e1.view(); in.numEchoes = 1;
    Settings s = strong();
    Image a = run(s, in, 200, 120, 17, 1, 1);
    Image b = run(s, in, 200, 120, 17, 1, 1);
    Image c = run(s, in, 200, 120, 17, 1, 8);
    CHECK(same(a, b));
    CHECK(same(a, c));
    s.seed = 2;
    CHECK(!same(a, run(s, in, 200, 120, 17, 1, 1)));
    // different frames give a different pattern (jitter speed 1)
    s.seed = 1;
    CHECK(!same(a, run(s, in, 200, 120, 18, 1, 1)));
    // jitter speed 0 + no jitter-dependent change: frame 17 == frame 18
    s.jitterSpeed = 0;
    CHECK(same(run(s, in, 200, 120, 17, 1, 1), run(s, in, 200, 120, 18, 1, 1)));
}

static void testNoBlackBordersAndAlpha() {
    std::puts("flat input stays flat (no black borders, no NaN, alpha kept)");
    Image src(160, 90, 0.5f, 0.4f, 0.3f, 0.25f);
    FrameInputs in; in.current = src.view(); in.echoes[0] = src.view();
    in.echoes[1] = src.view(); in.numEchoes = 2;
    Image out = run(strong(), in, 160, 90, 3, 1, 4);
    double maxErr = 0;
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 160; ++x) {
            const float* p = out.at(x, y);
            const float* q = src.at(x, y);
            for (int c = 0; c < 4; ++c) {
                CHECK(std::isfinite(p[c]));
                maxErr = std::fmax(maxErr, std::fabs(p[c] - q[c]));
            }
        }
    std::printf("  max deviation: %g\n", maxErr);
    CHECK(maxErr < 1e-5);
}

static void testTemporalSources() {
    std::puts("time-shifted frame shows in displaced bands / ghost");
    Image cur(100, 100, 0, 0, 0, 1), old(100, 100, 1, 1, 1, 1);
    FrameInputs in; in.current = cur.view(); in.echoes[0] = old.view(); in.numEchoes = 1;
    Settings s; s.jitterAmount = 0; s.rgbSplit = 0; s.ghostAmount = 0;
    s.bandDensity = 1.0;  // every band displaced -> every row from 'old'
    Image a = run(s, in, 100, 100, 5, 1, 1);
    CHECK(std::fabs(a.at(50, 50)[0] - 1.0f) < 1e-6 && std::fabs(a.at(3, 97)[1] - 1.0f) < 1e-6);
    s.bandDensity = 0.0;  // no bands -> current frame
    Image b = run(s, in, 100, 100, 5, 1, 1);
    CHECK(std::fabs(b.at(50, 50)[0]) < 1e-6);
    s.ghostAmount = 0.5; s.ghostShift = 0; s.trailLength = 1;
    Image c = run(s, in, 100, 100, 5, 1, 1);
    CHECK(std::fabs(c.at(50, 50)[0] - 0.5f) < 1e-6);
    s.blend = 0.5;
    Image d = run(s, in, 100, 100, 5, 1, 1);
    CHECK(std::fabs(d.at(50, 50)[0] - 0.25f) < 1e-6);
}

static void testEchoCount() {
    std::puts("echo frames needed respects quality");
    Settings s; s.timingOffset = 2; s.ghostAmount = 0.5; s.trailLength = 4;
    s.quality = Quality::Best;   CHECK(echoFramesNeeded(s) == 4);
    s.quality = Quality::Normal; CHECK(echoFramesNeeded(s) == 2);
    s.quality = Quality::Draft;  CHECK(echoFramesNeeded(s) == 1);
    s.timingOffset = 0;          CHECK(echoFramesNeeded(s) == 0);
}

static int edgeX(Image& img, int y) {
    for (int x = 0; x < img.w; ++x) if (img.at(x, y)[0] > 0.5f) return x;
    return -1;
}

static void testResolutionIndependence() {
    std::puts("displacement scales with resolution (HD vs UHD)");
    Settings s; s.timingOffset = 0; s.jitterAmount = 0; s.rgbSplit = 0;
    s.ghostAmount = 0; s.bandDensity = 1.0; s.hDisplacement = 100;
    for (int k = 1; k <= 2; ++k) {
        const int w = 1920 * k, h = 64 * k;
        Image src(w, h, 0, 0, 0, 1);
        for (int y = 0; y < h; ++y) for (int x = w / 2; x < w; ++x) src.at(x, y)[0] = 1;
        FrameInputs in; in.current = src.view();
        Image out = run(s, in, w, h, 1, k, 2);
        for (int y = 0; y < h; y += 7) {
            const int shift = edgeX(out, y) - w / 2;
            CHECK(shift >= int(100 * k * 0.35 * 0.92) - 1 && shift <= int(100 * k * 1.08) + 1);
        }
    }
}

static void testRenderWindow() {
    std::puts("only pixels inside the render window are written");
    Image src = pattern(80, 60);
    FrameInputs in; in.current = src.view();
    Image out(80, 60, -7, -7, -7, -7);
    render(strong(), in, out.mview(), 10, 10, 30, 20, 1, 1, 3);
    CHECK(out.at(9, 15)[0] == -7 && out.at(30, 15)[0] == -7 && out.at(15, 20)[0] == -7);
    CHECK(out.at(10, 10)[0] != -7 && out.at(29, 19)[3] != -7);
}

static void benchmark() {
    std::puts("benchmark (CPU, all threads):");
    for (int k = 1; k <= 2; ++k) {
        const int w = 1920 * k, h = 1080 * k;
        Image src = pattern(w, h);
        FrameInputs in; in.current = src.view();
        for (int e = 0; e < 4; ++e) in.echoes[e] = src.view();
        in.numEchoes = 4;
        for (Quality q : {Quality::Draft, Quality::Normal, Quality::Best}) {
            Settings s = strong(); s.quality = q;
            in.numEchoes = echoFramesNeeded(s);
            Image out(w, h, 0, 0, 0, 0);
            render(s, in, out.mview(), 0, 0, w, h, 1, k, 0);  // warm-up
            auto t0 = std::chrono::steady_clock::now();
            const int reps = 3;
            for (int r = 0; r < reps; ++r) render(s, in, out.mview(), 0, 0, w, h, r, k, 0);
            double ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - t0).count() / reps;
            std::printf("  %dx%d quality=%d echoes=%d: %.1f ms/frame\n", w, h, int(q),
                        in.numEchoes, ms);
        }
    }
}

int main() {
    testHash();
    testIdentity();
    testDeterminism();
    testNoBlackBordersAndAlpha();
    testTemporalSources();
    testEchoCount();
    testResolutionIndependence();
    testRenderWindow();
    benchmark();
    if (gFailures) { std::printf("%d check(s) FAILED\n", gFailures); return 1; }
    std::puts("ALL CORE TESTS PASSED");
    return 0;
}
