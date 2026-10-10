// preview.cpp - renders still previews of the look without DaVinci Resolve.
// Input: raw float32 RGBA file (top-down). Motion is simulated by panning the
// picture `pan` pixels per frame, so the time-shifted ghost frame differs.
// Usage: preview in.raw out.raw width height [key=value ...]
//   keys: gamma strength angle symmetric edges cleanlow cleanhigh shake
//         smooth smin smax seed ghost gdist gdir offset pan frame
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "TimingShiftCore.h"

int main(int argc, char** argv) {
    if (argc < 5) { std::puts("usage: preview in.raw out.raw w h [key=value ...]"); return 2; }
    const int w = std::atoi(argv[3]), h = std::atoi(argv[4]);
    tsfx::Settings s;
    double pan = 4, t = 12;
    for (int i = 5; i < argc; ++i) {
        std::string kv = argv[i];
        const size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = kv.substr(0, eq);
        const double v = std::atof(kv.c_str() + eq + 1);
        if (k == "gamma") s.inputGamma = static_cast<tsfx::Gamma>(int(v));
        else if (k == "strength") s.blurStrength = v;
        else if (k == "angle") s.blurAngle = v;
        else if (k == "symmetric") s.symmetricBlur = v != 0;
        else if (k == "edges") s.edges = static_cast<tsfx::Edges>(int(v));
        else if (k == "cleanlow") s.cleanupLow = v;
        else if (k == "cleanhigh") s.cleanupHigh = v;
        else if (k == "shake") s.shake = v != 0;
        else if (k == "smooth") s.shakeSmoothness = v;
        else if (k == "smin") s.strengthMin = v;
        else if (k == "smax") s.strengthMax = v;
        else if (k == "seed") s.seed = int(v);
        else if (k == "ghost") s.ghostAmount = v;
        else if (k == "gdist") s.ghostDistance = v;
        else if (k == "gdir") s.ghostDirection = v;
        else if (k == "offset") s.timingOffset = int(v);
        else if (k == "pan") pan = v;
        else if (k == "frame") t = v;
    }
    std::vector<float> src(size_t(w) * h * 4);
    FILE* f = std::fopen(argv[1], "rb");
    if (!f || std::fread(src.data(), 4, src.size(), f) != src.size()) return 1;
    std::fclose(f);
    auto frame = [&](double ft) {
        std::vector<float> img(src.size());
        const int dx = int(pan * ft);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int sx = x - dx; sx = sx < 0 ? 0 : (sx >= w ? w - 1 : sx);
                std::memcpy(&img[(size_t(y) * w + x) * 4], &src[(size_t(y) * w + sx) * 4], 16);
            }
        return img;
    };
    // Images are stored top-down here, OFX is bottom-up: flip via negative rowBytes.
    auto view = [&](const std::vector<float>& v) {
        return tsfx::ImageView{v.data() + size_t(h - 1) * w * 4, 0, 0, w, h, -std::ptrdiff_t(w) * 16};
    };
    std::vector<float> cur = frame(t), ghost = frame(t - s.timingOffset);
    tsfx::FrameInputs in;
    in.current = view(cur);
    in.ghost = view(ghost);
    std::vector<float> out(src.size());
    tsfx::MutableImageView dst{out.data() + size_t(h - 1) * w * 4, 0, 0, w, h, -std::ptrdiff_t(w) * 16};
    tsfx::render(s, in, dst, 0, 0, w, h, t, w / 1920.0);
    f = std::fopen(argv[2], "wb");
    std::fwrite(out.data(), 4, out.size(), f);
    std::fclose(f);
    return 0;
}
