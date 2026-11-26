/*
 * Noise Padifier - DSP tests (native build, see ../build.sh; built twice: strict options, and the
 * options DPF gives the plugin, -O3 -ffast-math).
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 1. Convolver: output == direct convolution (double) delayed by 256 samples, for responses
 *    shorter than a block up to several tail partitions, at 48, 96 and 192 kHz (the three tail
 *    block sizes), odd host block sizes; the dry output is the input delayed exactly.  With
 *    cross responses (true stereo): each output == the sum of both inputs convolved.
 * 2. Change of response: old response before the crossfade, (1 - g) old + g new during it
 *    (g linear over M samples, at the place given by the head of Convolver.hpp), the new one
 *    after; the old set comes back to be freed.
 * 3. Plugin: latency 256; mix 0 and bypass: the input delayed, exactly; mix 100 % with the
 *    filters off == the convolution with the responses of the parameters, also with Cross; a
 *    change of length rebuilt by the thread and crossfaded in; the tempo of the host rebuilds
 *    the response of the rhythm; pre-delay: the wet signal delayed exactly; low cut, high cut;
 *    width 0: L == R; presets finite; meters; FPU state of the caller restored.
 * 4. Cost (printed, not checked): time per second of audio.
 * The impulse responses themselves are tested by tests/test_ir.cpp.
 */
#include "src/DistrhoPluginInternal.hpp"
#include "src/DistrhoPlugin.cpp"
#include "src/DistrhoUtils.cpp"
#include "../plugin/PadifierPlugin.cpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

USE_NAMESPACE_DISTRHO

static int gFailures = 0;

static void check(const bool ok, const char* what)
{
    gFailures += !ok;
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static const int kSizes[] = { 1, 7, 64, 333, 512 };
static const int kLatency = PadifierConv::kHeadBlock;

static std::vector<float> randomSignal(const size_t n, const unsigned seed)
{
    std::mt19937 g(seed);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::vector<float> x(n);
    for (float& v : x)
        v = u(g);
    return x;
}

// y(t) = sum h(k) x(t - k), in double, at sample t
static double direct(const std::vector<float>& x, const std::vector<float>& h, const size_t t)
{
    double y = 0.0;
    const size_t kmax = std::min(h.size() - 1, t);
    for (size_t k = 0; k <= kmax; ++k)
        y += static_cast<double>(h[k]) * x[t - k];
    return y;
}

static double peakAbs(const std::vector<float>& v)
{
    double p = 0.0;
    for (const float x : v)
        p = std::max(p, static_cast<double>(std::fabs(x)));
    return p;
}

// Runs an engine over x (both channels), odd block sizes; offers `next` at input sample offerAt.
struct EngineRun {
    std::vector<float> wet[2], dry[2];
    PadifierConv::IRSet* retired = nullptr;
};

// h[c]: direct responses; cross (optional): cross[0] right -> left, cross[1] left -> right
static PadifierConv::IRSet* makeSet(const PadifierConv::Setups& setups, const std::vector<float> h[2],
                                    const std::vector<float>* cross = nullptr)
{
    const float* hp[2][2] = { { h[0].data(), cross ? cross[1].data() : nullptr },
                              { cross ? cross[0].data() : nullptr, h[1].data() } };
    return new PadifierConv::IRSet(setups, hp, static_cast<int>(h[0].size()));
}

static EngineRun runEngine(const double fs, const std::vector<float> h[2], const std::vector<float> x[2],
                           const std::vector<float>* h2 = nullptr, const size_t offerAt = 0,
                           const std::vector<float>* cross = nullptr)
{
    PadifierConv::Setups setups(PadifierConv::kHeadBlock, PadifierConv::tailBlock(fs));
    PadifierConv::Engine e;
    size_t maxLen = h[0].size();
    if (h2)
        maxLen = std::max(maxLen, h2[0].size());
    e.configure(&setups, static_cast<int>(maxLen));
    e.setNow(makeSet(setups, h, cross));
    EngineRun r;
    const size_t N = x[0].size();
    for (int c = 0; c < 2; ++c)
    {
        r.wet[c].assign(N, 0.0f);
        r.dry[c].assign(N, 0.0f);
    }
    size_t n = 0, k = 0;
    bool offered = false;
    while (n < N)
    {
        if (h2 && !offered && n >= offerAt)
        {
            e.offer(makeSet(setups, h2));
            offered = true;
        }
        size_t f = std::min<size_t>(kSizes[k++ % 5], N - n);
        if (h2 && !offered)
            f = std::min(f, offerAt - n);
        const float* in[2] = { &x[0][n], &x[1][n] };
        float* w[2] = { &r.wet[0][n], &r.wet[1][n] };
        float* d[2] = { &r.dry[0][n], &r.dry[1][n] };
        e.process(in, w, d, static_cast<int>(f));
        n += f;
    }
    if (h2)
    {
        r.retired = e.takeRetired();
        check(!e.crossfading(), "crossfade finished");
        check(r.retired != nullptr && r.retired->length == static_cast<int>(h[0].size()), "old response handed back");
        delete r.retired;
    }
    e.discard();
    return r;
}

// largest |wet(t + latency) - expected(t)| over sampled t, relative to the peak of expected
template <typename F>
static double convolutionError(const std::vector<float>& wet, const size_t N, F expected, double& peak)
{
    double err = 0.0;
    peak = 0.0;
    const size_t stride = N > 60000 ? 101 : 3;   // long cases: every 101st sample (direct convolution is slow)
    for (size_t t = 0; t + kLatency < N; t += stride)
    {
        const double y = expected(t);
        peak = std::max(peak, std::fabs(y));
        err = std::max(err, std::fabs(wet[t + kLatency] - y));
    }
    return err / std::max(peak, 1e-30);
}

static void testConvolver()
{
    std::printf("1. Convolver against the direct convolution\n");
    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    for (const double fs : rates)
    {
        const int M = PadifierConv::tailBlock(fs), B = PadifierConv::kHeadBlock, M2 = PadifierConv::kStageRatio * M;
        const int lengths[] = { 1, 100, B, B + 1, 2 * M - 1, 2 * M, 2 * M + 1, 3 * M + 17, 2 * M2 - 1, 2 * M2, 2 * M2 + 1,
                                4 * M2 + 7 };
        double worst = 0.0;
        bool dryExact = true;
        for (const int L : lengths)
        {
            std::vector<float> h[2] = { randomSignal(L, 11 + L), randomSignal(L, 23 + L) };
            const size_t N = static_cast<size_t>(L) + 3 * M2 + 1000;
            std::vector<float> x[2] = { randomSignal(N, 5), randomSignal(N, 6) };
            const EngineRun r = runEngine(fs, h, x);
            for (int c = 0; c < 2; ++c)
            {
                double peak;
                const double e = convolutionError(r.wet[c], N, [&](size_t t) { return direct(x[c], h[c], t); }, peak);
                worst = std::max(worst, e);
                for (size_t t = 0; t < N; ++t)
                    dryExact = dryExact && r.dry[c][t] == (t >= static_cast<size_t>(kLatency) ? x[c][t - kLatency] : 0.0f);
            }
        }
        char what[160];
        std::snprintf(what, sizeof(what), "%.0f kHz (M1 = %d, M2 = %d), 12 lengths 1 .. 4 M2 + 7: largest error %.2e of the peak (< 1e-5)",
                      fs / 1000.0, M, M2, worst);
        check(worst < 1e-5, what);
        std::snprintf(what, sizeof(what), "%.0f kHz: dry = input delayed by %d samples, exactly", fs / 1000.0, kLatency);
        check(dryExact, what);
    }
}

static void testCross()
{
    for (const double fs : { 48000.0, 96000.0 })
    {
        const int M = PadifierConv::tailBlock(fs);
        const int L = 3 * M + 17;
        std::vector<float> h[2] = { randomSignal(L, 41), randomSignal(L, 42) };
        std::vector<float> cross[2] = { randomSignal(L, 43), randomSignal(L, 44) };
        const size_t N = static_cast<size_t>(L) + 3 * M + 1000;
        std::vector<float> x[2] = { randomSignal(N, 45), randomSignal(N, 46) };
        const EngineRun r = runEngine(fs, h, x, nullptr, 0, cross);
        double worst = 0.0, peak;
        for (int c = 0; c < 2; ++c)
            worst = std::max(worst, convolutionError(r.wet[c], N, [&](size_t t) {
                return direct(x[c], h[c], t) + direct(x[1 - c], cross[c], t);
            }, peak));
        char what[160];
        std::snprintf(what, sizeof(what), "%.0f kHz, cross responses: each output = both inputs convolved (error %.2e, < 1e-5)",
                      fs / 1000.0, worst);
        check(worst < 1e-5, what);
    }
}

static void testChange()
{
    std::printf("2. Change of response\n");
    const double fs = 48000.0;
    const int M = PadifierConv::tailBlock(fs), B = PadifierConv::kHeadBlock, R2 = PadifierConv::kStageRatio * M / B;
    const int M2 = PadifierConv::kStageRatio * M;
    const int L1 = 3 * M2 + 100, L2 = 2 * M2 + 500;   // both use the three stages
    std::vector<float> h1[2] = { randomSignal(L1, 1), randomSignal(L1, 2) };
    std::vector<float> h2[2] = { randomSignal(L2, 3), randomSignal(L2, 4) };
    const size_t N = 10 * static_cast<size_t>(M2);
    std::vector<float> x[2] = { randomSignal(N, 7), randomSignal(N, 8) };
    const size_t offerAt = 3 * static_cast<size_t>(M) + 1234;
    const EngineRun r = runEngine(fs, h1, x, h2, offerAt);

    // frame A: the first frame k with k = 0 mod R2 whose call comes after the offer ((k+1)B > offerAt);
    // the crossfade runs over [A + R2, A + R2 + R1) frames, i.e. M1 samples
    size_t A = 0;
    while (!((A + 1) * B > offerAt && A % R2 == 0))
        ++A;
    const size_t t0 = (A + R2) * B, t1 = t0 + M;
    double worst[3] = { 0, 0, 0 };
    for (int c = 0; c < 2; ++c)
    {
        double peak;
        const double e = convolutionError(r.wet[c], N, [&](size_t t) {
            const double y1 = direct(x[c], h1[c], t), y2 = direct(x[c], h2[c], t);
            if (t < t0)
                return y1;
            if (t >= t1)
                return y2;
            const double g = static_cast<double>(t - t0) / M;
            return y1 + g * (y2 - y1);
        }, peak);
        worst[0] = std::max(worst[0], e);
    }
    char what[200];
    std::snprintf(what, sizeof(what), "offer at sample %zu: old up to %zu, crossfade over %d samples, new after "
                  "(largest error %.2e of the peak)", offerAt, t0, M, worst[0]);
    check(worst[0] < 1e-5, what);
}

// ---- the plugin

struct Change {
    size_t at;
    int param;
    float value;
};

static void processPlugin(const float* values, const double sr, const std::vector<float>& L, const std::vector<float>& R,
                          std::vector<float>& oL, std::vector<float>& oR, const std::vector<Change>& changes = {},
                          float* outs = nullptr, uint32_t* latency = nullptr, const double tempo = 0.0)
{
    d_nextSampleRate = sr;
    d_nextBufferSize = 512;
    PluginExporter p(nullptr, nullptr, nullptr, nullptr);
    for (int i = 0; i < kPresetParamCount; ++i)
        p.setParameterValue(i, values[i]);
    p.activate();
    if (latency)
        *latency = p.getLatency();

    const size_t N = L.size();
    oL.assign(N, 0.0f);
    oR.assign(N, 0.0f);
    size_t n = 0, k = 0, c = 0;
    while (n < N)
    {
        while (c < changes.size() && changes[c].at <= n)
        {
            p.setParameterValue(changes[c].param, changes[c].value);
            // the thread rebuilds the response: give it time, as real time would
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            ++c;
        }
        size_t f = std::min<size_t>(kSizes[k++ % 5], N - n);
        if (c < changes.size())
            f = std::min(f, changes[c].at - n);
        const float* in[2] = { &L[n], &R[n] };
        float* out[2] = { &oL[n], &oR[n] };
        if (tempo > 0.0)
        {
            TimePosition tp;
            tp.bbt.valid = true;
            tp.bbt.beatsPerMinute = tempo;
            p.setTimePosition(tp);
        }
        p.run(in, out, static_cast<uint32_t>(f));
        if (tempo > 0.0 && n == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(300));   // the thread rebuilds
        n += f;
    }
    if (outs)
        for (int i = kParamFirstOutput; i < kParamCount; ++i)
            outs[i] = p.getParameterValue(i);
    p.deactivate();
}

static void neutral(float* v)
{
    for (int i = 0; i < kPresetParamCount; ++i)
        v[i] = kPadifierRanges[i].def;
    v[kParamLength] = 0.5f;
    v[kParamMix] = 100.0f;
}

static double tonePower(const std::vector<float>& y, const size_t a, const size_t b)
{
    double q = 0.0;
    for (size_t i = a; i < b; ++i)
        q += static_cast<double>(y[i]) * y[i];
    return q / (b - a);
}

static std::vector<float> tone(const double hz, const double fs, const size_t n)
{
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * hz * i / fs));
    return x;
}

static void testPlugin()
{
    std::printf("3. Plugin\n");
    const double fs = 48000.0;
    const size_t N = 2 * 48000;
    std::vector<float> L = randomSignal(N, 31), R = randomSignal(N, 32), oL, oR;
    for (float& v : L)
        v *= 0.25f;
    for (float& v : R)
        v *= 0.25f;
    float v[kPresetParamCount];

    neutral(v);
    v[kParamMix] = 0.0f;
    uint32_t latency = 0;
    processPlugin(v, fs, L, R, oL, oR, {}, nullptr, &latency);
    bool exact = true;
    for (size_t t = 0; t < N; ++t)
        exact = exact && oL[t] == (t >= 256 ? L[t - 256] : 0.0f) && oR[t] == (t >= 256 ? R[t - 256] : 0.0f);
    check(latency == 256, "latency reported: 256 samples");
    check(exact, "mix 0 %: the input delayed by 256 samples, exactly");

    neutral(v);
    {
        d_nextSampleRate = fs;
        d_nextBufferSize = 512;
        PluginExporter p(nullptr, nullptr, nullptr, nullptr);
        for (int i = 0; i < kPresetParamCount; ++i)
            p.setParameterValue(i, v[i]);
        p.setParameterValue(kParamBypass, 1.0f);
        p.activate();
        oL.assign(N, 0.0f);
        oR.assign(N, 0.0f);
        for (size_t n = 0; n < N; n += 500)
        {
            const uint32_t f = static_cast<uint32_t>(std::min<size_t>(500, N - n));
            const float* in[2] = { &L[n], &R[n] };
            float* out[2] = { &oL[n], &oR[n] };
            p.run(in, out, f);
        }
        exact = true;
        for (size_t t = 0; t < N; ++t)
            exact = exact && oL[t] == (t >= 256 ? L[t - 256] : 0.0f) && oR[t] == (t >= 256 ? R[t - 256] : 0.0f);
        check(exact, "bypass: the input delayed by 256 samples, exactly");
    }

    // mix 100 %, filters off: the convolution with the response of the parameters
    neutral(v);
    v[kParamLength] = 0.5f;
    processPlugin(v, fs, L, R, oL, oR);
    PadifierIR::Settings s = PadifierIR::fromParams(v, fs);
    PadifierIR::Response ir = PadifierIR::generate(s);
    double peak;
    double e = std::max(convolutionError(oL, N, [&](size_t t) { return direct(L, ir.h[0][0], t); }, peak),
                        convolutionError(oR, N, [&](size_t t) { return direct(R, ir.h[1][1], t); }, peak));
    char what[200];
    std::snprintf(what, sizeof(what), "mix 100 %%: the convolution with the noise response (error %.2e of the peak)", e);
    check(e < 1e-5, what);

    // with Cross and treatments: each output = both inputs convolved with the responses
    float vc[kPresetParamCount];
    std::copy(v, v + kPresetParamCount, vc);
    vc[kParamCross] = 70.0f;
    vc[kParamTune] = 2.0f;
    vc[kParamDamping] = 4.0f;
    vc[kParamPanDepth] = 50.0f;
    processPlugin(vc, fs, L, R, oL, oR);
    const PadifierIR::Response irc = PadifierIR::generate(PadifierIR::fromParams(vc, fs));
    e = std::max(convolutionError(oL, N, [&](size_t t) { return direct(L, irc.h[0][0], t) + direct(R, irc.h[1][0], t); }, peak),
                 convolutionError(oR, N, [&](size_t t) { return direct(R, irc.h[1][1], t) + direct(L, irc.h[0][1], t); }, peak));
    std::snprintf(what, sizeof(what), "cross 70 %%, chord, damping, motion: both inputs convolved (error %.2e of the peak)", e);
    check(irc.cross && e < 1e-5, what);

    // rhythm on, host at 90 BPM (the response was built at 120 by activate): rebuilt for 90
    vc[kParamCross] = 0.0f;
    vc[kParamTune] = 0.0f;
    vc[kParamRhythm] = 4.0f;   // 1/8
    float outs[kParamCount];
    processPlugin(vc, fs, L, R, oL, oR, {}, outs, nullptr, 90.0);
    const PadifierIR::Response irt = PadifierIR::generate(PadifierIR::fromParams(vc, fs, 90.0));
    double err = 0.0, pk = 0.0;
    for (size_t t = 48000; t + 256 < N; t += 3)
    {
        const double y = direct(L, irt.h[0][0], t);
        pk = std::max(pk, std::fabs(y));
        err = std::max(err, std::fabs(oL[t + 256] - y));
    }
    std::snprintf(what, sizeof(what), "rhythm 1/8, host at 90 BPM: rebuilt for 90 (error %.2e of the peak), tempo shown %.0f",
                  err / pk, outs[kParamTempo]);
    check(err / pk < 1e-5 && outs[kParamTempo] == 90.0f, what);

    // pre-delay 10 ms: the wet signal 480 samples later, exactly (set before the start)
    neutral(vc);
    std::vector<float> pL, pR;
    processPlugin(vc, fs, L, R, pL, pR);
    vc[kParamPredelay] = 10.0f;
    processPlugin(vc, fs, L, R, oL, oR);
    bool shifted = true;
    for (size_t t = 0; t < N; ++t)
        shifted = shifted && oL[t] == (t >= 480 ? pL[t - 480] : 0.0f) && oR[t] == (t >= 480 ? pR[t - 480] : 0.0f);
    check(shifted, "pre-delay 10 ms: the pad 480 samples later, exactly");

    // change of length at 0.5 s, rebuilt by the thread: the new response 1 s later
    PadifierIR::Settings s2 = s;
    s2.length = 0.3;
    const PadifierIR::Response ir2 = PadifierIR::generate(s2);
    processPlugin(v, fs, L, R, oL, oR, { { 24000, kParamLength, 0.3f } });
    err = 0.0;
    pk = 0.0;
    for (size_t t = 72000; t + 256 < N; t += 3)
    {
        const double y = direct(L, ir2.h[0][0], t);
        pk = std::max(pk, std::fabs(y));
        err = std::max(err, std::fabs(oL[t + 256] - y));
    }
    std::snprintf(what, sizeof(what), "length changed to 0.3 s: the new response 1 s later (error %.2e of the peak)",
                  err / pk);
    check(err / pk < 1e-5, what);
    // largest step from one sample to the next around the change, against the last second
    const auto maxStep = [&](const size_t a, const size_t b) {
        double m = 0.0;
        for (size_t t = a + 1; t < b; ++t)
            m = std::max(m, static_cast<double>(std::fabs(oL[t] - oL[t - 1])));
        return m;
    };
    const double around = maxStep(24000, 72000), after = maxStep(72000, N);
    std::snprintf(what, sizeof(what), "no click at the change: largest step %.3f, %.3f in the last second (ratio < 1.5)",
                  around, after);
    check(around < 1.5 * after, what);

    // low cut 500 Hz on a 50 Hz tone, high cut 1 kHz on a 10 kHz tone: wet power down >= 30 dB
    const std::vector<float> t50 = tone(50.0, fs, N), t10k = tone(10000.0, fs, N);
    neutral(v);
    processPlugin(v, fs, t50, t50, oL, oR);
    const double p0 = tonePower(oL, N / 2, N);
    v[kParamLowCut] = 500.0f;
    processPlugin(v, fs, t50, t50, oL, oR);
    const double p1 = tonePower(oL, N / 2, N);
    neutral(v);
    processPlugin(v, fs, t10k, t10k, oL, oR);
    const double p2 = tonePower(oL, N / 2, N);
    v[kParamHighCut] = 1000.0f;
    processPlugin(v, fs, t10k, t10k, oL, oR);
    const double p3 = tonePower(oL, N / 2, N);
    std::snprintf(what, sizeof(what), "low cut 500 Hz: 50 Hz down %.1f dB; high cut 1 kHz: 10 kHz down %.1f dB (>= 30)",
                  10.0 * std::log10(p0 / p1), 10.0 * std::log10(p2 / p3));
    check(10.0 * std::log10(p0 / p1) >= 30.0 && 10.0 * std::log10(p2 / p3) >= 30.0, what);

    // width 0: mono wet
    neutral(v);
    v[kParamWidth] = 0.0f;
    processPlugin(v, fs, L, R, oL, oR);
    bool mono = true;
    for (size_t t = 0; t < N; ++t)
        mono = mono && oL[t] == oR[t];
    check(mono, "width 0 %: L == R");

    // presets: finite output, sensible peak for noise at -12 dBFS; meters
    bool finite = true;
    double worstPeak = 0.0;
    for (int i = 0; i < kPadifierPresetCount; ++i)
    {
        float outs[kParamCount];
        processPlugin(kPadifierPresets[i].values, fs, L, R, oL, oR, {}, outs);
        for (size_t t = 0; t < N; ++t)
            finite = finite && std::isfinite(oL[t]) && std::isfinite(oR[t]);
        worstPeak = std::max(worstPeak, std::max(peakAbs(oL), peakAbs(oR)));
        finite = finite && outs[kParamMeterIn] > -14.0f && outs[kParamMeterIn] < -10.0f;
    }
    std::snprintf(what, sizeof(what), "%d presets: finite output, input meter about -12 dBFS, largest peak %.2f (< 1)",
                  kPadifierPresetCount, worstPeak);
    check(finite && worstPeak < 1.0, what);

    // FPU state of the caller
#if defined(__SSE__) || defined(_M_X64)
    const unsigned before = _mm_getcsr();
    neutral(v);
    processPlugin(v, fs, L, R, oL, oR);
    check(_mm_getcsr() == before, "FPU state (MXCSR) of the caller restored");
#endif
}

static void benchmark()
{
    std::printf("4. Cost (one core, per second of stereo audio, blocks of 256)\n");
    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    for (const double fs : rates)
        for (const float length : { 3.0f, 10.0f, -10.0f, 30.0f, -30.0f })   // negative: with Cross
        {
            float v[kPresetParamCount];
            neutral(v);
            v[kParamLength] = std::fabs(length);
            v[kParamCross] = length < 0.0f ? 100.0f : 0.0f;
            d_nextSampleRate = fs;
            d_nextBufferSize = 512;
            PluginExporter p(nullptr, nullptr, nullptr, nullptr);
            for (int i = 0; i < kPresetParamCount; ++i)
                p.setParameterValue(i, v[i]);
            p.activate();
            const size_t N = static_cast<size_t>(5 * fs);
            std::vector<float> L = randomSignal(N, 1), R = randomSignal(N, 2), oL(N), oR(N);
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t n = 0; n + 256 <= N; n += 256)
            {
                const float* in[2] = { &L[n], &R[n] };
                float* out[2] = { &oL[n], &oR[n] };
                p.run(in, out, 256);
            }
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("  %3.0f kHz, %4.1f s%s: %.1f ms per second of audio (%.2f %% of a core)\n", fs / 1000.0, std::fabs(length),
                        length < 0.0f ? ", cross" : "       ", 1000.0 * s / 5.0, 100.0 * s / 5.0);
            p.deactivate();
        }
}

int main(int argc, char** argv)
{
    testConvolver();
    testCross();
    testChange();
    testPlugin();
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0)
        benchmark();
    std::printf("%s (%d failure%s)\n", gFailures == 0 ? "all DSP tests passed" : "DSP TESTS FAILED", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
