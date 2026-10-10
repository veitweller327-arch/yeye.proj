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

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"

#include "TimingShiftCore.h"

#define TSFX_PLUGIN_ID "com.yeyeproj.TimingShiftFX"
#define TSFX_LABEL "Timing Shift FX"
#define TSFX_GROUP "Timing Shift FX"
#define TSFX_VERSION_MAJOR 1
#define TSFX_VERSION_MINOR 1

namespace {

OfxHost* gHost = nullptr;
const OfxPropertySuiteV1* gProp = nullptr;
const OfxImageEffectSuiteV1* gEffect = nullptr;
const OfxParameterSuiteV1* gParam = nullptr;

// ---- parameter names (internal ids, never change them after release!) ----
namespace P {
const char* const ghostAmount = "ghostAmount";
const char* const ghostDistance = "ghostDistance";
const char* const ghostDirection = "ghostDirection";
const char* const ghostStretch = "ghostStretch";
const char* const timingOffset = "timingOffset";
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
    const tsfx::Settings d;  // defaults as fallback
    tsfx::Settings s;
    s.ghostAmount = getDouble(ps, P::ghostAmount, t, d.ghostAmount);
    s.ghostDistance = getDouble(ps, P::ghostDistance, t, d.ghostDistance);
    s.ghostDirection = getDouble(ps, P::ghostDirection, t, d.ghostDirection);
    s.ghostStretch = getDouble(ps, P::ghostStretch, t, d.ghostStretch);
    s.timingOffset = getInt(ps, P::timingOffset, t, d.timingOffset);
    return s;
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
        "Analog timing-error ghost: a time-shifted, displaced and stretched "
        "copy of the image. Inspired by analog timing artifacts; not "
        "affiliated with ARRI.");
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

    defineDouble(ps, P::ghostAmount, "Ghost Amount",
                 "Strength (opacity) of the ghost image. 0 = off, 1 = only ghost.",
                 nullptr, d.ghostAmount, 0, 1, 0, 1, 0.01);
    defineDouble(ps, P::ghostDistance, "Ghost Distance",
                 "How far the ghost is moved, in pixels (at 1080p).", nullptr,
                 d.ghostDistance, 0, 4000, 0, 500, 1);
    defineDouble(ps, P::ghostDirection, "Ghost Direction",
                 "Direction in degrees: 0 = right, 90 = up, 180 = left, -90 = down.",
                 nullptr, d.ghostDirection, -180, 180, -180, 180, 1);
    defineDouble(ps, P::ghostStretch, "Ghost Distortion",
                 "Stretches / smears the ghost along its direction. 0 = sharp copy.",
                 nullptr, d.ghostStretch, 0, 1, 0, 1, 0.01);
    defineInt(ps, P::timingOffset, "Timing Offset (frames)",
              "Which frame the ghost shows: +2 = 2 frames ago, -2 = 2 frames ahead, "
              "0 = same frame.",
              nullptr, d.timingOffset, -12, 12);
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

// Time of the ghost frame, clamped to the clip so we never ask for frames
// that do not exist.
double ghostTime(OfxImageEffectHandle effect, double t, int offset) {
    double e = t - static_cast<double>(offset);
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
    if (tsfx::needsGhostFrame(s)) {
        const double g = ghostTime(effect, t, s.timingOffset);
        lo = std::fmin(lo, g);
        hi = std::fmax(hi, g);
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

    // Fetch the time-shifted ghost frame. If the host cannot deliver it
    // (e.g. outside the media), the current frame is used instead of
    // failing - this keeps playback stable at clip boundaries.
    FetchedImage ghostImg;
    if (tsfx::needsGhostFrame(s)) {
        const double gt = ghostTime(effect, t, s.timingOffset);
        if (gEffect->clipGetImage(srcClip, gt, nullptr, &ghostImg.h) != kOfxStatOK ||
            !toView(ghostImg.h, inputs.ghost))
            inputs.ghost = tsfx::ImageView{};
    }

    // Pixel scale: keeps distances identical at HD, UHD and proxy resolution.
    double pixelScale = scale[0];
    OfxRectD rod;
    if (gEffect->clipGetRegionOfDefinition(srcClip, t, &rod) == kOfxStatOK &&
        rod.x2 > rod.x1)
        pixelScale = (rod.x2 - rod.x1) / 1920.0 * scale[0];

    if (!tsfx::render(s, inputs, dst, win[0], win[1], win[2], win[3], pixelScale))
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
