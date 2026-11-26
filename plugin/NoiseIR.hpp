/*
 * Noise Padifier - the impulse responses: noise, tuned and shaped along the response.
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Streams: k = 0 left -> left, 1 right -> right (direct), 2 right -> left, 3 left -> right
 * (cross, made only when Cross > 0).  Each stream, N = round(length fs) samples, is made in
 * this order:
 * 1. source w: Gaussian noise (Box-Muller on SplitMix64 seeded by (seed, k): the same samples
 *    on every computer), or sparse "velvet" noise when Density is below its maximum: one
 *    impulse of random sign at a random place in each period fs / density, scaled to unit
 *    power;
 * 2. colour: spectrum (FFT over the next power of two, circular) multiplied by
 *    (max(f, 20 Hz) / 1 kHz)^(colour / 6.0206): colour dB per octave of power, DC removed
 *    (-3: pink, 0: white, unchanged);
 * 3. tune: combs y(n) = x(n) + g y(n - D(n)) on the note (comb) or on each note of the chord
 *    (chord, summed), D = fs / f(n), f(n) = note x 2^(LFO pitch x sin(2 pi LFO rate t) / 12),
 *    g = 10^(-3 D / (fs T60)), T60 = ring time of Resonance; the combs run over a lead-in of
 *    min(T60, 5 s) of noise before t = 0, so that they ring steadily from the start;
 * 4. envelope.  Without damping, bloom or LFO on the tone: e(t) in the time domain,
 *      rise   sin^2(pi/2 t / Ta), Ta = swell x length,
 *      decay  10^(decay_dB / 20 x (t - Ta) / (length - Ta)) (straight line in dB),
 *    as in 1.0.  With one of them: short-time Fourier transform (sqrt-Hann windows of
 *    2048 samples at 48 kHz, hop 1/4) and a gain per frame and frequency:
 *      e(f, t) with Ta(f) = Ta + bloom x length / 2 x u(f), u = log2(f / 1 kHz) / 4 within
 *      [-1, 1] (bloom > 0: the highs peak later), and an end level
 *      decay_dB - damping x max(0, log2(f / 250 Hz)) (the highs die sooner),
 *      times (max(f, 20 Hz) / 1 kHz)^(LFO tone x sin(2 pi LFO rate t) / 6.0206);
 *    then, in both cases, the fade cos^2 over the last min(20 ms, length / 10);
 * 5. rhythm: gate of period T = (beats of the division) x 60 / tempo; each step k from kT:
 *    rise sin^2 over Attack (a fraction of T, at least 1 ms, at most Duty), held until Duty,
 *    fall cos^2 over Release (at least 1 ms) after Duty; the gate is the largest of the current
 *    step and the falls of the previous ones (Duty 100 % with no attack: always open);
 * 6. unit energy (sum of h^2 = 1).
 * Then the output o (0 left, 1 right) is made of the direct stream o x cos(c pi / 4) and the
 * cross stream x sin(c pi / 4), c = Cross: an input with uncorrelated channels keeps its level.
 * The streams are computed in parallel threads (never in the audio thread).
 * Last, stereo motion: output o multiplied by sqrt(2) cos (left) or sin (right) of
 * pi/4 + depth pi/4 sin(2 pi pan rate t).
 */
#ifndef PADIFIER_NOISE_IR_HPP_INCLUDED
#define PADIFIER_NOISE_IR_HPP_INCLUDED

#include "PadifierParams.hpp"
#include "pffft/pffft.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace PadifierIR {

// dB per octave of power for an amplitude in f^1: 20 log10(2)
static const double kDbPerOctave = 6.020599913279624;

struct Settings {
    double colour = -3.0;    // dB / octave
    double length = 3.0;     // s
    double swell = 0.25;     // fraction of the length
    double decayDb = -30.0;  // level at the end, dB
    int seed = 1;
    double density = kDensitySmooth;   // impulses / s; >= kDensitySmooth: Gaussian noise
    int tune = 0;            // 0 off, 1 comb, 2 chord
    double note = 57.0;      // MIDI
    int chord = 0;           // index in kChords
    double resonance = 50.0; // %
    double damping = 0.0;    // dB / octave
    double bloom = 0.0;      // -1 .. 1
    double lfoRate = 0.5;    // Hz
    double lfoPitch = 0.0;   // semitones
    double lfoTone = 0.0;    // dB / octave
    int rhythm = 0;          // index in kDivisions, 0 off
    double duty = 0.5;       // fraction
    double attack = 0.1;     // fraction of the step
    double release = 0.25;   // fraction of the step
    double tempo = kDefaultTempo;   // BPM
    double panRate = 0.3;    // Hz
    double panDepth = 0.0;   // 0 .. 1
    double cross = 0.0;      // 0 .. 1
    double fs = 48000.0;
};

static inline double clampf(const double v, const double a, const double b) { return std::fmin(std::fmax(v, a), b); }

static inline double param(const float* const p, const int i)
{
    return clampf(p[i], kPadifierRanges[i].min, kPadifierRanges[i].max);
}

// params: values of all the inputs (kParamFirstOutput)
static inline Settings fromParams(const float* const p, const double fs, const double tempo = kDefaultTempo)
{
    Settings s;
    s.colour = param(p, kParamColour);
    s.length = param(p, kParamLength);
    s.swell = param(p, kParamSwell) * 0.01;
    s.decayDb = param(p, kParamDecay);
    s.seed = PadifierLaw::seed(p[kParamSeed]);
    s.density = param(p, kParamDensity);
    s.tune = PadifierLaw::choice(p[kParamTune], 3);
    s.note = param(p, kParamNote);
    s.chord = PadifierLaw::choice(p[kParamChord], kChordCount);
    s.resonance = param(p, kParamResonance);
    s.damping = param(p, kParamDamping);
    s.bloom = param(p, kParamBloom) * 0.01;
    s.lfoRate = param(p, kParamLfoRate);
    s.lfoPitch = param(p, kParamLfoPitch);
    s.lfoTone = param(p, kParamLfoTone);
    s.rhythm = PadifierLaw::choice(p[kParamRhythm], kDivisionCount);
    s.duty = param(p, kParamDuty) * 0.01;
    s.attack = param(p, kParamAttack) * 0.01;
    s.release = param(p, kParamRelease) * 0.01;
    s.tempo = s.rhythm > 0 ? clampf(tempo, 20.0, 300.0) : kDefaultTempo;   // no rebuild for the tempo when off
    s.panRate = param(p, kParamPanRate);
    s.panDepth = param(p, kParamPanDepth) * 0.01;
    s.cross = param(p, kParamCross) * 0.01;
    s.fs = fs;
    // settings without effect are set to their defaults, so that they do not cause a rebuild
    const Settings d;
    if (s.tune == 0)
    {
        s.note = d.note;
        s.chord = d.chord;
        s.resonance = d.resonance;
        s.lfoPitch = d.lfoPitch;
    }
    else if (s.tune == 1)
        s.chord = d.chord;
    if (s.lfoPitch == 0.0 && s.lfoTone == 0.0)
        s.lfoRate = d.lfoRate;
    if (s.rhythm == 0)
    {
        s.duty = d.duty;
        s.attack = d.attack;
        s.release = d.release;
    }
    if (s.panDepth == 0.0)
        s.panRate = d.panRate;
    return s;
}

static inline bool operator==(const Settings& a, const Settings& b)
{
    return a.colour == b.colour && a.length == b.length && a.swell == b.swell && a.decayDb == b.decayDb
        && a.seed == b.seed && a.density == b.density && a.tune == b.tune && a.note == b.note && a.chord == b.chord
        && a.resonance == b.resonance && a.damping == b.damping && a.bloom == b.bloom && a.lfoRate == b.lfoRate
        && a.lfoPitch == b.lfoPitch && a.lfoTone == b.lfoTone && a.rhythm == b.rhythm && a.duty == b.duty
        && a.attack == b.attack && a.release == b.release
        && a.tempo == b.tempo && a.panRate == b.panRate && a.panDepth == b.panDepth && a.cross == b.cross
        && a.fs == b.fs;
}

static inline int lengthSamples(const Settings& s)
{
    return std::max(1, static_cast<int>(std::lround(s.length * s.fs)));
}

static inline bool spectralEnvelope(const Settings& s)
{
    return s.damping != 0.0 || s.bloom != 0.0 || s.lfoTone != 0.0;
}

// ------------------------------------------------------------------------------------------------
// Laws along the response, shared with the interface (drawing) and the tests

static inline double lfo(const Settings& s, const double t) { return std::sin(2.0 * M_PI * s.lfoRate * t); }

// peak time of frequency f, s
static inline double peakTime(const Settings& s, const double f)
{
    const double L = s.length;
    const double u = clampf(std::log2(std::fmax(f, 1.0) / 1000.0) / 4.0, -1.0, 1.0);
    return clampf(s.swell * L + s.bloom * 0.5 * L * u, 0.0, L);
}

// level at the end for frequency f, dB
static inline double endDb(const Settings& s, const double f)
{
    return s.decayDb - s.damping * std::fmax(0.0, std::log2(std::fmax(f, 1.0) / 250.0));
}

// rise and decay with a given peak time and end level (no final fade); t in [0, L)
static inline double riseDecay(const double L, const double ta, const double endLevelDb, const double t)
{
    if (t < 0.0 || t >= L)
        return 0.0;
    if (t < ta)
    {
        const double r = std::sin(0.5 * M_PI * t / ta);
        return r * r;
    }
    return L - ta > 0.0 ? std::pow(10.0, endLevelDb / 20.0 * (t - ta) / (L - ta)) : 1.0;
}

// final fade cos^2 over the last min(20 ms, L / 10)
static inline double endFade(const Settings& s, const double t)
{
    const double L = s.length, tf = std::fmin(0.02, 0.1 * L);
    if (t < 0.0 || t >= L)
        return 0.0;
    if (t <= L - tf)
        return 1.0;
    const double c = std::cos(0.5 * M_PI * (t - (L - tf)) / tf);
    return c * c;
}

// envelope of 1.0 (frequency independent), with the fade
static inline double envelope(const Settings& s, const double t)
{
    return riseDecay(s.length, s.swell * s.length, s.decayDb, t) * endFade(s, t);
}

// gain of frequency f at time t: envelope, damping, bloom, LFO on the tone (no fade, no gate)
static inline double spectralGain(const Settings& s, const double f, const double t)
{
    double g = riseDecay(s.length, peakTime(s, f), endDb(s, f), t);
    if (s.lfoTone != 0.0)
        g *= std::pow(std::fmax(f, 20.0) / 1000.0, s.lfoTone * lfo(s, t) / kDbPerOctave);
    return g;
}

// rhythm gate at time t, 0 .. 1
// one step of the gate, tau s after its start (period T)
static inline double gateStep(const Settings& s, const double period, const double tau)
{
    const double open = s.duty * period;
    const double rise = std::fmin(std::fmax(s.attack * period, 0.001), open);
    const double fall = std::fmax(s.release * period, 0.001);
    if (tau < 0.0)
        return 0.0;
    if (tau < rise)
    {
        const double r = std::sin(0.5 * M_PI * tau / rise);
        return r * r;
    }
    if (tau < open)
        return 1.0;
    if (tau < open + fall)
    {
        const double c = std::cos(0.5 * M_PI * (tau - open) / fall);
        return c * c;
    }
    return 0.0;
}

// rhythm gate at time t, 0 .. 1
static inline double gate(const Settings& s, const double t)
{
    if (s.rhythm <= 0 || (s.duty >= 1.0 && s.attack <= 0.0))
        return 1.0;
    const double period = kDivisions[s.rhythm].beats * 60.0 / s.tempo;
    const double tt = std::fmax(t, 0.0);
    const double k = std::floor(tt / period);
    // the current step and the falls of the two previous ones (a fall lasts at most T + 1 ms)
    double g = 0.0;
    for (int j = 0; j < 3 && k - j >= 0.0; ++j)
        g = std::fmax(g, gateStep(s, period, tt - (k - j) * period));
    return g;
}

// stereo motion: gain of output o (0 left, 1 right) at time t
static inline double panGain(const Settings& s, const int o, const double t)
{
    if (s.panDepth <= 0.0)
        return 1.0;
    const double a = 0.25 * M_PI * (1.0 + s.panDepth * std::sin(2.0 * M_PI * s.panRate * t));
    return M_SQRT2 * (o == 0 ? std::cos(a) : std::sin(a));
}

// frequencies of the combs at t = 0, Hz
static inline std::vector<double> notes(const Settings& s)
{
    std::vector<double> f;
    if (s.tune == 1)
        f.push_back(PadifierLaw::noteHz(s.note));
    else if (s.tune == 2)
        for (int i = 0; i < kChords[s.chord].count; ++i)
            f.push_back(PadifierLaw::noteHz(s.note + kChords[s.chord].steps[i]));
    return f;
}

// ------------------------------------------------------------------------------------------------

// SplitMix64 (Steele, Lea, Flood 2014): portable, the same sequence everywhere
struct Random {
    uint64_t x;
    explicit Random(const uint64_t s) : x(s) {}
    uint64_t next()
    {
        uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // uniform in (0, 1]
    double uniform() { return (static_cast<double>(next() >> 11) + 1.0) * (1.0 / 9007199254740992.0); }
};

static inline int nextPowerOf2(const int n)
{
    int p = 64;   // pffft: real transforms of at least 32 points
    while (p < n)
        p *= 2;
    return p;
}

// pffft buffers and setup of one size, freed on exit
struct Fft {
    int n;
    PFFFT_Setup* setup;
    float* buf;
    float* work;
    explicit Fft(const int size)
        : n(size), setup(pffft_new_setup(size, PFFFT_REAL)),
          buf(static_cast<float*>(pffft_aligned_malloc(sizeof(float) * size))),
          work(static_cast<float*>(pffft_aligned_malloc(sizeof(float) * size))) {}
    ~Fft()
    {
        pffft_aligned_free(work);
        pffft_aligned_free(buf);
        pffft_destroy_setup(setup);
    }
    Fft(const Fft&) = delete;
    Fft& operator=(const Fft&) = delete;
    // ordered layout: [DC, Nyquist, re1, im1, re2, im2, ...]
    void forward() { pffft_transform_ordered(setup, buf, buf, work, PFFFT_FORWARD); }
    void backward() { pffft_transform_ordered(setup, buf, buf, work, PFFFT_BACKWARD); }
};

// 1. source: n samples of unit power for stream k
static inline std::vector<double> source(const Settings& s, const int k, const int n)
{
    std::vector<double> w(static_cast<size_t>(n), 0.0);
    Random rng(static_cast<uint64_t>(s.seed) * 0x100000001B3ull + static_cast<uint64_t>(k) + 1u);
    if (s.density >= kDensitySmooth)
    {
        for (int i = 0; i < n; i += 2)
        {
            const double r = std::sqrt(-2.0 * std::log(rng.uniform())), a = 2.0 * M_PI * rng.uniform();
            w[i] = r * std::cos(a);
            if (i + 1 < n)
                w[i + 1] = r * std::sin(a);
        }
        return w;
    }
    // velvet noise: one impulse per cell of td samples
    const double td = s.fs / s.density, amp = std::sqrt(td);
    for (long j = 0;; ++j)
    {
        const double start = j * td;
        if (start >= n)
            break;
        const long pos = static_cast<long>(start + rng.uniform() * td * (1.0 - 1e-12));
        const double sign = rng.uniform() < 0.5 ? -1.0 : 1.0;
        if (pos < n)
            w[static_cast<size_t>(pos)] = sign * amp;
    }
    return w;
}

// Tables shared by the streams (they depend on the settings only)
struct Tables {
    int n = 0, lead = 0;              // samples of the response, of the lead-in of the combs
    int nf = 0;                       // FFT size of the colour
    std::vector<float> colourGain;    // per bin of nf
    std::vector<double> pitch;        // LFO factor of the frequency of the combs, per sample
    int W = 0, hop = 0, frames = 0;   // short-time transform
    std::vector<double> window;       // sqrt of the periodic Hann window
    std::vector<float> spectral;      // gains, frames x (W / 2 + 1)
};

static inline Tables makeTables(const Settings& s)
{
    Tables t;
    t.n = lengthSamples(s);
    t.lead = s.tune > 0
        ? static_cast<int>(std::ceil(std::fmin(PadifierLaw::ringSeconds(static_cast<float>(s.resonance)), 5.0) * s.fs))
        : 0;
    const int total = t.n + t.lead;
    if (s.colour != 0.0)
    {
        t.nf = nextPowerOf2(total);
        t.colourGain.resize(static_cast<size_t>(t.nf / 2 + 1));
        const double df = s.fs / t.nf, e = s.colour / kDbPerOctave;
        for (int k = 0; k <= t.nf / 2; ++k)
            t.colourGain[k] = k == 0 ? 0.0f : static_cast<float>(std::exp(e * std::log(std::fmax(k * df, 20.0) / 1000.0)));
    }
    if (s.tune > 0 && s.lfoPitch != 0.0)
    {
        t.pitch.resize(static_cast<size_t>(total));
        for (int i = 0; i < total; ++i)
            t.pitch[i] = std::exp2(s.lfoPitch * lfo(s, (i - t.lead) / s.fs) / 12.0);
    }
    if (spectralEnvelope(s))
    {
        t.W = std::max(256, nextPowerOf2(static_cast<int>(2048.0 * s.fs / 48000.0) - 1));
        t.hop = t.W / 4;
        t.frames = (t.n + t.W + t.hop - 1) / t.hop;
        t.window.resize(static_cast<size_t>(t.W));
        for (int i = 0; i < t.W; ++i)
            t.window[i] = std::sqrt(0.5 - 0.5 * std::cos(2.0 * M_PI * i / t.W));
        const int bins = t.W / 2 + 1;
        // per bin: peak time, end level, log of the frequency for the tone
        std::vector<double> ta(static_cast<size_t>(bins)), slope(static_cast<size_t>(bins)), lnf(static_cast<size_t>(bins));
        const double L = s.length, ln10_20 = std::log(10.0) / 20.0;
        for (int k = 0; k < bins; ++k)
        {
            const double f = k * s.fs / t.W;
            ta[k] = peakTime(s, f);
            slope[k] = L - ta[k] > 0.0 ? endDb(s, f) * ln10_20 / (L - ta[k]) : 0.0;
            lnf[k] = std::log(std::fmax(f, 20.0) / 1000.0);
        }
        t.spectral.resize(static_cast<size_t>(t.frames) * bins);
        for (int j = 0; j < t.frames; ++j)
        {
            const double tc = (-t.W + j * t.hop + 0.5 * t.W) / s.fs;
            const double tone = s.lfoTone != 0.0 ? s.lfoTone * lfo(s, tc) / kDbPerOctave : 0.0;
            float* const g = &t.spectral[static_cast<size_t>(j) * bins];
            for (int k = 0; k < bins; ++k)
            {
                double v;
                if (tc < 0.0 || tc >= L)
                    v = 0.0;
                else if (tc < ta[k])
                {
                    const double r = std::sin(0.5 * M_PI * tc / ta[k]);
                    v = r * r;
                }
                else
                    v = std::exp(slope[k] * (tc - ta[k]));
                if (tone != 0.0)
                    v *= std::exp(tone * lnf[k]);
                g[k] = static_cast<float>(v);
            }
        }
    }
    return t;
}

// 2. colour, in place
static inline void colour(const Tables& t, std::vector<double>& w)
{
    if (t.nf == 0)
        return;
    const int n = static_cast<int>(w.size()), nf = t.nf;
    Fft fft(nf);
    for (int i = 0; i < nf; ++i)
        fft.buf[i] = i < n ? static_cast<float>(w[i]) : 0.0f;
    fft.forward();
    fft.buf[0] = 0.0f;
    fft.buf[1] *= t.colourGain[nf / 2];
    for (int k = 1; k < nf / 2; ++k)
    {
        fft.buf[2 * k] *= t.colourGain[k];
        fft.buf[2 * k + 1] *= t.colourGain[k];
    }
    fft.backward();
    const double scale = 1.0 / nf;
    for (int i = 0; i < n; ++i)
        w[i] = fft.buf[i] * scale;
}

// 3. combs over the whole signal (lead-in samples before t = 0 included)
static inline std::vector<double> combs(const Settings& s, const Tables& t, const std::vector<double>& x)
{
    const std::vector<double> f0 = notes(s);
    const int n = static_cast<int>(x.size());
    const double c = 6.907755278982137 / (s.fs * PadifierLaw::ringSeconds(static_cast<float>(s.resonance)));   // ln(1000)
    const bool lfoOn = !t.pitch.empty();
    std::vector<double> y(static_cast<size_t>(n), 0.0);
    for (const double f : f0)
    {
        const double d0 = s.fs / f;
        const double dmax = d0 * std::exp2(s.lfoPitch / 12.0) + 4.0;
        const int size = nextPowerOf2(static_cast<int>(dmax) + 4), mask = size - 1;
        std::vector<double> ring(static_cast<size_t>(size), 0.0);
        double d = d0, g = std::exp(-c * d0);   // feedback: 10^(-3 d / (fs T60))
        for (int i = 0; i < n; ++i)
        {
            if (lfoOn)
            {
                d = d0 / t.pitch[i];
                if ((i & 15) == 0)
                    g = std::exp(-c * d);   // ring time: updated every 16 samples
            }
            const double pos = i - d;
            const long p0 = static_cast<long>(std::floor(pos));
            const double a = pos - p0;
            const double past = (1.0 - a) * ring[static_cast<size_t>(p0 & mask)] + a * ring[static_cast<size_t>((p0 + 1) & mask)];
            const double v = x[i] + g * past;
            ring[static_cast<size_t>(i & mask)] = v;
            y[i] += v;
        }
    }
    return y;
}

// 4. envelope by short-time Fourier transform with the gains of the tables, in place
static inline void spectralShape(const Tables& t, std::vector<double>& x)
{
    const int n = static_cast<int>(x.size()), W = t.W, bins = W / 2 + 1;
    const double norm = 1.0 / (2.0 * W);   // Hann (analysis x synthesis) summed at hop W/4: 2
    std::vector<double> y(static_cast<size_t>(n), 0.0);
    Fft fft(W);
    for (int j = 0; j < t.frames; ++j)
    {
        const int start = -W + j * t.hop;
        bool any = false;
        for (int i = 0; i < W; ++i)
        {
            const int k = start + i;
            const double v = k >= 0 && k < n ? x[k] * t.window[i] : 0.0;
            fft.buf[i] = static_cast<float>(v);
            any = any || v != 0.0;
        }
        const float* const g = &t.spectral[static_cast<size_t>(j) * bins];
        if (!any)
            continue;
        fft.forward();
        fft.buf[0] *= g[0];
        fft.buf[1] *= g[W / 2];
        for (int k = 1; k < W / 2; ++k)
        {
            fft.buf[2 * k] *= g[k];
            fft.buf[2 * k + 1] *= g[k];
        }
        fft.backward();
        for (int i = 0; i < W; ++i)
        {
            const int k = start + i;
            if (k >= 0 && k < n)
                y[k] += fft.buf[i] * t.window[i] * norm;
        }
    }
    x.swap(y);
}

// one stream, steps 1 to 6 (unit energy)
static inline std::vector<double> stream(const Settings& s, const Tables& t, const int k)
{
    const int n = t.n;
    std::vector<double> w = source(s, k, n + t.lead);
    colour(t, w);
    if (s.tune > 0)
    {
        std::vector<double> y = combs(s, t, w);
        w.assign(y.begin() + t.lead, y.end());
    }
    const bool spectral = spectralEnvelope(s);
    if (spectral)
        spectralShape(t, w);
    double energy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double tt = i / s.fs;
        w[i] *= (spectral ? endFade(s, tt) : envelope(s, tt)) * gate(s, tt);
        energy += w[i] * w[i];
    }
    const double g = energy > 0.0 ? 1.0 / std::sqrt(energy) : 0.0;
    for (double& v : w)
        v *= g;
    return w;
}

// The responses: h[i][o] from input i to output o; h[1][0] and h[0][1] empty without Cross.
struct Response {
    int length = 0;
    bool cross = false;
    std::vector<float> h[2][2];
};

static inline Response generate(const Settings& s)
{
    Response r;
    r.length = lengthSamples(s);
    r.cross = s.cross > 0.0;
    const Tables t = makeTables(s);
    const double direct = std::cos(0.25 * M_PI * s.cross), cross = std::sin(0.25 * M_PI * s.cross);
    // the streams are independent: one thread each (outside the audio thread; same result)
    const int count = r.cross ? 4 : 2;
    std::vector<double> streams[4];
    std::thread threads[4];
    for (int k = 0; k < count; ++k)
        threads[k] = std::thread([&s, &t, &streams, k] { streams[k] = stream(s, t, k); });
    for (int k = 0; k < count; ++k)
        threads[k].join();
    for (int o = 0; o < 2; ++o)
    {
        const std::vector<double>& d = streams[o];
        const std::vector<double>& c = streams[2 + o];   // from the other input
        std::vector<float>& hd = r.h[o][o];
        std::vector<float>& hc = r.h[1 - o][o];
        hd.resize(static_cast<size_t>(r.length));
        if (r.cross)
            hc.resize(static_cast<size_t>(r.length));
        for (int i = 0; i < r.length; ++i)
        {
            const double p = panGain(s, o, i / s.fs);
            hd[i] = static_cast<float>(d[i] * direct * p);
            if (r.cross)
                hc[i] = static_cast<float>(c[i] * cross * p);
        }
    }
    return r;
}

}   // namespace PadifierIR

#endif
