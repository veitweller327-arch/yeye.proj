// core_tests.cpp - unit tests for the host-independent ghost effect core.
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

static Image run(const Settings& s, const FrameInputs& in, int w, int h, double ps, int threads) {
    Image out(w, h, -1, -1, -1, -1);
    render(s, in, out.mview(), 0, 0, w, h, ps, threads);
    return out;
}

static bool same(const Image& a, const Image& b) {
    return a.px.size() == b.px.size() &&
           std::memcmp(a.px.data(), b.px.data(), a.px.size() * sizeof(float)) == 0;
}

static void testIdentity() {
    std::puts("amount 0 -> exact copy; identity detection");
    Image src = pattern(64, 48);
    FrameInputs in; in.current = src.view();
    Settings s; s.ghostAmount = 0;
    CHECK(isIdentity(s));
    CHECK(same(run(s, in, 64, 48, 1, 1), src));
    s = Settings{}; s.timingOffset = 0; s.ghostDistance = 0;
    CHECK(isIdentity(s));
    CHECK(!isIdentity(Settings{}));
    CHECK(needsGhostFrame(Settings{}));
    s = Settings{}; s.timingOffset = 0;
    CHECK(!needsGhostFrame(s));
}

static void testDeterminism() {
    std::puts("deterministic: same result twice and for 1 vs 8 threads");
    Image src = pattern(200, 120), g = pattern(200, 120);
    for (float& v : g.px) v = 1.0f - v;
    FrameInputs in; in.current = src.view(); in.ghost = g.view();
    Settings s; s.ghostStretch = 0.6;
    Image a = run(s, in, 200, 120, 1, 1);
    CHECK(same(a, run(s, in, 200, 120, 1, 1)));
    CHECK(same(a, run(s, in, 200, 120, 1, 8)));
}

static void testNoBlackBordersAndAlpha() {
    std::puts("flat input stays flat (no black borders, no NaN, alpha kept)");
    Image src(160, 90, 0.5f, 0.4f, 0.3f, 0.25f);
    FrameInputs in; in.current = src.view(); in.ghost = src.view();
    Settings s; s.ghostAmount = 1; s.ghostDistance = 300; s.ghostStretch = 1; s.ghostDirection = 33;
    Image out = run(s, in, 160, 90, 1, 4);
    double maxErr = 0;
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 160; ++x)
            for (int c = 0; c < 4; ++c) {
                CHECK(std::isfinite(out.at(x, y)[c]));
                maxErr = std::fmax(maxErr, std::fabs(out.at(x, y)[c] - src.at(x, y)[c]));
            }
    std::printf("  max deviation: %g\n", maxErr);
    CHECK(maxErr < 1e-5);
}

static void testAmountAndTimeSource() {
    std::puts("ghost amount mixes the time-shifted frame");
    Image cur(100, 100, 0, 0, 0, 1), old(100, 100, 1, 1, 1, 1);
    FrameInputs in; in.current = cur.view(); in.ghost = old.view();
    Settings s; s.ghostStretch = 0;
    for (double a : {0.25, 0.5, 1.0}) {
        s.ghostAmount = a;
        Image o = run(s, in, 100, 100, 1, 1);
        CHECK(std::fabs(o.at(50, 50)[0] - float(a)) < 1e-6);
    }
    // Missing ghost frame -> falls back to the current frame (no failure).
    in.ghost = ImageView{};
    s.ghostAmount = 1; s.ghostDistance = 0;
    CHECK(same(run(s, in, 100, 100, 1, 1), cur));
}

// Finds where a single bright dot ended up in the ghost.
static void dotPosition(const Image& img, double& cx, double& cy) {
    double sx = 0, sy = 0, sw = 0;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const float v = img.px[(size_t(y) * img.w + x) * 4];
            sx += v * x; sy += v * y; sw += v;
        }
    cx = sx / sw; cy = sy / sw;
}

static void testDirectionDistanceStretch() {
    std::puts("direction / distance / distortion move the ghost correctly");
    const int W = 200, H = 200;
    Image dot(W, H, 0, 0, 0, 1);
    dot.at(100, 100)[0] = 1;
    FrameInputs in; in.current = Image(W, H, 0, 0, 0, 1).view();
    Image blank(W, H, 0, 0, 0, 1);
    in.current = blank.view();
    in.ghost = dot.view();
    Settings s; s.ghostAmount = 1; s.ghostDistance = 40; s.ghostStretch = 0;
    struct { double dir, ex, ey; } cases[] = {{0, 140, 100}, {90, 100, 140}, {180, 60, 100}, {-90, 100, 60}};
    for (auto& c : cases) {
        s.ghostDirection = c.dir;
        double x, y;
        dotPosition(run(s, in, W, H, 1, 2), x, y);
        std::printf("  direction %4.0f deg -> ghost at (%.1f, %.1f)\n", c.dir, x, y);
        CHECK(std::fabs(x - c.ex) < 0.5 && std::fabs(y - c.ey) < 0.5);
    }
    // Distortion: the dot becomes a streak from 0.5*40 to 40 px to the right.
    s.ghostDirection = 0; s.ghostStretch = 0.5;
    Image o = run(s, in, W, H, 1, 2);
    int minX = W, maxX = -1;
    for (int x = 0; x < W; ++x) if (o.at(x, 100)[0] > 1e-4f) { minX = std::min(minX, x); maxX = std::max(maxX, x); }
    std::printf("  distortion 0.5 -> streak x = %d..%d\n", minX, maxX);
    CHECK(minX >= 119 && minX <= 121 && maxX >= 139 && maxX <= 141);
    // Resolution independence: UHD (pixelScale 2) moves twice as far.
    s.ghostStretch = 0;
    Image dot2(2 * W, 2 * H, 0, 0, 0, 1), blank2(2 * W, 2 * H, 0, 0, 0, 1);
    dot2.at(200, 200)[0] = 1;
    FrameInputs in2; in2.current = blank2.view(); in2.ghost = dot2.view();
    double x, y;
    dotPosition(run(s, in2, 2 * W, 2 * H, 2, 2), x, y);
    CHECK(std::fabs(x - 280) < 0.5 && std::fabs(y - 200) < 0.5);
}

static void testRenderWindow() {
    std::puts("only pixels inside the render window are written");
    Image src = pattern(80, 60);
    FrameInputs in; in.current = src.view();
    Image out(80, 60, -7, -7, -7, -7);
    render(Settings{}, in, out.mview(), 10, 10, 30, 20, 1, 3);
    CHECK(out.at(9, 15)[0] == -7 && out.at(30, 15)[0] == -7 && out.at(15, 20)[0] == -7);
    CHECK(out.at(10, 10)[0] != -7 && out.at(29, 19)[3] != -7);
}

static void benchmark() {
    std::puts("benchmark (CPU, all threads):");
    for (int k = 1; k <= 2; ++k) {
        const int w = 1920 * k, h = 1080 * k;
        Image src = pattern(w, h);
        FrameInputs in; in.current = src.view(); in.ghost = src.view();
        for (double st : {0.0, 0.3, 1.0}) {
            Settings s; s.ghostStretch = st;
            Image out(w, h, 0, 0, 0, 0);
            render(s, in, out.mview(), 0, 0, w, h, k, 0);  // warm-up
            auto t0 = std::chrono::steady_clock::now();
            const int reps = 3;
            for (int r = 0; r < reps; ++r) render(s, in, out.mview(), 0, 0, w, h, k, 0);
            double ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - t0).count() / reps;
            std::printf("  %dx%d distortion=%.1f: %.1f ms/frame\n", w, h, st, ms);
        }
    }
}

int main() {
    testIdentity();
    testDeterminism();
    testNoBlackBordersAndAlpha();
    testAmountAndTimeSource();
    testDirectionDistanceStretch();
    testRenderWindow();
    benchmark();
    if (gFailures) { std::printf("%d check(s) FAILED\n", gFailures); return 1; }
    std::puts("ALL CORE TESTS PASSED");
    return 0;
}
