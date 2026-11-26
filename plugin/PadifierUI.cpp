/*
 * Noise Padifier - user interface (DPF / NanoVG).
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Everything is drawn in a logical 1200 x 760 space, scaled to the window size (resizable,
 * aspect ratio kept).  Five sections, left to right in the order of the signal:
 *   1 SOURCE (colour, density, seed) -> 2 TUNE (off / comb / chord, note, chord, resonance,
 *   vibrato) -> 3 ENVELOPE (length, swell, decay, damping, bloom) -> 4 MOTION (LFO: rate, then
 *   depth and phase on tone, resonance, density, low cut, high cut; stereo motion, cross) -> 5 OUTPUT (mix, output, filters, pre-delay, width),
 * and below, a time-frequency map of the response (an image, made again only when the
 * response or the theme changes), the spectrum of the pad and the meters.
 * Knobs: drag vertically (Shift = fine), mouse wheel, double-click or Ctrl+click = default,
 * right-click = type a value (Enter / Escape).  Under each depth of the LFO (and VIBRATO), a box of
 * its phase, 0 / 90 / 180 / 270 degrees: click = next, right-click = previous, wheel.  Every
 * control has a tooltip.
 * A / B: two sets of settings to compare (header: A | B, copy to the other); the inactive one is
 * saved with the project (DPF state "ab").
 * Presets: drop-down list in the header (click to open, wheel over the closed box to step),
 * factory presets then the presets saved by the user (PadifierUserPresets.hpp: "Save current
 * settings...", name typed in the box; delete with the cross of a saved preset, clicked twice).
 * Two themes, Dusk (dark) and Pearl (light), switched by the DUSK | PEARL pill of the header; the
 * choice is a preference of the user (PadifierPrefs.hpp), not saved with the project.
 */

#include "DistrhoUI.hpp"
#include "PadifierParams.hpp"
#include "PadifierPrefs.hpp"
#include "PadifierUserPresets.hpp"
#include "NoiseIR.hpp"
#include "Convolver.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

static const float kW = 1200.0f;
static const float kH = 760.0f;
static const float kPi = 3.14159265358979f;
static const float kArcStart = 0.75f * kPi;
static const float kArcSweep = 1.5f * kPi;

// ------------------------------------------------------------------------------------------------
// Themes: every colour of the interface.  Dusk (dark: lavender and pale amber on dark violet
// grey) and Pearl (light, opalescent: milky lilac surfaces with a lavender, peach and aqua sheen;
// violet accents, coral "fire" instead of the amber).  The choice is a preference of the
// user (PadifierPrefs.hpp), switched by the DUSK | PEARL pill of the header.

struct Theme {
    // background, header, panels
    Color bg0, bg1, bgGlow, header0, header1, headerLine, panel0, panel1, panelEdge, panelShadow, divider;
    // text: normal, secondary, faint, on an accent fill
    Color text, dim, faint, onAccent;
    // accents: lavender (hot), amber (gold), dark violet (deep)
    Color hot, gold, deep, arrow, brandGlow;
    // buttons, LEDs, segmented selectors, recessed wells
    Color btnOn0, btnOn1, btnOff0, btnOff1, btnOnEdge, ledGlow, ledGlowEnd, ledOn, ledOff;
    Color well, plot, deepWell, hoverEdge, sel0, sel1;
    // knobs
    Color knobShadow, knob0, knob1, knobIn0, knobIn1, knobEdge, arcGlow, arcGlowActive, pointerGlow, pointer;
    // plots, meters
    Color grid, gridZero, curveGlow, tone, meterEdge;
    // tooltip; label of the Bypass button when bypassed
    Color tipBg, tipEdge, bypassText;
    // levels of the time-frequency map above the plot colour (-32, -16, 0 dB), also the meters
    Color map1, map2, map3;
    // sheen of the panels and header: diagonal sheen0 -> sheen1, glint sheen2 from the top left
    // (all transparent: none)
    Color sheen0, sheen1, sheen2;
};

static Color transparent(const Color& c)
{
    Color t(c);
    t.alpha = 0.0f;
    return t;
}

static const Theme kThemes[PadifierPrefs::kThemeCount] = {
    {   // Dusk
        Color(14, 13, 19), Color(30, 27, 38), Color(150, 130, 215, 0.16f), Color(34, 31, 44), Color(16, 15, 22),
        Color(Color(178, 160, 238), Color(0, 0, 0), 0.35f),
        Color(33, 30, 42), Color(24, 22, 32), Color(66, 60, 82), Color(0, 0, 0, 0.55f), Color(66, 60, 82, 0.7f),
        Color(232, 228, 238), Color(150, 144, 165), Color(100, 94, 118), Color(248, 245, 255),
        Color(178, 160, 238), Color(232, 196, 120), Color(72, 56, 128), Color(178, 160, 238, 0.7f),
        Color(178, 160, 238, 0.6f),
        Color(104, 84, 170), Color(62, 46, 112), Color(40, 37, 52), Color(26, 24, 33), Color(178, 160, 238, 0.8f),
        Color(200, 185, 250, 0.8f), Color(200, 185, 250, 0.0f), Color(238, 230, 255), Color(56, 52, 68),
        Color(16, 15, 21), Color(13, 12, 18), Color(10, 9, 14), Color(178, 160, 238, 0.6f),
        Color(118, 98, 188), Color(70, 54, 124),
        Color(0, 0, 0, 0.7f), Color(92, 86, 108), Color(31, 29, 40), Color(80, 75, 96), Color(27, 25, 34),
        Color(0, 0, 0, 0.6f), Color(178, 160, 238, 0.18f), Color(178, 160, 238, 0.30f), Color(178, 160, 238, 0.35f),
        Color(228, 220, 250),
        Color(76, 70, 94, 0.6f), Color(120, 112, 140, 0.8f), Color(232, 196, 120, 0.22f), Color(178, 160, 238),
        Color(56, 52, 70),
        Color(10, 9, 14, 0.95f), Color(178, 160, 238, 0.6f), Color(232, 196, 120),
        Color(72, 56, 128), Color(178, 160, 238), Color(232, 196, 120),
        Color(0, 0, 0, 0.0f), Color(0, 0, 0, 0.0f), Color(0, 0, 0, 0.0f),
    },
    {   // Pearl
        Color(222, 215, 234), Color(240, 235, 247), Color(240, 172, 146, 0.22f), Color(251, 249, 254),
        Color(238, 232, 247), Color(124, 94, 204, 0.50f),
        Color(253, 251, 255), Color(242, 237, 249), Color(208, 198, 228), Color(70, 50, 120, 0.14f),
        Color(208, 198, 228, 0.8f),
        Color(40, 32, 58), Color(106, 96, 130), Color(150, 141, 172), Color(255, 252, 255),
        Color(112, 82, 198), Color(170, 84, 104), Color(72, 50, 138), Color(112, 82, 198, 0.75f),
        Color(150, 120, 235, 0.28f),
        Color(136, 108, 220), Color(94, 68, 176), Color(253, 251, 255), Color(238, 232, 247),
        Color(112, 82, 198, 0.9f),
        Color(160, 130, 245, 0.55f), Color(160, 130, 245, 0.0f), Color(132, 100, 232), Color(198, 190, 216),
        Color(236, 231, 245), Color(249, 246, 253), Color(225, 218, 238), Color(112, 82, 198, 0.7f),
        Color(136, 108, 220), Color(94, 68, 176),
        Color(60, 40, 110, 0.26f), Color(255, 254, 255), Color(226, 219, 240), Color(252, 250, 255),
        Color(234, 228, 244),
        Color(108, 96, 142, 0.40f), Color(112, 82, 198, 0.12f), Color(112, 82, 198, 0.22f), Color(112, 82, 198, 0.18f),
        Color(88, 62, 166),
        Color(150, 138, 182, 0.35f), Color(120, 108, 152, 0.7f), Color(238, 152, 140, 0.16f), Color(112, 82, 198),
        Color(206, 196, 226),
        Color(254, 252, 255, 0.97f), Color(112, 82, 198, 0.6f), Color(255, 228, 214),
        Color(206, 190, 246), Color(140, 108, 224), Color(238, 152, 140),
        Color(176, 150, 250, 0.11f), Color(250, 182, 150, 0.11f), Color(140, 222, 214, 0.13f),
    },
};

static const char* const kThemeNames[PadifierPrefs::kThemeCount] = { "DUSK", "PEARL" };

// ------------------------------------------------------------------------------------------------
// Layout

struct Rect {
    float x, y, w, h;
    bool contains(const float px, const float py) const
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

// sections
static const Rect kSecSource = { 20, 70, 180, 400 };    // 1 SOURCE
static const Rect kSecTune   = { 210, 70, 200, 400 };   // 2 TUNE
static const Rect kSecEnv    = { 420, 70, 320, 400 };   // 3 ENVELOPE
static const Rect kSecMotion = { 750, 70, 230, 400 };   // 4 MOTION
static const Rect kSecOut    = { 990, 70, 190, 400 };   // 5 OUTPUT
static const Rect kBottom    = { 20, 480, 1160, 268 };
static const Rect kMap       = { 150, 494, 620, 222 };  // time-frequency map of the response
static const Rect kTonePlot  = { 830, 494, 320, 104 };  // spectrum of the pad

enum { kKnobColour = 0, kKnobDensity, kKnobSeed, kKnobNote, kKnobResonance, kKnobLfoPitch, kKnobLength,
       kKnobSwell, kKnobDecay, kKnobDamping, kKnobBloom, kKnobLfoRate, kKnobLfoTone, kKnobLfoResonance,
       kKnobLfoDensity, kKnobLfoLowCut, kKnobLfoHighCut, kKnobPanRate, kKnobPanDepth, kKnobCross, kKnobMix, kKnobOutput, kKnobLowCut, kKnobHighCut, kKnobPredelay,
       kKnobWidth, kNumKnobs };

// a round knob
struct KnobDef {
    int param;
    float cx, cy, r;
    const char* label;
    const char* help;
};

static const KnobDef kKnobs[kNumKnobs] = {
    { kParamColour,    110.0f, 165.0f, 33.0f, "COLOUR",
      "Spectral slope of the noise, dB per octave: -6 brown (dark), -3 pink (warm, the usual choice "
      "for pads), 0 white (bright, airy), +3 blue, +6 violet." },
    { kParamDensity,    65.0f, 305.0f, 24.0f, "DENSITY",
      "Impulses per second of sparse (velvet) noise: low values give a cloud of distinct grains or "
      "echoes, high values a smooth noise. At the maximum: smooth Gaussian noise." },
    { kParamSeed,      155.0f, 305.0f, 24.0f, "SEED",
      "Draw of the noise: each value gives another noise of the same kind, another grain of the "
      "pad. The same seed always gives the same response." },
    { kParamNote,      265.0f, 190.0f, 24.0f, "NOTE",
      "Note of the comb (COMB) or root of the chord (CHORD): the pad is tuned to it." },
    { kParamResonance, 265.0f, 305.0f, 24.0f, "RESONANCE",
      "Ring time of the combs, 20 ms to 5 s: low, a light tint; high, a clearly pitched pad." },
    { kParamLfoPitch,  355.0f, 305.0f, 24.0f, "VIBRATO",
      "Depth of the LFO on the note of the combs, in semitones (rate: LFO RATE). It runs along the "
      "response, from the attack of each sound." },
    { kParamLength,    480.0f, 165.0f, 33.0f, "LENGTH",
      "Length of the impulse response: how long every sound is smeared. Long: a slow pad; short: "
      "a smear that thickens the sound. A change rebuilds the response, crossfaded in." },
    { kParamSwell,     580.0f, 165.0f, 33.0f, "SWELL",
      "Rise of the envelope, in % of the length: the pad fades in and peaks at that time. 0: at "
      "once; 100 %: rises over the whole length (reverse swell)." },
    { kParamDecay,     680.0f, 165.0f, 33.0f, "DECAY",
      "Part of the length taken by the fall to the minimum (-60 dB, then silence), a straight line in "
      "dB; before it the pad is held at full level. 0 %: held to the end; 100 %: falls from the peak." },
    { kParamDamping,   530.0f, 305.0f, 24.0f, "DAMPING",
      "The highs die sooner: extra loss at the end, in dB per octave above 250 Hz, as in a reverb." },
    { kParamBloom,     630.0f, 305.0f, 24.0f, "BLOOM",
      "The peak moves with frequency: positive, the highs peak later (the pad opens up); "
      "negative, the lows peak later (it warms up)." },
    { kParamLfoRate,   793.0f, 142.0f, 20.0f, "LFO RATE",
      "Rate of the LFO along the response, shared by all its targets: VIBRATO and, here, the depths "
      "on the tone, resonance, density, low cut and high cut. It starts again at the attack of each "
      "sound." },
    { kParamLfoTone,   865.0f, 142.0f, 20.0f, "TONE",
      "Depth of the LFO on the colour: the pad gets brighter and darker, in dB per octave." },
    { kParamLfoResonance, 937.0f, 142.0f, 20.0f, "RESON",
      "Depth of the LFO on RESONANCE, in % of its scale: the combs ring longer and shorter, the pad "
      "goes from a light tint to a clearly pitched sound. Needs COMB or CHORD." },
    { kParamLfoDensity, 793.0f, 258.0f, 20.0f, "DENSITY",
      "Depth of the LFO on DENSITY, in octaves: the grains of sparse noise thicken and thin out. "
      "Needs DENSITY below its maximum." },
    { kParamLfoLowCut,  865.0f, 258.0f, 20.0f, "LOW CUT",
      "Depth of the LFO on LOW CUT, in octaves around its value: the low end opens and closes. "
      "The swept high-pass is then part of the response and replaces the fixed one." },
    { kParamLfoHighCut, 937.0f, 258.0f, 20.0f, "HIGH CUT",
      "Depth of the LFO on HIGH CUT, in octaves around its value: the pad opens and closes like a "
      "filter sweep. The swept low-pass is then part of the response and replaces the fixed one." },
    { kParamPanRate,   793.0f, 388.0f, 20.0f, "PAN RATE",
      "Rate of the stereo motion along the response." },
    { kParamPanDepth,  865.0f, 388.0f, 20.0f, "PAN DEPTH",
      "Depth of the stereo motion: the pad swings between left and right." },
    { kParamCross,     937.0f, 388.0f, 20.0f, "CROSS",
      "True stereo: each input also reaches the other side through its own noise. 0: none (half "
      "the computation); 100 %: as much as the direct path." },
    { kParamMix,      1040.0f, 150.0f, 22.0f, "MIX",
      "Dry / pad, equal power (pad sin, dry cos of 90 degrees x mix). At 100 %: pad only (for a "
      "send)." },
    { kParamOutput,   1130.0f, 150.0f, 22.0f, "OUTPUT",
      "Gain of the whole output, dry and pad." },
    { kParamLowCut,   1040.0f, 265.0f, 22.0f, "LOW CUT",
      "High-pass on the pad only (12 dB / octave): pink and brown noise carry a lot of low end. "
      "At the minimum: off." },
    { kParamHighCut,  1130.0f, 265.0f, 22.0f, "HIGH CUT",
      "Low-pass on the pad only (12 dB / octave): a darker, softer pad. At the maximum: off." },
    { kParamPredelay, 1040.0f, 380.0f, 22.0f, "PRE-DELAY",
      "Delay of the pad: it starts later than the dry sound. A change crossfades in 30 ms." },
    { kParamWidth,    1130.0f, 380.0f, 22.0f, "WIDTH",
      "Stereo width of the pad: 0 % mono, 100 % as made, 200 % side doubled." },
};

static const Rect kTune     = { 222, 104, 176, 28 };   // OFF | COMB | CHORD

// settings with a few named values: a box that opens a list (instead of a knob)
struct ListDef {
    int param;
    Rect box;
    const char* label;
    const char* help;
};

enum { kListChord = 0, kNumLists };

static const ListDef kLists[kNumLists] = {
    { kParamChord,  { 317, 177, 76, 26 }, "CHORD",
      "Kind of chord (CHORD): one comb on each of its notes. Click: the list; wheel: next / previous." },
};

static const float kListRowH = 19.0f;

// phase of the LFO on a target: a box under the knob of its depth
struct PhaseDef {
    int param;   // kParamPhase*
    int knob;    // knob of the depth
};

static const PhaseDef kPhases[kLfoPhaseTargets] = {
    { kParamPhasePitch, kKnobLfoPitch }, { kParamPhaseTone, kKnobLfoTone },
    { kParamPhaseResonance, kKnobLfoResonance }, { kParamPhaseDensity, kKnobLfoDensity },
    { kParamPhaseLowCut, kKnobLfoLowCut }, { kParamPhaseHighCut, kKnobLfoHighCut },
};

// under the value of the knob (small knobs: label at r + 17, value 15 below)
static Rect phaseRect(const int i)
{
    const KnobDef& k = kKnobs[kPhases[i].knob];
    return { k.cx - 17.0f, k.cy + k.r + 41.0f, 34.0f, 15.0f };
}
static const Rect kTuneInfo = { 222, 396, 176, 70 };   // readout of the tuning
static const Rect kEnvInfo  = { 440, 380, 280, 76 };   // readout of the envelope
static const Rect kPreset  = { 640, 16, 220, 26 };     // drop-down box; the list opens below it
static const Rect kAB      = { 870, 17, 50, 24 };      // A | B
static const Rect kCopy    = { 924, 17, 34, 24 };      // copy the current set to the other
static const float kMenuItemH = 22.0f;     // preset list: item
static const float kMenuHeaderH = 20.0f;   // section title (FACTORY, USER)
static const float kMenuDividerH = 9.0f;   // line before "Save current settings..."

// rows of the preset list
enum { kRowHeader = 0, kRowFactory, kRowUser, kRowNone, kRowDivider, kRowSave };

struct MenuRow {
    int kind;
    int index;    // factory preset, saved preset (in fUserNames) or section (0 factory, 1 user)
    float y, h;   // y from the top of the list content
};
static const Rect kTheme   = { 966, 17, 90, 24 };
static const Rect kBypass  = { 1066, 14, 114, 30 };

// meters: three horizontal bars in the bottom panel, under the spectrum
static const float kMeterX = 880.0f, kMeterW = 220.0f, kMeterH = 9.0f;
static const float kMeterY[3] = { 650.0f, 672.0f, 694.0f };   // IN, PAD, OUT

// hover zones with a tooltip (knobs are handled separately)
enum {
    kZoneTune = 0, kZoneTuneInfo, kZoneEnvInfo, kZoneMap, kZoneTonePlot, kZonePreset,
    kZoneBypass, kZoneTheme, kZoneAB, kZoneCopy, kZoneIn, kZoneWet, kZoneOut, kNumZones
};

struct Zone {
    Rect r;
    const char* title;
    const char* help;
};

static const Zone kZones[kNumZones] = {
    { kTune, "TUNE",
      "OFF: noise only. COMB: a comb filter on the noise, tuned to NOTE: the pad takes its harmonic "
      "series. CHORD: one comb on each note of the chord." },
    { kTuneInfo, "TUNING", "Frequency of the note and ring time of the combs." },
    { kEnvInfo, "IMPULSE RESPONSE",
      "The response is noise under this envelope, a different noise for each path, scaled to unit "
      "energy: a broadband input keeps its level in the pad. Latency 256 samples, reported to the "
      "host." },
    { kMap, "TIME-FREQUENCY MAP",
      "Level of the response against time (left to right, 0 to Length) and frequency (bottom to "
      "top, 20 Hz to 20 kHz): envelope, swell and decay, damping, bloom, LFO on the tone and on the filters. "
      "Lines: notes of the combs with their vibrato. Colour of the noise: see the spectrum." },
    { kTonePlot, "SPECTRUM OF THE PAD",
      "Colour of the noise and the Low cut and High cut filters (dB, highest point at 0 dB; at their "
      "centre when the LFO sweeps them)." },
    { kPreset, "PRESETS",
      "Click to choose a preset: factory presets, then yours (saved for all projects on this "
      "computer). Wheel: next / previous. A * means a setting was changed since it was loaded." },
    { kBypass, "BYPASS",
      "Passes the input unprocessed (delayed by the latency, crossfaded in 10 ms, no click)." },
    { kTheme, "THEME",
      "DUSK: dark. PEARL: light. Kept for all instances and projects (a setting of this computer, "
      "not of the project); other open windows change when reopened." },
    { kAB, "A / B",
      "Two sets of settings to compare: click A or B to switch. Both are saved with the project." },
    { kCopy, "COPY",
      "Copies the current settings into the other set (A > B or B > A)." },
    { { kMeterX - 46, kMeterY[0] - 7, kMeterW + 100, 16 }, "IN", "Input peak level, dBFS." },
    { { kMeterX - 46, kMeterY[1] - 7, kMeterW + 100, 16 }, "PAD",
      "Peak level of the pad alone (after the filters and width, before the mix), dBFS." },
    { { kMeterX - 46, kMeterY[2] - 7, kMeterW + 100, 16 }, "OUT", "Output peak level, dBFS." },
};

static const int kHoverZoneBase = 100;
static const int kHoverListBase = 200;   // hover on the box of a list
static const int kHoverPhaseBase = 300;  // hover on the box of a phase

// ------------------------------------------------------------------------------------------------

class PadifierUI : public UI
{
public:
    PadifierUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, true)
    {
        for (int i = 0; i < kParamCount; ++i)
            fValues[i] = kPadifierRanges[i].def;
        for (int i = 0; i < kPresetParamCount; ++i)
            fPresetValues[i] = kPadifierPresets[0].values[i];
        loadSharedResources();
        fFont = findFont(NANOVG_DEJAVU_SANS_TTF);
#ifdef PADIFIER_UI_DEMO_THEME
        fTheme = PADIFIER_UI_DEMO_THEME;   // screenshots: forced, never saved
#else
        fTheme = PadifierPrefs::loadTheme();
#endif
        fT = &kThemes[fTheme];
        fLastPaint = std::chrono::steady_clock::now();
#ifdef PADIFIER_UI_DEMO
        // screenshots only (-DPADIFIER_UI_DEMO=<hover id> [-DPADIFIER_UI_DEMO_PRESET=<index>]):
        // a tooltip and meters, since there is no audio
        fHover = PADIFIER_UI_DEMO;
        fValues[kParamMeterIn] = -9.5f;
        fValues[kParamMeterWet] = -17.0f;
        fValues[kParamMeterOut] = -8.2f;
#endif
    }

protected:
    // ------------------------------------------------------------------ host -> UI

    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index >= kParamCount || fValues[index] == value)
            return;
#ifdef PADIFIER_UI_DEMO
        if (index >= static_cast<uint32_t>(kParamFirstOutput))
            return;   // keep the meters of the screenshot
#endif
        fValues[index] = value;
        if (index < static_cast<uint32_t>(kParamFirstOutput))
            repaint();
        else
            fMetersChanged = true;
    }

    // saved A / B set: "slot=<0|1>;other=<values of the inactive set>"
    void stateChanged(const char* const key, const char* const value) override
    {
        if (std::strcmp(key, kStateKeys[kStateAB]) == 0)
            readABState(value);
    }

    void programLoaded(const uint32_t index) override
    {
        if (index < static_cast<uint32_t>(kPadifierPresetCount))
        {
            fPreset = static_cast<int>(index);
            fPresetUser = false;
            fPresetName = kPadifierPresets[index].name;
            for (int i = 0; i < kPresetParamCount; ++i)
                fValues[i] = fPresetValues[i] = kPadifierPresets[index].values[i];
            repaint();
        }
    }

    void uiIdle() override
    {
#ifdef PADIFIER_UI_DEMO_PRESET
        if (!fDemoPresetDone)
        {
            fDemoPresetDone = true;
            applyPreset(PADIFIER_UI_DEMO_PRESET);
        }
#endif
#ifdef PADIFIER_UI_DEMO_LIST
        // screenshots: list PADIFIER_UI_DEMO_LIST open, the mouse over its row 2
        if (!fDemoListDone)
        {
            fDemoListDone = true;
            openList(PADIFIER_UI_DEMO_LIST);
            fListHover = 2;
        }
#endif
#ifdef PADIFIER_UI_DEMO_MENU
        // screenshots: the list open, the mouse over row PADIFIER_UI_DEMO_MENU
        // (with PADIFIER_UI_DEMO_ARM: its cross clicked once)
        if (!fDemoMenuDone)
        {
            fDemoMenuDone = true;
            openMenu();
            fMenuHover = PADIFIER_UI_DEMO_MENU;
# ifdef PADIFIER_UI_DEMO_ARM
            fDeleteArmed = fMenuHover;
# endif
        }
#endif
#ifdef PADIFIER_UI_DEMO_NAME
        // screenshots: a name being typed
        if (!fDemoNameDone)
        {
            fDemoNameDone = true;
            startNaming();
            fNameText = PADIFIER_UI_DEMO_NAME;
        }
#endif
        // meters: at most 30 repaints per second
        const auto now = std::chrono::steady_clock::now();
        if (!fMetersChanged || now - fLastPaint < std::chrono::milliseconds(33))
            return;
        fMetersChanged = false;
        fLastPaint = now;
        repaint();
    }

    // ------------------------------------------------------------------ drawing

    void onNanoDisplay() override
    {
        const float s = scaleFactor();
        save();
        scale(s, s);

        drawBackground();
        drawHeader();
        drawSourceSection();
        drawTuneSection();
        drawEnvelopeSection();
        drawMotionSection();
        drawOutputSection();
        drawPlots();
        if (fMenuOpen)
            drawPresetMenu();
        else if (fList >= 0)
            drawList(fList);
        else if (fNaming)
            drawNamingHint();
        else if (fHover >= 0 && fDrag < 0 && fEdit < 0)
            drawTooltip(fHover);

        restore();
    }

    // ------------------------------------------------------------------ mouse

    bool onMouse(const MouseEvent& ev) override
    {
        const float s = scaleFactor();
        const float x = static_cast<float>(ev.pos.getX()) / s;
        const float y = static_cast<float>(ev.pos.getY()) / s;

        if (!ev.press)
        {
            if (ev.button == kMouseButtonLeft && fDrag >= 0)
            {
                editParameter(kKnobs[fDrag].param, false);
                fDrag = -1;
                repaint();
                return true;
            }
            return false;
        }

        if (fEdit >= 0)
            commitEdit(true);
        if (fNaming)
            cancelNaming();   // a click elsewhere cancels the name being typed

        // open list: a click chooses an item; outside the list it closes it (and goes no further)
        if (fMenuOpen)
        {
            menuClick(x, y, ev.button == kMouseButtonLeft);
            return true;
        }
        if (fList >= 0)
        {
            const int row = listRowAt(fList, x, y);
            const int l = fList;
            closeList();
            if (row >= 0 && ev.button == kMouseButtonLeft)
                setOnce(kLists[l].param, static_cast<float>(row));
            return true;
        }
        if (ev.button == kMouseButtonLeft)
        {
            const int l = listAt(x, y);
            if (l >= 0)
            {
                openList(l);
                return true;
            }
        }

        const int ph = phaseAt(x, y);
        if (ph >= 0 && (ev.button == kMouseButtonLeft || ev.button == kMouseButtonRight))
        {
            stepPhase(ph, ev.button == kMouseButtonLeft ? 1 : -1);
            return true;
        }

        const int k = knobAt(x, y);

        if (ev.button == kMouseButtonRight)
        {
            if (k < 0)
                return false;
            fEdit = k;
            formatNumber(kKnobs[k].param, fEditText, sizeof(fEditText));
            fEditFresh = true;
            repaint();
            return true;
        }

        if (ev.button != kMouseButtonLeft)
            return false;

        if (kPreset.contains(x, y)) { openMenu(); return true; }
        if (kAB.contains(x, y)) { switchSlot(static_cast<int>(segmentAt(kAB, x, 2))); return true; }
        if (kCopy.contains(x, y)) { copySlot(); return true; }
        if (kBypass.contains(x, y)) { toggle(kParamBypass); return true; }
        if (kTheme.contains(x, y)) { setTheme(static_cast<int>(segmentAt(kTheme, x, PadifierPrefs::kThemeCount))); return true; }
        if (kTune.contains(x, y)) { setOnce(kParamTune, segmentAt(kTune, x, 3)); return true; }

        if (k < 0)
            return false;

        const int p = kKnobs[k].param;
        const bool doubleClick = k == fLastClickTarget && ev.time - fLastClickTime < 350;
        fLastClickTarget = k;
        fLastClickTime = ev.time;

        if (doubleClick || (ev.mod & kModifierControl))
        {
            setOnce(p, defaultValue(p));
            fLastClickTarget = -1;
            return true;
        }

        fDrag = k;
        fLastY = y;
        editParameter(p, true);
        repaint();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float s = scaleFactor();
        const float x = static_cast<float>(ev.pos.getX()) / s;
        const float y = static_cast<float>(ev.pos.getY()) / s;

        if (fList >= 0)
        {
            const int row = listRowAt(fList, x, y);
            if (row != fListHover)
            {
                fListHover = row;
                repaint();
            }
            return true;
        }

        if (fMenuOpen)
        {
            int row = rowAt(x, y);
            if (row >= 0 && fRows[row].kind != kRowFactory && fRows[row].kind != kRowUser && fRows[row].kind != kRowSave)
                row = -1;
            const bool onCross = row >= 0 && fRows[row].kind == kRowUser && crossRect(rowRect(row)).contains(x, y);
            if (row != fMenuHover || onCross != fMenuOnCross)
            {
                if (row != fMenuHover)
                    fDeleteArmed = -1;
                fMenuHover = row;
                fMenuOnCross = onCross;
                repaint();
            }
            return true;
        }

        if (fDrag >= 0)
        {
            const int p = kKnobs[fDrag].param;
            const float pixels = (ev.mod & kModifierShift) ? 900.0f : 220.0f;
            const float norm = toNorm(p, fValues[p]) + (fLastY - y) / pixels;
            fLastY = y;
            changeValue(p, fromNorm(p, norm));
            return true;
        }

        int h = knobAt(x, y);
        if (h < 0 && listAt(x, y) >= 0)
            h = kHoverListBase + listAt(x, y);
        if (h < 0 && phaseAt(x, y) >= 0)
            h = kHoverPhaseBase + phaseAt(x, y);
        if (h < 0)
            for (int z = 0; z < kNumZones; ++z)
                if (kZones[z].r.contains(x, y))
                {
                    h = kHoverZoneBase + z;
                    break;
                }
        if (h != fHover)
        {
            fHover = h;
            repaint();
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float s = scaleFactor();
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;
        const float d = static_cast<float>(ev.delta.getY());
        if (fMenuOpen)
        {
            // long list: scrolls
            const float most = fMenuContentH - menuVisibleH();
            if (most > 0.0f && d != 0.0f)
            {
                fMenuScroll = std::fmin(std::fmax(fMenuScroll - d * kMenuItemH, 0.0f), most);
                fMenuHover = fDeleteArmed = -1;
                repaint();
            }
            return true;
        }
        if (kPreset.contains(x, y) && !fNaming)
        {
            // wheel up: previous preset, down: next
            if (d != 0.0f)
                stepPreset(d > 0.0f ? -1 : 1);
            return true;
        }
        if (fList >= 0)
            return true;
        const int l = listAt(x, y);
        if (l >= 0)
        {
            // wheel up: previous value, down: next
            if (d != 0.0f)
                setOnce(kLists[l].param, static_cast<float>(std::min(std::max(listValue(l) + (d > 0.0f ? -1 : 1), 0),
                                                                     listCount(l) - 1)));
            return true;
        }
        const int ph = phaseAt(x, y);
        if (ph >= 0)
        {
            if (d != 0.0f)
                stepPhase(ph, d > 0.0f ? -1 : 1);
            return true;
        }
        const int k = knobAt(x, y);
        if (k < 0)
            return false;
        const int p = kKnobs[k].param;
        if (whole(p))
        {
            // one step per notch (a fraction of the range would round back to the same value)
            if (d != 0.0f)
                setOnce(p, clampValue(p, std::round(fValues[p]) + (d > 0.0f ? 1.0f : -1.0f)));
            return true;
        }
        const float step = (ev.mod & kModifierShift) ? 0.005f : 0.025f;
        setOnce(p, fromNorm(p, toNorm(p, fValues[p]) + d * step));
        return true;
    }

    // ------------------------------------------------------------------ keyboard (value entry)

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (fMenuOpen && ev.press && ev.key == kKeyEscape)
        {
            closeMenu();
            return true;
        }
        if (fList >= 0 && ev.press && ev.key == kKeyEscape)
        {
            closeList();
            return true;
        }
        if (fNaming)
        {
            if (!ev.press)
                return true;
            if (ev.key == kKeyEnter)
                commitNaming();
            else if (ev.key == kKeyEscape)
                cancelNaming();
            else if ((ev.key == kKeyBackspace || ev.key == kKeyDelete) && !fNameText.empty())
            {
                fNameText.pop_back();
                fNameError = false;
                repaint();
            }
            return true;
        }
        if (fEdit < 0 || !ev.press)
            return fEdit >= 0;
        if (ev.key == kKeyEnter)
            commitEdit(true);
        else if (ev.key == kKeyEscape)
            commitEdit(false);
        else if (ev.key == kKeyBackspace || ev.key == kKeyDelete)
        {
            const size_t n = std::strlen(fEditText);
            if (fEditFresh)
                fEditText[0] = '\0';
            else if (n > 0)
                fEditText[n - 1] = '\0';
            fEditFresh = false;
            repaint();
        }
        return true;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (fNaming)
        {
            // characters allowed in a preset name (PadifierUserPresets::validName)
            const uint32_t c = ev.character;
            if (c >= 32 && c <= 126 && std::strchr("<>:\"/\\|?*", static_cast<int>(c)) == nullptr
                && fNameText.size() < PadifierUserPresets::kMaxNameLength)
            {
                fNameText.push_back(static_cast<char>(c));
                fNameError = false;
                repaint();
            }
            return true;
        }
        if (fEdit < 0)
            return false;
        char c = static_cast<char>(ev.character);
        if (c == ',')
            c = '.';
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+')
        {
            if (fEditFresh)
                fEditText[0] = '\0';
            fEditFresh = false;
            const size_t n = std::strlen(fEditText);
            if (n + 1 < sizeof(fEditText))
            {
                fEditText[n] = c;
                fEditText[n + 1] = '\0';
            }
            repaint();
        }
        return true;
    }

private:
    // ------------------------------------------------------------------ helpers

    float scaleFactor() const { return static_cast<float>(getWidth()) / kW; }

    double sampleRate() const { return getSampleRate() > 0.0 ? getSampleRate() : 44100.0; }

    static float defaultValue(const int p) { return kPadifierRanges[p].def; }

    static bool logarithmic(const int p)
    {
        return p == kParamLength || p == kParamLowCut || p == kParamHighCut || p == kParamDensity || p == kParamLfoRate
            || p == kParamPanRate;
    }

    static bool whole(const int p)
    {
        return p == kParamSeed || p == kParamNote || p == kParamChord || p == kParamTune;
    }

    // knob position: length, density, rates and filters logarithmic, the others linear
    static float toNorm(const int p, const float v)
    {
        const PadifierRange& r = kPadifierRanges[p];
        if (logarithmic(p))
            return std::log(std::fmax(v, r.min) / r.min) / std::log(r.max / r.min);
        return (v - r.min) / (r.max - r.min);
    }

    // seed, note and lists: whole numbers
    static float fromNorm(const int p, float n)
    {
        const PadifierRange& r = kPadifierRanges[p];
        n = std::fmin(std::fmax(n, 0.0f), 1.0f);
        if (logarithmic(p))
            return r.min * std::pow(r.max / r.min, n);
        const float v = r.min + n * (r.max - r.min);
        return whole(p) ? std::round(v) : v;
    }

    static float clampValue(const int p, const float v)
    {
        const PadifierRange& r = kPadifierRanges[p];
        return std::fmin(std::fmax(v, r.min), r.max);
    }

    static float segmentAt(const Rect& r, const float x, const int count)
    {
        const int i = static_cast<int>((x - r.x) / (r.w / count));
        return static_cast<float>(i < 0 ? 0 : i >= count ? count - 1 : i);
    }

    void changeValue(const int p, const float v)
    {
        if (fValues[p] == v)
            return;
        fValues[p] = v;
        setParameterValue(p, v);
        repaint();
    }

    // one complete edit gesture
    void setOnce(const int p, const float v)
    {
        editParameter(p, true);
        changeValue(p, v);
        editParameter(p, false);
    }

    void toggle(const int p) { setOnce(p, fValues[p] > 0.5f ? 0.0f : 1.0f); }

    // interface only: no parameter, no effect on the sound; saved as a preference of the user
    void setTheme(const int theme)
    {
        if (theme == fTheme)
            return;
        fTheme = theme;
        fT = &kThemes[theme];
#ifndef PADIFIER_UI_DEMO_THEME
        PadifierPrefs::saveTheme(theme);
#endif
        repaint();
    }

    // ---- A / B: the active set is the live parameters, the other one is kept here

    void storeSlot(const int slot)
    {
        Slot& s = fSlots[slot];
        for (int i = 0; i < kPresetParamCount; ++i)
        {
            s.values[i] = fValues[i];
            s.presetValues[i] = fPresetValues[i];
        }
        s.preset = fPreset;
        s.user = fPresetUser;
        s.name = fPresetName;
        s.set = true;
    }

    void switchSlot(const int slot)
    {
        if (slot == fSlot)
            return;
        storeSlot(fSlot);
        fSlot = slot;
        if (!fSlots[slot].set)
            storeSlot(slot);   // first visit: starts as a copy of the other set
        const Slot s = fSlots[slot];
        loadValues(s.values);
        for (int i = 0; i < kPresetParamCount; ++i)
            fPresetValues[i] = s.presetValues[i];
        fPreset = s.preset;
        fPresetUser = s.user;
        fPresetName = s.name;
        writeABState();
        repaint();
    }

    void copySlot()
    {
        storeSlot(1 - fSlot);
        writeABState();
        repaint();
    }

    void writeABState()
    {
        const Slot& other = fSlots[1 - fSlot];
        std::string text = "slot=" + std::to_string(fSlot) + ";other=";
        for (int i = 0; i < kPresetParamCount; ++i)
            text += (i ? "," : "") + PadifierUserPresets::number(other.set ? other.values[i] : fValues[i]);
        setState(kStateKeys[kStateAB], text.c_str());
    }

    void readABState(const char* const value)
    {
        const char* const slot = std::strstr(value, "slot=");
        const char* const other = std::strstr(value, "other=");
        if (slot == nullptr || other == nullptr || (slot[5] != '0' && slot[5] != '1'))
            return;
        std::istringstream in(other + 6);
        in.imbue(std::locale::classic());
        float values[kPresetParamCount];
        for (int i = 0; i < kPresetParamCount; ++i)
        {
            float v = kPadifierRanges[i].def;
            char comma = ',';
            if (!(in >> v) || !std::isfinite(v) || (i + 1 < kPresetParamCount && !(in >> comma)) || comma != ',')
                return;
            values[i] = clampValue(i, v);
        }
        fSlot = slot[5] - '0';
        Slot& s = fSlots[1 - fSlot];
        for (int i = 0; i < kPresetParamCount; ++i)
            s.values[i] = s.presetValues[i] = values[i];
        s.preset = -1;
        s.user = false;
        s.name = fSlot == 0 ? "Set B" : "Set A";   // the preset of a saved set is not kept
        s.set = true;
        repaint();
    }

    // loads a set of values: one edit gesture per parameter, remembered to show the "*"
    void loadValues(const float values[kPresetParamCount])
    {
        for (int i = 0; i < kPresetParamCount; ++i)
        {
            if (!isPresetParam(i))
                continue;
            setOnce(i, values[i]);
            fPresetValues[i] = values[i];
        }
        repaint();
    }

    void applyPreset(const int index)
    {
        loadValues(kPadifierPresets[index].values);
        fPreset = index;
        fPresetUser = false;
        fPresetName = kPadifierPresets[index].name;
    }

    // a saved preset, read from its file now (it may have been changed by another instance)
    void applyUserPreset(const std::string& name)
    {
        float values[kPresetParamCount];
        if (!PadifierUserPresets::load(name, values))
            return;
        loadValues(values);
        fPreset = -1;
        fPresetUser = true;
        fPresetName = name;
    }

    // next (+1) or previous (-1) preset, factory presets then saved ones, in a loop
    void stepPreset(const int step)
    {
        fUserNames = PadifierUserPresets::list();
        const int n = kPadifierPresetCount + static_cast<int>(fUserNames.size());
        int pos = fPreset;
        if (fPresetUser)
        {
            pos = kPadifierPresetCount - 1;   // deleted meanwhile: between the two sections
            for (size_t j = 0; j < fUserNames.size(); ++j)
                if (PadifierUserPresets::sameName(fUserNames[j], fPresetName))
                    pos = kPadifierPresetCount + static_cast<int>(j);
        }
        pos = (pos + step + n) % n;
        if (pos < kPadifierPresetCount)
            applyPreset(pos);
        else
            applyUserPreset(fUserNames[pos - kPadifierPresetCount]);
    }

    bool isCurrentRow(const MenuRow& m) const
    {
        if (m.kind == kRowFactory)
            return !fPresetUser && fPreset == m.index;
        return m.kind == kRowUser && fPresetUser && PadifierUserPresets::sameName(fUserNames[m.index], fPresetName);
    }

    // ---- preset list: rows, read again from the folder each time the list opens

    void buildRows()
    {
        fUserNames = PadifierUserPresets::list();
        fRows.clear();
        float y = 4.0f;
        const auto add = [&](const int kind, const int index, const float h) {
            fRows.push_back({ kind, index, y, h });
            y += h;
        };
        add(kRowHeader, 0, kMenuHeaderH);
        for (int i = 0; i < kPadifierPresetCount; ++i)
            add(kRowFactory, i, kMenuItemH);
        add(kRowHeader, 1, kMenuHeaderH + 10.0f);
        if (fUserNames.empty())
            add(kRowNone, 0, kMenuItemH);
        for (size_t j = 0; j < fUserNames.size(); ++j)
            add(kRowUser, static_cast<int>(j), kMenuItemH);
        add(kRowDivider, 0, kMenuDividerH);
        add(kRowSave, 0, kMenuItemH);
        fMenuContentH = y + 4.0f;
        fMenuScroll = std::fmin(fMenuScroll, std::fmax(fMenuContentH - menuVisibleH(), 0.0f));
    }

    static float menuTop() { return kPreset.y + kPreset.h + 4.0f; }

    float menuVisibleH() const { return std::fmin(fMenuContentH, kH - menuTop() - 8.0f); }

    Rect rowRect(const int i) const
    {
        return { kPreset.x, menuTop() + fRows[i].y - fMenuScroll, kPreset.w, fRows[i].h };
    }

    // cross that deletes a saved preset (on the right of its row)
    static Rect crossRect(const Rect& r) { return { r.x + r.w - 26.0f, r.y, 26.0f, r.h }; }

    int rowAt(const float x, const float y) const
    {
        const Rect list = { kPreset.x, menuTop(), kPreset.w, menuVisibleH() };
        if (!list.contains(x, y))
            return -1;
        for (int i = 0; i < static_cast<int>(fRows.size()); ++i)
            if (rowRect(i).contains(x, y))
                return i;
        return -1;
    }

    void openMenu()
    {
        fMenuScroll = 0.0f;
        buildRows();
        fMenuOpen = true;
        fMenuHover = fDeleteArmed = -1;
        fMenuOnCross = false;
        repaint();
    }

    void closeMenu()
    {
        fMenuOpen = false;
        fMenuHover = fDeleteArmed = -1;
        repaint();
    }

    void menuClick(const float x, const float y, const bool left)
    {
        const int row = rowAt(x, y);
        if (row < 0)
        {
            closeMenu();
            return;
        }
        if (!left)
            return;
        const MenuRow m = fRows[row];
        if (m.kind == kRowUser && crossRect(rowRect(row)).contains(x, y))
        {
            // first click arms, second click deletes (no undo)
            if (fDeleteArmed == row)
            {
                PadifierUserPresets::erase(fUserNames[m.index]);
                buildRows();
                fDeleteArmed = fMenuHover = -1;
            }
            else
                fDeleteArmed = row;
            repaint();
            return;
        }
        if (m.kind == kRowFactory)
        {
            closeMenu();
            applyPreset(m.index);
        }
        else if (m.kind == kRowUser)
        {
            const std::string name = fUserNames[m.index];
            closeMenu();
            applyUserPreset(name);
        }
        else if (m.kind == kRowSave)
        {
            closeMenu();
            startNaming();
        }
    }

    // ---- saving: the name is typed in the preset box

    void startNaming()
    {
        fUserNames = PadifierUserPresets::list();
        fNaming = true;
        fNameText = fPresetUser ? fPresetName : std::string();
        fNameError = false;
        repaint();
    }

    void cancelNaming()
    {
        fNaming = false;
        repaint();
    }

    static std::string trimmed(const std::string& s)
    {
        const size_t a = s.find_first_not_of(' ');
        if (a == std::string::npos)
            return std::string();
        return s.substr(a, s.find_last_not_of(' ') - a + 1);
    }

    void commitNaming()
    {
        const std::string name = trimmed(fNameText);
        if (!PadifierUserPresets::validName(name) || PadifierUserPresets::isFactoryName(name))
            return;   // the hint says why
        float values[kPresetParamCount];
        for (int i = 0; i < kPresetParamCount; ++i)
            values[i] = fValues[i];
        if (!PadifierUserPresets::save(name, values))
        {
            fNameError = true;
            repaint();
            return;
        }
        for (int i = 0; i < kPresetParamCount; ++i)
            fPresetValues[i] = values[i];
        fNaming = false;
        fPreset = -1;
        fPresetUser = true;
        fPresetName = name;
        repaint();
    }

    // what Enter will do with the name typed; error: Enter does nothing
    const char* namingHint(bool& error) const
    {
        const std::string name = trimmed(fNameText);
        error = true;
        if (fNameError)
            return "Could not write the preset file (folder not writable).";
        if (!name.empty() && PadifierUserPresets::isFactoryName(name))
            return "This is the name of a factory preset: choose another one.";
        if (!name.empty() && !PadifierUserPresets::validName(name))
            return "Invalid name: no dot at the end, not CON, PRN, AUX, NUL, COM1 or LPT1.";
        error = false;
        if (name.empty())
            return "Type a name. Enter: save. Esc: cancel.";
        for (const std::string& other : fUserNames)
            if (PadifierUserPresets::sameName(other, name))
                return "Enter: replace your preset of this name. Esc: cancel.";
        return "Enter: save (kept for all projects on this computer). Esc: cancel.";
    }

    bool presetModified() const
    {
        for (int i = 0; i < kPresetParamCount; ++i)
            if (isPresetParam(i) && std::fabs(fValues[i] - fPresetValues[i]) > 1e-4f)
                return true;
        return false;
    }

    void commitEdit(const bool apply)
    {
        if (fEdit < 0)
            return;
        if (apply && fEditText[0] != '\0')
        {
            char* end = nullptr;
            const float v = std::strtof(fEditText, &end);
            if (end != fEditText && std::isfinite(v))
            {
                const int p = kKnobs[fEdit].param;
                setOnce(p, whole(p) ? std::round(clampValue(p, v)) : clampValue(p, v));
            }
        }
        fEdit = -1;
        repaint();
    }

    int knobAt(const float x, const float y) const
    {
        for (int i = 0; i < kNumKnobs; ++i)
        {
            const KnobDef& k = kKnobs[i];
            const float dx = x - k.cx, dy = y - k.cy, r = k.r + 8.0f;
            if (dx * dx + dy * dy <= r * r)
                return i;
        }
        return -1;
    }

    // value typed with the keyboard: the number in the unit of the parameter
    void formatNumber(const int p, char* buf, const size_t size) const
    {
        const float v = fValues[p];
        const char* fmt = "%.0f";
        if (p == kParamLength || p == kParamLfoRate || p == kParamPanRate || p == kParamLfoPitch)
            fmt = "%.2f";
        else if (p == kParamOutput || p == kParamColour || p == kParamDamping || p == kParamLfoTone)
            fmt = "%.1f";
        std::snprintf(buf, size, fmt, v);
    }

    PadifierIR::Settings irSettings() const
    {
        return PadifierIR::fromParams(fValues, sampleRate());
    }

    static void frequencyText(const double hz, char* buf, const size_t size)
    {
        if (hz >= 1000.0)
            std::snprintf(buf, size, "%.1f kHz", hz / 1000.0);
        else
            std::snprintf(buf, size, "%.0f Hz", hz);
    }

    static void secondsText(const double t, char* buf, const size_t size)
    {
        if (t < 1.0)
            std::snprintf(buf, size, "%.0f ms", 1000.0 * t);
        else
            std::snprintf(buf, size, "%.2f s", t);
    }

    static const char* colourName(const float v)
    {
        return v <= -5.0f ? "brown" : v <= -2.0f ? "pink" : v < 1.0f ? "white" : v < 4.5f ? "blue" : "violet";
    }

    // short text under a knob; detail: the longer form of the tooltip title
    void valueText(const int p, char* buf, const size_t size, const bool detail) const
    {
        const float v = fValues[p];
        char f[32];
        switch (p)
        {
        case kParamColour:
            std::snprintf(buf, size, detail ? "%+.1f dB/octave (%s)" : "%+.1f dB/oct", v, colourName(v));
            break;
        case kParamDensity:
            if (v >= kDensitySmooth)
                std::snprintf(buf, size, "smooth");
            else
                std::snprintf(buf, size, detail ? "%.0f impulses / s" : "%.0f /s", v);
            break;
        case kParamSeed:  std::snprintf(buf, size, "%d", PadifierLaw::seed(v)); break;
        case kParamNote:
            PadifierLaw::noteName(static_cast<int>(std::lround(v)), f, sizeof(f));
            if (detail)
                std::snprintf(buf, size, "%s (%.1f Hz)", f, PadifierLaw::noteHz(std::lround(v)));
            else
                std::snprintf(buf, size, "%s", f);
            break;
        case kParamChord: std::snprintf(buf, size, "%s", kChords[PadifierLaw::choice(v, kChordCount)].name); break;
        case kParamResonance:
            secondsText(PadifierLaw::ringSeconds(v), f, sizeof(f));
            std::snprintf(buf, size, detail ? "%.0f %% (ring %s)" : "%.0f %%", v, f);
            break;
        case kParamLfoPitch:
            if (v <= 0.0f)
                std::snprintf(buf, size, "off");
            else
                std::snprintf(buf, size, detail ? "+-%.2f semitones" : "%.2f st", v);
            break;
        case kParamLength:
            secondsText(v, f, sizeof(f));
            if (detail)
                std::snprintf(buf, size, "%s (%d samples per path)", f, PadifierIR::lengthSamples(irSettings()));
            else
                std::snprintf(buf, size, "%s", f);
            break;
        case kParamSwell:
            secondsText(irSettings().swell * irSettings().length, f, sizeof(f));
            std::snprintf(buf, size, detail ? "%.0f %% (peak at %s)" : "%.0f %%", v, f);
            break;
        case kParamDecay:
            if (detail)
            {
                const PadifierIR::Settings st = irSettings();
                secondsText(PadifierIR::fallStart(st, st.swell * st.length), f, sizeof(f));
                if (v <= 0.0f)
                    std::snprintf(buf, size, "0 %% (held to the end)");
                else
                    std::snprintf(buf, size, "%.0f %% (falls to -60 dB from %s)", v, f);
            }
            else
                std::snprintf(buf, size, "%.0f %%", v);
            break;
        case kParamDamping:
            if (v <= 0.0f)
                std::snprintf(buf, size, "off");
            else
                std::snprintf(buf, size, detail ? "%.1f dB/octave above 250 Hz" : "%.1f dB/oct", v);
            break;
        case kParamBloom:
            if (v == 0.0f)
                std::snprintf(buf, size, "off");
            else
                std::snprintf(buf, size, detail ? "%+.0f %% (%s later)" : "%+.0f %%", v, v > 0.0f ? "highs" : "lows");
            break;
        case kParamLfoRate:
            std::snprintf(buf, size, detail ? "%.2f Hz (period %.2f s)" : "%.2f Hz", v, 1.0 / v);
            break;
        case kParamLfoTone:
            if (v <= 0.0f)
                std::snprintf(buf, size, "off");
            else
                std::snprintf(buf, size, detail ? "+-%.1f dB/octave" : "%.1f dB/oct", v);
            break;
        case kParamLfoResonance:
            if (v <= 0.0f)
                std::snprintf(buf, size, "off");
            else
                std::snprintf(buf, size, detail ? "+-%.0f %% of the scale" : "%.0f %%", v);
            break;
        case kParamLfoDensity:
        case kParamLfoLowCut:
        case kParamLfoHighCut:
            if (v <= 0.0f)
                std::snprintf(buf, size, "off");
            else
                std::snprintf(buf, size, detail ? "+-%.1f octaves" : "%.1f oct", v);
            break;
        case kParamPhasePitch:
        case kParamPhaseTone:
        case kParamPhaseResonance:
        case kParamPhaseDensity:
        case kParamPhaseLowCut:
        case kParamPhaseHighCut:
            std::snprintf(buf, size, "%s\xc2\xb0", kPhaseNames[PadifierLaw::choice(v, kPhaseCount)]);
            break;
        case kParamPanRate:
            std::snprintf(buf, size, detail ? "%.2f Hz (period %.2f s)" : "%.2f Hz", v, 1.0 / v);
            break;
        case kParamPanDepth:
        case kParamCross:
            if (v <= 0.0f)
                std::snprintf(buf, size, "off");
            else
                std::snprintf(buf, size, "%.0f %%", v);
            break;
        case kParamPredelay: std::snprintf(buf, size, "%.0f ms", v); break;
        case kParamLowCut:
            // swept by the LFO: a centre, never off
            if (v <= kLowCutOff + 0.05f && !lowCutInResponse(fValues))
                std::snprintf(buf, size, "off");
            else
                std::snprintf(buf, size, detail && lowCutInResponse(fValues) ? "%.0f Hz, swept by the LFO" : "%.0f Hz", v);
            break;
        case kParamHighCut:
            if (v >= kHighCutOff - 0.5f && !highCutInResponse(fValues))
                std::snprintf(buf, size, "off");
            else
            {
                frequencyText(v, buf, size);
                if (detail && highCutInResponse(fValues))
                    std::snprintf(buf + std::strlen(buf), size - std::strlen(buf), ", swept by the LFO");
            }
            break;
        case kParamMix:
            if (detail)
                std::snprintf(buf, size, "%.0f %% (pad %s, dry %s)", v, dbText(PadifierLaw::wetGain(v)).c_str(),
                              dbText(PadifierLaw::dryGain(v)).c_str());
            else
                std::snprintf(buf, size, "%.0f %%", v);
            break;
        case kParamWidth:  std::snprintf(buf, size, "%.0f %%", v); break;
        case kParamOutput: std::snprintf(buf, size, "%+.1f dB", v); break;
        default:           std::snprintf(buf, size, "%.2f", v); break;
        }
    }

    static int listCount(const int) { return kChordCount; }

    static const char* listName(const int, const int i) { return kChords[i].name; }

    int listValue(const int l) const { return PadifierLaw::choice(fValues[kLists[l].param], listCount(l)); }

    bool listActive(const int l) const
    {
        return l != kListChord || PadifierLaw::choice(fValues[kParamTune], 3) == 2;
    }

    int listAt(const float x, const float y) const
    {
        for (int l = 0; l < kNumLists; ++l)
            if (kLists[l].box.contains(x, y))
                return l;
        return -1;
    }

    // the open list: under its box, one row per value
    Rect listRect(const int l) const
    {
        const Rect& b = kLists[l].box;
        return { b.x, b.y + b.h + 2.0f, std::max(b.w, 90.0f), listCount(l) * kListRowH + 6.0f };
    }

    int listRowAt(const int l, const float x, const float y) const
    {
        const Rect r = listRect(l);
        if (!r.contains(x, y))
            return -1;
        const int i = static_cast<int>((y - r.y - 3.0f) / kListRowH);
        return i >= 0 && i < listCount(l) ? i : -1;
    }

    void openList(const int l)
    {
        fList = l;
        fListHover = -1;
        repaint();
    }

    void closeList()
    {
        fList = -1;
        repaint();
    }

    int phaseAt(const float x, const float y) const
    {
        for (int i = 0; i < kLfoPhaseTargets; ++i)
            if (phaseRect(i).contains(x, y))
                return i;
        return -1;
    }

    // next (+1) or previous (-1) quarter turn, round the circle
    void stepPhase(const int i, const int step)
    {
        const int q = PadifierLaw::choice(fValues[kPhases[i].param], kPhaseCount);
        setOnce(kPhases[i].param, static_cast<float>((q + step + kPhaseCount) % kPhaseCount));
    }

    // a phase matters while its target moves
    bool phaseActive(const int i) const
    {
        const int k = kPhases[i].knob;
        return knobActive(k) && fValues[kKnobs[k].param] > 0.0f;
    }

    // knobs without effect with the other settings: drawn faint
    bool knobActive(const int k) const
    {
        const int tune = PadifierLaw::choice(fValues[kParamTune], 3);
        switch (kKnobs[k].param)
        {
        case kParamNote:
        case kParamResonance:
        case kParamLfoPitch: return tune > 0;
        case kParamChord:    return tune == 2;
        case kParamLfoResonance: return tune > 0;
        case kParamLfoDensity:   return fValues[kParamDensity] < kDensitySmooth;
        case kParamLfoRate:
            return (tune > 0 && (fValues[kParamLfoPitch] > 0.0f || fValues[kParamLfoResonance] > 0.0f))
                || fValues[kParamLfoTone] > 0.0f || fValues[kParamLfoLowCut] > 0.0f || fValues[kParamLfoHighCut] > 0.0f
                || (fValues[kParamDensity] < kDensitySmooth && fValues[kParamLfoDensity] > 0.0f);
        case kParamPanRate:  return fValues[kParamPanDepth] > 0.0f;
        default:             return true;
        }
    }

    static std::string dbText(const float g)
    {
        char b[24];
        if (g <= 1e-6f)
            std::snprintf(b, sizeof(b), "-inf dB");
        else
            std::snprintf(b, sizeof(b), "%.1f dB", 20.0f * std::log10(g));
        return b;
    }

    void label(const float x, const float y, const float size, const Color& c, const int align,
               const char* str, const float spacing = 0.0f)
    {
        fontFaceId(fFont);
        fontSize(size);
        textLetterSpacing(spacing);
        fillColor(c);
        textAlign(align);
        text(x, y, str, nullptr);
    }

    void panel(const Rect& r)
    {
        beginPath();
        rect(r.x - 10, r.y - 10, r.w + 20, r.h + 24);
        roundedRect(r.x, r.y, r.w, r.h, 8);
        pathWinding(CW);
        fillPaint(boxGradient(r.x, r.y + 3, r.w, r.h, 10, 14, fT->panelShadow, transparent(fT->panelShadow)));
        fill();

        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 8);
        fillPaint(linearGradient(r.x, r.y, r.x, r.y + r.h, fT->panel0, fT->panel1));
        fill();
        sheen(r.x, r.y, r.w, r.h, 8);
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 8);
        strokeWidth(1.0f);
        strokeColor(fT->panelEdge);
        stroke();
    }

    // opalescent sheen over a filled box (nothing when the theme has none)
    void sheen(const float x, const float y, const float w, const float h, const float radius)
    {
        if (fT->sheen0.alpha <= 0.0f && fT->sheen1.alpha <= 0.0f && fT->sheen2.alpha <= 0.0f)
            return;
        beginPath();
        roundedRect(x, y, w, h, radius);
        fillPaint(linearGradient(x, y, x + w, y + h, fT->sheen0, fT->sheen1));
        fill();
        beginPath();
        roundedRect(x, y, w, h, radius);
        fillPaint(radialGradient(x + w * 0.2f, y, 0, std::fmax(w, h) * 0.7f, fT->sheen2, transparent(fT->sheen2)));
        fill();
    }

    // numbered section title: "1  INPUT  what it does"
    void sectionTitle(const Rect& r, const char* number, const char* title, const char* subtitle)
    {
        const float cx = r.x + 18, cy = r.y + 17;
        beginPath();
        circle(cx, cy, 8.5f);
        fillPaint(linearGradient(0, cy - 8, 0, cy + 8, fT->hot, fT->deep));
        fill();
        label(cx, cy + 0.5f, 10, fT->onAccent, ALIGN_CENTER | ALIGN_MIDDLE, number);
        label(cx + 15, cy, 11, fT->text, ALIGN_LEFT | ALIGN_MIDDLE, title, 2);
        label(r.x + r.w - 12, cy, 9, fT->faint, ALIGN_RIGHT | ALIGN_MIDDLE, subtitle, 0.5f);
    }

    void divider(const float x0, const float x1, const float y)
    {
        beginPath();
        moveTo(x0, y + 0.5f);
        lineTo(x1, y + 0.5f);
        strokeWidth(1.0f);
        strokeColor(fT->divider);
        stroke();
    }

    // small arrow in the gap between two sections: the signal goes from left to right
    void flowArrow(const float x, const float y)
    {
        beginPath();
        moveTo(x - 3, y - 7);
        lineTo(x + 4, y);
        lineTo(x - 3, y + 7);
        closePath();
        fillColor(fT->arrow);
        fill();
    }

    void button(const Rect& r, const bool on, const char* str, const float size = 11.0f)
    {
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 5);
        fillPaint(linearGradient(r.x, r.y, r.x, r.y + r.h,
                                 on ? fT->btnOn0 : fT->btnOff0,
                                 on ? fT->btnOn1 : fT->btnOff1));
        fill();
        strokeWidth(1.0f);
        strokeColor(on ? fT->btnOnEdge : fHover == hoverOf(r) ? fT->hoverEdge : fT->panelEdge);
        stroke();
        if (str)
            label(r.x + r.w * 0.5f, r.y + r.h * 0.5f, size, on ? fT->onAccent : fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, str, 1);
    }

    void led(const float x, const float y, const bool on)
    {
        if (on)
        {
            beginPath();
            circle(x, y, 10);
            fillPaint(radialGradient(x, y, 2, 10, fT->ledGlow, fT->ledGlowEnd));
            fill();
        }
        beginPath();
        circle(x, y, 4.0f);
        fillColor(on ? fT->ledOn : fT->ledOff);
        fill();
    }

    void segmented(const Rect& r, const char* const* labels, const int count, const int selected,
                   const float size)
    {
        const float sw = r.w / count;
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 5);
        fillColor(fT->well);
        fill();
        strokeWidth(1.0f);
        strokeColor(fHover == hoverOf(r) ? fT->hoverEdge : fT->panelEdge);
        stroke();
        for (int i = 0; i < count; ++i)
        {
            if (i == selected)
            {
                beginPath();
                roundedRect(r.x + i * sw + 1.5f, r.y + 1.5f, sw - 3.0f, r.h - 3.0f, 4);
                fillPaint(linearGradient(0, r.y, 0, r.y + r.h, fT->sel0, fT->sel1));
                fill();
            }
            label(r.x + (i + 0.5f) * sw, r.y + r.h * 0.5f, size, i == selected ? fT->onAccent : fT->dim,
                  ALIGN_CENTER | ALIGN_MIDDLE, labels[i], 0.5f);
        }
    }

    int hoverOf(const Rect& r) const
    {
        for (int z = 0; z < kNumZones; ++z)
            if (kZones[z].r.x == r.x && kZones[z].r.y == r.y)
                return kHoverZoneBase + z;
        return -2;
    }

    // ------------------------------------------------------------------ parts

    void drawBackground()
    {
        beginPath();
        rect(0, 0, kW, kH);
        fillPaint(linearGradient(0, 0, 0, kH, fT->bg1, fT->bg0));
        fill();

        beginPath();
        rect(0, 0, kW, kH);
        fillPaint(radialGradient(kW * 0.5f, kH + 80, 40, 560, fT->bgGlow, transparent(fT->bgGlow)));
        fill();

        const Rect* const sections[5] = { &kSecSource, &kSecTune, &kSecEnv, &kSecMotion, &kSecOut };
        for (int i = 0; i < 5; ++i)
        {
            panel(*sections[i]);
            if (i < 4)
                flowArrow(sections[i]->x + sections[i]->w + 5, sections[i]->y + sections[i]->h * 0.5f);
        }
        panel(kBottom);
    }

    void drawHeader()
    {
        beginPath();
        rect(0, 0, kW, 56);
        fillPaint(linearGradient(0, 0, 0, 56, fT->header0, fT->header1));
        fill();
        sheen(0, 0, kW, 56, 0);
        beginPath();
        moveTo(0, 56.5f);
        lineTo(kW, 56.5f);
        strokeColor(fT->headerLine);
        strokeWidth(1.0f);
        stroke();

        label(26, 31, 14, fT->dim, ALIGN_LEFT | ALIGN_MIDDLE, "NOISE", 5);
        fontFaceId(fFont);
        fontSize(30);
        textLetterSpacing(5);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fontBlur(10);
        fillColor(fT->brandGlow);
        text(104, 29, "PADIFIER", nullptr);
        fontBlur(0);
        fillPaint(linearGradient(0, 14, 0, 44, fT->ledOn, fT->hot));
        text(104, 29, "PADIFIER", nullptr);

        // preset drop-down box
        beginPath();
        roundedRect(kPreset.x, kPreset.y, kPreset.w, kPreset.h, 5);
        fillColor(fT->well);
        fill();
        strokeWidth(1.0f);
        strokeColor(fMenuOpen || fNaming ? fT->gold : fHover == hoverOf(kPreset) ? fT->hoverEdge : fT->panelEdge);
        stroke();
        char name[64];
        if (fNaming)
        {
            std::snprintf(name, sizeof(name), "%s_", fNameText.c_str());
            label(kPreset.x + 12, kPreset.y + kPreset.h * 0.5f, 12, fT->gold, ALIGN_LEFT | ALIGN_MIDDLE, name, 0.5f);
        }
        else
        {
            std::snprintf(name, sizeof(name), "%s%s", fPresetName.c_str(), presetModified() ? " *" : "");
            label(kPreset.x + 12, kPreset.y + kPreset.h * 0.5f, 12, fT->text, ALIGN_LEFT | ALIGN_MIDDLE, name, 0.5f);
        }
        // arrow: down when closed, up when open
        const float ax = kPreset.x + kPreset.w - 14, ay = kPreset.y + kPreset.h * 0.5f, d = fMenuOpen ? -1.0f : 1.0f;
        beginPath();
        moveTo(ax - 5, ay - 2.5f * d);
        lineTo(ax + 5, ay - 2.5f * d);
        lineTo(ax, ay + 3.5f * d);
        closePath();
        fillColor(fMenuOpen ? fT->gold : fT->dim);
        fill();

        // A / B and copy
        const char* const ab[2] = { "A", "B" };
        segmented(kAB, ab, 2, fSlot, 11);
        button(kCopy, false, fSlot == 0 ? "A>B" : "B>A", 9);

        // theme
        segmented(kTheme, kThemeNames, PadifierPrefs::kThemeCount, fTheme, 9);

        // bypass
        const bool bypassed = fValues[kParamBypass] > 0.5f;
        button(kBypass, bypassed, nullptr);
        led(kBypass.x + 16, kBypass.y + kBypass.h * 0.5f, !bypassed);
        label(kBypass.x + kBypass.w * 0.5f + 9, kBypass.y + kBypass.h * 0.5f, 11.5f, bypassed ? fT->bypassText : fT->text,
              ALIGN_CENTER | ALIGN_MIDDLE, bypassed ? "BYPASSED" : "ACTIVE", 1.8f);
    }

    void drawSourceSection()
    {
        const Rect& r = kSecSource;
        sectionTitle(r, "1", "SOURCE", "noise");
        drawKnob(kKnobColour);
        divider(r.x + 12, r.x + r.w - 12, 258);
        drawKnob(kKnobDensity);
        drawKnob(kKnobSeed);
    }

    void drawTuneSection()
    {
        const Rect& r = kSecTune;
        sectionTitle(r, "2", "TUNE", "combs on the noise");
        const char* const names[3] = { "OFF", "COMB", "CHORD" };
        const int tune = PadifierLaw::choice(fValues[kParamTune], 3);
        segmented(kTune, names, 3, tune, 10);
        for (int k = kKnobNote; k <= kKnobLfoPitch; ++k)
            drawKnob(k);
        drawListBox(kListChord);
        drawPhaseBox(0);
        divider(r.x + 12, r.x + r.w - 12, 394);
        const float cx = r.x + r.w * 0.5f;
        char a[64], b[96];
        if (tune == 0)
        {
            label(cx, 422, 10, fT->faint, ALIGN_CENTER | ALIGN_MIDDLE, "noise only", 0.5f);
            return;
        }
        const int note = static_cast<int>(std::lround(fValues[kParamNote]));
        PadifierLaw::noteName(note, a, sizeof(a));
        if (tune == 1)
            std::snprintf(b, sizeof(b), "%s  %.1f Hz", a, PadifierLaw::noteHz(note));
        else
        {
            const PadifierChord& c = kChords[PadifierLaw::choice(fValues[kParamChord], kChordCount)];
            std::string list;
            for (int i = 0; i < c.count; ++i)
            {
                PadifierLaw::noteName(note + c.steps[i], a, sizeof(a));
                list += (i ? " " : "") + std::string(a);
            }
            std::snprintf(b, sizeof(b), "%s", list.c_str());
        }
        label(cx, 416, 13, fT->gold, ALIGN_CENTER | ALIGN_MIDDLE, b, 0.8f);
        secondsText(PadifierLaw::ringSeconds(fValues[kParamResonance]), a, sizeof(a));
        std::snprintf(b, sizeof(b), "ring %s", a);
        label(cx, 440, 9.5f, fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, b, 0.3f);
    }

    void drawEnvelopeSection()
    {
        const Rect& r = kSecEnv;
        sectionTitle(r, "3", "ENVELOPE", "of the impulse response");
        for (int k = kKnobLength; k <= kKnobBloom; ++k)
            drawKnob(k);
        divider(r.x + 12, r.x + r.w - 12, 370);

        const PadifierIR::Settings st = irSettings();
        const float cx = r.x + r.w * 0.5f;
        char a[32], b[128];
        label(cx, 388, 9.5f, fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, "PEAK OF THE ENVELOPE", 1.2f);
        secondsText(st.swell * st.length, a, sizeof(a));
        if (st.bloom != 0.0)
        {
            char lo[32], hi[32];
            secondsText(PadifierIR::peakTime(st, 125.0), lo, sizeof(lo));
            secondsText(PadifierIR::peakTime(st, 8000.0), hi, sizeof(hi));
            std::snprintf(b, sizeof(b), "%s   (125 Hz %s, 8 kHz %s)", a, lo, hi);
            label(cx, 410, 15, fT->gold, ALIGN_CENTER | ALIGN_MIDDLE, b, 0.5f);
        }
        else
            label(cx, 410, 20, fT->gold, ALIGN_CENTER | ALIGN_MIDDLE, a, 1);
        char c[32];
        secondsText(st.length, a, sizeof(a));
        secondsText(PadifierIR::fallStart(st, st.swell * st.length), c, sizeof(c));
        if (st.decay <= 0.0)
            std::snprintf(b, sizeof(b), "held to the end (%s)", a);
        else if (st.damping > 0.0)
            std::snprintf(b, sizeof(b), "falls from %s to -60 dB at %s (%.0f dB at 8 kHz)", c, a, PadifierIR::endDb(st, 8000.0));
        else
            std::snprintf(b, sizeof(b), "falls from %s to -60 dB at %s", c, a);
        label(cx, 432, 10, fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, b, 0.5f);
        std::snprintf(b, sizeof(b), "%d samples per path  |  latency %.1f ms", PadifierIR::lengthSamples(st),
                      1000.0 * PadifierConv::kHeadBlock / sampleRate());
        label(cx, 451, 9.5f, fT->faint, ALIGN_CENTER | ALIGN_MIDDLE, b, 0.3f);
    }

    void drawMotionSection()
    {
        const Rect& r = kSecMotion;
        sectionTitle(r, "4", "MOTION", "along the response");
        label(r.x + r.w * 0.5f, 106, 9, fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, "LFO: RATE, DEPTH AND PHASE", 1.2f);
        for (int k = kKnobLfoRate; k <= kKnobCross; ++k)
            drawKnob(k);
        for (int i = 1; i < kLfoPhaseTargets; ++i)
            drawPhaseBox(i);
        divider(r.x + 12, r.x + r.w - 12, 345);
    }

    // box of a phase: faint while its target does not move
    void drawPhaseBox(const int i)
    {
        const Rect b = phaseRect(i);
        const bool active = phaseActive(i), hover = fHover == kHoverPhaseBase + i;
        const int q = PadifierLaw::choice(fValues[kPhases[i].param], kPhaseCount);
        if (!active)
            globalAlpha(0.35f);
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 4);
        fillColor(fT->well);
        fill();
        strokeWidth(1.0f);
        strokeColor(hover ? fT->hoverEdge : fT->panelEdge);
        stroke();
        char t[16];
        valueText(kPhases[i].param, t, sizeof(t), false);
        label(b.x + b.w * 0.5f, b.y + b.h * 0.5f, 9.5f, q > 0 && active ? fT->gold : fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, t);
        if (!active)
            globalAlpha(1.0f);
    }

    void drawOutputSection()
    {
        const Rect& r = kSecOut;
        sectionTitle(r, "5", "OUTPUT", "mix and tone");
        for (int k = kKnobMix; k <= kKnobWidth; ++k)
            drawKnob(k);
        divider(r.x + 12, r.x + r.w - 12, 222);
        divider(r.x + 12, r.x + r.w - 12, 337);
    }

    // box of a list: label below, as a knob; faint while it has no effect
    void drawListBox(const int l)
    {
        const ListDef& d = kLists[l];
        const Rect& b = d.box;
        const bool active = listActive(l), open = fList == l;
        if (!active)
            globalAlpha(0.35f);
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 5);
        fillColor(fT->well);
        fill();
        strokeWidth(1.0f);
        strokeColor(open ? fT->gold : fHover == kHoverListBase + l ? fT->hoverEdge : fT->panelEdge);
        stroke();
        label(b.x + 9, b.y + b.h * 0.5f, 11.5f, open ? fT->gold : fT->text, ALIGN_LEFT | ALIGN_MIDDLE,
              listName(l, listValue(l)), 0.3f);
        const float ax = b.x + b.w - 11, ay = b.y + b.h * 0.5f, dd = open ? -1.0f : 1.0f;
        beginPath();
        moveTo(ax - 4, ay - 2 * dd);
        lineTo(ax + 4, ay - 2 * dd);
        lineTo(ax, ay + 3 * dd);
        closePath();
        fillColor(open ? fT->gold : fT->dim);
        fill();
        label(b.x + b.w * 0.5f, b.y + b.h + 28, 11, fT->text, ALIGN_CENTER | ALIGN_MIDDLE,
              d.label, 1.5f);
        if (!active)
            globalAlpha(1.0f);
    }

    // open list, over everything: the values, the current one marked with a dot
    void drawList(const int l)
    {
        const Rect r = listRect(l);
        beginPath();
        rect(r.x - 10, r.y - 6, r.w + 20, r.h + 22);
        roundedRect(r.x, r.y, r.w, r.h, 6);
        pathWinding(CW);
        fillPaint(boxGradient(r.x, r.y + 3, r.w, r.h, 8, 12, fT->panelShadow, transparent(fT->panelShadow)));
        fill();
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 6);
        fillColor(fT->tipBg);
        fill();
        strokeWidth(1.0f);
        strokeColor(fT->tipEdge);
        stroke();
        const int current = listValue(l);
        for (int i = 0; i < listCount(l); ++i)
        {
            const Rect row = { r.x, r.y + 3.0f + i * kListRowH, r.w, kListRowH };
            const float cy = row.y + row.h * 0.5f;
            const bool hover = i == fListHover;
            if (hover)
                menuHighlight(row);
            const Color c = hover ? fT->onAccent : i == current ? fT->gold : fT->text;
            if (i == current)
            {
                beginPath();
                circle(row.x + 10, cy, 2.5f);
                fillColor(c);
                fill();
            }
            label(row.x + 20, cy, 11.5f, c, ALIGN_LEFT | ALIGN_MIDDLE, listName(l, i), 0.3f);
        }
    }

    // faint while it has no effect with the other settings
    void drawKnob(const int i)
    {
        const bool active = knobActive(i);
        if (!active)
            globalAlpha(0.35f);
        drawKnobBody(i);
        if (!active)
            globalAlpha(1.0f);
    }

    void drawKnobBody(const int i)
    {
        const KnobDef& k = kKnobs[i];
        const int p = k.param;
        const bool active = i == fHover || i == fDrag || i == fEdit;
        const float n = toNorm(p, fValues[p]);
        const float a = kArcStart + n * kArcSweep;
        const bool big = k.r > 35.0f;
        const float track = k.r + (big ? 9.0f : 6.0f);
        const float tw = big ? 5.0f : 3.5f;

        beginPath();
        arc(k.cx, k.cy, track, kArcStart, kArcStart + kArcSweep, CW);
        lineCap(ROUND);
        strokeWidth(tw);
        strokeColor(fT->deepWell);
        stroke();

        if (n > 0.001f)
        {
            beginPath();
            arc(k.cx, k.cy, track, kArcStart, a, CW);
            strokeWidth(tw * 3.2f);
            strokeColor(active ? fT->arcGlowActive : fT->arcGlow);
            stroke();

            beginPath();
            arc(k.cx, k.cy, track, kArcStart, a, CW);
            strokeWidth(tw);
            strokePaint(linearGradient(k.cx - track, k.cy + track, k.cx + track, k.cy - track, fT->deep,
                                       active ? fT->gold : fT->hot));
            stroke();
        }

        beginPath();
        circle(k.cx, k.cy + 4, k.r + 2);
        fillPaint(radialGradient(k.cx, k.cy + 4, k.r - 4, k.r + 8, fT->knobShadow, transparent(fT->knobShadow)));
        fill();

        beginPath();
        circle(k.cx, k.cy, k.r);
        fillPaint(linearGradient(k.cx, k.cy - k.r, k.cx, k.cy + k.r, fT->knob0, fT->knob1));
        fill();
        beginPath();
        circle(k.cx, k.cy, k.r * 0.84f);
        fillPaint(radialGradient(k.cx - k.r * 0.3f, k.cy - k.r * 0.4f, 2, k.r * 1.1f,
                                 fT->knobIn0, fT->knobIn1));
        fill();
        strokeWidth(1.0f);
        strokeColor(fT->knobEdge);
        stroke();

        const float ca = std::cos(a), sa = std::sin(a);
        beginPath();
        moveTo(k.cx + ca * k.r * 0.30f, k.cy + sa * k.r * 0.30f);
        lineTo(k.cx + ca * k.r * 0.78f, k.cy + sa * k.r * 0.78f);
        lineCap(ROUND);
        strokeWidth(big ? 4.0f : 3.0f);
        strokeColor(fT->pointerGlow);
        stroke();
        strokeWidth(big ? 2.2f : 1.8f);
        strokeColor(active ? fT->gold : fT->pointer);
        stroke();

        char buf[48];
        valueText(p, buf, sizeof(buf), false);
        const float gap = big ? 26.0f : 17.0f;
        label(k.cx, k.cy + k.r + gap, big ? 13 : 11, fT->text, ALIGN_CENTER | ALIGN_MIDDLE, k.label, big ? 3 : 1.5f);
        const float vx = k.cx, vy = k.cy + k.r + gap + (big ? 17.0f : 15.0f);

        if (i == fEdit)
        {
            const float bw = 76.0f, bx = vx - bw * 0.5f;
            beginPath();
            roundedRect(bx, vy - 10, bw, 20, 4);
            fillColor(fT->deepWell);
            fill();
            strokeWidth(1.0f);
            strokeColor(fT->gold);
            stroke();
            char e[40];
            std::snprintf(e, sizeof(e), "%s_", fEditText);
            label(bx + bw * 0.5f, vy, 12, fT->gold, ALIGN_CENTER | ALIGN_MIDDLE, e);
        }
        else
        {
            label(vx, vy, big ? 12 : 11, active ? fT->gold : fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, buf);
        }
    }

    void meterBar(const int row, const char* name, const float db)
    {
        const float y = kMeterY[row] - kMeterH * 0.5f;
        label(kMeterX - 8, kMeterY[row], 9.5f, fT->text, ALIGN_RIGHT | ALIGN_MIDDLE, name, 1);
        beginPath();
        roundedRect(kMeterX, y, kMeterW, kMeterH, 2);
        fillColor(fT->deepWell);
        fill();
        strokeWidth(1.0f);
        strokeColor(fT->meterEdge);
        stroke();

        // -60 .. +6 dBFS
        const float f = std::fmin(std::fmax((db + 60.0f) / 66.0f, 0.0f), 1.0f);
        if (f > 0.002f)
        {
            beginPath();
            roundedRect(kMeterX + 1.5f, y + 1.5f, (kMeterW - 3.0f) * f, kMeterH - 3.0f, 1.5f);
            fillPaint(linearGradient(kMeterX, 0, kMeterX + kMeterW, 0, fT->map1, row == 1 ? fT->map2 : fT->map3));
            fill();
        }
        // 0 dBFS tick
        const float x0 = kMeterX + 1.5f + (kMeterW - 3.0f) * (60.0f / 66.0f);
        beginPath();
        moveTo(x0 + 0.5f, y - 2);
        lineTo(x0 + 0.5f, y + kMeterH + 2);
        strokeColor(fT->gridZero);
        stroke();

        char b[16];
        std::snprintf(b, sizeof(b), db <= -59.9f ? "-inf" : "%.1f", db);
        label(kMeterX + kMeterW + 8, kMeterY[row], 9.5f, fT->dim, ALIGN_LEFT | ALIGN_MIDDLE, b);
    }

    void drawMeters()
    {
        meterBar(0, "IN", fValues[kParamMeterIn]);
        meterBar(1, "PAD", fValues[kParamMeterWet]);
        meterBar(2, "OUT", fValues[kParamMeterOut]);
    }

    // spectrum of the pad, dB (not normalised): colour of the noise, low cut, high cut
    double toneDb(const double hz) const
    {
        double db = fValues[kParamColour] * std::log2(std::fmax(hz, 20.0) / 1000.0);
        const float lc = fValues[kParamLowCut], hc = fValues[kParamHighCut];
        if (lc > kLowCutOff)
        {
            const double r4 = std::pow(hz / lc, 4.0);
            db += 10.0 * std::log10(r4 / (1.0 + r4));
        }
        if (hc < kHighCutOff)
        {
            const double r4 = std::pow(hz / std::fmin(hc, 0.45 * sampleRate()), 4.0);
            db += 10.0 * std::log10(1.0 / (1.0 + r4));
        }
        return db;
    }

    void legendLine(const float y, const Color& c, const bool dashed, const char* text)
    {
        beginPath();
        if (dashed)
            for (float xx = kBottom.x + 12; xx < kBottom.x + 26; xx += 5.0f)
            {
                moveTo(xx, y + 0.5f);
                lineTo(xx + 2.5f, y + 0.5f);
            }
        else
        {
            moveTo(kBottom.x + 12, y + 0.5f);
            lineTo(kBottom.x + 26, y + 0.5f);
        }
        lineCap(ROUND);
        strokeWidth(dashed ? 1.6f : 2.0f);
        strokeColor(c);
        stroke();
        label(kBottom.x + 32, y, 9.5f, fT->dim, ALIGN_LEFT | ALIGN_MIDDLE, text, 0.3f);
    }

    void plotFrame(const Rect& r)
    {
        beginPath();
        rect(r.x, r.y, r.w, r.h);
        fillColor(fT->plot);
        fill();
    }

    // colour of a level of the map, 0 (-48 dB or less) .. 1 (0 dB)
    Color mapColour(const float u) const
    {
        const Color* const c[4] = { &fT->plot, &fT->map1, &fT->map2, &fT->map3 };
        const float x = std::fmin(std::fmax(u, 0.0f), 1.0f) * 3.0f;
        const int i = std::min(2, static_cast<int>(x));
        return Color(*c[i], *c[i + 1], x - i);
    }

    static float mapFrequency(const float yFraction)   // 0 top .. 1 bottom
    {
        return 20.0f * std::pow(1000.0f, 1.0f - yFraction);
    }

    // the image of the map, made again when the response or the theme changes
    void updateMap()
    {
        const PadifierIR::Settings st = irSettings();
        if (fMap.isValid() && fMapTheme == fTheme && st == fMapSettings)
            return;
        fMapSettings = st;
        fMapTheme = fTheme;
        const bool spectral = PadifierIR::spectralEnvelope(st);
        std::vector<unsigned char> rgba(static_cast<size_t>(kMapW) * kMapH * 4);
        for (int x = 0; x < kMapW; ++x)
        {
            const double t = (x + 0.5) / kMapW * st.length;
            const double common = spectral ? PadifierIR::endFade(st, t) : PadifierIR::envelope(st, t);
            for (int y = 0; y < kMapH; ++y)
            {
                double g = common;
                if (spectral && g > 0.0)
                    g *= PadifierIR::spectralGain(st, mapFrequency((y + 0.5f) / kMapH), t);
                const float u = g > 1e-6 ? static_cast<float>((20.0 * std::log10(g) + 48.0) / 48.0) : 0.0f;
                const Color c = mapColour(u);
                unsigned char* const px = &rgba[(static_cast<size_t>(y) * kMapW + x) * 4];
                px[0] = static_cast<unsigned char>(std::lround(c.red * 255.0f));
                px[1] = static_cast<unsigned char>(std::lround(c.green * 255.0f));
                px[2] = static_cast<unsigned char>(std::lround(c.blue * 255.0f));
                px[3] = 255;
            }
        }
        if (fMap.isValid())
            fMap.update(rgba.data());
        else
            fMap = createImageFromRGBA(kMapW, kMapH, rgba.data(), 0);
    }

    void drawPlots()
    {
        label(kBottom.x + 12, kBottom.y + 18, 10, fT->text, ALIGN_LEFT | ALIGN_MIDDLE, "IMPULSE", 1);
        label(kBottom.x + 12, kBottom.y + 32, 10, fT->text, ALIGN_LEFT | ALIGN_MIDDLE, "RESPONSE", 1);
        // scale of the map
        const float bx = kBottom.x + 14, by = kBottom.y + 56, bh = 90;
        beginPath();
        rect(bx, by, 10, bh);
        fillPaint(linearGradient(0, by, 0, by + bh * 0.34f, mapColour(1.0f), mapColour(0.66f)));
        fill();
        beginPath();
        rect(bx, by + bh * 0.33f, 10, bh * 0.67f);
        fillPaint(linearGradient(0, by + bh * 0.33f, 0, by + bh, mapColour(0.66f), mapColour(0.0f)));
        fill();
        for (int d = 0; d <= 48; d += 16)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "%d", -d);
            label(bx + 16, by + bh * d / 48.0f, 8, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE, b);
        }
        label(bx + 44, by + bh * 0.5f, 8, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE, "dB");
        legendLine(kBottom.y + 172, fT->text, false, "notes (combs)");
        legendLine(kBottom.y + 190, fT->tone, false, "spectrum (dB)");
        drawMap();
        drawTonePlot();
        drawMeters();
    }

    void drawMap()
    {
        const Rect& r = kMap;
        const PadifierIR::Settings st = irSettings();
        updateMap();
        beginPath();
        rect(r.x, r.y, r.w, r.h);
        fillPaint(imagePattern(r.x, r.y, r.w, r.h, 0.0f, fMap, 1.0f));
        fill();

        // axes: frequency (left), time (below)
        const auto fy = [&](const double hz) { return r.y + r.h * (1.0f - static_cast<float>(std::log(hz / 20.0) / std::log(1000.0))); };
        strokeWidth(1.0f);
        beginPath();
        for (const double hz : { 100.0, 1000.0, 10000.0 })
        {
            moveTo(r.x, fy(hz) + 0.5f);
            lineTo(r.x + r.w, fy(hz) + 0.5f);
        }
        strokeColor(fT->grid);
        stroke();
        const char* const fnames[3] = { "100", "1k", "10k" };
        const double fvals[3] = { 100.0, 1000.0, 10000.0 };
        for (int j = 0; j < 3; ++j)
            label(r.x - 5, fy(fvals[j]), 8, fT->faint, ALIGN_RIGHT | ALIGN_MIDDLE, fnames[j]);
        label(r.x - 5, r.y + 4, 8, fT->faint, ALIGN_RIGHT | ALIGN_MIDDLE, "Hz");
        const double L = st.length;
        double step = 0.05;
        for (const double c : { 0.05, 0.1, 0.25, 0.5, 1.0, 2.0 })
            if (L / c <= 8.0)
            {
                step = c;
                break;
            }
        for (double t = 0.0; t <= L + 1e-9; t += step)
        {
            char b[16];
            std::snprintf(b, sizeof(b), "%g", std::round(t * 100.0) / 100.0);
            label(r.x + r.w * static_cast<float>(t / L), r.y + r.h + 8, 8, fT->faint,
                  t == 0.0 ? ALIGN_LEFT | ALIGN_MIDDLE : ALIGN_CENTER | ALIGN_MIDDLE, b);
        }
        label(r.x + r.w, r.y + r.h + 18, 8, fT->faint, ALIGN_RIGHT | ALIGN_MIDDLE, "s");

        // notes of the combs (comb: harmonics; chord: the notes and two harmonics)
        const std::vector<double> f0 = PadifierIR::notes(st);
        if (!f0.empty())
        {
            save();
            scissor(r.x, r.y, r.w, r.h);
            const int harmonics = st.tune == 1 ? 24 : 3;
            for (const double f : f0)
                for (int k = 1; k <= harmonics && k * f < 20000.0; ++k)
                {
                    beginPath();
                    for (int j = 0; j <= 80; ++j)
                    {
                        const double t = L * j / 80.0;
                        const double hz = k * f * std::exp2(st.lfoPitch * PadifierIR::lfo(st, t, PadifierIR::kLfoPitch) / 12.0);
                        if (j == 0)
                            moveTo(r.x, fy(hz));
                        else
                            lineTo(r.x + r.w * j / 80.0f, fy(hz));
                    }
                    strokeWidth(k == 1 ? 1.4f : 1.0f);
                    strokeColor(Color(fT->text, transparent(fT->text), k == 1 ? 0.25f : 0.65f));
                    stroke();
                }
            restore();
        }
        // peak of the envelope (dashed), without bloom
        if (st.bloom == 0.0)
        {
            const float xp = r.x + r.w * static_cast<float>(st.swell);
            beginPath();
            for (float yy = r.y; yy < r.y + r.h; yy += 6.0f)
            {
                moveTo(xp, yy);
                lineTo(xp, yy + 3.0f);
            }
            strokeWidth(1.0f);
            strokeColor(Color(fT->text, transparent(fT->text), 0.5f));
            stroke();
        }
    }

    void drawTonePlot()
    {
        const Rect& r = kTonePlot;
        const double fmax = std::fmin(20000.0, 0.5 * sampleRate());
        const auto fx = [&](const double hz) {
            return r.x + r.w * static_cast<float>(std::log(hz / 20.0) / std::log(1000.0));
        };
        double top = -1e9;
        const int n = 120;
        for (int j = 0; j <= n; ++j)
            top = std::fmax(top, toneDb(20.0 * std::pow(fmax / 20.0, static_cast<double>(j) / n)));
        const auto dy = [&](const double db) {
            return r.y + r.h * static_cast<float>(std::fmin(std::fmax((top - db) / 36.0, 0.0), 1.0));
        };
        plotFrame(r);

        strokeWidth(1.0f);
        beginPath();
        const double grid[] = { 100, 1000, 10000 };
        for (const double hz : grid)
        {
            moveTo(fx(hz) + 0.5f, r.y);
            lineTo(fx(hz) + 0.5f, r.y + r.h);
        }
        for (int d = 12; d < 36; d += 12)
        {
            moveTo(r.x, r.y + r.h * d / 36.0f + 0.5f);
            lineTo(r.x + r.w, r.y + r.h * d / 36.0f + 0.5f);
        }
        strokeColor(fT->grid);
        stroke();
        const char* const gridNames[] = { "100", "1k", "10k" };
        for (int j = 0; j < 3; ++j)
            label(fx(grid[j]), r.y + r.h + 8, 8, fT->faint, ALIGN_CENTER | ALIGN_MIDDLE, gridNames[j]);
        label(r.x + r.w, r.y + r.h + 8, 8, fT->faint, ALIGN_RIGHT | ALIGN_MIDDLE, "Hz");
        for (int d = 0; d <= 36; d += 12)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "%d", -d);
            label(r.x + r.w + 5, r.y + r.h * d / 36.0f, 8, Color(fT->tone, fT->faint, 0.4f), ALIGN_LEFT | ALIGN_MIDDLE, b);
        }

        save();
        scissor(r.x, r.y - 2, r.w, r.h + 4);
        beginPath();
        for (int j = 0; j <= n; ++j)
        {
            const double hz = 20.0 * std::pow(fmax / 20.0, static_cast<double>(j) / n);
            if (j == 0)
                moveTo(fx(hz), dy(toneDb(hz)));
            else
                lineTo(fx(hz), dy(toneDb(hz)));
        }
        lineCap(ROUND);
        lineJoin(ROUND);
        strokeWidth(2.0f);
        strokeColor(fT->tone);
        stroke();
        restore();
    }

    // open preset list, drawn over everything: factory presets, a line, saved presets, a line,
    // "Save current settings..."; the current preset is marked with a dot
    void drawPresetMenu()
    {
        const float x = kPreset.x, w = kPreset.w, top = menuTop(), h = menuVisibleH();
        beginPath();
        rect(x - 10, top - 6, w + 20, h + 22);
        roundedRect(x, top, w, h, 6);
        pathWinding(CW);
        fillPaint(boxGradient(x, top + 3, w, h, 8, 12, fT->panelShadow, transparent(fT->panelShadow)));
        fill();
        beginPath();
        roundedRect(x, top, w, h, 6);
        fillColor(fT->tipBg);
        fill();
        strokeWidth(1.0f);
        strokeColor(fT->tipEdge);
        stroke();

        save();
        scissor(x, top + 1, w, h - 2);
        for (int i = 0; i < static_cast<int>(fRows.size()); ++i)
        {
            const MenuRow& m = fRows[i];
            const Rect r = rowRect(i);
            if (r.y + r.h < top || r.y > top + h)
                continue;
            const float cy = r.y + r.h * 0.5f;
            const bool hover = i == fMenuHover;
            switch (m.kind)
            {
            case kRowHeader:
                if (m.index == 1)
                    menuLine(x, w, r.y + 5.0f);
                label(x + 12, r.y + r.h - 9.0f, 8.5f, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE,
                      m.index == 0 ? "FACTORY" : "USER", 1.5f);
                break;
            case kRowDivider:
                menuLine(x, w, cy);
                break;
            case kRowNone:
                label(x + 22, cy, 11, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE, "none saved yet", 0.3f);
                break;
            case kRowSave:
                if (hover)
                    menuHighlight(r);
                label(x + 12, cy, 12, hover ? fT->onAccent : fT->dim, ALIGN_LEFT | ALIGN_MIDDLE, "+", 0.0f);
                label(x + 22, cy, 12, hover ? fT->onAccent : fT->dim, ALIGN_LEFT | ALIGN_MIDDLE,
                      "Save current settings...", 0.3f);
                break;
            default:   // factory or saved preset
            {
                const bool user = m.kind == kRowUser;
                const bool armed = user && i == fDeleteArmed;
                if (hover)
                    menuHighlight(r);
                const Color c = hover ? fT->onAccent : isCurrentRow(m) ? fT->gold : fT->text;
                if (isCurrentRow(m))
                {
                    beginPath();
                    circle(x + 12, cy, 2.5f);
                    fillColor(c);
                    fill();
                }
                save();
                intersectScissor(x, r.y, w - (armed ? 70.0f : user && hover ? 28.0f : 6.0f), r.h);
                label(x + 22, cy, 12, c, ALIGN_LEFT | ALIGN_MIDDLE,
                      user ? fUserNames[m.index].c_str() : kPadifierPresets[m.index].name, 0.5f);
                restore();
                if (user && (hover || armed))
                {
                    // cross: delete (first click arms, the second deletes)
                    const Rect cr = crossRect(r);
                    const float ccx = cr.x + cr.w * 0.5f;
                    const Color xc = armed ? fT->ledOn : fMenuOnCross ? fT->onAccent : Color(fT->onAccent, fT->sel0, 0.45f);
                    if (armed)
                    {
                        beginPath();
                        roundedRect(cr.x + 3, r.y + 3, cr.w - 6, r.h - 6, 3);
                        fillColor(fT->deep);
                        fill();
                        label(cr.x - 4, cy, 10, hover ? fT->onAccent : fT->hot, ALIGN_RIGHT | ALIGN_MIDDLE, "delete?", 0.5f);
                    }
                    beginPath();
                    moveTo(ccx - 3.5f, cy - 3.5f);
                    lineTo(ccx + 3.5f, cy + 3.5f);
                    moveTo(ccx + 3.5f, cy - 3.5f);
                    lineTo(ccx - 3.5f, cy + 3.5f);
                    lineCap(ROUND);
                    strokeWidth(1.6f);
                    strokeColor(xc);
                    stroke();
                }
                break;
            }
            }
        }
        restore();

        // scroll bar when the list is longer than the window
        if (fMenuContentH > h)
        {
            const float most = fMenuContentH - h;
            const float bh = h * h / fMenuContentH;
            beginPath();
            roundedRect(x + w - 4, top + 2 + (h - 4 - bh) * (fMenuScroll / most), 2.5f, bh, 1.2f);
            fillColor(fT->dim);
            fill();
        }
    }

    void menuLine(const float x, const float w, const float y)
    {
        beginPath();
        moveTo(x + 8, y + 0.5f);
        lineTo(x + w - 8, y + 0.5f);
        strokeWidth(1.0f);
        strokeColor(fT->tipEdge);
        stroke();
    }

    void menuHighlight(const Rect& r)
    {
        beginPath();
        roundedRect(r.x + 2, r.y + 1, r.w - 4, r.h - 2, 4);
        fillPaint(linearGradient(0, r.y, 0, r.y + r.h, fT->sel0, fT->sel1));
        fill();
    }

    // under the box while a name is typed: what Enter will do
    void drawNamingHint()
    {
        bool error = false;
        const char* const hint = namingHint(error);
        const float w = 260.0f, pad = 10.0f, x = kPreset.x, y = kPreset.y + kPreset.h + 6.0f;
        fontFaceId(fFont);
        fontSize(10);
        textLetterSpacing(0);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        float bounds[4];
        textBoxBounds(0, 0, w - 2 * pad, hint, nullptr, bounds);
        const float h = 26.0f + (bounds[3] - bounds[1]) + 6.0f;
        beginPath();
        roundedRect(x, y, w, h, 6);
        fillColor(fT->tipBg);
        fill();
        strokeWidth(1.0f);
        strokeColor(error ? fT->hot : fT->tipEdge);
        stroke();
        label(x + pad, y + 13, 11, fT->gold, ALIGN_LEFT | ALIGN_MIDDLE, "SAVE PRESET", 0.5f);
        fontSize(10);
        textLetterSpacing(0);
        fillColor(error ? fT->hot : fT->text);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        textBox(x + pad, y + 24, w - 2 * pad, hint, nullptr);
    }

    void drawTooltip(const int hover)
    {
        char title[128];
        const char* help;
        Rect anchor;
        if (hover >= kHoverPhaseBase)
        {
            const int i = hover - kHoverPhaseBase;
            char v[24];
            valueText(kPhases[i].param, v, sizeof(v), true);
            std::snprintf(title, sizeof(title), "PHASE  %s  %s", kKnobs[kPhases[i].knob].label, v);
            help = "Phase of the LFO on this target: 0, 90, 180 or 270 degrees. Targets at the same phase "
                   "move together; at 180 degrees apart, one rises while the other falls; at 90, one "
                   "follows the other by a quarter of the period. Click: next; right-click: previous; wheel.";
            anchor = phaseRect(i);
        }
        else if (hover >= kHoverListBase)
        {
            const ListDef& d = kLists[hover - kHoverListBase];
            char v[64];
            valueText(d.param, v, sizeof(v), true);
            std::snprintf(title, sizeof(title), "%s  %s", d.label, v);
            help = d.help;
            anchor = d.box;
        }
        else if (hover < kHoverZoneBase)
        {
            const KnobDef& d = kKnobs[hover];
            char v[80];
            valueText(d.param, v, sizeof(v), true);
            std::snprintf(title, sizeof(title), "%s  %s", d.label, v);
            help = d.help;
            const float r = d.r + 10;
            anchor = { d.cx - r, d.cy - r, 2 * r, 2 * r };
        }
        else
        {
            const Zone& z = kZones[hover - kHoverZoneBase];
            std::snprintf(title, sizeof(title), "%s", z.title);
            help = z.help;
            anchor = z.r;
        }

        const float w = 290.0f, pad = 10.0f;
        fontFaceId(fFont);
        fontSize(10);
        textLetterSpacing(0);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        float bounds[4];
        textBoxBounds(0, 0, w - 2 * pad, help, nullptr, bounds);
        const float h = 26.0f + (bounds[3] - bounds[1]) + 6.0f;

        float x = anchor.x + anchor.w * 0.5f - w * 0.5f;
        x = std::fmin(std::fmax(x, 6.0f), kW - w - 6.0f);
        float y = anchor.y + anchor.h + 6.0f;
        if (y + h > kH - 4.0f)
            y = anchor.y - h - 6.0f;

        beginPath();
        roundedRect(x, y, w, h, 6);
        fillColor(fT->tipBg);
        fill();
        strokeWidth(1.0f);
        strokeColor(fT->tipEdge);
        stroke();
        label(x + pad, y + 13, 11, fT->gold, ALIGN_LEFT | ALIGN_MIDDLE, title, 0.5f);
        fontSize(10);
        textLetterSpacing(0);
        fillColor(fT->text);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        textBox(x + pad, y + 24, w - 2 * pad, help, nullptr);
    }

    // ------------------------------------------------------------------ state

    float fValues[kParamCount];
    // time-frequency map of the response: image, and what it was made for
    static const int kMapW = 200, kMapH = 72;
    NanoImage fMap;
    PadifierIR::Settings fMapSettings;
    int fMapTheme = -1;
    int fTheme = PadifierPrefs::kThemeDusk;
    const Theme* fT = &kThemes[PadifierPrefs::kThemeDusk];
    FontId fFont = 0;
    int fDrag = -1, fHover = -1, fEdit = -1;
    int fList = -1, fListHover = -1;   // open list (kLists) and the row under the mouse
    int fLastClickTarget = -1;
    uint fLastClickTime = 0;
    float fLastY = 0.0f;
    // current preset: factory (fPreset) or saved (fPresetUser), its values when loaded (for the "*")
    int fPreset = 0;
    bool fPresetUser = false;
    std::string fPresetName = kPadifierPresets[0].name;
    float fPresetValues[kPresetParamCount];
    // preset list
    bool fMenuOpen = false;
    std::vector<MenuRow> fRows;
    std::vector<std::string> fUserNames;   // saved presets, read when the list opens
    int fMenuHover = -1;                   // selectable row under the mouse (-1: none)
    bool fMenuOnCross = false;             // the mouse is on the cross of that row
    int fDeleteArmed = -1;                 // row whose cross was clicked once
    float fMenuScroll = 0.0f, fMenuContentH = 0.0f;
    // A / B sets: fSlot is live, the other is stored
    struct Slot {
        float values[kPresetParamCount];
        float presetValues[kPresetParamCount];
        int preset = 0;
        bool user = false;
        std::string name;
        bool set = false;
    };
    Slot fSlots[2];
    int fSlot = 0;
    // name of a preset being saved
    bool fNaming = false, fNameError = false;
    std::string fNameText;
    char fEditText[16] = { 0 };
    bool fEditFresh = false;
    // meters: repainted at most 30 times per second
    bool fMetersChanged = false;
    std::chrono::steady_clock::time_point fLastPaint;
#ifdef PADIFIER_UI_DEMO_PRESET
    bool fDemoPresetDone = false;
#endif
#ifdef PADIFIER_UI_DEMO_LIST
    bool fDemoListDone = false;
#endif
#ifdef PADIFIER_UI_DEMO_MENU
    bool fDemoMenuDone = false;
#endif
#ifdef PADIFIER_UI_DEMO_NAME
    bool fDemoNameDone = false;
#endif

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadifierUI)
};

UI* createUI()
{
    return new PadifierUI();
}

END_NAMESPACE_DISTRHO
