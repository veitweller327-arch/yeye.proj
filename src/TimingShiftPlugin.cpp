// TimingShiftPlugin.cpp
// -----------------------------------------------------------------------------
// OpenFX (OFX) glue for "Timing Shift FX".
//
// DaVinci Resolve loads the compiled TimingShiftFX.ofx file, calls
// OfxGetNumberOfPlugins / OfxGetPlugin, and then sends "actions" (describe,
// render, ...) to pluginMain() below. This file:
//   * declares the clips (Source -> Output) and all Inspector parameters,
//   * tells Resolve which extra frames we need (temporal access),
//   * fetches those frames, reads the parameters and calls tsfx::render().
//
// It uses only the plain C API from the official OpenFX headers
// (external/openfx/include), no extra support library is needed.
// -----------------------------------------------------------------------------
#include <cmath>
#include <cstring>
#include <string>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"

#include "TimingShiftCore.h"

#define TSFX_PLUGIN_ID "com.yeyeproj.TimingShiftFX"
#define TSFX_LABEL "Timing Shift FX"
#define TSFX_GROUP "Timing Shift FX"
#define TSFX_VERSION_MAJOR 1
#define TSFX_VERSION_MINOR 0

namespace {

OfxHost* gHost = nullptr;
const OfxPropertySuiteV1* gProp = nullptr;
const OfxImageEffectSuiteV1* gEffect = nullptr;
const OfxParameterSuiteV1* gParam = nullptr;

// ---- parameter names (internal ids, never change them after release!) ----
namespace P {
const char* const preset = "preset";
const char* const enable = "enable";
const char* const intensity = "masterIntensity";
const char* const blend = "blend";
const char* const quality = "quality";
const char* const timingOffset = "timingOffset";
const char* const ghostAmount = "ghostAmount";
const char* const trailLength = "trailLength";
const char* const ghostShift = "ghostShift";
const char* const hDisp = "hDisplacement";
const char* const bandSize = "bandSize";
const char* const bandDensity = "bandDensity";
const char* const rgbSplit = "rgbSplit";
const char* const redShift = "redShift";
const char* const greenShift = "greenShift";
const char* const blueShift = "blueShift";
const char* const jitterAmount = "jitterAmount";
const char* const jitterSpeed = "jitterSpeed";
const char* const seed = "seed";
const char* const streakEnable = "streakEnable";
const char* const streakAmount = "streakAmount";
const char* const streakLength = "streakLength";
}  // namespace P

// ---------------------------------------------------------------------------
// Small helpers around the property / parameter suites.
// ---------------------------------------------------------------------------
OfxParamSetHandle paramSetOf(OfxImageEffectHandle effect) {
    OfxParamSetHandle ps = nullptr;
    gEffect->getParamSet(effect, &ps);
    return ps;
}

OfxPropertySetHandle defineParam(OfxParamSetHandle ps, const char* type,
                                 const char* name, const char* label,
                                 const char* hint, const char* parent) {
    OfxPropertySetHandle props = nullptr;
    gParam->paramDefine(ps, type, name, &props);
    gProp->propSetString(props, kOfxPropLabel, 0, label);
    if (hint) gProp->propSetString(props, kOfxParamPropHint, 0, hint);
    if (parent) gProp->propSetString(props, kOfxParamPropParent, 0, parent);
    return props;
}

void defineDouble(OfxParamSetHandle ps, const char* name, const char* label,
                  const char* hint, const char* parent, double def, double mn,
                  double mx, double dispMin, double dispMax, double inc) {
    OfxPropertySetHandle p =
        defineParam(ps, kOfxParamTypeDouble, name, label, hint, parent);
    gProp->propSetDouble(p, kOfxParamPropDefault, 0, def);
    gProp->propSetDouble(p, kOfxParamPropMin, 0, mn);
    gProp->propSetDouble(p, kOfxParamPropMax, 0, mx);
    gProp->propSetDouble(p, kOfxParamPropDisplayMin, 0, dispMin);
    gProp->propSetDouble(p, kOfxParamPropDisplayMax, 0, dispMax);
    gProp->propSetDouble(p, kOfxParamPropIncrement, 0, inc);
    gProp->propSetInt(p, kOfxParamPropDigits, 0, 2);
}

void defineInt(OfxParamSetHandle ps, const char* name, const char* label,
               const char* hint, const char* parent, int def, int mn, int mx) {
    OfxPropertySetHandle p =
        defineParam(ps, kOfxParamTypeInteger, name, label, hint, parent);
    gProp->propSetInt(p, kOfxParamPropDefault, 0, def);
    gProp->propSetInt(p, kOfxParamPropMin, 0, mn);
    gProp->propSetInt(p, kOfxParamPropMax, 0, mx);
    gProp->propSetInt(p, kOfxParamPropDisplayMin, 0, mn);
    gProp->propSetInt(p, kOfxParamPropDisplayMax, 0, mx);
}

void defineBool(OfxParamSetHandle ps, const char* name, const char* label,
                const char* hint, const char* parent, bool def) {
    OfxPropertySetHandle p =
        defineParam(ps, kOfxParamTypeBoolean, name, label, hint, parent);
    gProp->propSetInt(p, kOfxParamPropDefault, 0, def ? 1 : 0);
}

void defineChoice(OfxParamSetHandle ps, const char* name, const char* label,
                  const char* hint, const char* parent,
                  std::initializer_list<const char*> options, int def,
                  bool animates) {
    OfxPropertySetHandle p =
        defineParam(ps, kOfxParamTypeChoice, name, label, hint, parent);
    int i = 0;
    for (const char* o : options) gProp->propSetString(p, kOfxParamPropChoiceOption, i++, o);
    gProp->propSetInt(p, kOfxParamPropDefault, 0, def);
    gProp->propSetInt(p, kOfxParamPropAnimates, 0, animates ? 1 : 0);
}

void defineGroup(OfxParamSetHandle ps, const char* name, const char* label,
                 bool open) {
    OfxPropertySetHandle p =
        defineParam(ps, kOfxParamTypeGroup, name, label, nullptr, nullptr);
    gProp->propSetInt(p, kOfxParamPropGroupOpen, 0, open ? 1 : 0);
}

OfxParamHandle param(OfxParamSetHandle ps, const char* name) {
    OfxParamHandle h = nullptr;
    gParam->paramGetHandle(ps, name, &h, nullptr);
    return h;
}

double getDouble(OfxParamSetHandle ps, const char* name, double t, double fallback) {
    double v = fallback;
    if (OfxParamHandle h = param(ps, name)) gParam->paramGetValueAtTime(h, t, &v);
    return v;
}

int getInt(OfxParamSetHandle ps, const char* name, double t, int fallback) {
    int v = fallback;
    if (OfxParamHandle h = param(ps, name)) gParam->paramGetValueAtTime(h, t, &v);
    return v;
}

// Reads every parameter at the given time into a Settings struct.
tsfx::Settings readSettings(OfxImageEffectHandle effect, double t) {
    OfxParamSetHandle ps = paramSetOf(effect);
    tsfx::Settings d;  // defaults as fallback
    tsfx::Settings s;
    s.enabled = getInt(ps, P::enable, t, 1) != 0;
    s.masterIntensity = getDouble(ps, P::intensity, t, d.masterIntensity);
    s.blend = getDouble(ps, P::blend, t, d.blend);
    s.quality = static_cast<tsfx::Quality>(getInt(ps, P::quality, t, 1));
    s.timingOffset = getInt(ps, P::timingOffset, t, d.timingOffset);
    s.ghostAmount = getDouble(ps, P::ghostAmount, t, d.ghostAmount);
    s.trailLength = getInt(ps, P::trailLength, t, d.trailLength);
    s.ghostShift = getDouble(ps, P::ghostShift, t, d.ghostShift);
    s.hDisplacement = getDouble(ps, P::hDisp, t, d.hDisplacement);
    s.bandSize = getDouble(ps, P::bandSize, t, d.bandSize);
    s.bandDensity = getDouble(ps, P::bandDensity, t, d.bandDensity);
    s.rgbSplit = getDouble(ps, P::rgbSplit, t, d.rgbSplit);
    s.redShift = getDouble(ps, P::redShift, t, d.redShift);
    s.greenShift = getDouble(ps, P::greenShift, t, d.greenShift);
    s.blueShift = getDouble(ps, P::blueShift, t, d.blueShift);
    s.jitterAmount = getDouble(ps, P::jitterAmount, t, d.jitterAmount);
    s.jitterSpeed = getDouble(ps, P::jitterSpeed, t, d.jitterSpeed);
    s.seed = getInt(ps, P::seed, t, d.seed);
    s.streakEnable = getInt(ps, P::streakEnable, t, 0) != 0;
    s.streakAmount = getDouble(ps, P::streakAmount, t, d.streakAmount);
    s.streakLength = getDouble(ps, P::streakLength, t, d.streakLength);
    if (s.quality < tsfx::Quality::Draft || s.quality > tsfx::Quality::Best)
        s.quality = tsfx::Quality::Normal;
    return s;
}

void applyPreset(OfxImageEffectHandle effect, const tsfx::Settings& s) {
    OfxParamSetHandle ps = paramSetOf(effect);
    auto setD = [&](const char* n, double v) {
        if (OfxParamHandle h = param(ps, n)) gParam->paramSetValue(h, v);
    };
    auto setI = [&](const char* n, int v) {
        if (OfxParamHandle h = param(ps, n)) gParam->paramSetValue(h, v);
    };
    setI(P::enable, s.enabled ? 1 : 0);
    setD(P::intensity, s.masterIntensity);
    setD(P::blend, s.blend);
    setI(P::quality, static_cast<int>(s.quality));
    setI(P::timingOffset, s.timingOffset);
    setD(P::ghostAmount, s.ghostAmount);
    setI(P::trailLength, s.trailLength);
    setD(P::ghostShift, s.ghostShift);
    setD(P::hDisp, s.hDisplacement);
    setD(P::bandSize, s.bandSize);
    setD(P::bandDensity, s.bandDensity);
    setD(P::rgbSplit, s.rgbSplit);
    setD(P::redShift, s.redShift);
    setD(P::greenShift, s.greenShift);
    setD(P::blueShift, s.blueShift);
    setD(P::jitterAmount, s.jitterAmount);
    setD(P::jitterSpeed, s.jitterSpeed);
    setI(P::seed, s.seed);
    setI(P::streakEnable, s.streakEnable ? 1 : 0);
    setD(P::streakAmount, s.streakAmount);
    setD(P::streakLength, s.streakLength);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
OfxStatus onLoad() {
    if (!gHost) return kOfxStatErrMissingHostFeature;
    gProp = static_cast<const OfxPropertySuiteV1*>(
        gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1));
    gEffect = static_cast<const OfxImageEffectSuiteV1*>(
        gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1));
    gParam = static_cast<const OfxParameterSuiteV1*>(
        gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1));
    if (!gProp || !gEffect || !gParam) return kOfxStatErrMissingHostFeature;
    return kOfxStatOK;
}

OfxStatus describe(OfxImageEffectHandle effect) {
    OfxPropertySetHandle props = nullptr;
    gEffect->getPropertySet(effect, &props);
    gProp->propSetString(props, kOfxPropLabel, 0, TSFX_LABEL);
    gProp->propSetString(props, kOfxImageEffectPluginPropGrouping, 0, TSFX_GROUP);
    gProp->propSetString(props, kOfxPropPluginDescription, 0,
        "Analog timing-error look: time-shifted bands, RGB misalignment, "
        "ghost trails, jitter and streaks. Inspired by analog timing "
        "artifacts; not affiliated with ARRI.");
    gProp->propSetString(props, kOfxImageEffectPropSupportedContexts, 0,
                         kOfxImageEffectContextFilter);
    gProp->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 0,
                         kOfxBitDepthFloat);
    gProp->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);
    gProp->propSetInt(props, kOfxImageEffectPropTemporalClipAccess, 0, 1);
    gProp->propSetInt(props, kOfxImageEffectPropSupportsMultiResolution, 0, 1);
    gProp->propSetInt(props, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
    gProp->propSetInt(props, kOfxImageEffectPluginPropSingleInstance, 0, 0);
    gProp->propSetInt(props, kOfxImageEffectPluginPropHostFrameThreading, 0, 0);
    gProp->propSetString(props, kOfxImageEffectPluginRenderThreadSafety, 0,
                         kOfxImageEffectRenderFullySafe);
    return kOfxStatOK;
}

OfxStatus describeInContext(OfxImageEffectHandle effect) {
    OfxPropertySetHandle clip = nullptr;
    gEffect->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &clip);
    gProp->propSetString(clip, kOfxImageEffectPropSupportedComponents, 0,
                         kOfxImageComponentRGBA);
    gProp->propSetInt(clip, kOfxImageEffectPropTemporalClipAccess, 0, 1);
    gProp->propSetInt(clip, kOfxImageClipPropIsMask, 0, 0);
    gProp->propSetInt(clip, kOfxImageEffectPropSupportsTiles, 0, 0);

    gEffect->clipDefine(effect, kOfxImageEffectOutputClipName, &clip);
    gProp->propSetString(clip, kOfxImageEffectPropSupportedComponents, 0,
                         kOfxImageComponentRGBA);
    gProp->propSetInt(clip, kOfxImageEffectPropSupportsTiles, 0, 0);

    OfxParamSetHandle ps = paramSetOf(effect);
    const tsfx::Settings d;

    defineChoice(ps, P::preset, "Look Preset",
                 "Choose a look to load its values. 'Custom' keeps your values.",
                 nullptr, {"Custom", "Subtle Cinematic", "Music Video Extreme"},
                 0, false);
    defineBool(ps, P::enable, "Effect Enable", "Turns the whole effect on/off.",
               nullptr, true);
    defineDouble(ps, P::intensity, "Master Intensity",
                 "Scales every artifact at once (1 = as set below).", nullptr,
                 d.masterIntensity, 0, 4, 0, 2, 0.01);
    defineDouble(ps, P::blend, "Blend Amount",
                 "Mix between original (0) and effect (1).", nullptr, d.blend,
                 0, 1, 0, 1, 0.01);
    defineChoice(ps, P::quality, "Quality",
                 "Draft = fast nearest sampling, 1 echo frame. Normal = smooth "
                 "sampling, up to 2 echo frames. Best = all echo frames.",
                 nullptr, {"Draft (fast)", "Normal", "Best (slow)"},
                 static_cast<int>(d.quality), false);

    defineGroup(ps, "grpTiming", "Timing Shift & Ghosting", true);
    defineInt(ps, P::timingOffset, "Timing Offset (frames)",
              "How many frames the shifted image lags behind (+) or runs ahead (-).",
              "grpTiming", d.timingOffset, -12, 12);
    defineDouble(ps, P::ghostAmount, "Ghosting Amount",
                 "Opacity of the time-shifted ghost image.", "grpTiming",
                 d.ghostAmount, 0, 1, 0, 1, 0.01);
    defineInt(ps, P::trailLength, "Trail Length (echoes)",
              "Number of ghost echoes (each one Timing Offset further away).",
              "grpTiming", d.trailLength, 1, tsfx::kMaxTrailFrames);
    defineDouble(ps, P::ghostShift, "Ghost Horizontal Shift",
                 "Sideways offset of each ghost echo in pixels (at 1080p).",
                 "grpTiming", d.ghostShift, -1000, 1000, -200, 200, 0.5);

    defineGroup(ps, "grpDisp", "Band Displacement", true);
    defineDouble(ps, P::hDisp, "Horizontal Displacement",
                 "Push of the displaced bands in pixels (at 1080p). "
                 "Positive = right, negative = left.",
                 "grpDisp", d.hDisplacement, -2000, 2000, -400, 400, 0.5);
    defineDouble(ps, P::bandSize, "Band Size",
                 "Average height of the displaced bands in pixels (at 1080p).",
                 "grpDisp", d.bandSize, 1, 2000, 2, 400, 1);
    defineDouble(ps, P::bandDensity, "Band Density",
                 "How many bands are displaced (0 = none, 1 = all).", "grpDisp",
                 d.bandDensity, 0, 1, 0, 1, 0.01);

    defineGroup(ps, "grpRgb", "Colour Channels", true);
    defineDouble(ps, P::rgbSplit, "RGB Split",
                 "Distance between colour channels in pixels (at 1080p).",
                 "grpRgb", d.rgbSplit, 0, 500, 0, 50, 0.1);
    defineDouble(ps, P::redShift, "Red Offset", "Red direction/amount (x RGB Split).",
                 "grpRgb", d.redShift, -2, 2, -1, 1, 0.01);
    defineDouble(ps, P::greenShift, "Green Offset", "Green direction/amount (x RGB Split).",
                 "grpRgb", d.greenShift, -2, 2, -1, 1, 0.01);
    defineDouble(ps, P::blueShift, "Blue Offset", "Blue direction/amount (x RGB Split).",
                 "grpRgb", d.blueShift, -2, 2, -1, 1, 0.01);

    defineGroup(ps, "grpJitter", "Analog Jitter", true);
    defineDouble(ps, P::jitterAmount, "Jitter Amount",
                 "Frame-to-frame shake in pixels (at 1080p).", "grpJitter",
                 d.jitterAmount, 0, 500, 0, 30, 0.1);
    defineDouble(ps, P::jitterSpeed, "Jitter Speed",
                 "How often the random pattern changes (1 = every frame, "
                 "0.5 = every 2nd frame, smooth; 0 = frozen).",
                 "grpJitter", d.jitterSpeed, 0, 10, 0, 4, 0.01);
    defineInt(ps, P::seed, "Random Seed",
              "Different number = different (but repeatable) random pattern.",
              "grpJitter", d.seed, 0, 99999);

    defineGroup(ps, "grpStreak", "Horizontal Streaks", false);
    defineBool(ps, P::streakEnable, "Enable Streaks",
               "Turns the horizontal smear on/off.", "grpStreak", d.streakEnable);
    defineDouble(ps, P::streakAmount, "Streak Amount", "Strength of the smear.",
                 "grpStreak", d.streakAmount, 0, 1, 0, 1, 0.01);
    defineDouble(ps, P::streakLength, "Streak Length",
                 "Length of the smear in pixels (at 1080p).", "grpStreak",
                 d.streakLength, 1, 5000, 2, 1000, 1);
    return kOfxStatOK;
}

// Source clip frame range, so we never ask for frames outside the clip.
bool sourceFrameRange(OfxImageEffectHandle effect, double& first, double& last) {
    OfxImageClipHandle clip = nullptr;
    OfxPropertySetHandle cprops = nullptr;
    if (gEffect->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName,
                               &clip, &cprops) != kOfxStatOK || !cprops)
        return false;
    double r[2] = {0, 0};
    if (gProp->propGetDoubleN(cprops, kOfxImageEffectPropFrameRange, 2, r) != kOfxStatOK)
        return false;
    first = r[0];
    last = r[1];
    return last >= first;
}

double echoTime(OfxImageEffectHandle effect, double t, int offset, int k) {
    double e = t - static_cast<double>(offset) * (k + 1);
    double first, last;
    if (sourceFrameRange(effect, first, last)) {
        if (e < first) e = first;  // clip start: reuse the first frame
        if (e > last) e = last;    // clip end: reuse the last frame
    }
    return e;
}

OfxStatus getFramesNeeded(OfxImageEffectHandle effect, OfxPropertySetHandle in,
                          OfxPropertySetHandle out) {
    double t = 0;
    gProp->propGetDouble(in, kOfxPropTime, 0, &t);
    const tsfx::Settings s = readSettings(effect, t);
    double lo = t, hi = t;
    const int n = tsfx::isIdentity(s) ? 0 : tsfx::echoFramesNeeded(s);
    for (int k = 0; k < n; ++k) {
        const double e = echoTime(effect, t, s.timingOffset, k);
        lo = std::fmin(lo, e);
        hi = std::fmax(hi, e);
    }
    double range[2] = {lo, hi};
    gProp->propSetDoubleN(out, "OfxImageClipPropFrameRange_Source", 2, range);
    return kOfxStatOK;
}

OfxStatus isIdentityAction(OfxImageEffectHandle effect, OfxPropertySetHandle in,
                           OfxPropertySetHandle out) {
    double t = 0;
    gProp->propGetDouble(in, kOfxPropTime, 0, &t);
    if (tsfx::isIdentity(readSettings(effect, t))) {
        gProp->propSetString(out, kOfxPropName, 0, kOfxImageEffectSimpleSourceClipName);
        return kOfxStatOK;
    }
    return kOfxStatReplyDefault;
}

OfxStatus instanceChanged(OfxImageEffectHandle effect, OfxPropertySetHandle in) {
    char* type = nullptr;
    char* name = nullptr;
    char* reason = nullptr;
    gProp->propGetString(in, kOfxPropType, 0, &type);
    gProp->propGetString(in, kOfxPropName, 0, &name);
    gProp->propGetString(in, kOfxPropChangeReason, 0, &reason);
    if (!type || !name || !reason) return kOfxStatReplyDefault;
    if (std::strcmp(type, kOfxTypeParameter) != 0 ||
        std::strcmp(name, P::preset) != 0 ||
        std::strcmp(reason, kOfxChangeUserEdited) != 0)
        return kOfxStatReplyDefault;

    int choice = 0;
    if (OfxParamHandle h = param(paramSetOf(effect), P::preset))
        gParam->paramGetValue(h, &choice);
    if (choice == 1) applyPreset(effect, tsfx::presetSubtle());
    else if (choice == 2) applyPreset(effect, tsfx::presetMusicVideo());
    return kOfxStatOK;
}

// RAII wrapper so every fetched image is released on every code path.
struct FetchedImage {
    OfxPropertySetHandle h = nullptr;
    ~FetchedImage() { if (h) gEffect->clipReleaseImage(h); }
};

bool toView(OfxPropertySetHandle img, tsfx::ImageView& v) {
    void* data = nullptr;
    int b[4] = {0, 0, 0, 0};
    int rowBytes = 0;
    char* depth = nullptr;
    char* comps = nullptr;
    gProp->propGetPointer(img, kOfxImagePropData, 0, &data);
    gProp->propGetIntN(img, kOfxImagePropBounds, 4, b);
    gProp->propGetInt(img, kOfxImagePropRowBytes, 0, &rowBytes);
    gProp->propGetString(img, kOfxImageEffectPropPixelDepth, 0, &depth);
    gProp->propGetString(img, kOfxImageEffectPropComponents, 0, &comps);
    if (!data || !depth || !comps) return false;
    if (std::strcmp(depth, kOfxBitDepthFloat) != 0) return false;
    if (std::strcmp(comps, kOfxImageComponentRGBA) != 0) return false;
    v.data = data;
    v.x1 = b[0]; v.y1 = b[1]; v.x2 = b[2]; v.y2 = b[3];
    v.rowBytes = rowBytes;
    return v.valid();
}

OfxStatus renderAction(OfxImageEffectHandle effect, OfxPropertySetHandle in) {
    double t = 0;
    int win[4] = {0, 0, 0, 0};
    double scale[2] = {1, 1};
    gProp->propGetDouble(in, kOfxPropTime, 0, &t);
    gProp->propGetIntN(in, kOfxImageEffectPropRenderWindow, 4, win);
    gProp->propGetDoubleN(in, kOfxImageEffectPropRenderScale, 2, scale);

    OfxImageClipHandle srcClip = nullptr, dstClip = nullptr;
    if (gEffect->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName,
                               &srcClip, nullptr) != kOfxStatOK ||
        gEffect->clipGetHandle(effect, kOfxImageEffectOutputClipName, &dstClip,
                               nullptr) != kOfxStatOK)
        return kOfxStatFailed;

    FetchedImage dstImg, curImg;
    if (gEffect->clipGetImage(dstClip, t, nullptr, &dstImg.h) != kOfxStatOK)
        return kOfxStatFailed;
    if (gEffect->clipGetImage(srcClip, t, nullptr, &curImg.h) != kOfxStatOK)
        return kOfxStatFailed;

    tsfx::ImageView dstRead;
    tsfx::FrameInputs inputs;
    if (!toView(dstImg.h, dstRead) || !toView(curImg.h, inputs.current))
        return kOfxStatErrImageFormat;
    tsfx::MutableImageView dst;
    dst.data = const_cast<void*>(dstRead.data);
    dst.x1 = dstRead.x1; dst.y1 = dstRead.y1;
    dst.x2 = dstRead.x2; dst.y2 = dstRead.y2;
    dst.rowBytes = dstRead.rowBytes;

    const tsfx::Settings s = readSettings(effect, t);

    // Fetch the time-shifted frames. If the host cannot deliver one (e.g.
    // outside the media), we fall back to the current frame instead of
    // failing - this keeps playback stable at clip boundaries.
    FetchedImage echoImg[tsfx::kMaxTrailFrames];
    inputs.numEchoes = s.enabled ? tsfx::echoFramesNeeded(s) : 0;
    for (int k = 0; k < inputs.numEchoes; ++k) {
        const double et = echoTime(effect, t, s.timingOffset, k);
        if (gEffect->clipGetImage(srcClip, et, nullptr, &echoImg[k].h) == kOfxStatOK &&
            toView(echoImg[k].h, inputs.echoes[k])) {
            continue;
        }
        inputs.echoes[k] = inputs.current;
    }

    // Pixel scale: keeps distances identical at HD, UHD and proxy resolution.
    double pixelScale = scale[0];
    OfxRectD rod;
    if (gEffect->clipGetRegionOfDefinition(srcClip, t, &rod) == kOfxStatOK &&
        rod.x2 > rod.x1)
        pixelScale = (rod.x2 - rod.x1) / 1920.0 * scale[0];

    if (!tsfx::render(s, inputs, dst, win[0], win[1], win[2], win[3], t,
                      pixelScale))
        return kOfxStatFailed;
    return kOfxStatOK;
}

OfxStatus pluginMain(const char* action, const void* handle,
                     OfxPropertySetHandle in, OfxPropertySetHandle out) {
    try {
        auto effect = static_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
        if (std::strcmp(action, kOfxActionLoad) == 0) return onLoad();
        if (std::strcmp(action, kOfxActionUnload) == 0) return kOfxStatOK;
        if (std::strcmp(action, kOfxActionDescribe) == 0) return describe(effect);
        if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0)
            return describeInContext(effect);
        if (std::strcmp(action, kOfxActionCreateInstance) == 0) return kOfxStatOK;
        if (std::strcmp(action, kOfxActionDestroyInstance) == 0) return kOfxStatOK;
        if (std::strcmp(action, kOfxImageEffectActionRender) == 0)
            return renderAction(effect, in);
        if (std::strcmp(action, kOfxImageEffectActionIsIdentity) == 0)
            return isIdentityAction(effect, in, out);
        if (std::strcmp(action, kOfxImageEffectActionGetFramesNeeded) == 0)
            return getFramesNeeded(effect, in, out);
        if (std::strcmp(action, kOfxActionInstanceChanged) == 0)
            return instanceChanged(effect, in);
    } catch (...) {
        // Exceptions must never cross the C boundary into the host.
        return kOfxStatFailed;
    }
    return kOfxStatReplyDefault;
}

void setHost(OfxHost* host) { gHost = host; }

OfxPlugin gPlugin = {
    kOfxImageEffectPluginApi, 1, TSFX_PLUGIN_ID, TSFX_VERSION_MAJOR,
    TSFX_VERSION_MINOR,       setHost, pluginMain};

}  // namespace

extern "C" {
OfxExport int OfxGetNumberOfPlugins(void) { return 1; }
OfxExport OfxPlugin* OfxGetPlugin(int nth) { return nth == 0 ? &gPlugin : nullptr; }
}
