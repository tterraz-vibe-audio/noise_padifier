/*
 * Noise Padifier - parameters and presets, shared by the DSP, the UI and the tests.
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PADIFIER_PARAMS_HPP_INCLUDED
#define PADIFIER_PARAMS_HPP_INCLUDED

#include <cmath>
#include <cstddef>
#include <cstdio>

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

enum PadifierParam {
    // inputs.  The indices of 1.0 are kept (projects of 1.0 keep their settings), except that
    // Noise (white / pink) became Colour; the parameters of 1.1 follow.
    kParamColour = 0,     // spectral slope of the noise, dB / octave (-3 pink, 0 white)
    kParamLength,         // length of the impulse response, s
    kParamSwell,          // rise time of the envelope, % of the length
    kParamDecay,          // part of the length taken by the fall to the minimum, % (1.2: was dB)
    kParamSeed,           // realisation of the noise (integer)
    kParamLowCut,         // high-pass on the wet signal, Hz (minimum = off)
    kParamHighCut,        // low-pass on the wet signal, Hz (maximum = off)
    kParamWidth,          // stereo width of the wet signal, %
    kParamMix,            // dry / wet, % (equal power)
    kParamOutput,         // output gain, dB
    kParamBypass,         // host bypass
    // 1.1
    kParamDensity,        // impulses per second of sparse (velvet) noise; maximum = smooth noise
    kParamTune,           // 0 off, 1 comb on the note, 2 combs on the notes of a chord
    kParamNote,           // root note, MIDI number
    kParamChord,          // kind of chord (kChords)
    kParamResonance,      // ring time of the combs, % (20 ms .. 5 s)
    kParamDamping,        // extra loss of the highs at the end, dB / octave above 250 Hz
    kParamBloom,          // shift of the peak with frequency, % (+: highs later, -: lows later)
    kParamLfoRate,        // LFO along the response, Hz
    kParamLfoPitch,       // depth on the note of the combs, semitones
    kParamLfoTone,        // depth on the spectral slope, dB / octave
    kParamRhythm,         // gate along the response, division of the beat (kDivisions; 0 off)
    kParamDuty,           // open part of each period of the gate, %
    kParamPanRate,        // stereo motion along the response, Hz
    kParamPanDepth,       // depth of the stereo motion, %
    kParamCross,          // true stereo: left -> right and right -> left, %
    kParamPredelay,       // delay of the wet signal, ms
    // 1.1.1
    kParamAttack,         // rise at the start of each step of the gate, % of the step
    kParamRelease,        // fall after the open part of each step, % of the step
    // outputs
    kParamMeterIn,        // input peak, dBFS
    kParamMeterWet,       // wet peak, dBFS
    kParamMeterOut,       // output peak, dBFS
    kParamTempo,          // tempo given by the host, BPM (for the interface)
    kParamCount
};

static const int kParamFirstOutput = kParamMeterIn;

struct PadifierRange { float min, max, def; };

static const PadifierRange kPadifierRanges[kParamCount] = {
    {  -6.0f,     6.0f,    -3.0f },   // colour, dB / octave
    {   0.1f,    30.0f,     3.0f },   // length, s
    {   0.0f,   100.0f,    25.0f },   // swell, %
    {   0.0f,   100.0f,    75.0f },   // decay, % of the length (fall to -60 dB)
    {   1.0f,    99.0f,     1.0f },   // seed
    {  20.0f,  1000.0f,    20.0f },   // low cut, Hz (20 = off)
    { 1000.0f, 20000.0f, 20000.0f },  // high cut, Hz (20000 = off)
    {   0.0f,   200.0f,   100.0f },   // width, %
    {   0.0f,   100.0f,    50.0f },   // mix, %
    { -24.0f,    12.0f,     0.0f },   // output, dB
    {   0.0f,     1.0f,     0.0f },   // bypass
    {  10.0f, 10000.0f, 10000.0f },   // density, impulses / s (10000 = smooth)
    {   0.0f,     2.0f,     0.0f },   // tune: off
    {  24.0f,    96.0f,    57.0f },   // note: A3, 220 Hz
    {   0.0f,     8.0f,     0.0f },   // chord: major
    {   0.0f,   100.0f,    50.0f },   // resonance, %
    {   0.0f,    12.0f,     0.0f },   // damping, dB / octave
    {-100.0f,   100.0f,     0.0f },   // bloom, %
    {  0.05f,    10.0f,     0.5f },   // LFO rate, Hz
    {   0.0f,    12.0f,     0.0f },   // LFO pitch, semitones
    {   0.0f,     6.0f,     0.0f },   // LFO tone, dB / octave
    {   0.0f,    12.0f,     0.0f },   // rhythm: off
    {   5.0f,   100.0f,    50.0f },   // duty, %
    {  0.05f,    10.0f,     0.3f },   // pan rate, Hz
    {   0.0f,   100.0f,     0.0f },   // pan depth, %
    {   0.0f,   100.0f,     0.0f },   // cross, %
    {   0.0f,   500.0f,     0.0f },   // pre-delay, ms
    {   0.0f,   100.0f,    10.0f },   // attack, % of the step
    {   0.0f,   100.0f,    25.0f },   // release, % of the step
    { -60.0f,     6.0f,   -60.0f },   // meter in
    { -60.0f,     6.0f,   -60.0f },   // meter wet
    { -60.0f,     6.0f,   -60.0f },   // meter out
    {  20.0f,   300.0f,   120.0f },   // tempo
};

static const float kLowCutOff = 20.0f;       // low cut at its minimum: no filter
static const float kHighCutOff = 20000.0f;   // high cut at its maximum: no filter
static const float kDensitySmooth = 10000.0f; // density at its maximum: Gaussian noise
static const float kDefaultTempo = 120.0f;   // when the host gives none

// Parameters that define the impulse response (a change rebuilds it).  The tempo also does,
// while the rhythm is on.
static inline bool isIRParam(const int i)
{
    return i == kParamColour || i == kParamLength || i == kParamSwell || i == kParamDecay || i == kParamSeed
        || (i >= kParamDensity && i <= kParamCross) || i == kParamAttack || i == kParamRelease;
}

// ------------------------------------------------------------------------------------------------
// Chords (semitones above the root) and divisions of the beat for the rhythm

struct PadifierChord { const char* name; int count; int steps[4]; };

static const PadifierChord kChords[] = {
    { "major", 3, { 0, 4, 7 } },
    { "minor", 3, { 0, 3, 7 } },
    { "sus2", 3, { 0, 2, 7 } },
    { "sus4", 3, { 0, 5, 7 } },
    { "fifth", 3, { 0, 7, 12 } },
    { "maj7", 4, { 0, 4, 7, 11 } },
    { "min7", 4, { 0, 3, 7, 10 } },
    { "dom7", 4, { 0, 4, 7, 10 } },
    { "octaves", 3, { 0, 12, 24 } },
};
static const int kChordCount = sizeof(kChords) / sizeof(kChords[0]);

struct PadifierDivision { const char* name; double beats; };   // beats: quarter notes

static const PadifierDivision kDivisions[] = {
    { "off", 0.0 }, { "1/1", 4.0 }, { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/16", 0.25 },
    { "1/32", 0.125 }, { "1/4.", 1.5 }, { "1/8.", 0.75 }, { "1/16.", 0.375 }, { "1/4T", 2.0 / 3.0 },
    { "1/8T", 1.0 / 3.0 }, { "1/16T", 1.0 / 6.0 },
};
static const int kDivisionCount = sizeof(kDivisions) / sizeof(kDivisions[0]);

static const char* const kTuneNames[] = { "off", "comb", "chord" };

// ------------------------------------------------------------------------------------------------
// Factory presets: values of all the inputs (the bypass slot is ignored).  Set from the laws,
// not by ear.

static const int kPresetParamCount = kParamFirstOutput;

// the bypass is not part of a preset, nor of the A / B sets
static inline bool isPresetParam(const int i) { return i >= 0 && i < kPresetParamCount && i != kParamBypass; }

struct PadifierPreset {
    const char* name;
    float values[kPresetParamCount];
};

struct PadifierSetting { int param; float value; };

// the defaults, then the given settings
template <size_t N>
static PadifierPreset makePreset(const char* name, const PadifierSetting (&settings)[N])
{
    PadifierPreset p;
    p.name = name;
    for (int i = 0; i < kPresetParamCount; ++i)
        p.values[i] = kPadifierRanges[i].def;
    for (size_t k = 0; k < N; ++k)
        p.values[settings[k].param] = settings[k].value;
    return p;
}

static const PadifierSetting kNone[] = { { kParamBypass, 0.0f } };

static const PadifierPreset kPadifierPresets[] = {
    makePreset("Default", kNone),
    makePreset("Soft pad", { { kParamLength, 4 }, { kParamSwell, 50 }, { kParamDecay, 50 },
        { kParamLowCut, 80 }, { kParamHighCut, 8000 }, { kParamWidth, 120 }, { kParamMix, 60 },
        { kParamDamping, 3 } }),
    makePreset("Endless wash", { { kParamLength, 8 }, { kParamSwell, 40 }, { kParamDecay, 20 },
        { kParamSeed, 7 }, { kParamLowCut, 60 }, { kParamHighCut, 12000 }, { kParamWidth, 140 },
        { kParamMix, 70 }, { kParamCross, 40 } }),
    makePreset("Dark cloud", { { kParamColour, -4.5f }, { kParamLength, 6 }, { kParamSwell, 60 },
        { kParamDecay, 40 }, { kParamSeed, 3 }, { kParamLowCut, 40 }, { kParamHighCut, 3000 },
        { kParamMix, 60 }, { kParamDamping, 6 } }),
    makePreset("Airy halo", { { kParamColour, 0 }, { kParamSwell, 30 }, { kParamDecay, 70 },
        { kParamSeed, 5 }, { kParamLowCut, 400 }, { kParamWidth, 150 }, { kParamMix, 35 },
        { kParamBloom, 50 } }),
    makePreset("Short smear", { { kParamColour, 0 }, { kParamLength, 0.4f }, { kParamSwell, 20 },
        { kParamDecay, 80 }, { kParamSeed, 2 }, { kParamLowCut, 100 }, { kParamHighCut, 14000 },
        { kParamMix, 30 } }),
    makePreset("Reverse swell", { { kParamLength, 2 }, { kParamSwell, 100 }, { kParamDecay, 0 },
        { kParamSeed, 4 }, { kParamLowCut, 60 }, { kParamHighCut, 16000 }, { kParamWidth, 120 } }),
    makePreset("Tuned drone", { { kParamLength, 5 }, { kParamSwell, 40 }, { kParamDecay, 30 },
        { kParamTune, 1 }, { kParamNote, 45 }, { kParamResonance, 70 }, { kParamLowCut, 40 },
        { kParamMix, 60 } }),
    makePreset("Chord pad", { { kParamLength, 6 }, { kParamSwell, 45 }, { kParamDecay, 50 },
        { kParamTune, 2 }, { kParamNote, 50 }, { kParamChord, 6 }, { kParamResonance, 60 },
        { kParamLowCut, 80 }, { kParamWidth, 130 }, { kParamMix, 60 }, { kParamLfoRate, 0.2f },
        { kParamLfoPitch, 0.15f } }),
    makePreset("Pulse gate", { { kParamLength, 4 }, { kParamSwell, 10 }, { kParamDecay, 90 },
        { kParamRhythm, 4 }, { kParamDuty, 40 }, { kParamAttack, 10 }, { kParamRelease, 30 }, { kParamLowCut, 80 }, { kParamMix, 50 },
        { kParamPanDepth, 60 }, { kParamPanRate, 1 } }),
    makePreset("Grain cloud", { { kParamColour, -1.5f }, { kParamLength, 3 }, { kParamSwell, 30 },
        { kParamDecay, 70 }, { kParamDensity, 40 }, { kParamWidth, 150 }, { kParamMix, 45 },
        { kParamCross, 60 } }),
    makePreset("Blooming", { { kParamLength, 5 }, { kParamSwell, 35 }, { kParamDecay, 65 },
        { kParamBloom, -60 }, { kParamDamping, 4 }, { kParamLfoRate, 0.4f }, { kParamLfoTone, 2 },
        { kParamLowCut, 60 }, { kParamMix, 55 } }),
    makePreset("Send (100 % wet)", { { kParamLowCut, 60 }, { kParamMix, 100 } }),
};

static const int kPadifierPresetCount = sizeof(kPadifierPresets) / sizeof(kPadifierPresets[0]);

// States saved with the project, written by the UI only (the DSP ignores them)
enum { kStateAB = 0, kStateCount };
static const char* const kStateKeys[kStateCount] = { "ab" };

// ------------------------------------------------------------------------------------------------

namespace PadifierLaw {

// equal-power mix: wet = sin(pi/2 m), dry = cos(pi/2 m), exact at both ends
static inline float wetGain(const float percent)
{
    const float m = std::fmin(std::fmax(percent * 0.01f, 0.0f), 1.0f);
    return m >= 1.0f ? 1.0f : m <= 0.0f ? 0.0f : static_cast<float>(std::sin(0.5 * M_PI * m));
}

static inline float dryGain(const float percent)
{
    const float m = std::fmin(std::fmax(percent * 0.01f, 0.0f), 1.0f);
    return m >= 1.0f ? 0.0f : m <= 0.0f ? 1.0f : static_cast<float>(std::cos(0.5 * M_PI * m));
}

static inline int seed(const float v) { return static_cast<int>(std::lround(v)); }
static inline int choice(const float v, const int count)
{
    const int i = static_cast<int>(std::lround(v));
    return i < 0 ? 0 : i >= count ? count - 1 : i;
}

// MIDI note -> Hz (A4 = 69 = 440 Hz)
static inline double noteHz(const double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

// "A3", "C#4"
static inline void noteName(const int note, char* buf, const size_t size)
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    std::snprintf(buf, size, "%s%d", names[((note % 12) + 12) % 12], note / 12 - 1);
}

// ring time (-60 dB) of the combs, s: 20 ms at 0 %, 5 s at 100 %
static inline double ringSeconds(const float percent)
{
    return 0.02 * std::pow(250.0, std::fmin(std::fmax(percent * 0.01, 0.0), 1.0));
}

}   // namespace PadifierLaw

#endif
