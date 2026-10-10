// mock_host.cpp - a tiny OpenFX host used to test the REAL plugin binary.
//
// It loads TimingShiftFX.ofx (dlopen / LoadLibrary) exactly like an OFX host
// (DaVinci Resolve, Nuke, Natron, ...) does, then walks through the same
// action sequence: load -> describe -> describeInContext -> createInstance ->
// getFramesNeeded -> isIdentity -> render -> instanceChanged -> unload.
//
// It is NOT DaVinci Resolve. It proves the plugin follows the OFX C API and
// renders correctly; the final check must still happen inside Resolve.
//
// Works on Linux/macOS (dlopen) and Windows (LoadLibrary).
// Usage: mock_host path/to/TimingShiftFX.ofx
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
static void* dlopenAny(const char* p) { return reinterpret_cast<void*>(LoadLibraryA(p)); }
static void* dlsymAny(void* l, const char* n) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(l), n)); }
static void dlcloseAny(void* l) { FreeLibrary(static_cast<HMODULE>(l)); }
static const char* dlerrorAny() { return "LoadLibrary failed"; }
#else
#include <dlfcn.h>
static void* dlopenAny(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void* dlsymAny(void* l, const char* n) { return dlsym(l, n); }
static void dlcloseAny(void* l) { dlclose(l); }
static const char* dlerrorAny() { return dlerrorAny(); }
#endif

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"

static int gFailures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("  FAILED: %s (line %d)\n", #cond, __LINE__);  \
            ++gFailures;                                                \
        }                                                               \
    } while (0)

// ----------------------------------------------------------------- properties
struct Value {
    int i = 0;
    double d = 0;
    std::string s;
    void* p = nullptr;
};
struct PropertySet {
    std::map<std::string, std::vector<Value>> m;
    Value& at(const char* name, int idx) {
        auto& v = m[name];
        if (int(v.size()) <= idx) v.resize(idx + 1);
        return v[idx];
    }
    const Value* get(const char* name, int idx) const {
        auto it = m.find(name);
        if (it == m.end() || idx < 0 || idx >= int(it->second.size())) return nullptr;
        return &it->second[idx];
    }
};
static PropertySet* PS(OfxPropertySetHandle h) { return reinterpret_cast<PropertySet*>(h); }
static OfxPropertySetHandle H(PropertySet* p) { return reinterpret_cast<OfxPropertySetHandle>(p); }

static OfxStatus setP(OfxPropertySetHandle h, const char* n, int i, void* v) { PS(h)->at(n, i).p = v; return kOfxStatOK; }
static OfxStatus setS(OfxPropertySetHandle h, const char* n, int i, const char* v) { PS(h)->at(n, i).s = v; return kOfxStatOK; }
static OfxStatus setD(OfxPropertySetHandle h, const char* n, int i, double v) { PS(h)->at(n, i).d = v; return kOfxStatOK; }
static OfxStatus setI(OfxPropertySetHandle h, const char* n, int i, int v) { PS(h)->at(n, i).i = v; return kOfxStatOK; }
static OfxStatus setPN(OfxPropertySetHandle h, const char* n, int c, void* const* v) { for (int i = 0; i < c; ++i) setP(h, n, i, v[i]); return kOfxStatOK; }
static OfxStatus setSN(OfxPropertySetHandle h, const char* n, int c, const char* const* v) { for (int i = 0; i < c; ++i) setS(h, n, i, v[i]); return kOfxStatOK; }
static OfxStatus setDN(OfxPropertySetHandle h, const char* n, int c, const double* v) { PS(h)->m[n].clear(); for (int i = 0; i < c; ++i) setD(h, n, i, v[i]); return kOfxStatOK; }
static OfxStatus setIN(OfxPropertySetHandle h, const char* n, int c, const int* v) { for (int i = 0; i < c; ++i) setI(h, n, i, v[i]); return kOfxStatOK; }
static OfxStatus getP(OfxPropertySetHandle h, const char* n, int i, void** v) { auto x = PS(h)->get(n, i); if (!x) return kOfxStatErrUnknown; *v = x->p; return kOfxStatOK; }
static OfxStatus getS(OfxPropertySetHandle h, const char* n, int i, char** v) { auto x = PS(h)->get(n, i); if (!x) return kOfxStatErrUnknown; *v = const_cast<char*>(x->s.c_str()); return kOfxStatOK; }
static OfxStatus getD(OfxPropertySetHandle h, const char* n, int i, double* v) { auto x = PS(h)->get(n, i); if (!x) return kOfxStatErrUnknown; *v = x->d; return kOfxStatOK; }
static OfxStatus getI(OfxPropertySetHandle h, const char* n, int i, int* v) { auto x = PS(h)->get(n, i); if (!x) return kOfxStatErrUnknown; *v = x->i; return kOfxStatOK; }
static OfxStatus getPN(OfxPropertySetHandle h, const char* n, int c, void** v) { for (int i = 0; i < c; ++i) if (getP(h, n, i, &v[i])) return kOfxStatErrUnknown; return kOfxStatOK; }
static OfxStatus getSN(OfxPropertySetHandle h, const char* n, int c, char** v) { for (int i = 0; i < c; ++i) if (getS(h, n, i, &v[i])) return kOfxStatErrUnknown; return kOfxStatOK; }
static OfxStatus getDN(OfxPropertySetHandle h, const char* n, int c, double* v) { for (int i = 0; i < c; ++i) if (getD(h, n, i, &v[i])) return kOfxStatErrUnknown; return kOfxStatOK; }
static OfxStatus getIN(OfxPropertySetHandle h, const char* n, int c, int* v) { for (int i = 0; i < c; ++i) if (getI(h, n, i, &v[i])) return kOfxStatErrUnknown; return kOfxStatOK; }
static OfxStatus propReset(OfxPropertySetHandle h, const char* n) { PS(h)->m.erase(n); return kOfxStatOK; }
static OfxStatus propDim(OfxPropertySetHandle h, const char* n, int* c) { auto it = PS(h)->m.find(n); *c = it == PS(h)->m.end() ? 0 : int(it->second.size()); return kOfxStatOK; }

// ----------------------------------------------------------------- params
struct Param {
    std::string type;
    PropertySet props;
    double d = 0;
    int i = 0;
};
struct ParamSet {
    std::map<std::string, std::unique_ptr<Param>> params;
    std::vector<std::string> order;
};
static ParamSet* PSet(OfxParamSetHandle h) { return reinterpret_cast<ParamSet*>(h); }
static Param* PRM(OfxParamHandle h) { return reinterpret_cast<Param*>(h); }

static bool isDoubleType(const std::string& t) { return t == kOfxParamTypeDouble; }

static OfxStatus paramDefine(OfxParamSetHandle ps, const char* type, const char* name, OfxPropertySetHandle* props) {
    auto p = std::make_unique<Param>();
    p->type = type;
    if (props) *props = H(&p->props);
    PSet(ps)->order.push_back(name);
    PSet(ps)->params[name] = std::move(p);
    return kOfxStatOK;
}
static OfxStatus paramGetHandle(OfxParamSetHandle ps, const char* name, OfxParamHandle* h, OfxPropertySetHandle* props) {
    auto it = PSet(ps)->params.find(name);
    if (it == PSet(ps)->params.end()) return kOfxStatErrUnknown;
    *h = reinterpret_cast<OfxParamHandle>(it->second.get());
    if (props) *props = H(&it->second->props);
    return kOfxStatOK;
}
static OfxStatus paramGetPropertySet(OfxParamHandle h, OfxPropertySetHandle* props) { *props = H(&PRM(h)->props); return kOfxStatOK; }
static OfxStatus readValue(Param* p, va_list ap) {
    if (isDoubleType(p->type)) *va_arg(ap, double*) = p->d;
    else *va_arg(ap, int*) = p->i;
    return kOfxStatOK;
}
static int gGetValueCalls = 0;  // paramGetValue must not be used in render
static OfxStatus paramGetValue(OfxParamHandle h, ...) {
    ++gGetValueCalls;
    va_list ap; va_start(ap, h); OfxStatus s = readValue(PRM(h), ap); va_end(ap); return s;
}
static OfxStatus paramGetValueAtTime(OfxParamHandle h, OfxTime t, ...) {
    va_list ap; va_start(ap, t); OfxStatus s = readValue(PRM(h), ap); va_end(ap); return s;
}
static OfxStatus paramSetValue(OfxParamHandle h, ...) {
    va_list ap; va_start(ap, h);
    Param* p = PRM(h);
    if (isDoubleType(p->type)) p->d = va_arg(ap, double); else p->i = va_arg(ap, int);
    va_end(ap); return kOfxStatOK;
}

// ----------------------------------------------------------------- clips & images
constexpr double kFirstFrame = 0, kLastFrame = 99;
struct Image {
    PropertySet props;
    std::vector<float> px;
};
struct Clip {
    PropertySet props;
    bool isOutput = false;
};
struct Effect {
    PropertySet props;
    ParamSet params;
    std::map<std::string, std::unique_ptr<Clip>> clips;
};
static Effect* EF(OfxImageEffectHandle h) { return reinterpret_cast<Effect*>(h); }

static int gW = 320, gH = 180;
static int gOutstandingImages = 0;
static int gOutOfRangeRequests = 0;
static std::vector<double> gFetchedTimes;

static OfxStatus getPropertySet(OfxImageEffectHandle e, OfxPropertySetHandle* p) { *p = H(&EF(e)->props); return kOfxStatOK; }
static OfxStatus getParamSet(OfxImageEffectHandle e, OfxParamSetHandle* p) { *p = reinterpret_cast<OfxParamSetHandle>(&EF(e)->params); return kOfxStatOK; }
static OfxStatus clipDefine(OfxImageEffectHandle e, const char* name, OfxPropertySetHandle* p) {
    auto c = std::make_unique<Clip>();
    c->isOutput = std::strcmp(name, kOfxImageEffectOutputClipName) == 0;
    *p = H(&c->props);
    EF(e)->clips[name] = std::move(c);
    return kOfxStatOK;
}
static OfxStatus clipGetHandle(OfxImageEffectHandle e, const char* name, OfxImageClipHandle* c, OfxPropertySetHandle* p) {
    auto it = EF(e)->clips.find(name);
    if (it == EF(e)->clips.end()) return kOfxStatErrBadHandle;
    *c = reinterpret_cast<OfxImageClipHandle>(it->second.get());
    if (p) *p = H(&it->second->props);
    return kOfxStatOK;
}
static OfxStatus clipGetPropertySet(OfxImageClipHandle c, OfxPropertySetHandle* p) { *p = H(&reinterpret_cast<Clip*>(c)->props); return kOfxStatOK; }
static OfxStatus clipGetImage(OfxImageClipHandle ch, OfxTime t, const OfxRectD*, OfxPropertySetHandle* out) {
    Clip* c = reinterpret_cast<Clip*>(ch);
    if (!c->isOutput) {
        gFetchedTimes.push_back(t);
        if (t < kFirstFrame || t > kLastFrame) { ++gOutOfRangeRequests; return kOfxStatFailed; }
    }
    auto* img = new Image;
    img->px.assign(size_t(gW) * gH * 4, 0.0f);
    if (!c->isOutput) {
        // Source frame: a vertical stripe that moves 3 px per frame, so
        // different frames are clearly different.
        for (int y = 0; y < gH; ++y)
            for (int x = 0; x < gW; ++x) {
                float* p = &img->px[(size_t(y) * gW + x) * 4];
                const bool stripe = ((x + int(t) * 3) / 20) % 2 == 0;
                p[0] = stripe ? 0.9f : 0.1f;
                p[1] = float(y) / gH;
                p[2] = float(t) / 100.0f;
                p[3] = 1.0f;
            }
    }
    PropertySet& p = img->props;
    p.at(kOfxImagePropData, 0).p = img->px.data();
    const int b[4] = {0, 0, gW, gH};
    for (int i = 0; i < 4; ++i) p.at(kOfxImagePropBounds, i).i = b[i];
    p.at(kOfxImagePropRowBytes, 0).i = gW * 16;
    p.at(kOfxImageEffectPropPixelDepth, 0).s = kOfxBitDepthFloat;
    p.at(kOfxImageEffectPropComponents, 0).s = kOfxImageComponentRGBA;
    p.at("mockImage", 0).p = img;
    ++gOutstandingImages;
    *out = H(&img->props);
    return kOfxStatOK;
}
static Image* gLastOutput = nullptr;  // kept alive for inspection
static OfxStatus clipReleaseImage(OfxPropertySetHandle h) {
    Image* img = static_cast<Image*>(PS(h)->get("mockImage", 0)->p);
    --gOutstandingImages;
    if (PS(h)->get("mockOutput", 0)) return kOfxStatOK;
    delete img;
    return kOfxStatOK;
}
static OfxStatus clipGetRoD(OfxImageClipHandle, OfxTime, OfxRectD* r) { *r = OfxRectD{0, 0, double(gW), double(gH)}; return kOfxStatOK; }
static int abortFn(OfxImageEffectHandle) { return 0; }

// ----------------------------------------------------------------- host
static OfxPropertySuiteV1 gPropSuite;
static OfxParameterSuiteV1 gParamSuite;
static OfxImageEffectSuiteV1 gEffectSuite;
static PropertySet gHostProps;
static const void* fetchSuite(OfxPropertySetHandle, const char* name, int version) {
    if (!std::strcmp(name, kOfxPropertySuite) && version == 1) return &gPropSuite;
    if (!std::strcmp(name, kOfxParameterSuite) && version == 1) return &gParamSuite;
    if (!std::strcmp(name, kOfxImageEffectSuite) && version == 1) return &gEffectSuite;
    return nullptr;
}
static OfxHost gHost{H(&gHostProps), fetchSuite};

static void initSuites() {
    gPropSuite.propSetPointer = setP; gPropSuite.propSetString = setS; gPropSuite.propSetDouble = setD;
    gPropSuite.propSetInt = setI; gPropSuite.propSetPointerN = setPN; gPropSuite.propSetStringN = setSN;
    gPropSuite.propSetDoubleN = setDN; gPropSuite.propSetIntN = setIN; gPropSuite.propGetPointer = getP;
    gPropSuite.propGetString = getS; gPropSuite.propGetDouble = getD; gPropSuite.propGetInt = getI;
    gPropSuite.propGetPointerN = getPN; gPropSuite.propGetStringN = getSN; gPropSuite.propGetDoubleN = getDN;
    gPropSuite.propGetIntN = getIN; gPropSuite.propReset = propReset; gPropSuite.propGetDimension = propDim;
    gParamSuite.paramDefine = paramDefine; gParamSuite.paramGetHandle = paramGetHandle;
    gParamSuite.paramGetPropertySet = paramGetPropertySet; gParamSuite.paramGetValue = paramGetValue;
    gParamSuite.paramGetValueAtTime = paramGetValueAtTime; gParamSuite.paramSetValue = paramSetValue;
    gEffectSuite.getPropertySet = getPropertySet; gEffectSuite.getParamSet = getParamSet;
    gEffectSuite.clipDefine = clipDefine; gEffectSuite.clipGetHandle = clipGetHandle;
    gEffectSuite.clipGetPropertySet = clipGetPropertySet; gEffectSuite.clipGetImage = clipGetImage;
    gEffectSuite.clipReleaseImage = clipReleaseImage; gEffectSuite.clipGetRegionOfDefinition = clipGetRoD;
    gEffectSuite.abort = abortFn;
}

static OfxPlugin* gPlugin = nullptr;
static OfxStatus act(const char* action, Effect* e, PropertySet* in = nullptr, PropertySet* out = nullptr) {
    return gPlugin->mainEntry(action, e, in ? H(in) : nullptr, out ? H(out) : nullptr);
}

// Instance = copy of the context descriptor's params (with defaults applied).
static std::unique_ptr<Effect> makeInstance(Effect& desc) {
    auto inst = std::make_unique<Effect>();
    for (const auto& name : desc.params.order) {
        const Param& d = *desc.params.params[name];
        auto p = std::make_unique<Param>(d);
        if (const Value* v = d.props.get(kOfxParamPropDefault, 0)) { p->d = v->d; p->i = v->i; }
        inst->params.order.push_back(name);
        inst->params.params[name] = std::move(p);
    }
    for (auto& [name, clip] : desc.clips) {
        auto c = std::make_unique<Clip>(*clip);
        c->props.at(kOfxImageEffectPropFrameRange, 0).d = kFirstFrame;
        c->props.at(kOfxImageEffectPropFrameRange, 1).d = kLastFrame;
        inst->clips[name] = std::move(c);
    }
    return inst;
}
static Param* P(Effect& e, const char* n) { auto it = e.params.params.find(n); return it == e.params.params.end() ? nullptr : it->second.get(); }

static OfxStatus framesNeeded(Effect& e, double t, double& lo, double& hi) {
    PropertySet in, out;
    in.at(kOfxPropTime, 0).d = t;
    out.at("OfxImageClipPropFrameRange_Source", 0).d = t;
    out.at("OfxImageClipPropFrameRange_Source", 1).d = t;
    OfxStatus s = act(kOfxImageEffectActionGetFramesNeeded, &e, &in, &out);
    lo = out.get("OfxImageClipPropFrameRange_Source", 0)->d;
    hi = out.get("OfxImageClipPropFrameRange_Source", 1)->d;
    return s;
}

// A render where we capture the output: wrap clipGetImage for the output clip.
static std::vector<float> gCaptured;
static OfxStatus (*gRealGetImage)(OfxImageClipHandle, OfxTime, const OfxRectD*, OfxPropertySetHandle*) = nullptr;
static OfxStatus captureGetImage(OfxImageClipHandle ch, OfxTime t, const OfxRectD* r, OfxPropertySetHandle* out) {
    OfxStatus s = gRealGetImage(ch, t, r, out);
    if (s == kOfxStatOK && reinterpret_cast<Clip*>(ch)->isOutput) {
        Image* img = static_cast<Image*>(PS(*out)->get("mockImage", 0)->p);
        for (float& v : img->px) v = NAN;
        PS(*out)->at("mockOutput", 0).i = 1;
        gLastOutput = img;
    }
    return s;
}
static std::vector<float> renderCapture(Effect& e, double t, OfxStatus& st) {
    gRealGetImage = clipGetImage;
    gEffectSuite.clipGetImage = captureGetImage;
    gLastOutput = nullptr;
    PropertySet in;
    in.at(kOfxPropTime, 0).d = t;
    const int win[4] = {0, 0, gW, gH};
    for (int i = 0; i < 4; ++i) in.at(kOfxImageEffectPropRenderWindow, i).i = win[i];
    in.at(kOfxImageEffectPropRenderScale, 0).d = 1;
    in.at(kOfxImageEffectPropRenderScale, 1).d = 1;
    gFetchedTimes.clear();
    st = act(kOfxImageEffectActionRender, &e, &in, nullptr);
    gEffectSuite.clipGetImage = clipGetImage;
    std::vector<float> r;
    if (gLastOutput) { r = gLastOutput->px; delete gLastOutput; gLastOutput = nullptr; }
    return r;
}

static bool allFinite(const std::vector<float>& v) {
    for (float f : v) if (!std::isfinite(f)) return false;
    return !v.empty();
}

int main(int argc, char** argv) {
    if (argc < 2) { std::puts("usage: mock_host TimingShiftFX.ofx"); return 2; }
    initSuites();
    void* lib = dlopenAny(argv[1]);
    if (!lib) { std::printf("dlopen failed: %s\n", dlerrorAny()); return 1; }
    auto getNum = reinterpret_cast<int (*)()>(dlsymAny(lib, "OfxGetNumberOfPlugins"));
    auto getPlugin = reinterpret_cast<OfxPlugin* (*)(int)>(dlsymAny(lib, "OfxGetPlugin"));

    std::puts("[1] entry points");
    CHECK(getNum && getPlugin);
    if (!getNum || !getPlugin) return 1;
    CHECK(getNum() == 1);
    gPlugin = getPlugin(0);
    CHECK(gPlugin && !std::strcmp(gPlugin->pluginApi, kOfxImageEffectPluginApi));
    std::printf("  plugin: %s v%u.%u\n", gPlugin->pluginIdentifier, gPlugin->pluginVersionMajor, gPlugin->pluginVersionMinor);
    gPlugin->setHost(&gHost);

    std::puts("[2] load + describe");
    CHECK(act(kOfxActionLoad, nullptr) == kOfxStatOK);
    Effect desc;
    CHECK(act(kOfxActionDescribe, &desc) == kOfxStatOK);
    int temporal = 0; getI(H(&desc.props), kOfxImageEffectPropTemporalClipAccess, 0, &temporal);
    CHECK(temporal == 1);
    char* s = nullptr;
    CHECK(getS(H(&desc.props), kOfxImageEffectPropSupportedContexts, 0, &s) == kOfxStatOK && !std::strcmp(s, kOfxImageEffectContextFilter));
    CHECK(getS(H(&desc.props), kOfxPropLabel, 0, &s) == kOfxStatOK);
    std::printf("  label: %s\n", s);

    std::puts("[3] describeInContext(filter): clips + Inspector parameters");
    Effect ctx;
    PropertySet ctxIn;
    ctxIn.at(kOfxImageEffectPropContext, 0).s = kOfxImageEffectContextFilter;
    CHECK(act(kOfxImageEffectActionDescribeInContext, &ctx, &ctxIn) == kOfxStatOK);
    CHECK(ctx.clips.count(kOfxImageEffectSimpleSourceClipName) == 1);
    CHECK(ctx.clips.count(kOfxImageEffectOutputClipName) == 1);
    const char* required[] = {"enable", "masterIntensity", "timingOffset", "hDisplacement", "rgbSplit",
                              "ghostAmount", "jitterAmount", "jitterSpeed", "streakAmount", "streakEnable",
                              "blend", "seed", "quality", "preset"};
    for (const char* r : required) {
        const bool has = P(ctx, r) != nullptr;
        if (!has) std::printf("  missing param: %s\n", r);
        CHECK(has);
    }
    std::printf("  %zu parameters defined:", ctx.params.order.size());
    for (auto& n : ctx.params.order) {
        char* lbl = nullptr; getS(H(&P(ctx, n.c_str())->props), kOfxPropLabel, 0, &lbl);
        std::printf(" [%s]", lbl ? lbl : n.c_str());
    }
    std::puts("");

    std::puts("[4] create instance + frames needed (temporal access, clip boundaries)");
    auto inst = makeInstance(ctx);
    CHECK(act(kOfxActionCreateInstance, inst.get()) == kOfxStatOK);
    double lo, hi;
    CHECK(framesNeeded(*inst, 50, lo, hi) == kOfxStatOK);
    std::printf("  t=50 offset=2 -> [%g, %g]\n", lo, hi);
    CHECK(lo == 48 && hi == 50);
    CHECK(framesNeeded(*inst, 1, lo, hi) == kOfxStatOK);
    std::printf("  t=1  offset=2 -> [%g, %g] (clamped to clip start 0)\n", lo, hi);
    CHECK(lo == 0 && hi == 1);
    P(*inst, "timingOffset")->i = -3;
    P(*inst, "ghostAmount")->d = 0.5;
    P(*inst, "trailLength")->i = 2;
    CHECK(framesNeeded(*inst, 98, lo, hi) == kOfxStatOK);
    std::printf("  t=98 offset=-3 trail=2 -> [%g, %g] (clamped to clip end 99)\n", lo, hi);
    CHECK(lo == 98 && hi == 99);
    P(*inst, "timingOffset")->i = 2;
    P(*inst, "ghostAmount")->d = 0.25;
    P(*inst, "trailLength")->i = 1;

    std::puts("[5] isIdentity");
    {
        PropertySet in, out;
        in.at(kOfxPropTime, 0).d = 10;
        CHECK(act(kOfxImageEffectActionIsIdentity, inst.get(), &in, &out) == kOfxStatReplyDefault);
        P(*inst, "enable")->i = 0;
        CHECK(act(kOfxImageEffectActionIsIdentity, inst.get(), &in, &out) == kOfxStatOK);
        CHECK(out.get(kOfxPropName, 0) && out.get(kOfxPropName, 0)->s == kOfxImageEffectSimpleSourceClipName);
        P(*inst, "enable")->i = 1;
    }

    std::puts("[6] render (default 'subtle' look)");
    OfxStatus st;
    gGetValueCalls = 0;
    auto a = renderCapture(*inst, 50, st);
    CHECK(st == kOfxStatOK);
    CHECK(allFinite(a));  // every pixel written, no NaN
    CHECK(gOutstandingImages == 0);  // all images released
    CHECK(gGetValueCalls == 0);      // render uses paramGetValueAtTime only
    bool fetched48 = false;
    for (double t : gFetchedTimes) fetched48 |= (t == 48);
    CHECK(fetched48);
    auto b = renderCapture(*inst, 50, st);
    CHECK(a == b);  // deterministic

    std::puts("[7] render at clip start (t=0, offset 2) - boundary handling");
    auto c = renderCapture(*inst, 0, st);
    CHECK(st == kOfxStatOK && allFinite(c));
    CHECK(gOutOfRangeRequests == 0);
    CHECK(gOutstandingImages == 0);

    std::puts("[8] preset 'Music Video Extreme' via instanceChanged");
    {
        P(*inst, "preset")->i = 2;
        PropertySet in;
        in.at(kOfxPropType, 0).s = kOfxTypeParameter;
        in.at(kOfxPropName, 0).s = "preset";
        in.at(kOfxPropChangeReason, 0).s = kOfxChangePluginEdited;  // e.g. project load
        act(kOfxActionInstanceChanged, inst.get(), &in);
        CHECK(P(*inst, "timingOffset")->i == 2);  // NOT applied on non-user changes
        in.at(kOfxPropChangeReason, 0).s = kOfxChangeUserEdited;
        CHECK(act(kOfxActionInstanceChanged, inst.get(), &in) == kOfxStatOK);
        CHECK(P(*inst, "timingOffset")->i == 4);
        CHECK(P(*inst, "streakEnable")->i == 1);
        CHECK(P(*inst, "quality")->i == 2);
        CHECK(std::fabs(P(*inst, "hDisplacement")->d - 140.0) < 1e-9);
    }
    gW = 3840; gH = 2160;
    auto t0 = std::chrono::steady_clock::now();
    auto d = renderCapture(*inst, 50, st);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  UHD render with extreme preset: %.0f ms, fetched %zu source frames\n", ms, gFetchedTimes.size());
    CHECK(st == kOfxStatOK && allFinite(d));
    CHECK(gFetchedTimes.size() == 4);  // current + 3 echoes (trail 3, quality Best)
    CHECK(gOutstandingImages == 0);

    std::puts("[9] disabled effect renders an exact copy");
    gW = 320; gH = 180;
    P(*inst, "enable")->i = 0;
    auto e = renderCapture(*inst, 20, st);
    {
        // reference: what the host's source frame 20 looks like
        OfxPropertySetHandle src;
        Clip* srcClip = inst->clips[kOfxImageEffectSimpleSourceClipName].get();
        clipGetImage(reinterpret_cast<OfxImageClipHandle>(srcClip), 20, nullptr, &src);
        Image* ref = static_cast<Image*>(PS(src)->get("mockImage", 0)->p);
        CHECK(st == kOfxStatOK && e == ref->px);
        clipReleaseImage(src);
    }

    act(kOfxActionDestroyInstance, inst.get());
    CHECK(act(kOfxActionUnload, nullptr) == kOfxStatOK);
    dlcloseAny(lib);

    if (gFailures) { std::printf("%d check(s) FAILED\n", gFailures); return 1; }
    std::puts("ALL MOCK-HOST TESTS PASSED");
    return 0;
}
