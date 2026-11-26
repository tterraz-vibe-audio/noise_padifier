/*
 * Noise Padifier - user interface (DPF / NanoVG).
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Everything is drawn in a logical 900 x 600 space, scaled to the window size (resizable,
 * aspect ratio kept).  Three sections, left to right in the order of the signal:
 *   1 NOISE (white or pink, seed) -> 2 ENVELOPE (length, swell, decay)
 *   -> 3 OUTPUT (mix, width, output, low cut, high cut, meters),
 * and below, the envelope of the impulse response against time and the spectrum of the pad
 * (colour of the noise and the two filters).
 * Knobs: drag vertically (Shift = fine), mouse wheel, double-click or Ctrl+click = default,
 * right-click = type a value (Enter / Escape).  Every control has a tooltip.
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

static const float kW = 900.0f;
static const float kH = 600.0f;
static const float kPi = 3.14159265358979f;
static const float kArcStart = 0.75f * kPi;
static const float kArcSweep = 1.5f * kPi;

// ------------------------------------------------------------------------------------------------
// Themes: every colour of the interface.  Dusk (dark: lavender and pale amber on dark violet
// grey) and Pearl (light: the same accents on pale pearl).  The choice is a preference of the
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
    },
    {   // Pearl
        Color(222, 218, 226), Color(236, 233, 240), Color(98, 70, 170, 0.07f), Color(246, 244, 249),
        Color(232, 228, 237), Color(98, 70, 170, 0.55f),
        Color(249, 250, 251), Color(240, 243, 245), Color(200, 194, 210), Color(50, 40, 70, 0.16f),
        Color(200, 194, 210, 0.8f),
        Color(38, 32, 48), Color(105, 98, 120), Color(150, 144, 162), Color(250, 253, 255),
        Color(98, 70, 170), Color(150, 105, 25), Color(60, 40, 115), Color(98, 70, 170, 0.75f),
        Color(98, 70, 170, 0.22f),
        Color(118, 92, 190), Color(78, 54, 145), Color(250, 251, 252), Color(236, 233, 240),
        Color(98, 70, 170, 0.9f),
        Color(140, 110, 220, 0.55f), Color(140, 110, 220, 0.0f), Color(120, 90, 210), Color(190, 198, 205),
        Color(232, 236, 239), Color(250, 251, 252), Color(220, 215, 226), Color(98, 70, 170, 0.7f),
        Color(118, 92, 190), Color(78, 54, 145),
        Color(50, 40, 70, 0.30f), Color(253, 254, 255), Color(220, 215, 226), Color(248, 250, 251),
        Color(228, 224, 233),
        Color(100, 92, 120, 0.45f), Color(98, 70, 170, 0.12f), Color(98, 70, 170, 0.22f), Color(98, 70, 170, 0.18f),
        Color(78, 54, 145),
        Color(150, 144, 168, 0.35f), Color(120, 112, 138, 0.7f), Color(150, 105, 25, 0.15f), Color(98, 70, 170),
        Color(196, 190, 206),
        Color(253, 254, 255, 0.97f), Color(98, 70, 170, 0.6f), Color(255, 236, 200),
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
static const Rect kSecIn    = { 20, 70, 220, 380 };    // 1 NOISE
static const Rect kSecEnv   = { 250, 70, 370, 380 };   // 2 ENVELOPE
static const Rect kSecOut   = { 630, 70, 250, 380 };   // 3 OUTPUT
static const Rect kBottom   = { 20, 460, 860, 128 };
static const Rect kEnvPlot  = { 150, 472, 380, 96 };   // envelope against time
static const Rect kTonePlot = { 586, 472, 264, 96 };   // spectrum of the pad

enum { kKnobSeed = 0, kKnobLength, kKnobSwell, kKnobDecay, kKnobMix, kKnobWidth, kKnobOutput, kKnobLowCut,
       kKnobHighCut, kNumKnobs };

struct KnobDef {
    int param;
    float cx, cy, r;
    const char* label;
    const char* help;
};

static const KnobDef kKnobs[kNumKnobs] = {
    { kParamSeed,     130.0f, 300.0f, 44.0f, "SEED",
      "Draw of the noise: each value gives another noise of the same colour, another grain of the "
      "pad. The same seed always gives the same response." },
    { kParamLength,   320.0f, 165.0f, 40.0f, "LENGTH",
      "Length of the impulse response: how long every sound is smeared. Long: a slow pad; short: "
      "a smear that thickens the sound. A change rebuilds the response, crossfaded in." },
    { kParamSwell,    435.0f, 165.0f, 40.0f, "SWELL",
      "Rise of the envelope, in % of the length: the pad fades in and peaks at that time. 0: at "
      "once; 100 %: rises over the whole length (reverse swell)." },
    { kParamDecay,    550.0f, 165.0f, 40.0f, "DECAY",
      "Level of the envelope at the end, after the peak (a straight line in dB). 0 dB: flat, a "
      "held pad; -60 dB: dies away like a reverb." },
    { kParamMix,      670.0f, 150.0f, 22.0f, "MIX",
      "Dry / pad, equal power (pad sin, dry cos of 90 degrees x mix). At 100 %: pad only (for a "
      "send)." },
    { kParamWidth,    755.0f, 150.0f, 22.0f, "WIDTH",
      "Stereo width of the pad: 0 % mono, 100 % as made (independent noises left and right), "
      "200 % side doubled." },
    { kParamOutput,   840.0f, 150.0f, 22.0f, "OUTPUT",
      "Gain of the whole output, dry and pad." },
    { kParamLowCut,   712.0f, 268.0f, 22.0f, "LOW CUT",
      "High-pass on the pad only (12 dB / octave): pink noise carries a lot of low end. At the "
      "minimum: off." },
    { kParamHighCut,  798.0f, 268.0f, 22.0f, "HIGH CUT",
      "Low-pass on the pad only (12 dB / octave): a darker, softer pad. At the maximum: off." },
};

static const Rect kNoise   = { 45, 124, 170, 32 };     // WHITE | PINK
static const Rect kInfo    = { 270, 312, 330, 128 };   // readout of the envelope
static const Rect kPreset  = { 384, 16, 196, 26 };     // drop-down box; the list opens below it
static const Rect kAB      = { 588, 17, 50, 24 };      // A | B
static const Rect kCopy    = { 642, 17, 34, 24 };      // copy the current set to the other
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
static const Rect kTheme   = { 684, 17, 76, 24 };
static const Rect kBypass  = { 768, 14, 108, 30 };

// meters: three horizontal bars in the output section
static const float kMeterX = 690.0f, kMeterW = 132.0f, kMeterH = 9.0f;
static const float kMeterY[3] = { 384.0f, 404.0f, 424.0f };   // IN, PAD, OUT

// hover zones with a tooltip (knobs are handled separately)
enum {
    kZoneNoise = 0, kZoneInfo, kZoneEnvPlot, kZoneTonePlot, kZonePreset, kZoneBypass, kZoneTheme, kZoneAB,
    kZoneCopy, kZoneIn, kZoneWet, kZoneOut, kNumZones
};

struct Zone {
    Rect r;
    const char* title;
    const char* help;
};

static const Zone kZones[kNumZones] = {
    { kNoise, "NOISE",
      "Colour of the noise convolved with the input. WHITE: flat spectrum, bright and airy. PINK: "
      "-3 dB per octave from 20 Hz, warm and full (the usual choice for pads)." },
    { kInfo, "IMPULSE RESPONSE",
      "The response is noise under this envelope, a different noise left and right, scaled to "
      "unit energy: a broadband input keeps its level in the pad. Latency 256 samples, reported to "
      "the host." },
    { kEnvPlot, "ENVELOPE",
      "Level of the impulse response against time (dB): rise (Swell), then a straight line in dB "
      "down to the Decay level at the end of Length. Dashed: the peak." },
    { kTonePlot, "SPECTRUM OF THE PAD",
      "Colour of the noise and the Low cut and High cut filters (dB, highest point at 0 dB)." },
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
        drawInputSection();
        drawEnvelopeSection();
        drawOutputSection();
        drawPlots();
        if (fMenuOpen)
            drawPresetMenu();
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
        if (kNoise.contains(x, y)) { setOnce(kParamNoise, segmentAt(kNoise, x, 2)); return true; }

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
        const int k = knobAt(x, y);
        if (k < 0)
            return false;
        const int p = kKnobs[k].param;
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

    static bool logarithmic(const int p) { return p == kParamLength || p == kParamLowCut || p == kParamHighCut; }

    // knob position: length and filters logarithmic, the others linear
    static float toNorm(const int p, const float v)
    {
        const PadifierRange& r = kPadifierRanges[p];
        if (logarithmic(p))
            return std::log(std::fmax(v, r.min) / r.min) / std::log(r.max / r.min);
        return (v - r.min) / (r.max - r.min);
    }

    // seed and noise: whole numbers
    static float fromNorm(const int p, float n)
    {
        const PadifierRange& r = kPadifierRanges[p];
        n = std::fmin(std::fmax(n, 0.0f), 1.0f);
        if (logarithmic(p))
            return r.min * std::pow(r.max / r.min, n);
        const float v = r.min + n * (r.max - r.min);
        return p == kParamSeed || p == kParamNoise ? std::round(v) : v;
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
            if (std::fabs(fValues[i] - fPresetValues[i]) > 1e-4f)
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
                setOnce(p, p == kParamSeed ? std::round(clampValue(p, v)) : clampValue(p, v));
            }
        }
        fEdit = -1;
        repaint();
    }

    int knobAt(const float x, const float y) const
    {
        for (int i = 0; i < kNumKnobs; ++i)
        {
            const float dx = x - kKnobs[i].cx, dy = y - kKnobs[i].cy, r = kKnobs[i].r + 8.0f;
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
        if (p == kParamLength)
            fmt = "%.2f";
        else if (p == kParamDecay || p == kParamOutput)
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

    // short text under a knob; detail: the longer form of the tooltip title
    void valueText(const int p, char* buf, const size_t size, const bool detail) const
    {
        const float v = fValues[p];
        char f[24];
        switch (p)
        {
        case kParamLength:
            secondsText(v, f, sizeof(f));
            if (detail)
                std::snprintf(buf, size, "%s (%d samples per channel)", f, PadifierIR::lengthSamples(irSettings()));
            else
                std::snprintf(buf, size, "%s", f);
            break;
        case kParamSwell:
            secondsText(irSettings().swell * irSettings().length, f, sizeof(f));
            std::snprintf(buf, size, detail ? "%.0f %% (peak at %s)" : "%.0f %%", v, f);
            break;
        case kParamDecay: std::snprintf(buf, size, detail ? "%.1f dB at the end" : "%.0f dB", v); break;
        case kParamSeed:  std::snprintf(buf, size, "%d", PadifierLaw::seed(v)); break;
        case kParamLowCut:
            if (v <= kLowCutOff + 0.05f)
                std::snprintf(buf, size, "off");
            else
                std::snprintf(buf, size, "%.0f Hz", v);
            break;
        case kParamHighCut:
            if (v >= kHighCutOff - 0.5f)
                std::snprintf(buf, size, "off");
            else
                frequencyText(v, buf, size);
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
        strokeWidth(1.0f);
        strokeColor(fT->panelEdge);
        stroke();
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

        panel(kSecIn);
        panel(kSecEnv);
        panel(kSecOut);
        panel(kBottom);
        flowArrow(kSecIn.x + kSecIn.w + 5, kSecIn.y + kSecIn.h * 0.5f);
        flowArrow(kSecEnv.x + kSecEnv.w + 5, kSecEnv.y + kSecEnv.h * 0.5f);
    }

    void drawHeader()
    {
        beginPath();
        rect(0, 0, kW, 56);
        fillPaint(linearGradient(0, 0, 0, 56, fT->header0, fT->header1));
        fill();
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

    void drawInputSection()
    {
        sectionTitle(kSecIn, "1", "NOISE", "convolved with");
        const char* const names[2] = { "WHITE", "PINK" };
        const bool pink = PadifierLaw::pink(fValues[kParamNoise]);
        segmented(kNoise, names, 2, pink ? 1 : 0, 11);
        const float cx = kSecIn.x + kSecIn.w * 0.5f;
        label(cx, 176, 10, fT->dim, ALIGN_CENTER | ALIGN_MIDDLE,
              pink ? "-3 dB per octave: warm, full" : "flat spectrum: bright, airy", 0.3f);
        // a small sketch of the spectrum, 20 Hz .. 20 kHz
        const float x0 = cx - 60, x1 = cx + 60, y0 = 196, y1 = 214;
        beginPath();
        moveTo(x0, pink ? y0 : 0.5f * (y0 + y1));
        lineTo(x1, pink ? y1 : 0.5f * (y0 + y1));
        lineCap(ROUND);
        strokeWidth(2.0f);
        strokeColor(fT->hot);
        stroke();
        label(x0 - 6, y1 + 1, 8, fT->faint, ALIGN_RIGHT | ALIGN_MIDDLE, "20");
        label(x1 + 6, y1 + 1, 8, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE, "20k");
        divider(kSecIn.x + 12, kSecIn.x + kSecIn.w - 12, 230);
        drawKnob(kKnobSeed);
    }

    void drawEnvelopeSection()
    {
        sectionTitle(kSecEnv, "2", "ENVELOPE", "of the impulse response");
        drawKnob(kKnobLength);
        drawKnob(kKnobSwell);
        drawKnob(kKnobDecay);
        divider(kSecEnv.x + 12, kSecEnv.x + kSecEnv.w - 12, 300);

        const PadifierIR::Settings st = irSettings();
        const float cx = kSecEnv.x + kSecEnv.w * 0.5f;
        char a[32], b[96];
        label(cx, 330, 9.5f, fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, "PEAK OF THE ENVELOPE", 1.2f);
        secondsText(st.swell * st.length, a, sizeof(a));
        label(cx, 358, 22, fT->gold, ALIGN_CENTER | ALIGN_MIDDLE, a, 1);
        secondsText(st.length, a, sizeof(a));
        std::snprintf(b, sizeof(b), "end at %s, %.0f dB", a, st.decayDb);
        label(cx, 386, 10, fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, b, 0.5f);
        std::snprintf(b, sizeof(b), "%d samples per channel  |  latency %.1f ms", PadifierIR::lengthSamples(st),
                      1000.0 * PadifierConv::kHeadBlock / sampleRate());
        label(cx, 408, 9.5f, fT->faint, ALIGN_CENTER | ALIGN_MIDDLE, b, 0.3f);
    }

    void drawOutputSection()
    {
        sectionTitle(kSecOut, "3", "OUTPUT", "mix and tone");
        for (int k = kKnobMix; k <= kKnobHighCut; ++k)
            drawKnob(k);
        divider(kSecOut.x + 12, kSecOut.x + kSecOut.w - 12, 360);
        drawMeters();
    }

    void drawKnob(const int i)
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
            fillPaint(linearGradient(kMeterX, 0, kMeterX + kMeterW, 0, fT->deep, row == 1 ? fT->hot : fT->gold));
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
        double db = PadifierLaw::pink(fValues[kParamNoise]) ? -10.0 * std::log10(std::fmax(hz, 20.0) / 1000.0) : 0.0;
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

    void drawPlots()
    {
        label(kBottom.x + 12, kBottom.y + 16, 10, fT->text, ALIGN_LEFT | ALIGN_MIDDLE, "IMPULSE", 1);
        label(kBottom.x + 12, kBottom.y + 30, 10, fT->text, ALIGN_LEFT | ALIGN_MIDDLE, "RESPONSE", 1);
        legendLine(kBottom.y + 56, fT->gold, false, "envelope (dB)");
        legendLine(kBottom.y + 74, fT->tone, false, "spectrum (dB)");
        drawEnvelopePlot();
        drawTonePlot();
    }

    void plotFrame(const Rect& r)
    {
        beginPath();
        rect(r.x, r.y, r.w, r.h);
        fillColor(fT->plot);
        fill();
    }

    void drawEnvelopePlot()
    {
        const Rect& r = kEnvPlot;
        const PadifierIR::Settings st = irSettings();
        const double L = st.length;
        const auto px = [&](const double t) { return r.x + r.w * static_cast<float>(t / L); };
        const auto py = [&](const double db) {
            return r.y + r.h * static_cast<float>(std::fmin(std::fmax(-db / 60.0, 0.0), 1.0));
        };
        plotFrame(r);

        // grid: time, 20 dB
        double step = 0.05;
        for (const double c : { 0.05, 0.1, 0.25, 0.5, 1.0, 2.0 })
            if (L / c <= 6.0)
            {
                step = c;
                break;
            }
        strokeWidth(1.0f);
        beginPath();
        for (double t = step; t < L - 1e-9; t += step)
        {
            moveTo(px(t) + 0.5f, r.y);
            lineTo(px(t) + 0.5f, r.y + r.h);
        }
        for (int d = 20; d < 60; d += 20)
        {
            moveTo(r.x, py(-d) + 0.5f);
            lineTo(r.x + r.w, py(-d) + 0.5f);
        }
        strokeColor(fT->grid);
        stroke();
        for (double t = 0.0; t <= L + 1e-9; t += step)
        {
            char b[16];
            std::snprintf(b, sizeof(b), "%g", std::round(t * 100.0) / 100.0);
            label(px(t), r.y + r.h + 8, 8, fT->faint, t == 0.0 ? ALIGN_LEFT | ALIGN_MIDDLE : ALIGN_CENTER | ALIGN_MIDDLE, b);
        }
        label(r.x + r.w, r.y + r.h + 8, 8, fT->faint, ALIGN_RIGHT | ALIGN_MIDDLE, "s");
        for (int d = 0; d <= 60; d += 20)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "%d", -d);
            label(r.x - 5, py(-d), 8, Color(fT->gold, fT->faint, 0.4f), ALIGN_RIGHT | ALIGN_MIDDLE, b);
        }

        save();
        scissor(r.x, r.y - 2, r.w, r.h + 4);
        const int n = 240;
        const auto env = [&](const int j) {
            const double t = L * j / n;
            const double e = PadifierIR::envelope(st, std::fmin(t, L * (1.0 - 1e-9)));
            return e > 1e-6 ? 20.0 * std::log10(e) : -120.0;
        };
        // filled area, then the curve with its glow
        beginPath();
        moveTo(r.x, r.y + r.h);
        for (int j = 0; j <= n; ++j)
            lineTo(px(L * j / n), py(env(j)));
        lineTo(r.x + r.w, r.y + r.h);
        closePath();
        fillPaint(linearGradient(0, r.y, 0, r.y + r.h, fT->curveGlow, transparent(fT->curveGlow)));
        fill();
        for (int pass = 0; pass < 2; ++pass)
        {
            beginPath();
            for (int j = 0; j <= n; ++j)
            {
                if (j == 0)
                    moveTo(px(0.0), py(env(0)));
                else
                    lineTo(px(L * j / n), py(env(j)));
            }
            lineCap(ROUND);
            lineJoin(ROUND);
            strokeWidth(pass == 0 ? 7.0f : 2.2f);
            strokeColor(pass == 0 ? fT->curveGlow : fT->gold);
            stroke();
        }
        // peak (dashed)
        const float xp = px(st.swell * L);
        beginPath();
        for (float yy = r.y; yy < r.y + r.h; yy += 6.0f)
        {
            moveTo(xp, yy);
            lineTo(xp, yy + 3.0f);
        }
        strokeWidth(1.2f);
        strokeColor(fT->hot);
        stroke();
        restore();
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
        if (hover < kHoverZoneBase)
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
    int fTheme = PadifierPrefs::kThemeDusk;
    const Theme* fT = &kThemes[PadifierPrefs::kThemeDusk];
    FontId fFont = 0;
    int fDrag = -1, fHover = -1, fEdit = -1;
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
