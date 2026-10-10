// TimingShiftCore.h
// -----------------------------------------------------------------------------
// Host-independent image processing for the "Timing Shift FX" effect.
//
// This file knows NOTHING about OpenFX or DaVinci Resolve. It only works on
// plain RGBA float buffers. That separation lets us unit-test the look on any
// computer (see tests/core_tests.cpp) and keeps the OFX glue code small.
// -----------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>

namespace tsfx {

// Maximum number of temporally offset frames ("echoes") we ever blend.
constexpr int kMaxTrailFrames = 4;

// A read-only view of an RGBA float image (4 floats per pixel).
// Coordinates follow OpenFX "pixel coordinates": bounds are [x1,x2) x [y1,y2).
// rowBytes may be negative (bottom-up images) - we always use it as given.
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

enum class Quality : int { Draft = 0, Normal = 1, Best = 2 };

// All user parameters, already evaluated at the current time.
// Distances are in "pixels at 1920 px width"; the core rescales them so the
// look is identical for HD, UHD and Resolve's proxy / render-scale previews.
struct Settings {
    bool enabled = true;
    double masterIntensity = 1.0;   // 0..2, scales every artifact
    int timingOffset = 2;           // frames; >0 = past frames, <0 = future
    double hDisplacement = 24.0;    // px, signed: + = right, - = left
    double bandSize = 40.0;         // px, average height of displaced bands
    double bandDensity = 0.35;      // 0..1, fraction of bands that are displaced
    double rgbSplit = 3.0;          // px
    double redShift = 1.0;          // -1..1 multiplier of rgbSplit
    double greenShift = 0.0;
    double blueShift = -1.0;
    double ghostAmount = 0.25;      // 0..1
    int trailLength = 1;            // 1..kMaxTrailFrames echo frames
    double ghostShift = 8.0;        // px horizontal offset of the ghost image
    double jitterAmount = 2.0;      // px
    double jitterSpeed = 1.0;       // pattern changes per frame (0 = frozen)
    bool streakEnable = false;
    double streakAmount = 0.3;      // 0..1
    double streakLength = 60.0;     // px
    double blend = 1.0;             // 0..1 mix with the untouched original
    int seed = 1;
    Quality quality = Quality::Normal;
};

// The frames the effect reads.
//  current  - the source frame at the render time (required)
//  echoes[k] - source frame at time - timingOffset*(k+1); may be the current
//              frame again when the host could not deliver that frame.
struct FrameInputs {
    ImageView current;
    ImageView echoes[kMaxTrailFrames];
    int numEchoes = 0;
};

// Number of echo frames the given settings need (respects quality limits).
int echoFramesNeeded(const Settings& s);

// Render the effect into dst for the pixels inside the render window.
//  time        - host time in frames (fractional values allowed)
//  pixelScale  - (image width in canonical px / 1920) * renderScale.x
//  numThreads  - <= 0 means "use all hardware threads"
// Returns false if the inputs are unusable (dst is left untouched).
bool render(const Settings& s, const FrameInputs& in,
            const MutableImageView& dst, int winX1, int winY1, int winX2,
            int winY2, double time, double pixelScale, int numThreads = 0);

// True if the settings produce an unchanged image (lets the host skip us).
bool isIdentity(const Settings& s);

// Built-in looks. presetSubtle() == the default value of every parameter.
Settings presetSubtle();
Settings presetMusicVideo();

// Deterministic hash noise in [0,1). Exposed for tests.
float hashNoise(std::uint32_t seed, std::int64_t a, std::int64_t b,
                std::uint32_t channel);

}  // namespace tsfx
