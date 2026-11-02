// Screen Refilm - OpenFX image effect plugin (C API, no support library).

#include <cstring>
#include <algorithm>
#include <new>
#include <vector>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"

#include "refilm_params.h"
#include "refilm_render.h"

namespace {

OfxHost* gHost = nullptr;
OfxImageEffectSuiteV1* gEffect = nullptr;
OfxPropertySuiteV1* gProp = nullptr;
OfxParameterSuiteV1* gParam = nullptr;

const char* kPluginId = "org.editingplugins.ScreenRefilm";

OfxStatus onLoad() {
    if (!gHost) return kOfxStatErrMissingHostFeature;
    gEffect = (OfxImageEffectSuiteV1*)gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1);
    gProp = (OfxPropertySuiteV1*)gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1);
    gParam = (OfxParameterSuiteV1*)gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1);
    return (gEffect && gProp && gParam) ? kOfxStatOK : kOfxStatErrMissingHostFeature;
}

OfxStatus describe(OfxImageEffectHandle effect) {
    OfxPropertySetHandle props;
    gEffect->getPropertySet(effect, &props);
    gProp->propSetString(props, kOfxPropLabel, 0, "Screen Refilm");
    gProp->propSetString(props, kOfxPropShortLabel, 0, "Screen Refilm");
    gProp->propSetString(props, kOfxPropLongLabel, 0, "Screen Refilm (re-film footage off a display)");
    gProp->propSetString(props, kOfxPropPluginDescription, 0,
                         "Puts footage on a physical screen (LCD monitor, CRT TV or OLED phone) and re-films it "
                         "with a tilted close-up camera: visible RGB sub-pixels, focus falloff, lens bulge, "
                         "colour fringing, glow, flicker and moire.");
    gProp->propSetString(props, kOfxImageEffectPluginPropGrouping, 0, "Editing Plugins");
    gProp->propSetString(props, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    gProp->propSetString(props, kOfxImageEffectPropSupportedContexts, 1, kOfxImageEffectContextGeneral);
    gProp->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);
    gProp->propSetInt(props, kOfxImageEffectPluginPropSingleInstance, 0, 0);
    gProp->propSetString(props, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderFullySafe);
    gProp->propSetInt(props, kOfxImageEffectPluginPropHostFrameThreading, 0, 0);
    gProp->propSetInt(props, kOfxImageEffectPropSupportsMultiResolution, 0, 0);
    gProp->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);
    gProp->propSetInt(props, kOfxImageEffectPropTemporalClipAccess, 0, 0);
    gProp->propSetInt(props, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
    return kOfxStatOK;
}

void setStr(OfxPropertySetHandle p, const char* name, const char* v) { gProp->propSetString(p, name, 0, v); }

OfxStatus describeInContext(OfxImageEffectHandle effect) {
    OfxPropertySetHandle clip;
    gEffect->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &clip);
    setStr(clip, kOfxImageEffectPropSupportedComponents, kOfxImageComponentRGBA);
    gProp->propSetInt(clip, kOfxImageEffectPropSupportsTiles, 0, 0);
    gEffect->clipDefine(effect, kOfxImageEffectOutputClipName, &clip);
    setStr(clip, kOfxImageEffectPropSupportedComponents, kOfxImageComponentRGBA);
    gProp->propSetInt(clip, kOfxImageEffectPropSupportsTiles, 0, 0);

    OfxParamSetHandle params;
    gEffect->getParamSet(effect, &params);
    size_t n = 0;
    const refilm::ParamDesc* table = refilm::paramTable(&n);
    for (size_t i = 0; i < n; ++i) {
        const refilm::ParamDesc& d = table[i];
        const char* type = kOfxParamTypeDouble;
        switch (d.type) {
        case refilm::kTypeDouble: type = kOfxParamTypeDouble; break;
        case refilm::kTypeDouble2D: type = kOfxParamTypeDouble2D; break;
        case refilm::kTypeInt: type = kOfxParamTypeInteger; break;
        case refilm::kTypeBool: type = kOfxParamTypeBoolean; break;
        case refilm::kTypeChoice: type = kOfxParamTypeChoice; break;
        case refilm::kTypeGroup: type = kOfxParamTypeGroup; break;
        }
        OfxPropertySetHandle p;
        if (gParam->paramDefine(params, type, d.id, &p) != kOfxStatOK) return kOfxStatFailed;
        setStr(p, kOfxPropLabel, d.label);
        setStr(p, kOfxParamPropScriptName, d.id);
        if (d.hint) setStr(p, kOfxParamPropHint, d.hint);
        if (d.parent) setStr(p, kOfxParamPropParent, d.parent);

        switch (d.type) {
        case refilm::kTypeGroup:
            gProp->propSetInt(p, kOfxParamPropGroupOpen, 0, 1);
            break;
        case refilm::kTypeDouble:
            setStr(p, kOfxParamPropDoubleType, kOfxParamDoubleTypePlain);
            gProp->propSetDouble(p, kOfxParamPropDefault, 0, d.def);
            gProp->propSetDouble(p, kOfxParamPropMin, 0, d.min);
            gProp->propSetDouble(p, kOfxParamPropMax, 0, d.max);
            gProp->propSetDouble(p, kOfxParamPropDisplayMin, 0, d.dispMin);
            gProp->propSetDouble(p, kOfxParamPropDisplayMax, 0, d.dispMax);
            gProp->propSetDouble(p, kOfxParamPropIncrement, 0, (d.dispMax - d.dispMin) / 100.0);
            gProp->propSetInt(p, kOfxParamPropDigits, 0, 3);
            break;
        case refilm::kTypeDouble2D:
            setStr(p, kOfxParamPropDoubleType, kOfxParamDoubleTypePlain);
            for (int k = 0; k < 2; ++k) {
                gProp->propSetDouble(p, kOfxParamPropDefault, k, k ? d.def2 : d.def);
                gProp->propSetDouble(p, kOfxParamPropMin, k, d.min);
                gProp->propSetDouble(p, kOfxParamPropMax, k, d.max);
                gProp->propSetDouble(p, kOfxParamPropDisplayMin, k, d.dispMin);
                gProp->propSetDouble(p, kOfxParamPropDisplayMax, k, d.dispMax);
            }
            gProp->propSetString(p, kOfxParamPropDimensionLabel, 0, "X");
            gProp->propSetString(p, kOfxParamPropDimensionLabel, 1, "Y");
            gProp->propSetDouble(p, kOfxParamPropIncrement, 0, 0.01);
            gProp->propSetInt(p, kOfxParamPropDigits, 0, 4);
            break;
        case refilm::kTypeInt:
            gProp->propSetInt(p, kOfxParamPropDefault, 0, int(d.def));
            gProp->propSetInt(p, kOfxParamPropMin, 0, int(d.min));
            gProp->propSetInt(p, kOfxParamPropMax, 0, int(d.max));
            gProp->propSetInt(p, kOfxParamPropDisplayMin, 0, int(d.dispMin));
            gProp->propSetInt(p, kOfxParamPropDisplayMax, 0, int(d.dispMax));
            break;
        case refilm::kTypeBool:
            gProp->propSetInt(p, kOfxParamPropDefault, 0, int(d.def));
            break;
        case refilm::kTypeChoice:
            for (int k = 0; k < d.numOptions; ++k) gProp->propSetString(p, kOfxParamPropChoiceOption, k, d.options[k]);
            gProp->propSetInt(p, kOfxParamPropDefault, 0, int(d.def));
            if (std::strcmp(d.id, "preset") == 0) {
                gProp->propSetInt(p, kOfxParamPropAnimates, 0, 0);
                gProp->propSetInt(p, kOfxParamPropEvaluateOnChange, 0, 0);
            }
            break;
        }
    }
    return kOfxStatOK;
}

refilm::Params readParams(OfxImageEffectHandle instance, OfxTime time) {
    refilm::Params P;
    OfxParamSetHandle params;
    gEffect->getParamSet(instance, &params);
    size_t n = 0;
    const refilm::ParamDesc* table = refilm::paramTable(&n);
    for (size_t i = 0; i < n; ++i) {
        const refilm::ParamDesc& d = table[i];
        if (d.type == refilm::kTypeGroup) continue;
        OfxParamHandle h;
        if (gParam->paramGetHandle(params, d.id, &h, nullptr) != kOfxStatOK) continue;
        if (d.type == refilm::kTypeDouble) {
            double v = d.def;
            gParam->paramGetValueAtTime(h, time, &v);
            P.*(d.field) = v;
        } else if (d.type == refilm::kTypeDouble2D) {
            double x = d.def, y = d.def2;
            gParam->paramGetValueAtTime(h, time, &x, &y);
            P.*(d.field) = x;
            P.*(d.field2) = y;
        } else {
            int v = int(d.def);
            gParam->paramGetValueAtTime(h, time, &v);
            P.*(d.field) = v;
        }
    }
    return P;
}

OfxStatus instanceChanged(OfxImageEffectHandle instance, OfxPropertySetHandle inArgs) {
    char* type = nullptr;
    char* name = nullptr;
    char* reason = nullptr;
    gProp->propGetString(inArgs, kOfxPropType, 0, &type);
    gProp->propGetString(inArgs, kOfxPropName, 0, &name);
    gProp->propGetString(inArgs, kOfxPropChangeReason, 0, &reason);
    if (!type || !name || std::strcmp(type, kOfxTypeParameter) != 0 || std::strcmp(name, "preset") != 0)
        return kOfxStatReplyDefault;
    if (reason && std::strcmp(reason, kOfxChangeUserEdited) != 0) return kOfxStatReplyDefault;

    OfxParamSetHandle params;
    gEffect->getParamSet(instance, &params);
    OfxParamHandle presetH;
    if (gParam->paramGetHandle(params, "preset", &presetH, nullptr) != kOfxStatOK) return kOfxStatFailed;
    int index = 0;
    gParam->paramGetValue(presetH, &index);
    if (index <= 0) return kOfxStatOK;

    refilm::Params P;
    refilm::applyPreset(P, index);
    gParam->paramEditBegin(params, "Apply Screen Refilm preset");
    size_t n = 0;
    const refilm::ParamDesc* table = refilm::paramTable(&n);
    for (size_t i = 0; i < n; ++i) {
        const refilm::ParamDesc& d = table[i];
        if (d.type == refilm::kTypeGroup || std::strcmp(d.id, "preset") == 0) continue;
        OfxParamHandle h;
        if (gParam->paramGetHandle(params, d.id, &h, nullptr) != kOfxStatOK) continue;
        if (d.type == refilm::kTypeDouble) gParam->paramSetValue(h, P.*(d.field));
        else if (d.type == refilm::kTypeDouble2D) gParam->paramSetValue(h, P.*(d.field), P.*(d.field2));
        else gParam->paramSetValue(h, int(P.*(d.field)));
    }
    gParam->paramEditEnd(params);
    return kOfxStatOK;
}

struct ImageRef {
    OfxPropertySetHandle handle = nullptr;
    void* data = nullptr;
    OfxRectI bounds{0, 0, 0, 0};
    int rowBytes = 0;
    bool ok = false;
    ~ImageRef() { if (handle) gEffect->clipReleaseImage(handle); }

    void fetch(OfxImageEffectHandle instance, const char* clipName, OfxTime time) {
        OfxImageClipHandle clip;
        if (gEffect->clipGetHandle(instance, clipName, &clip, nullptr) != kOfxStatOK) return;
        if (gEffect->clipGetImage(clip, time, nullptr, &handle) != kOfxStatOK) { handle = nullptr; return; }
        gProp->propGetPointer(handle, kOfxImagePropData, 0, &data);
        gProp->propGetIntN(handle, kOfxImagePropBounds, 4, &bounds.x1);
        gProp->propGetInt(handle, kOfxImagePropRowBytes, 0, &rowBytes);
        char* depth = nullptr;
        char* comps = nullptr;
        gProp->propGetString(handle, kOfxImageEffectPropPixelDepth, 0, &depth);
        gProp->propGetString(handle, kOfxImageEffectPropComponents, 0, &comps);
        ok = data && depth && comps && std::strcmp(depth, kOfxBitDepthFloat) == 0 &&
             std::strcmp(comps, kOfxImageComponentRGBA) == 0;
    }
    int width() const { return bounds.x2 - bounds.x1; }
    int height() const { return bounds.y2 - bounds.y1; }
};

OfxStatus render(OfxImageEffectHandle instance, OfxPropertySetHandle inArgs) {
    OfxTime time = 0;
    gProp->propGetDouble(inArgs, kOfxPropTime, 0, &time);
    double renderScale[2] = {1, 1};
    gProp->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, renderScale);
    OfxRectI window{0, 0, 0, 0};
    gProp->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, &window.x1);

    ImageRef dst;
    dst.fetch(instance, kOfxImageEffectOutputClipName, time);
    if (!dst.handle) return kOfxStatFailed;
    if (!dst.ok) return kOfxStatErrImageFormat;

    // Frame rate turns host frames into seconds for the automatic motion.
    double fps = 0;
    OfxPropertySetHandle effectProps;
    gEffect->getPropertySet(instance, &effectProps);
    if (gProp->propGetDouble(effectProps, kOfxImageEffectPropFrameRate, 0, &fps) != kOfxStatOK || fps <= 0) {
        OfxImageClipHandle clip;
        OfxPropertySetHandle clipProps;
        if (gEffect->clipGetHandle(instance, kOfxImageEffectSimpleSourceClipName, &clip, &clipProps) == kOfxStatOK)
            gProp->propGetDouble(clipProps, kOfxImageEffectPropFrameRate, 0, &fps);
    }
    if (fps <= 0) fps = 24.0;

    refilm::Params P = readParams(instance, time);
    P.sourceScale = renderScale[1] > 0 ? renderScale[1] : 1.0;

    ImageRef src;
    src.fetch(instance, kOfxImageEffectSimpleSourceClipName, time);

    // Render the full output frame, then copy the requested window.
    const int W = dst.width(), H = dst.height();
    float* out = static_cast<float*>(dst.data);
    const std::ptrdiff_t dstStride = dst.rowBytes / std::ptrdiff_t(sizeof(float));

    float black[4] = {0, 0, 0, 1};
    refilm::ConstImage srcImg{black, 1, 1, 4};
    if (src.ok && src.width() > 0 && src.height() > 0)
        srcImg = {static_cast<const float*>(src.data), src.width(), src.height(),
                  src.rowBytes / std::ptrdiff_t(sizeof(float))};

    const bool fullWindow = window.x1 <= dst.bounds.x1 && window.y1 <= dst.bounds.y1 && window.x2 >= dst.bounds.x2 &&
                            window.y2 >= dst.bounds.y2;
    if (fullWindow) {
        refilm::Image dstImg{out, W, H, dstStride};
        refilm::render(srcImg, dstImg, time / fps, P);
        return kOfxStatOK;
    }
    std::vector<float> buf(size_t(W) * H * 4);
    refilm::Image tmpImg{buf.data(), W, H, std::ptrdiff_t(W) * 4};
    refilm::render(srcImg, tmpImg, time / fps, P);
    for (int y = std::max(window.y1, dst.bounds.y1); y < std::min(window.y2, dst.bounds.y2); ++y) {
        int x0 = std::max(window.x1, dst.bounds.x1), x1 = std::min(window.x2, dst.bounds.x2);
        if (x1 <= x0) break;
        const float* s = buf.data() + (size_t(y - dst.bounds.y1) * W + (x0 - dst.bounds.x1)) * 4;
        float* d = out + std::ptrdiff_t(y - dst.bounds.y1) * dstStride + std::ptrdiff_t(x0 - dst.bounds.x1) * 4;
        std::memcpy(d, s, size_t(x1 - x0) * 4 * sizeof(float));
    }
    return kOfxStatOK;
}

OfxStatus pluginMain(const char* action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle) {
    try {
        OfxImageEffectHandle effect = (OfxImageEffectHandle)handle;
        if (std::strcmp(action, kOfxActionLoad) == 0) return onLoad();
        if (std::strcmp(action, kOfxActionDescribe) == 0) return describe(effect);
        if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) return describeInContext(effect);
        if (std::strcmp(action, kOfxImageEffectActionRender) == 0) return render(effect, inArgs);
        if (std::strcmp(action, kOfxActionInstanceChanged) == 0) return instanceChanged(effect, inArgs);
    } catch (const std::bad_alloc&) {
        return kOfxStatErrMemory;
    } catch (...) {
        return kOfxStatFailed;
    }
    return kOfxStatReplyDefault;
}

void setHost(OfxHost* host) { gHost = host; }

OfxPlugin gPlugin = {kOfxImageEffectPluginApi, 1, kPluginId, 1, 0, setHost, pluginMain};

} // namespace

OfxExport int OfxGetNumberOfPlugins(void) { return 1; }
OfxExport OfxPlugin* OfxGetPlugin(int nth) { return nth == 0 ? &gPlugin : nullptr; }
