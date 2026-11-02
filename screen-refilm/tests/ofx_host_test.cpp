// Minimal OpenFX host used to smoke-test the plugin binary: loads the .ofx,
// runs load/describe/describeInContext, applies a preset through
// instanceChanged and renders one frame. Exit code 0 on success.

#include <dlfcn.h>

#include <algorithm>
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

namespace {

struct Value {
    std::vector<std::string> s;
    std::vector<int> i;
    std::vector<double> d;
    std::vector<void*> p;
};
struct PropSet {
    std::map<std::string, Value> v;
};

template <class T> void put(std::vector<T>& vec, int idx, T val) {
    if (int(vec.size()) <= idx) vec.resize(idx + 1);
    vec[idx] = val;
}

OfxStatus propSetPointer(OfxPropertySetHandle h, const char* n, int i, void* v) { put(((PropSet*)h)->v[n].p, i, v); return kOfxStatOK; }
OfxStatus propSetString(OfxPropertySetHandle h, const char* n, int i, const char* v) { put(((PropSet*)h)->v[n].s, i, std::string(v)); return kOfxStatOK; }
OfxStatus propSetDouble(OfxPropertySetHandle h, const char* n, int i, double v) { put(((PropSet*)h)->v[n].d, i, v); return kOfxStatOK; }
OfxStatus propSetInt(OfxPropertySetHandle h, const char* n, int i, int v) { put(((PropSet*)h)->v[n].i, i, v); return kOfxStatOK; }
OfxStatus propSetPointerN(OfxPropertySetHandle h, const char* n, int c, void* const* v) { for (int k = 0; k < c; ++k) propSetPointer(h, n, k, v[k]); return kOfxStatOK; }
OfxStatus propSetStringN(OfxPropertySetHandle h, const char* n, int c, const char* const* v) { for (int k = 0; k < c; ++k) propSetString(h, n, k, v[k]); return kOfxStatOK; }
OfxStatus propSetDoubleN(OfxPropertySetHandle h, const char* n, int c, const double* v) { for (int k = 0; k < c; ++k) propSetDouble(h, n, k, v[k]); return kOfxStatOK; }
OfxStatus propSetIntN(OfxPropertySetHandle h, const char* n, int c, const int* v) { for (int k = 0; k < c; ++k) propSetInt(h, n, k, v[k]); return kOfxStatOK; }

Value* find(OfxPropertySetHandle h, const char* n) {
    auto& m = ((PropSet*)h)->v;
    auto it = m.find(n);
    return it == m.end() ? nullptr : &it->second;
}
OfxStatus propGetPointer(OfxPropertySetHandle h, const char* n, int i, void** v) { Value* x = find(h, n); if (!x || int(x->p.size()) <= i) return kOfxStatErrUnknown; *v = x->p[i]; return kOfxStatOK; }
OfxStatus propGetString(OfxPropertySetHandle h, const char* n, int i, char** v) { Value* x = find(h, n); if (!x || int(x->s.size()) <= i) return kOfxStatErrUnknown; *v = (char*)x->s[i].c_str(); return kOfxStatOK; }
OfxStatus propGetDouble(OfxPropertySetHandle h, const char* n, int i, double* v) { Value* x = find(h, n); if (!x || int(x->d.size()) <= i) return kOfxStatErrUnknown; *v = x->d[i]; return kOfxStatOK; }
OfxStatus propGetInt(OfxPropertySetHandle h, const char* n, int i, int* v) { Value* x = find(h, n); if (!x || int(x->i.size()) <= i) return kOfxStatErrUnknown; *v = x->i[i]; return kOfxStatOK; }
OfxStatus propGetPointerN(OfxPropertySetHandle h, const char* n, int c, void** v) { for (int k = 0; k < c; ++k) if (propGetPointer(h, n, k, v + k)) return kOfxStatErrUnknown; return kOfxStatOK; }
OfxStatus propGetStringN(OfxPropertySetHandle h, const char* n, int c, char** v) { for (int k = 0; k < c; ++k) if (propGetString(h, n, k, v + k)) return kOfxStatErrUnknown; return kOfxStatOK; }
OfxStatus propGetDoubleN(OfxPropertySetHandle h, const char* n, int c, double* v) { for (int k = 0; k < c; ++k) if (propGetDouble(h, n, k, v + k)) return kOfxStatErrUnknown; return kOfxStatOK; }
OfxStatus propGetIntN(OfxPropertySetHandle h, const char* n, int c, int* v) { for (int k = 0; k < c; ++k) if (propGetInt(h, n, k, v + k)) return kOfxStatErrUnknown; return kOfxStatOK; }
OfxStatus propReset(OfxPropertySetHandle, const char*) { return kOfxStatOK; }
OfxStatus propGetDimension(OfxPropertySetHandle h, const char* n, int* c) {
    Value* x = find(h, n);
    *c = x ? int(std::max(std::max(x->s.size(), x->i.size()), std::max(x->d.size(), x->p.size()))) : 0;
    return kOfxStatOK;
}
OfxPropertySuiteV1 gPropSuite = {propSetPointer, propSetString, propSetDouble, propSetInt, propSetPointerN,
                                 propSetStringN, propSetDoubleN, propSetIntN, propGetPointer, propGetString,
                                 propGetDouble, propGetInt, propGetPointerN, propGetStringN, propGetDoubleN,
                                 propGetIntN, propReset, propGetDimension};

// --- Params ---------------------------------------------------------------
struct Param {
    std::string type;
    PropSet props;
    double v[2] = {0, 0};
    int iv = 0;
};
struct ParamSet {
    std::map<std::string, std::unique_ptr<Param>> params;
    std::vector<std::string> order;
};

OfxStatus paramDefine(OfxParamSetHandle ps, const char* type, const char* name, OfxPropertySetHandle* props) {
    auto* set = (ParamSet*)ps;
    if (set->params.count(name)) return kOfxStatErrExists;
    auto p = std::make_unique<Param>();
    p->type = type;
    if (props) *props = (OfxPropertySetHandle)&p->props;
    set->order.push_back(name);
    set->params[name] = std::move(p);
    return kOfxStatOK;
}
OfxStatus paramGetHandle(OfxParamSetHandle ps, const char* name, OfxParamHandle* h, OfxPropertySetHandle* props) {
    auto* set = (ParamSet*)ps;
    auto it = set->params.find(name);
    if (it == set->params.end()) return kOfxStatErrUnknown;
    *h = (OfxParamHandle)it->second.get();
    if (props) *props = (OfxPropertySetHandle)&it->second->props;
    return kOfxStatOK;
}
OfxStatus paramSetGetPropertySet(OfxParamSetHandle, OfxPropertySetHandle*) { return kOfxStatErrUnsupported; }
OfxStatus paramGetPropertySet(OfxParamHandle h, OfxPropertySetHandle* p) { *p = (OfxPropertySetHandle) & ((Param*)h)->props; return kOfxStatOK; }
bool isDouble(const Param* p) { return p->type == kOfxParamTypeDouble || p->type == kOfxParamTypeDouble2D; }
OfxStatus getValue(OfxParamHandle h, va_list ap) {
    Param* p = (Param*)h;
    if (isDouble(p)) {
        *va_arg(ap, double*) = p->v[0];
        if (p->type == kOfxParamTypeDouble2D) *va_arg(ap, double*) = p->v[1];
    } else {
        *va_arg(ap, int*) = p->iv;
    }
    return kOfxStatOK;
}
OfxStatus paramGetValue(OfxParamHandle h, ...) { va_list ap; va_start(ap, h); OfxStatus s = getValue(h, ap); va_end(ap); return s; }
OfxStatus paramGetValueAtTimeImpl(OfxParamHandle h, OfxTime t, ...) { va_list ap; va_start(ap, t); OfxStatus s = getValue(h, ap); va_end(ap); return s; }
OfxStatus paramSetValue(OfxParamHandle h, ...) {
    Param* p = (Param*)h;
    va_list ap; va_start(ap, h);
    if (isDouble(p)) {
        p->v[0] = va_arg(ap, double);
        if (p->type == kOfxParamTypeDouble2D) p->v[1] = va_arg(ap, double);
    } else {
        p->iv = va_arg(ap, int);
    }
    va_end(ap);
    return kOfxStatOK;
}
OfxStatus paramEditBegin(OfxParamSetHandle, const char*) { return kOfxStatOK; }
OfxStatus paramEditEnd(OfxParamSetHandle) { return kOfxStatOK; }

OfxParameterSuiteV1 makeParamSuite() {
    OfxParameterSuiteV1 s;
    std::memset(&s, 0, sizeof s);
    s.paramDefine = paramDefine;
    s.paramGetHandle = paramGetHandle;
    s.paramSetGetPropertySet = paramSetGetPropertySet;
    s.paramGetPropertySet = paramGetPropertySet;
    s.paramGetValue = paramGetValue;
    s.paramGetValueAtTime = paramGetValueAtTimeImpl;
    s.paramSetValue = paramSetValue;
    s.paramEditBegin = paramEditBegin;
    s.paramEditEnd = paramEditEnd;
    return s;
}
OfxParameterSuiteV1 gParamSuite = makeParamSuite();

// --- Effect ---------------------------------------------------------------
struct Clip {
    PropSet props;
    int w = 0, h = 0;
    std::vector<float> pixels;
    PropSet image;
};
struct Effect {
    PropSet props;
    ParamSet params;
    std::map<std::string, std::unique_ptr<Clip>> clips;
};

OfxStatus getPropertySet(OfxImageEffectHandle e, OfxPropertySetHandle* p) { *p = (OfxPropertySetHandle) & ((Effect*)e)->props; return kOfxStatOK; }
OfxStatus getParamSet(OfxImageEffectHandle e, OfxParamSetHandle* p) { *p = (OfxParamSetHandle) & ((Effect*)e)->params; return kOfxStatOK; }
OfxStatus clipDefine(OfxImageEffectHandle e, const char* name, OfxPropertySetHandle* p) {
    auto c = std::make_unique<Clip>();
    *p = (OfxPropertySetHandle)&c->props;
    ((Effect*)e)->clips[name] = std::move(c);
    return kOfxStatOK;
}
OfxStatus clipGetHandle(OfxImageEffectHandle e, const char* name, OfxImageClipHandle* c, OfxPropertySetHandle* p) {
    auto& m = ((Effect*)e)->clips;
    auto it = m.find(name);
    if (it == m.end()) return kOfxStatErrUnknown;
    *c = (OfxImageClipHandle)it->second.get();
    if (p) *p = (OfxPropertySetHandle)&it->second->props;
    return kOfxStatOK;
}
OfxStatus clipGetPropertySet(OfxImageClipHandle c, OfxPropertySetHandle* p) { *p = (OfxPropertySetHandle) & ((Clip*)c)->props; return kOfxStatOK; }
OfxStatus clipGetImage(OfxImageClipHandle ch, OfxTime, const OfxRectD*, OfxPropertySetHandle* img) {
    Clip* c = (Clip*)ch;
    PropSet& im = c->image;
    im.v.clear();
    propSetPointer((OfxPropertySetHandle)&im, kOfxImagePropData, 0, c->pixels.data());
    int b[4] = {0, 0, c->w, c->h};
    propSetIntN((OfxPropertySetHandle)&im, kOfxImagePropBounds, 4, b);
    propSetInt((OfxPropertySetHandle)&im, kOfxImagePropRowBytes, 0, c->w * 16);
    propSetString((OfxPropertySetHandle)&im, kOfxImageEffectPropPixelDepth, 0, kOfxBitDepthFloat);
    propSetString((OfxPropertySetHandle)&im, kOfxImageEffectPropComponents, 0, kOfxImageComponentRGBA);
    *img = (OfxPropertySetHandle)&im;
    return kOfxStatOK;
}
OfxStatus clipReleaseImage(OfxPropertySetHandle) { return kOfxStatOK; }

OfxImageEffectSuiteV1 makeEffectSuite() {
    OfxImageEffectSuiteV1 s;
    std::memset(&s, 0, sizeof s);
    s.getPropertySet = getPropertySet;
    s.getParamSet = getParamSet;
    s.clipDefine = clipDefine;
    s.clipGetHandle = clipGetHandle;
    s.clipGetPropertySet = clipGetPropertySet;
    s.clipGetImage = clipGetImage;
    s.clipReleaseImage = clipReleaseImage;
    return s;
}
OfxImageEffectSuiteV1 gEffectSuite = makeEffectSuite();

const void* fetchSuite(OfxPropertySetHandle, const char* name, int version) {
    if (!std::strcmp(name, kOfxPropertySuite) && version == 1) return &gPropSuite;
    if (!std::strcmp(name, kOfxParameterSuite) && version == 1) return &gParamSuite;
    if (!std::strcmp(name, kOfxImageEffectSuite) && version == 1) return &gEffectSuite;
    return nullptr;
}

PropSet gHostProps;
OfxHost gHost = {(OfxPropertySetHandle)&gHostProps, fetchSuite};

int fail(const char* m) { std::fprintf(stderr, "FAIL: %s\n", m); return 1; }

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return fail("usage: ofx_host_test path/to/ScreenRefilm.ofx");
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr, "%s\n", dlerror()); return 1; }
    auto getN = (int (*)())dlsym(lib, "OfxGetNumberOfPlugins");
    auto getP = (OfxPlugin * (*)(int)) dlsym(lib, "OfxGetPlugin");
    if (!getN || !getP || getN() != 1) return fail("entry points");
    OfxPlugin* pl = getP(0);
    std::printf("plugin %s v%u.%u api %s\n", pl->pluginIdentifier, pl->pluginVersionMajor, pl->pluginVersionMinor, pl->pluginApi);
    pl->setHost(&gHost);
    if (pl->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr) != kOfxStatOK) return fail("load");

    Effect desc;
    if (pl->mainEntry(kOfxActionDescribe, &desc, nullptr, nullptr) != kOfxStatOK) return fail("describe");
    char* label = nullptr;
    propGetString((OfxPropertySetHandle)&desc.props, kOfxPropLabel, 0, &label);
    std::printf("label: %s\n", label ? label : "?");

    Effect inst;
    PropSet ctxArgs;
    propSetString((OfxPropertySetHandle)&ctxArgs, kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);
    if (pl->mainEntry(kOfxImageEffectActionDescribeInContext, &inst, (OfxPropertySetHandle)&ctxArgs, nullptr) != kOfxStatOK)
        return fail("describeInContext");
    if (!inst.clips.count(kOfxImageEffectSimpleSourceClipName) || !inst.clips.count(kOfxImageEffectOutputClipName))
        return fail("clips");
    std::printf("params defined: %zu\n", inst.params.order.size());

    // Initialise params with their defaults, as a host would.
    for (auto& kv : inst.params.params) {
        Param& p = *kv.second;
        OfxPropertySetHandle ph = (OfxPropertySetHandle)&p.props;
        if (isDouble(&p)) { propGetDouble(ph, kOfxParamPropDefault, 0, &p.v[0]); propGetDouble(ph, kOfxParamPropDefault, 1, &p.v[1]); }
        else propGetInt(ph, kOfxParamPropDefault, 0, &p.iv);
    }
    Param* zoom = inst.params.params.at("zoom").get();
    if (std::fabs(zoom->v[0] - 3.0) > 1e-9) return fail("zoom default");

    // Choose the "Living-Room TV" preset through instanceChanged.
    inst.params.params.at("preset")->iv = 2;
    PropSet ic;
    propSetString((OfxPropertySetHandle)&ic, kOfxPropType, 0, kOfxTypeParameter);
    propSetString((OfxPropertySetHandle)&ic, kOfxPropName, 0, "preset");
    propSetString((OfxPropertySetHandle)&ic, kOfxPropChangeReason, 0, kOfxChangeUserEdited);
    propSetDouble((OfxPropertySetHandle)&ic, kOfxPropTime, 0, 0);
    if (pl->mainEntry(kOfxActionInstanceChanged, &inst, (OfxPropertySetHandle)&ic, nullptr) != kOfxStatOK)
        return fail("instanceChanged");
    if (inst.params.params.at("screen")->iv != 1 || std::fabs(zoom->v[0] - 1.6) > 1e-9) return fail("preset not applied");
    std::printf("preset applied: screen=%d zoom=%.2f\n", inst.params.params.at("screen")->iv, zoom->v[0]);

    const int W = 320, H = 180;
    for (auto& kv : inst.clips) { kv.second->w = W; kv.second->h = H; kv.second->pixels.assign(size_t(W) * H * 4, 0.f); }
    Clip& src = *inst.clips.at(kOfxImageEffectSimpleSourceClipName);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float* p = &src.pixels[(size_t(y) * W + x) * 4];
            p[0] = float(x) / W; p[1] = float(y) / H; p[2] = ((x / 20 + y / 20) & 1) ? 0.9f : 0.1f; p[3] = 1;
        }
    propSetDouble((OfxPropertySetHandle)&inst.props, kOfxImageEffectPropFrameRate, 0, 25.0);

    PropSet ra;
    propSetDouble((OfxPropertySetHandle)&ra, kOfxPropTime, 0, 12.0);
    int win[4] = {0, 0, W, H};
    propSetIntN((OfxPropertySetHandle)&ra, kOfxImageEffectPropRenderWindow, 4, win);
    double rs[2] = {1, 1};
    propSetDoubleN((OfxPropertySetHandle)&ra, kOfxImageEffectPropRenderScale, 2, rs);
    propSetString((OfxPropertySetHandle)&ra, kOfxImageEffectPropFieldToRender, 0, kOfxImageFieldNone);
    if (pl->mainEntry(kOfxImageEffectActionRender, &inst, (OfxPropertySetHandle)&ra, nullptr) != kOfxStatOK) return fail("render");

    const Clip& out = *inst.clips.at(kOfxImageEffectOutputClipName);
    double sum = 0, mn = 1e9, mx = -1e9;
    for (size_t i = 0; i < out.pixels.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            float v = out.pixels[i + c];
            if (!std::isfinite(v)) return fail("non-finite output");
            sum += v; mn = std::min(mn, double(v)); mx = std::max(mx, double(v));
        }
        if (out.pixels[i + 3] != 1.f) return fail("alpha");
    }
    double mean = sum / (out.pixels.size() / 4 * 3);
    std::printf("output mean %.3f min %.3f max %.3f\n", mean, mn, mx);
    if (mean < 0.05 || mx - mn < 0.1) return fail("output looks empty");

    pl->mainEntry(kOfxActionUnload, nullptr, nullptr, nullptr);
    std::printf("OK\n");
    return 0;
}
