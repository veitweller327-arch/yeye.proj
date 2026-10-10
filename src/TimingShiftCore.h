// TimingShiftCore.h
// -----------------------------------------------------------------------------
// Host-independent image processing for "Timing Shift FX".
//
// The look recreates an out-of-sync film camera (ARRI Timing Shift Box style):
// the image is converted to linear light, smeared along the film transport
// direction (bright areas leave long streaks), optionally mixed with a
// time-shifted ghost copy, and converted back.
//
// This file knows NOTHING about OpenFX or DaVinci Resolve. It only works on
// plain RGBA float buffers, so it can be unit-tested on any computer.
// -----------------------------------------------------------------------------
#pragma once

#include <cstddef>

namespace tsfx {

// A read-only view of an RGBA float image (4 floats per pixel).
// Coordinates follow OpenFX "pixel coordinates": bounds are [x1,x2) x [y1,y2),
// y grows upwards. rowBytes may be negative (top-down images).
struct ImageView {
    const void* data = nullptr;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    std::ptrdiff_t rowBytes = 0;

    bool valid() const { return data != nullptr && x2 > x1 && y2 > y1; }
    const float* pixel(int x, int y) const {
        return reinterpret_cast<const float*>(
                   static_cast<const char*>(data) + (y - y1) * rowBytes) +
               (x - x1) * 4;
    }
};

// A writable RGBA float image.
struct MutableImageView {
    void* data = nullptr;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    std::ptrdiff_t rowBytes = 0;

    float* pixel(int x, int y) const {
        return reinterpret_cast<float*>(static_cast<char*>(data) +
                                        (y - y1) * rowBytes) +
               (x - x1) * 4;
    }
};

// Transfer function of the incoming image. The streak is computed in linear
// light so highlights smear like real light on film.
enum class Gamma : int {
    Linear = 0,          // already linear, no conversion
    Rec709 = 1,          // Rec.709 / Gamma 2.4 (Resolve's default timeline)
    SonySLog3 = 2,
    ArriLogC3 = 3,       // LogC3, EI 800
    ArriLogC4 = 4,
    DaVinciIntermediate = 5,
};
constexpr int kNumGammas = 6;

// What happens when the smear reaches the frame edge.
enum class Edges : int {
    WrapAround = 0,  // continues from the opposite edge (like the film strip)
    Replicate = 1,   // repeats the edge pixels
};

// All user parameters, already evaluated at the current time.
// Lengths are in "pixels at 1920 px width"; the core rescales them so the look
// is identical for HD, UHD and Resolve's proxy / render-scale previews.
struct Settings {
    // --- streak (the Timing Shift look) ---
    Gamma inputGamma = Gamma::Rec709;
    double blurStrength = 1.0;    // 0..3   1.0 = streak of 150 px at 1080p
    double blurAngle = 90.0;      // deg    0 = right, 90 = up, -90 = down
    bool symmetricBlur = false;   // smear in both directions
    Edges edges = Edges::WrapAround;
    double cleanupLow = 0.0;      // 0..1  dark areas below this stay clean ...
    double cleanupHigh = 0.0;     // 0..1  ... fading to full effect at this level
    // --- shake: randomly varying streak length (the box's jitter) ---
    bool shake = false;
    double shakeSmoothness = 5.0; // frames between random values
    double strengthMin = 0.5;
    double strengthMax = 1.0;
    int seed = 0;
    // --- ghost: time-shifted, displaced copy ---
    double ghostAmount = 0.0;     // 0..1
    double ghostDistance = 40.0;  // px
    double ghostDirection = 90.0; // deg
    int timingOffset = 2;         // frames; >0 = past, <0 = future
};

// Streak length in px (at 1920 px width) for blurStrength = 1.
constexpr double kStreakLengthAtStrength1 = 150.0;

// The frames the effect reads.
//  current - the source frame at the render time (required)
//  ghost   - source frame at time - timingOffset. If invalid, the current
//            frame is used instead.
struct FrameInputs {
    ImageView current;
    ImageView ghost;
};

// True if the settings need a second (time-shifted) frame.
bool needsGhostFrame(const Settings& s);

// Blur strength actually used at this time (applies Shake).
double effectiveStrength(const Settings& s, double time);

// Render the effect into dst for the pixels inside the render window.
//  time        - host time in frames (used by Shake)
//  pixelScale  - (image width in canonical px / 1920) * renderScale.x
//  numThreads  - <= 0 means "use all hardware threads"
// Returns false if the inputs are unusable (dst is left untouched).
bool render(const Settings& s, const FrameInputs& in,
            const MutableImageView& dst, int winX1, int winY1, int winX2,
            int winY2, double time, double pixelScale, int numThreads = 0);

// True if the settings produce an unchanged image at this time.
bool isIdentity(const Settings& s, double time);

// Transfer functions (exposed for tests).
float toLinear(float v, Gamma g);
float fromLinear(float v, Gamma g);

// Deterministic hash noise in [0,1) (exposed for tests).
double hashNoise(unsigned seed, long long a);

}  // namespace tsfx
