// preview.cpp - renders still previews of the look without DaVinci Resolve.
// Input: raw float32 RGBA file. Motion is simulated by panning the picture
// `pan` pixels per frame, so the time-shifted frames really differ.
// Usage: preview in.raw out.raw width height amount distance direction distortion offset pan
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "TimingShiftCore.h"

int main(int argc, char** argv) {
    if (argc < 11) { std::puts("usage: preview in.raw out.raw w h amount distance direction distortion offset pan"); return 2; }
    const int w = std::atoi(argv[3]), h = std::atoi(argv[4]);
    tsfx::Settings s;
    s.ghostAmount = std::atof(argv[5]);
    s.ghostDistance = std::atof(argv[6]);
    s.ghostDirection = std::atof(argv[7]);
    s.ghostStretch = std::atof(argv[8]);
    s.timingOffset = std::atoi(argv[9]);
    const double pan = std::atof(argv[10]), t = 12;
    std::vector<float> src(size_t(w) * h * 4);
    FILE* f = std::fopen(argv[1], "rb");
    if (!f || std::fread(src.data(), 4, src.size(), f) != src.size()) return 1;
    std::fclose(f);
    // Build a "frame" at time ft by panning the source image.
    auto frame = [&](double ft) {
        std::vector<float> img(src.size());
        const int dx = int(pan * ft);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int sx = x - dx; sx = sx < 0 ? 0 : (sx >= w ? w - 1 : sx);
                for (int c = 0; c < 4; ++c) img[(size_t(y) * w + x) * 4 + c] = src[(size_t(y) * w + sx) * 4 + c];
            }
        return img;
    };
    // Images are stored top-down here, OFX is bottom-up: flip via negative rowBytes.
    auto view = [&](const std::vector<float>& v) {
        return tsfx::ImageView{v.data() + size_t(h - 1) * w * 4, 0, 0, w, h, -std::ptrdiff_t(w) * 16};
    };
    std::vector<float> cur = frame(t);
    std::vector<float> ghost = frame(t - s.timingOffset);
    tsfx::FrameInputs in;
    in.current = view(cur);
    in.ghost = view(ghost);
    std::vector<float> out(src.size());
    tsfx::MutableImageView dst{out.data() + size_t(h - 1) * w * 4, 0, 0, w, h, -std::ptrdiff_t(w) * 16};
    tsfx::render(s, in, dst, 0, 0, w, h, w / 1920.0);
    f = std::fopen(argv[2], "wb");
    std::fwrite(out.data(), 4, out.size(), f);
    std::fclose(f);
    return 0;
}
