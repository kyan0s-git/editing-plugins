#include "refilm_params.h"

#include <cstring>

namespace refilm {

namespace {

const char* const kScreenNames[] = {"LCD (laptop, monitor)", "CRT (old TV)", "Phone (OLED)"};
const char* const kShapeNames[] = {"Same as timeline", "16:9", "4:3", "9:16", "1:1"};

// Presets ----------------------------------------------------------------
const PresetValue kDeskMonitor[] = {
    {"screen", kScreenLCD}, {"zoom", 3.0}, {"tiltUD", -8}, {"tiltLR", 18},
    {"pushIn", 0.25}, {"focusBlur", 0.4}, {"vignette", 0.3}, {"rgbSplit", 0.2},
};
const PresetValue kLivingRoomTV[] = {
    {"screen", kScreenCRT}, {"zoom", 1.6}, {"tiltUD", -4}, {"tiltLR", 10},
    {"pushIn", 0.1}, {"focusBlur", 0.25}, {"glow", 0.45}, {"flicker", 0.25},
    {"flickerSpeed", 0.2}, {"vignette", 0.55}, {"lensBulge", 0.45},
    {"chromaticAberration", 0.3}, {"pixelSize", 3}, {"recordingShape", kShape4x3},
};
const PresetValue kPhoneCloseUp[] = {
    {"screen", kScreenOLED}, {"zoom", 4.0}, {"tiltUD", 12}, {"tiltLR", -22},
    {"pushIn", 0.2}, {"drift", 0.4}, {"focusBlur", 0.55}, {"glow", 0.15},
    {"vignette", 0.25}, {"pixelSize", 2},
};
const PresetValue kDreamyMacro[] = {
    {"screen", kScreenLCD}, {"zoom", 6.0}, {"tiltUD", -14}, {"tiltLR", 30},
    {"pushIn", 0.15}, {"drift", 0.25}, {"focusBlur", 0.8}, {"focusPull", 0.5},
    {"glow", 0.3}, {"vignette", 0.4}, {"rgbSplit", 0.4},
};
const PresetValue kGlitchyBroadcast[] = {
    {"screen", kScreenCRT}, {"zoom", 2.2}, {"tiltUD", -6}, {"tiltLR", 8},
    {"pushIn", 0.0}, {"drift", 0.3}, {"focusBlur", 0.2}, {"glow", 0.35},
    {"flicker", 0.7}, {"flickerSpeed", 0.9}, {"moire", 0.45}, {"moireBands", 40},
    {"rgbSplit", 0.9}, {"chromaticAberration", 0.5}, {"lensBulge", 0.3}, {"pixelSize", 2},
};
const PresetValue kMoireMess[] = {
    {"screen", kScreenLCD}, {"zoom", 2.5}, {"tiltUD", -10}, {"tiltLR", 25},
    {"pushIn", 0.25}, {"focusBlur", 0.35}, {"moire", 0.7}, {"moireBands", 90},
    {"moireColour", 0.8}, {"moireSpeed", 1.5},
};

#define PRESET(name, arr) {name, arr, int(sizeof(arr) / sizeof(arr[0]))}
const Preset kPresets[] = {
    {"None", nullptr, 0},
    PRESET("Desk Monitor", kDeskMonitor),
    PRESET("Living-Room TV", kLivingRoomTV),
    PRESET("Phone Close-Up", kPhoneCloseUp),
    PRESET("Dreamy Macro", kDreamyMacro),
    PRESET("Glitchy Broadcast", kGlitchyBroadcast),
    PRESET("Moire Mess", kMoireMess),
};
#undef PRESET

const char* kPresetNames[sizeof(kPresets) / sizeof(kPresets[0])];
const char* const* presetNames() {
    for (size_t i = 0; i < sizeof(kPresets) / sizeof(kPresets[0]); ++i) kPresetNames[i] = kPresets[i].name;
    return kPresetNames;
}

#define F(x) &Params::x
const ParamDesc kParams[] = {
    // id, label, parent, type, field, field2, def, def2, min, max, dispMin, dispMax, options, n, hint
    {"preset", "Preset", nullptr, kTypeChoice, F(preset), nullptr, 0, 0, 0, 6, 0, 6, presetNames(), 7,
     "Start a look from a preset. Choosing one resets every control."},
    {"screen", "Screen", nullptr, kTypeChoice, F(screen), nullptr, 0, 0, 0, 2, 0, 2, kScreenNames, 3,
     "The kind of display the footage is re-filmed off."},

    {"grpCamera", "Camera", nullptr, kTypeGroup, nullptr, nullptr, 0, 0, 0, 0, 0, 0, nullptr, 0, nullptr},
    {"zoom", "Zoom", "grpCamera", kTypeDouble, F(zoom), nullptr, 3.0, 0, 0.5, 40, 1, 10, nullptr, 0,
     "How close the camera is to the screen. 1 = the whole screen fills the frame."},
    {"lookAt", "Look At", "grpCamera", kTypeDouble2D, F(lookX), F(lookY), 0.5, 0.5, -1, 2, 0, 1, nullptr, 0,
     "Point on the screen the camera aims at (0-1, y up)."},
    {"tiltUD", "Tilt Up/Down", "grpCamera", kTypeDouble, F(tiltUD), nullptr, -8, 0, -75, 75, -60, 60, nullptr, 0,
     "Camera angle above/below the screen, in degrees."},
    {"tiltLR", "Tilt Left/Right", "grpCamera", kTypeDouble, F(tiltLR), nullptr, 18, 0, -80, 80, -70, 70, nullptr, 0,
     "Camera angle to the side of the screen, in degrees."},

    {"grpMovement", "Movement", nullptr, kTypeGroup, nullptr, nullptr, 0, 0, 0, 0, 0, 0, nullptr, 0, nullptr},
    {"pushIn", "Push In", "grpMovement", kTypeDouble, F(pushIn), nullptr, 0.25, 0, -1, 5, -1, 2, nullptr, 0,
     "Slow dolly towards the screen over time (negative pulls out)."},
    {"panLR", "Pan Left/Right", "grpMovement", kTypeDouble, F(panLR), nullptr, 0, 0, -10, 10, -2, 2, nullptr, 0,
     "Slow sideways glide across the screen over time."},
    {"panUD", "Pan Up/Down", "grpMovement", kTypeDouble, F(panUD), nullptr, 0, 0, -10, 10, -2, 2, nullptr, 0,
     "Slow vertical glide across the screen over time."},
    {"drift", "Drift", "grpMovement", kTypeDouble, F(drift), nullptr, 0, 0, 0, 5, 0, 1, nullptr, 0,
     "Handheld camera wobble."},

    {"grpFocus", "Focus", nullptr, kTypeGroup, nullptr, nullptr, 0, 0, 0, 0, 0, 0, nullptr, 0, nullptr},
    {"focusBlur", "Focus Blur", "grpFocus", kTypeDouble, F(focusBlur), nullptr, 0.4, 0, 0, 3, 0, 1, nullptr, 0,
     "How soft things get away from the sharp band."},
    {"focusShift", "Focus Shift", "grpFocus", kTypeDouble, F(focusShift), nullptr, 0, 0, -2, 2, -1, 1, nullptr, 0,
     "Moves the sharp band nearer/further."},
    {"focusPull", "Focus Pull", "grpFocus", kTypeDouble, F(focusPull), nullptr, 0, 0, 0, 2, 0, 1, nullptr, 0,
     "Rack the sharp band back and forth on its own, no keyframes needed."},
    {"focusAngle", "Focus Angle", "grpFocus", kTypeDouble, F(focusAngle), nullptr, 0, 0, -180, 180, -90, 90, nullptr, 0,
     "Rotates the sharp band (tilt-shift style)."},

    {"grpLight", "Light", nullptr, kTypeGroup, nullptr, nullptr, 0, 0, 0, 0, 0, 0, nullptr, 0, nullptr},
    {"brightness", "Brightness", "grpLight", kTypeDouble, F(brightness), nullptr, 1, 0, 0, 10, 0, 3, nullptr, 0,
     "Screen brightness / camera exposure."},
    {"glow", "Glow", "grpLight", kTypeDouble, F(glow), nullptr, 0, 0, 0, 5, 0, 1, nullptr, 0,
     "Light bleeding off the screen into the lens."},
    {"flicker", "Flicker", "grpLight", kTypeDouble, F(flicker), nullptr, 0, 0, 0, 2, 0, 1, nullptr, 0,
     "Brightness wavering."},
    {"flickerSpeed", "Flicker Speed", "grpLight", kTypeDouble, F(flickerSpeed), nullptr, 0.3, 0, 0, 1, 0, 1, nullptr, 0,
     "From slow breathing (0) to random glitching (1)."},
    {"flickerGlowOnly", "Flicker Glow Only", "grpLight", kTypeBool, F(flickerGlowOnly), nullptr, 0, 0, 0, 1, 0, 1, nullptr, 0,
     "Keep the picture steady and pulse only the glow."},

    {"grpLens", "Lens", nullptr, kTypeGroup, nullptr, nullptr, 0, 0, 0, 0, 0, 0, nullptr, 0, nullptr},
    {"vignette", "Vignette", "grpLens", kTypeDouble, F(vignette), nullptr, 0, 0, 0, 2, 0, 1, nullptr, 0,
     "Darkens the frame corners."},
    {"lensBulge", "Lens Bulge", "grpLens", kTypeDouble, F(lensBulge), nullptr, 0, 0, -2, 2, -1, 1, nullptr, 0,
     "Curves the screen out (positive, barrel) or in (negative, pincushion)."},
    {"chromaticAberration", "Chromatic Aberration", "grpLens", kTypeDouble, F(chromaticAberration), nullptr, 0, 0, 0, 3, 0, 1, nullptr, 0,
     "Colour fringes that grow towards the edges of the frame."},
    {"rgbSplit", "RGB Split", "grpLens", kTypeDouble, F(rgbSplit), nullptr, 0, 0, 0, 3, 0, 1, nullptr, 0,
     "Offsets red and blue sideways across the whole frame."},

    {"grpScreen", "Screen", nullptr, kTypeGroup, nullptr, nullptr, 0, 0, 0, 0, 0, 0, nullptr, 0, nullptr},
    {"recordingShape", "Recording Shape", "grpScreen", kTypeChoice, F(recordingShape), nullptr, 0, 0, 0, 4, 0, 4, kShapeNames, 5,
     "Shape of the physical screen. 'Same as timeline' keeps bars off the picture."},
    {"moire", "Moire", "grpScreen", kTypeDouble, F(moire), nullptr, 0, 0, 0, 2, 0, 1, nullptr, 0,
     "Interference bands from the camera sensor beating against the pixel grid."},
    {"moireBands", "Moire Bands", "grpScreen", kTypeDouble, F(moireBands), nullptr, 60, 0, 1, 1000, 5, 200, nullptr, 0,
     "How many bands fit across the screen."},
    {"moireColour", "Moire Colour", "grpScreen", kTypeDouble, F(moireColour), nullptr, 0.4, 0, 0, 1, 0, 1, nullptr, 0,
     "0 = grey bands, 1 = rainbow bands."},
    {"moireSpeed", "Moire Speed", "grpScreen", kTypeDouble, F(moireSpeed), nullptr, 1, 0, -10, 10, -3, 3, nullptr, 0,
     "How fast the bands crawl."},
    {"pixelSize", "Pixel Size", "grpScreen", kTypeInt, F(pixelSize), nullptr, 1, 0, 1, 32, 1, 8, nullptr, 0,
     "Source pixels per screen pixel. For full detail on a 4K recording keep the clip at 100% scale and use 1."},
    {"gridStrength", "Grid Strength", "grpScreen", kTypeDouble, F(gridStrength), nullptr, 1, 0, 0, 1, 0, 1, nullptr, 0,
     "Visibility of the sub-pixel grid."},
};
#undef F

} // namespace

const ParamDesc* paramTable(size_t* count) {
    *count = sizeof(kParams) / sizeof(kParams[0]);
    return kParams;
}

const ParamDesc* findParam(const char* id) {
    for (const ParamDesc& d : kParams)
        if (std::strcmp(d.id, id) == 0) return &d;
    return nullptr;
}

const Preset* presetTable(int* count) {
    *count = int(sizeof(kPresets) / sizeof(kPresets[0]));
    return kPresets;
}

const char* const* screenTypeNames(int* count) {
    *count = 3;
    return kScreenNames;
}

void applyPreset(Params& p, int index) {
    int n = 0;
    const Preset* presets = presetTable(&n);
    if (index <= 0 || index >= n) return;
    const double scale = p.sourceScale;
    p = Params();
    p.sourceScale = scale;
    p.preset = index;
    for (int i = 0; i < presets[index].count; ++i) {
        const PresetValue& pv = presets[index].values[i];
        if (const ParamDesc* d = findParam(pv.id)) {
            p.*(d->field) = pv.v;
            if (d->field2) p.*(d->field2) = pv.v2;
        }
    }
}

} // namespace refilm
