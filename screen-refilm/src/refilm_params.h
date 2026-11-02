// Screen Refilm - parameter model shared by the OFX plugin and the CLI.
#pragma once

#include <cstddef>

namespace refilm {

enum ScreenType { kScreenLCD = 0, kScreenCRT = 1, kScreenOLED = 2 };
enum RecordingShape { kShapeTimeline = 0, kShape16x9, kShape4x3, kShape9x16, kShape1x1 };

// Every control is stored as a double so a single descriptor table can drive
// both the host UI and the command line. Enums/bools are cast at use.
struct Params {
    double preset = 0;
    double screen = kScreenLCD;
    // Camera
    double zoom = 3.0;
    double lookX = 0.5, lookY = 0.5;  // normalised position on the screen, y up
    double tiltUD = -8.0;             // degrees
    double tiltLR = 18.0;             // degrees
    // Movement (all animate on their own, no keyframes needed)
    double pushIn = 0.25;             // zoom growth per 10 s
    double panLR = 0.0, panUD = 0.0;  // screen heights per 20 s
    double drift = 0.0;               // handheld wobble
    // Focus
    double focusBlur = 0.4;
    double focusShift = 0.0;
    double focusPull = 0.0;
    double focusAngle = 0.0;          // degrees
    // Light
    double brightness = 1.0;
    double glow = 0.0;
    double flicker = 0.0;
    double flickerSpeed = 0.3;
    double flickerGlowOnly = 0;
    // Lens
    double vignette = 0.0;
    double lensBulge = 0.0;
    double chromaticAberration = 0.0;
    double rgbSplit = 0.0;
    // Screen
    double recordingShape = kShapeTimeline;
    double moire = 0.0;
    double moireBands = 60.0;
    double moireColour = 0.4;
    double moireSpeed = 1.0;
    double pixelSize = 1.0;
    double gridStrength = 1.0;

    // Not user facing: proxy render scale of the source (1 = full res).
    double sourceScale = 1.0;
};

enum ParamType { kTypeDouble, kTypeInt, kTypeBool, kTypeChoice, kTypeDouble2D, kTypeGroup };

struct ParamDesc {
    const char* id;
    const char* label;
    const char* parent;     // group id, or nullptr for top level
    ParamType type;
    double Params::*field;  // nullptr for groups
    double Params::*field2; // second component for 2D params
    double def, def2;
    double min, max;        // hard limits
    double dispMin, dispMax;
    const char* const* options;
    int numOptions;
    const char* hint;
};

const ParamDesc* paramTable(size_t* count);
const ParamDesc* findParam(const char* id);

struct PresetValue { const char* id; double v; double v2 = 0; };
struct Preset { const char* name; const PresetValue* values; int count; };

// Index 0 is "None" (leaves everything as is).
const Preset* presetTable(int* count);
const char* const* screenTypeNames(int* count);

// Resets all controls to defaults and applies preset `index` (>0).
void applyPreset(Params& p, int index);

} // namespace refilm
