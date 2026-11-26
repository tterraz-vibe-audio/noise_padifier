/*
 * Noise Padifier - DSP.
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.  See COPYING.
 *
 * The input is convolved with noise shaped along the response (NoiseIR.hpp): every sound is
 * smeared over the length of the response into a pad.  Signal flow:
 *   (L, R) -> convolution, L with the left response, R with the right one, plus the cross
 *   responses with Cross (Convolver.hpp; latency 256 samples, reported to the host)
 *   -> pre-delay -> low cut -> high cut -> width -> wet;
 *   out = wet x sin(pi/2 mix) + dry x cos(pi/2 mix) (dry delayed by the latency)
 *   -> output gain -> bypass crossfade (to the delayed dry signal: same latency).
 *
 * A change of a setting of the response (isIRParam), or of the tempo of the host while the
 * rhythm is on, rebuilds the response in a thread of the plugin (never in the audio thread);
 * the new response is crossfaded in by the convolver.  activate() builds it at once: rendering
 * starts with the right response.  Mix, width, filters and output go to their new values in
 * 20 ms; a change of pre-delay crossfades from the old delay to the new one in 30 ms.
 */

#include "DistrhoPlugin.hpp"
#include "PadifierParams.hpp"
#include "NoiseIR.hpp"
#include "Convolver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__SSE__) || defined(_M_X64)
# include <xmmintrin.h>
#endif

START_NAMESPACE_DISTRHO

// Flush-to-zero and denormals-are-zero (SSE) for the lifetime of the object, then the previous
// state (it belongs to the host thread).  Nothing on other processors.
class ScopedNoDenormals
{
public:
#if defined(__SSE__) || defined(_M_X64)
    ScopedNoDenormals() : fSaved(_mm_getcsr()) { _mm_setcsr(fSaved | 0x8040u); }   // FTZ bit 15, DAZ bit 6
    ~ScopedNoDenormals() { _mm_setcsr(fSaved); }

private:
    const unsigned fSaved;
#endif
};

static inline float dbToGain(const float db) { return std::pow(10.0f, db * 0.05f); }

static inline float gainToDb(const float g, const float floorDb)
{
    return g > 0.0f ? std::fmax(20.0f * std::log10(g), floorDb) : floorDb;
}

static const double kMaxLength = 10.0;   // s: kPadifierRanges[kParamLength].max

// Linear ramp to a target in a given number of samples, then exactly the target.
template <typename T>
struct Ramp
{
    T cur = 0, target = 0, step = 0;
    int left = 0;

    void reset(const T v)
    {
        cur = target = v;
        left = 0;
    }

    void set(const T v, const int n)
    {
        if (v == target)
            return;
        target = v;
        if (n <= 0)
        {
            cur = v;
            left = 0;
            return;
        }
        step = (v - cur) / n;
        left = n;
    }

    bool moving() const { return left > 0; }

    T next()
    {
        if (left > 0)
        {
            if (--left == 0)
                cur = target;
            else
                cur += step;
        }
        return cur;
    }
};

// 2nd-order Butterworth high-pass or low-pass (RBJ), transposed direct form II in double.
// Off: identity coefficients, so the output is the input exactly once the state has emptied.
struct Butterworth
{
    bool highPass = true;
    Ramp<double> c[5];   // b0, b1, b2, a1, a2
    double z1[2] = { 0, 0 }, z2[2] = { 0, 0 };

    void coefficients(const float hz, const double fs, double k[5]) const
    {
        const bool off = highPass ? hz <= kLowCutOff : hz >= kHighCutOff;
        if (off)
        {
            k[0] = 1.0;
            k[1] = k[2] = k[3] = k[4] = 0.0;
            return;
        }
        const double w = 2.0 * M_PI * std::fmin(hz, 0.45 * fs) / fs;
        const double cw = std::cos(w), alpha = std::sin(w) / (2.0 * M_SQRT1_2);
        const double a0 = 1.0 + alpha;
        const double b = highPass ? (1.0 + cw) * 0.5 : (1.0 - cw) * 0.5;
        k[0] = b / a0;
        k[1] = (highPass ? -2.0 : 2.0) * b / a0;
        k[2] = k[0];
        k[3] = -2.0 * cw / a0;
        k[4] = (1.0 - alpha) / a0;
    }

    void reset(const float hz, const double fs)
    {
        double k[5];
        coefficients(hz, fs, k);
        for (int i = 0; i < 5; ++i)
            c[i].reset(k[i]);
        z1[0] = z1[1] = z2[0] = z2[1] = 0.0;
    }

    void set(const float hz, const double fs, const int n)
    {
        double k[5];
        coefficients(hz, fs, k);
        for (int i = 0; i < 5; ++i)
            c[i].set(k[i], n);
    }

    bool identity() const
    {
        return !c[0].moving() && c[0].cur == 1.0 && c[1].cur == 0.0 && c[2].cur == 0.0 && c[3].cur == 0.0
            && c[4].cur == 0.0 && z1[0] == 0.0 && z1[1] == 0.0 && z2[0] == 0.0 && z2[1] == 0.0;
    }

    void process(float& l, float& r)
    {
        double k[5];
        for (int i = 0; i < 5; ++i)
            k[i] = c[i].next();
        float* const io[2] = { &l, &r };
        for (int ch = 0; ch < 2; ++ch)
        {
            const double x = *io[ch];
            const double y = k[0] * x + z1[ch];
            z1[ch] = k[1] * x - k[3] * y + z2[ch];
            z2[ch] = k[2] * x - k[4] * y;
            *io[ch] = static_cast<float>(y);
        }
    }
};

class PadifierPlugin : public Plugin
{
public:
    PadifierPlugin()
        : Plugin(kParamCount, kPadifierPresetCount, kStateCount)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            fParams[i] = kPadifierRanges[i].def;
        for (int i = 0; i < kParamFirstOutput; ++i)
            fIRParams[i].store(fParams[i]);
        fLowCut.highPass = true;
        fHighCut.highPass = false;
        setLatency(PadifierConv::kHeadBlock);
        if (isDummyInstance())
            return;   // asked for its description only: no buffers, no thread
        allocate();
        fWorker = std::thread([this] { workerLoop(); });
    }

    ~PadifierPlugin() override
    {
        if (fWorker.joinable())
        {
            {
                std::lock_guard<std::mutex> lock(fWakeMutex);
                fQuit = true;
            }
            fWake.notify_all();
            fWorker.join();
        }
        fEngine.discard();
    }

protected:
    const char* getLabel() const override { return "NoisePadifier"; }
    const char* getDescription() const override
    {
        return "Convolution with shaped noise (colour, tuning, envelope, rhythm, motion): turns any sound into a pad.";
    }
    const char* getMaker() const override { return "Padifier"; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }

    void initParameter(const uint32_t index, Parameter& p) override
    {
        if (index == kParamBypass)
        {
            p.initDesignation(kParameterDesignationBypass);
            return;
        }

        p.ranges.min = kPadifierRanges[index].min;
        p.ranges.max = kPadifierRanges[index].max;
        p.ranges.def = kPadifierRanges[index].def;
        p.hints = kParameterIsAutomatable;

        switch (index)
        {
        case kParamColour:   set(p, "Colour", "colour", "dB/oct"); break;
        case kParamLength:
            set(p, "Length", "length", "s");
            p.hints |= kParameterIsLogarithmic;
            break;
        case kParamSwell:  set(p, "Swell", "swell", "%"); break;
        case kParamDecay:  set(p, "Decay", "decay", "dB"); break;
        case kParamSeed:
            set(p, "Seed", "seed", "");
            p.hints |= kParameterIsInteger;
            break;
        case kParamLowCut:
            set(p, "Low cut", "lowcut", "Hz");
            p.hints |= kParameterIsLogarithmic;
            break;
        case kParamHighCut:
            set(p, "High cut", "highcut", "Hz");
            p.hints |= kParameterIsLogarithmic;
            break;
        case kParamWidth:  set(p, "Width", "width", "%"); break;
        case kParamMix:    set(p, "Mix", "mix", "%"); break;
        case kParamOutput: set(p, "Output", "output", "dB"); break;
        case kParamMeterIn:  output(p, "Input level", "meter_in", "dB"); break;
        case kParamMeterWet: output(p, "Pad level", "meter_wet", "dB"); break;
        case kParamMeterOut: output(p, "Output level", "meter_out", "dB"); break;
        case kParamTempo:
            output(p, "Tempo", "tempo", "BPM");
            p.ranges.min = kPadifierRanges[kParamTempo].min;
            p.ranges.max = kPadifierRanges[kParamTempo].max;
            p.ranges.def = kPadifierRanges[kParamTempo].def;
            break;
        case kParamDensity:
            set(p, "Density", "density", "/s");
            p.hints |= kParameterIsLogarithmic;
            break;
        case kParamTune:
            set(p, "Tune", "tune", "");
            enumeration(p, kTuneNames, 3);
            break;
        case kParamNote:
            set(p, "Note", "note", "");
            p.hints |= kParameterIsInteger;
            break;
        case kParamChord:
        {
            set(p, "Chord", "chord", "");
            const char* names[kChordCount];
            for (int i = 0; i < kChordCount; ++i)
                names[i] = kChords[i].name;
            enumeration(p, names, kChordCount);
            break;
        }
        case kParamResonance: set(p, "Resonance", "resonance", "%"); break;
        case kParamDamping:   set(p, "Damping", "damping", "dB/oct"); break;
        case kParamBloom:     set(p, "Bloom", "bloom", "%"); break;
        case kParamLfoRate:
            set(p, "LFO rate", "lforate", "Hz");
            p.hints |= kParameterIsLogarithmic;
            break;
        case kParamLfoPitch:  set(p, "LFO pitch", "lfopitch", "st"); break;
        case kParamLfoTone:   set(p, "LFO tone", "lfotone", "dB/oct"); break;
        case kParamRhythm:
        {
            set(p, "Rhythm", "rhythm", "");
            const char* names[kDivisionCount];
            for (int i = 0; i < kDivisionCount; ++i)
                names[i] = kDivisions[i].name;
            enumeration(p, names, kDivisionCount);
            break;
        }
        case kParamDuty:      set(p, "Duty", "duty", "%"); break;
        case kParamPanRate:
            set(p, "Pan rate", "panrate", "Hz");
            p.hints |= kParameterIsLogarithmic;
            break;
        case kParamPanDepth:  set(p, "Pan depth", "pandepth", "%"); break;
        case kParamCross:     set(p, "Cross", "cross", "%"); break;
        case kParamPredelay:  set(p, "Pre-delay", "predelay", "ms"); break;
        case kParamAttack:    set(p, "Attack", "attack", "%"); break;
        case kParamRelease:   set(p, "Release", "release", "%"); break;
        }
    }

    void initProgramName(const uint32_t index, String& name) override
    {
        name = kPadifierPresets[index].name;
    }

    // state of the interface (A/B comparison), saved with the project; nothing for the DSP
    void initState(const uint32_t index, State& state) override
    {
        if (index >= static_cast<uint32_t>(kStateCount))
            return;
        state.key = kStateKeys[index];
        state.defaultValue = "";
        state.label = "A/B comparison";
        state.hints = kStateIsOnlyForUI;
    }

    void setState(const char* const key, const char* const value) override
    {
        for (int i = 0; i < kStateCount; ++i)
            if (std::strcmp(key, kStateKeys[i]) == 0)
                fStates[i] = value;
    }

    String getState(const char* const key) const override
    {
        for (int i = 0; i < kStateCount; ++i)
            if (std::strcmp(key, kStateKeys[i]) == 0)
                return fStates[i];
        return String();
    }

    void loadProgram(const uint32_t index) override
    {
        if (index >= static_cast<uint32_t>(kPadifierPresetCount))
            return;
        for (int i = 0; i < kPresetParamCount; ++i)
            if (isPresetParam(i))
                setParameterValue(i, kPadifierPresets[index].values[i]);
    }

    float getParameterValue(const uint32_t index) const override
    {
        return fParams[index];
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= static_cast<uint32_t>(kParamFirstOutput))
            return;
        const bool changed = fParams[index] != value;
        fParams[index] = value;
        if (changed && isIRParam(static_cast<int>(index)))
        {
            fIRParams[index].store(value, std::memory_order_relaxed);
            fIRRequest.fetch_add(1, std::memory_order_release);
        }
    }

    void activate() override
    {
        if (isDummyInstance())
            return;
        {
            std::lock_guard<std::mutex> lock(fBuildMutex);
            fEngine.reset();
            buildNow();
        }
        resetState();
    }

    void sampleRateChanged(double) override
    {
        if (isDummyInstance())
            return;
        allocate();
        resetState();
    }

    void run(const float** inputs, float** outputs, const uint32_t frames) override
    {
        const ScopedNoDenormals noDenormals;
        if (frames == 0)
            return;
        setTargets(true);
        readTempo();

        float peakIn = 0.0f, peakWet = 0.0f, peakOut = 0.0f;
        float wet[2][kChunk], dry[2][kChunk];
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = static_cast<int>(std::min<uint32_t>(frames - done, kChunk));
            const float* const in[2] = { inputs[0] + done, inputs[1] + done };
            float* const w[2] = { wet[0], wet[1] };
            float* const d[2] = { dry[0], dry[1] };
            fEngine.process(in, w, d, n);
            preDelay(w, n);
            float* const outL = outputs[0] + done;
            float* const outR = outputs[1] + done;

            for (int i = 0; i < n; ++i)
            {
                peakIn = std::fmax(peakIn, std::fmax(std::fabs(in[0][i]), std::fabs(in[1][i])));
                float wl = wet[0][i], wr = wet[1][i];
                const float sl = dry[0][i], sr = dry[1][i];
                if (!fLowCut.identity())
                    fLowCut.process(wl, wr);
                if (!fHighCut.identity())
                    fHighCut.process(wl, wr);
                if (fWidth.moving() || fWidth.cur != 1.0f)
                {
                    const float wd = fWidth.next();
                    const float m = (wl + wr) * 0.5f, s = (wl - wr) * 0.5f * wd;
                    wl = m + s;
                    wr = m - s;
                }
                peakWet = std::fmax(peakWet, std::fmax(std::fabs(wl), std::fabs(wr)));

                const float wg = fWet.next(), dg = fDry.next(), g = fGain.next();
                float yl = (wg * wl + dg * sl) * g;
                float yr = (wg * wr + dg * sr) * g;

                // bypass: the (delayed) input, crossfaded
                const float b = fBypass.next();
                if (b == 1.0f)
                {
                    yl = sl;
                    yr = sr;
                }
                else if (b != 0.0f)
                {
                    yl += (sl - yl) * b;
                    yr += (sr - yr) * b;
                }
                outL[i] = yl;
                outR[i] = yr;
                peakOut = std::fmax(peakOut, std::fmax(std::fabs(yl), std::fabs(yr)));
            }
            done += static_cast<uint32_t>(n);
        }

        const float fall = std::exp(-static_cast<float>(frames) / (0.25f * static_cast<float>(getSampleRate())));
        fMeterIn = std::fmax(peakIn, fMeterIn * fall);
        fMeterWet = std::fmax(peakWet, fMeterWet * fall);
        fMeterOut = std::fmax(peakOut, fMeterOut * fall);
        fParams[kParamMeterIn] = gainToDb(fMeterIn, -60.0f);
        fParams[kParamMeterWet] = gainToDb(fMeterWet, -60.0f);
        fParams[kParamMeterOut] = gainToDb(fMeterOut, -60.0f);
    }

private:
    static const int kChunk = 256;

    static void set(Parameter& p, const char* name, const char* symbol, const char* unit)
    {
        p.name = name;
        p.symbol = symbol;
        p.unit = unit;
    }

    static void enumeration(Parameter& p, const char* const* names, const int count)
    {
        p.hints |= kParameterIsInteger;
        ParameterEnumerationValue* const v = new ParameterEnumerationValue[count];
        for (int i = 0; i < count; ++i)
        {
            v[i].value = static_cast<float>(i);
            v[i].label = names[i];
        }
        p.enumValues.count = static_cast<uint8_t>(count);
        p.enumValues.restrictedMode = true;
        p.enumValues.values = v;
    }

    // tempo of the host (the last one known when it gives none): a change rebuilds the response
    // while the rhythm is on
    void readTempo()
    {
        const TimePosition& tp = getTimePosition();
        if (tp.bbt.valid && tp.bbt.beatsPerMinute > 0.0)
        {
            const float bpm = static_cast<float>(std::fmin(std::fmax(tp.bbt.beatsPerMinute, 20.0), 300.0));
            if (bpm != fParams[kParamTempo])
            {
                fParams[kParamTempo] = bpm;
                fTempo.store(bpm, std::memory_order_relaxed);
                if (PadifierLaw::choice(fParams[kParamRhythm], kDivisionCount) > 0)
                    fIRRequest.fetch_add(1, std::memory_order_release);
            }
        }
    }

    // pre-delay of the wet signal: integer delay; a change crossfades from the old delay to the
    // new one in 30 ms, the next change waits for the end of the crossfade
    void preDelay(float* const w[2], const int n)
    {
        const unsigned target = std::min(static_cast<unsigned>(std::lround(fParams[kParamPredelay] * 1e-3 * getSampleRate())),
                                         fPreMask - 1);
        if (fPreFade == 0 && target != fPreCur)
        {
            fPreNext = target;
            fPreFadeLength = fPreFade = std::max(1, static_cast<int>(0.03 * getSampleRate()));
        }
        if (fPreCur == 0 && fPreFade == 0)
            return;   // no delay: nothing to do (the line keeps no state)
        for (int i = 0; i < n; ++i)
        {
            float t = 0.0f;
            if (fPreFade > 0)
                t = 1.0f - static_cast<float>(fPreFade) / fPreFadeLength;
            for (int c = 0; c < 2; ++c)
            {
                fPre[c][fPrePos] = w[c][i];
                float y = fPre[c][(fPrePos - fPreCur) & fPreMask];
                if (fPreFade > 0)
                    y += (fPre[c][(fPrePos - fPreNext) & fPreMask] - y) * t;
                w[c][i] = y;
            }
            if (fPreFade > 0 && --fPreFade == 0)
                fPreCur = fPreNext;
            fPrePos = (fPrePos + 1) & fPreMask;
        }
    }

    static void output(Parameter& p, const char* name, const char* symbol, const char* unit)
    {
        p.hints = kParameterIsOutput;
        p.name = name;
        p.symbol = symbol;
        p.unit = unit;
        p.ranges.min = -60.0f;
        p.ranges.max = 6.0f;
        p.ranges.def = -60.0f;
    }

    // buffers for the sample rate, and the response (not real-time)
    void allocate()
    {
        std::lock_guard<std::mutex> lock(fBuildMutex);
        const double sr = getSampleRate();
        fRate = sr;
        fEngine.discard();
        fSetups.reset(new PadifierConv::Setups(PadifierConv::kHeadBlock, PadifierConv::tailBlock(sr)));
        fEngine.configure(fSetups.get(), static_cast<int>(std::ceil(kMaxLength * sr)) + 1);
        unsigned size = 2;
        while (size < static_cast<unsigned>(1e-3 * kPadifierRanges[kParamPredelay].max * sr) + 2)
            size *= 2;
        for (int c = 0; c < 2; ++c)
            fPre[c].assign(size, 0.0f);
        fPreMask = size - 1;
        buildNow();
    }

    // the response of the current parameters, used at once (fBuildMutex held, not real-time)
    void buildNow()
    {
        fBuilt = PadifierIR::fromParams(fParams, fRate, fTempo.load(std::memory_order_relaxed));
        fBuiltRequest = fIRRequest.load(std::memory_order_acquire);
        delete fEngine.offer(nullptr);   // a response built before is outdated
        fEngine.setNow(build(fBuilt));
    }

    PadifierConv::IRSet* build(const PadifierIR::Settings& s) const
    {
        const PadifierIR::Response r = PadifierIR::generate(s);
        const float* hp[2][2];
        for (int i = 0; i < 2; ++i)
            for (int o = 0; o < 2; ++o)
                hp[i][o] = r.h[i][o].empty() ? nullptr : r.h[i][o].data();
        return new PadifierConv::IRSet(*fSetups, hp, r.length);
    }

    // thread of the plugin: builds the responses asked for, frees those no longer used
    void workerLoop()
    {
        std::unique_lock<std::mutex> wake(fWakeMutex);
        while (!fQuit)
        {
            wake.unlock();
            delete fEngine.takeRetired();
            {
                std::lock_guard<std::mutex> lock(fBuildMutex);
                const unsigned request = fIRRequest.load(std::memory_order_acquire);
                if (request != fBuiltRequest && fSetups)
                {
                    fBuiltRequest = request;
                    float p[kParamFirstOutput];
                    for (int i = 0; i < kParamFirstOutput; ++i)
                        p[i] = fIRParams[i].load(std::memory_order_relaxed);
                    const PadifierIR::Settings s = PadifierIR::fromParams(p, fRate, fTempo.load(std::memory_order_relaxed));
                    if (!(s == fBuilt))
                    {
                        fBuilt = s;
                        delete fEngine.offer(build(s));
                    }
                }
            }
            wake.lock();
            fWake.wait_for(wake, std::chrono::milliseconds(5), [this] { return fQuit; });
        }
    }

    void setTargets(const bool ramp)
    {
        const double sr = getSampleRate();
        const int n = ramp ? static_cast<int>(0.02 * sr) : 0;
        fWet.set(PadifierLaw::wetGain(fParams[kParamMix]), n);
        fDry.set(PadifierLaw::dryGain(fParams[kParamMix]), n);
        fWidth.set(fParams[kParamWidth] * 0.01f, n);
        fGain.set(fParams[kParamOutput] == 0.0f ? 1.0f : dbToGain(fParams[kParamOutput]), n);
        fBypass.set(fParams[kParamBypass] > 0.5f ? 1.0f : 0.0f, ramp ? static_cast<int>(0.01 * sr) : 0);
        fLowCut.set(fParams[kParamLowCut], sr, n);
        fHighCut.set(fParams[kParamHighCut], sr, n);
    }

    void resetState()
    {
        fWet.reset(0.0f);
        fDry.reset(0.0f);
        fWidth.reset(0.0f);
        fGain.reset(0.0f);
        fBypass.reset(0.0f);
        fLowCut.reset(fParams[kParamLowCut], getSampleRate());
        fHighCut.reset(fParams[kParamHighCut], getSampleRate());
        setTargets(false);
        fMeterIn = fMeterWet = fMeterOut = 0.0f;
        for (int c = 0; c < 2; ++c)
            std::fill(fPre[c].begin(), fPre[c].end(), 0.0f);
        fPrePos = 0;
        fPreCur = fPreNext = std::min(static_cast<unsigned>(std::lround(fParams[kParamPredelay] * 1e-3 * getSampleRate())),
                                      fPreMask - 1);
        fPreFade = 0;
    }

    float fParams[kParamCount];
    String fStates[kStateCount];   // strings of the interface, only kept for the host

    // the response: parameters for the thread, requests counted by the audio side
    std::atomic<float> fIRParams[kParamFirstOutput];   // all the inputs (those of the response used)
    std::atomic<float> fTempo { kDefaultTempo };        // tempo of the host, BPM
    std::atomic<unsigned> fIRRequest { 0 };
    std::mutex fBuildMutex;        // held while a response is built or the buffers change
    unsigned fBuiltRequest = 0;    // under fBuildMutex
    PadifierIR::Settings fBuilt;       // under fBuildMutex
    double fRate = 48000.0;        // sample rate of the buffers, under fBuildMutex
    std::unique_ptr<PadifierConv::Setups> fSetups;
    PadifierConv::Engine fEngine;
    std::thread fWorker;
    std::mutex fWakeMutex;
    std::condition_variable fWake;
    bool fQuit = false;            // under fWakeMutex

    Ramp<float> fWet, fDry, fWidth, fGain, fBypass;
    Butterworth fLowCut, fHighCut;
    float fMeterIn = 0.0f, fMeterWet = 0.0f, fMeterOut = 0.0f;
    std::vector<float> fPre[2];    // pre-delay lines
    unsigned fPreMask = 1, fPrePos = 0, fPreCur = 0, fPreNext = 0;
    int fPreFade = 0, fPreFadeLength = 1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadifierPlugin)
};

Plugin* createPlugin()
{
    return new PadifierPlugin();
}

END_NAMESPACE_DISTRHO
