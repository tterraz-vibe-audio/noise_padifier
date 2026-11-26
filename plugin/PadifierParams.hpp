/*
 * Noise Padifier - parameters and presets, shared by the DSP, the UI and the tests.
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PADIFIER_PARAMS_HPP_INCLUDED
#define PADIFIER_PARAMS_HPP_INCLUDED

#include <cmath>

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

enum PadifierParam {
    // inputs; the first five define the impulse response (a change rebuilds it)
    kParamNoise = 0,      // colour of the noise: 0 white, 1 pink
    kParamLength,         // length of the impulse response, s
    kParamSwell,          // rise time of the envelope, % of the length
    kParamDecay,          // level of the envelope at the end, dB
    kParamSeed,           // realisation of the noise (integer)
    kParamLowCut,         // high-pass on the wet signal, Hz (minimum = off)
    kParamHighCut,        // low-pass on the wet signal, Hz (maximum = off)
    kParamWidth,          // stereo width of the wet signal, %
    kParamMix,            // dry / wet, % (equal power)
    kParamOutput,         // output gain, dB
    kParamBypass,         // host bypass
    // outputs
    kParamMeterIn,        // input peak, dBFS
    kParamMeterWet,       // wet peak, dBFS
    kParamMeterOut,       // output peak, dBFS
    kParamCount
};

static const int kParamFirstOutput = kParamMeterIn;
static const int kIRParamCount = kParamLowCut;   // kParamNoise .. kParamSeed

struct PadifierRange { float min, max, def; };

static const PadifierRange kPadifierRanges[kParamCount] = {
    {   0.0f,     1.0f,     1.0f },   // noise: pink
    {   0.1f,    10.0f,     3.0f },   // length, s
    {   0.0f,   100.0f,    25.0f },   // swell, %
    { -60.0f,     0.0f,   -30.0f },   // decay, dB at the end
    {   1.0f,    99.0f,     1.0f },   // seed
    {  20.0f,  1000.0f,    20.0f },   // low cut, Hz (20 = off)
    { 1000.0f, 20000.0f, 20000.0f },  // high cut, Hz (20000 = off)
    {   0.0f,   200.0f,   100.0f },   // width, %
    {   0.0f,   100.0f,    50.0f },   // mix, %
    { -24.0f,    12.0f,     0.0f },   // output, dB
    {   0.0f,     1.0f,     0.0f },   // bypass
    { -60.0f,     6.0f,   -60.0f },   // meter in
    { -60.0f,     6.0f,   -60.0f },   // meter wet
    { -60.0f,     6.0f,   -60.0f },   // meter out
};

static const float kLowCutOff = 20.0f;      // low cut at its minimum: no filter
static const float kHighCutOff = 20000.0f;  // high cut at its maximum: no filter

// ------------------------------------------------------------------------------------------------
// Factory presets: values of kParamNoise .. kParamOutput (bypass excluded).  Set from the laws,
// not by ear.

static const int kPresetParamCount = kParamBypass;

struct PadifierPreset {
    const char* name;
    float values[kPresetParamCount];
};

static const PadifierPreset kPadifierPresets[] = {
    //                       noise length swell decay seed lowcut  highcut  width   mix   out
    { "Default",           { 1.0f,  3.0f, 25.0f, -30.0f, 1.0f,  20.0f, 20000.0f, 100.0f, 50.0f, 0.0f } },
    { "Soft pad",          { 1.0f,  4.0f, 50.0f, -24.0f, 1.0f,  80.0f,  8000.0f, 120.0f, 60.0f, 0.0f } },
    { "Endless wash",      { 1.0f,  8.0f, 40.0f,  -6.0f, 7.0f,  60.0f, 12000.0f, 140.0f, 70.0f, 0.0f } },
    { "Dark cloud",        { 1.0f,  6.0f, 60.0f, -18.0f, 3.0f,  40.0f,  3000.0f, 100.0f, 60.0f, 0.0f } },
    { "Airy halo",         { 0.0f,  3.0f, 30.0f, -36.0f, 5.0f, 400.0f, 20000.0f, 150.0f, 35.0f, 0.0f } },
    { "Short smear",       { 0.0f,  0.4f, 20.0f, -40.0f, 2.0f, 100.0f, 14000.0f, 100.0f, 30.0f, 0.0f } },
    { "Reverse swell",     { 1.0f,  2.0f, 100.0f,  0.0f, 4.0f,  60.0f, 16000.0f, 120.0f, 50.0f, 0.0f } },
    { "Send (100 % wet)",  { 1.0f,  3.0f, 25.0f, -30.0f, 1.0f,  60.0f, 20000.0f, 100.0f, 100.0f, 0.0f } },
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
static inline bool pink(const float v) { return v > 0.5f; }

}   // namespace PadifierLaw

#endif
