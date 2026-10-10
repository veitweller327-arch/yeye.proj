// TimingShiftCore.h
// -----------------------------------------------------------------------------
// Host-independent image processing for the "Timing Shift FX" ghost effect.
//
// This file knows NOTHING about OpenFX or DaVinci Resolve. It only works on
// plain RGBA float buffers. That separation lets us unit-test the look on any
// computer (see tests/core_tests.cpp) and keeps the OFX glue code small.
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

// All user parameters, already evaluated at the current time.
// Distances are in "pixels at 1920 px width"; the core rescales them so the
// look is identical for HD, UHD and Resolve's proxy / render-scale previews.
struct Settings {
    double ghostAmount = 0.4;      // 0..1  opacity of the ghost image
    double ghostDistance = 40.0;   // px    how far the ghost is moved
    double ghostDirection = 90.0;  // deg   0 = right, 90 = up, 180 = left, -90 = down
    double ghostStretch = 0.3;     // 0..1  smear of the ghost along the direction
    int timingOffset = 2;          // frames; >0 = ghost shows the past, <0 = the future
};

// The frames the effect reads.
//  current - the source frame at the render time (required)
//  ghost   - source frame at time - timingOffset. If invalid (e.g. the host
//            could not deliver it) the current frame is used instead.
struct FrameInputs {
    ImageView current;
    ImageView ghost;
};

// True if the settings need a second (time-shifted) frame.
bool needsGhostFrame(const Settings& s);

// Render the effect into dst for the pixels inside the render window.
//  pixelScale  - (image width in canonical px / 1920) * renderScale.x
//  numThreads  - <= 0 means "use all hardware threads"
// Returns false if the inputs are unusable (dst is left untouched).
bool render(const Settings& s, const FrameInputs& in,
            const MutableImageView& dst, int winX1, int winY1, int winX2,
            int winY2, double pixelScale, int numThreads = 0);

// True if the settings produce an unchanged image (lets the host skip us).
bool isIdentity(const Settings& s);

}  // namespace tsfx
