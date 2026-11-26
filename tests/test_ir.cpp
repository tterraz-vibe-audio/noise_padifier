/*
 * Noise Padifier - tests of the impulse responses (plugin/NoiseIR.hpp): each treatment of the
 * noise is measured on the responses and compared with its law.
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 1. Defaults: unit energy, the same bits twice, left / right uncorrelated, no cross streams.
 * 2. Colour: power per octave (125 Hz - 16 kHz) follows the slope, -6 to +6 dB / octave.
 * 3. Density: velvet noise, one impulse per period fs / density.
 * 4. Comb and chord: peaks at the harmonics of the note, at the notes of the chord; LFO on the
 *    pitch: the peak moves by the depth.
 * 5. Damping, bloom, LFO on the tone: levels by band and window against the law.
 * 6. Targets and phases of the LFO: resonance, density, swept low cut and high cut against
 *    their laws; a phase of 90 or 180 degrees moves the motion; settings without effect ignored.
 * 7. Stereo motion and cross: gains of the outputs, energy kept.
 * 8. Cost (printed).
 */
#include "NoiseIR.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace PadifierIR;

static int gFailures = 0;

static void check(const bool ok, const char* what)
{
    gFailures += !ok;
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static Settings base(const double fs = 48000.0)
{
    float p[kParamFirstOutput];
    for (int i = 0; i < kParamFirstOutput; ++i)
        p[i] = kPadifierRanges[i].def;
    return fromParams(p, fs);
}

static double energy(const std::vector<float>& h, size_t a = 0, size_t b = 0)
{
    if (b == 0)
        b = h.size();
    double e = 0.0;
    for (size_t i = a; i < b; ++i)
        e += static_cast<double>(h[i]) * h[i];
    return e;
}

static double dot(const std::vector<float>& a, const std::vector<float>& b)
{
    double e = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        e += static_cast<double>(a[i]) * b[i];
    return e;
}

static double db(const double p) { return 10.0 * std::log10(std::fmax(p, 1e-30)); }

// power spectrum of h[a, a + n) (Hann window), bins of fs / nfft
static std::vector<double> spectrum(const std::vector<float>& h, const size_t a, const int n, const int nfft)
{
    Fft fft(nfft);
    for (int i = 0; i < nfft; ++i)
    {
        const double w = i < n ? 0.5 - 0.5 * std::cos(2.0 * M_PI * (i + 0.5) / n) : 0.0;
        fft.buf[i] = i < n && a + i < h.size() ? static_cast<float>(h[a + i] * w) : 0.0f;
    }
    fft.forward();
    std::vector<double> p(static_cast<size_t>(nfft / 2), 0.0);
    for (int k = 1; k < nfft / 2; ++k)
        p[k] = static_cast<double>(fft.buf[2 * k]) * fft.buf[2 * k] + static_cast<double>(fft.buf[2 * k + 1]) * fft.buf[2 * k + 1];
    return p;
}

static double band(const std::vector<double>& p, const double f0, const double f1, const double df)
{
    double q = 0.0;
    for (int k = std::max(1, static_cast<int>(f0 / df)); k < static_cast<int>(f1 / df) && k < static_cast<int>(p.size()); ++k)
        q += p[k];
    return q;
}

static void testDefaults()
{
    std::printf("1. Defaults\n");
    const Settings s = base();
    check(!spectralEnvelope(s) && s.tune == 0 && s.cross == 0.0, "defaults: no treatment on");
    const Response a = generate(s), b = generate(s);
    const double el = energy(a.h[0][0]), er = energy(a.h[1][1]);
    char what[160];
    std::snprintf(what, sizeof(what), "unit energy left %.7f, right %.7f; no cross streams", el, er);
    check(std::fabs(el - 1.0) < 1e-5 && std::fabs(er - 1.0) < 1e-5 && !a.cross && a.h[0][1].empty() && a.h[1][0].empty(), what);
    check(a.h[0][0] == b.h[0][0] && a.h[1][1] == b.h[1][1], "the same bits twice (parallel streams)");
    std::snprintf(what, sizeof(what), "left / right correlation %.4f (< 0.05)", dot(a.h[0][0], a.h[1][1]));
    check(std::fabs(dot(a.h[0][0], a.h[1][1])) < 0.05, what);
}

static void testColour()
{
    std::printf("2. Colour\n");
    Settings s = base();
    s.length = 6.0;
    s.swell = 0.0;
    s.decay = 0.0;
    double worst = 0.0;
    for (const double c : { -6.0, -3.0, 0.0, 3.0, 6.0 })
    {
        s.colour = c;
        const Response r = generate(s);
        const int nfft = 1 << 18;
        const std::vector<double> p = spectrum(r.h[0][0], 0, nfft, nfft);
        const double df = s.fs / nfft;
        double prev = 0.0;
        for (int o = 0; o < 7; ++o)
        {
            const double f0 = 125.0 * std::pow(2.0, o);
            const double q = db(band(p, f0, 2.0 * f0, df));
            if (o > 0)
                worst = std::max(worst, std::fabs(q - prev - (c + kDbPerOctave * 0.0) - 10.0 * std::log10(2.0)));
            prev = q;
        }
    }
    // power per octave of a slope c dB/octave (per bin) grows by c + 3.01 dB per octave
    char what[160];
    std::snprintf(what, sizeof(what), "-6 .. +6 dB/octave: power per octave within %.2f dB of the slope (< 1)", worst);
    check(worst < 1.0, what);
}

static void testEnvelope()
{
    std::printf("2b. Envelope: rise, hold, fall to the minimum\n");
    Settings s = base();
    s.length = 4.0;
    s.swell = 0.25;   // peak at 1 s
    s.decay = 0.5;    // fall from 2 s to 4 s
    const double e1 = envelope(s, 0.5), e2 = envelope(s, 1.5), e3 = 20.0 * std::log10(envelope(s, 3.0)),
                 e4 = 20.0 * std::log10(envelope(s, 3.97));
    char what[200];
    std::snprintf(what, sizeof(what), "swell 25 %%, decay 50 %%, 4 s: %.3f at 0.5 s (0.5), %.3f held at 1.5 s (1), %.1f dB at 3 s "
                  "(-30), %.1f dB at 3.97 s (-59.1, the fade starts at 3.98 s)", e1, e2, e3, e4);
    check(std::fabs(e1 - 0.5) < 1e-9 && e2 == 1.0 && std::fabs(e3 + 30.0) < 1e-9 && std::fabs(e4 + 59.1) < 1e-9, what);
    s.decay = 0.9;   // longer than after the peak: falls from the peak
    std::snprintf(what, sizeof(what), "decay 90 %% with the peak at 25 %%: falls from the peak (%.1f dB at 2.5 s, -30)",
                  20.0 * std::log10(envelope(s, 2.5)));
    check(std::fabs(20.0 * std::log10(envelope(s, 2.5)) + 30.0) < 1e-9, what);
    s.decay = 0.0;
    check(envelope(s, 3.9) == 1.0, "decay 0 %: held to the end (before the final fade)");
    // with damping, the highs lose more: end level of 8 kHz
    s.decay = 1.0;
    s.damping = 6.0;
    std::snprintf(what, sizeof(what), "damping 6 dB/oct: end level %.1f dB at 8 kHz (-90), %.1f dB at 200 Hz (-60)",
                  endDb(s, 8000.0), endDb(s, 200.0));
    check(std::fabs(endDb(s, 8000.0) + 90.0) < 1e-9 && endDb(s, 200.0) == -60.0, what);
}

static void testDensity()
{
    std::printf("3. Density\n");
    Settings s = base();
    s.colour = 0.0;
    s.swell = 0.0;
    s.decay = 0.0;
    s.density = 40.0;
    const Response r = generate(s);
    int nonzero = 0;
    for (const float v : r.h[0][0])
        nonzero += v != 0.0f;
    const int expected = static_cast<int>(std::ceil(r.length / (s.fs / s.density)));
    char what[160];
    std::snprintf(what, sizeof(what), "40 impulses / s over 3 s: %d impulses (%d cells)", nonzero, expected);
    check(std::abs(nonzero - expected) <= 1, what);
}

static std::vector<Response> draws(Settings s);

// frequency of the largest peak in [f0, f1], zero-padded spectra of a window, averaged over the draws
static double peakFrequency(const std::vector<Response>& r, const size_t a, const int n, const double fs, const double f0,
                            const double f1)
{
    const int nfft = 1 << 18;
    std::vector<double> p(static_cast<size_t>(nfft / 2), 0.0);
    for (const Response& x : r)
        for (int c = 0; c < 2; ++c)
        {
            const std::vector<double> q = spectrum(x.h[c][c], a, n, nfft);
            for (size_t k = 0; k < p.size(); ++k)
                p[k] += q[k];
        }
    const double df = fs / nfft;
    int best = static_cast<int>(f0 / df);
    for (int k = static_cast<int>(f0 / df); k < static_cast<int>(f1 / df); ++k)
        if (p[k] > p[best])
            best = k;
    return best * df;
}

static void testTune()
{
    std::printf("4. Comb, chord, LFO on the pitch\n");
    Settings s = base();
    s.colour = 0.0;
    s.swell = 0.0;
    s.decay = 0.0;
    s.tune = 1;
    s.note = 57.0;   // 220 Hz
    s.resonance = 60.0;
    Response r = generate(s);
    const int nfft = 1 << 18;
    std::vector<double> p = spectrum(r.h[0][0], 0, r.length, nfft);
    const double df = s.fs / nfft;
    double worst = 1e9;
    for (int k = 1; k <= 10; ++k)
    {
        const double on = band(p, 220.0 * k - 3.0, 220.0 * k + 3.0, df) / 6.0;
        const double off = band(p, 220.0 * (k + 0.5) - 3.0, 220.0 * (k + 0.5) + 3.0, df) / 6.0;
        worst = std::fmin(worst, db(on) - db(off));
    }
    char what[160];
    std::snprintf(what, sizeof(what), "comb on A3: harmonics 1-10 at least %.1f dB above the gaps between them (> 15)", worst);
    check(worst > 15.0, what);

    s.tune = 2;
    s.chord = 0;   // major: A3, C#4, E4
    r = generate(s);
    p = spectrum(r.h[0][0], 0, r.length, nfft);
    const double notesHz[3] = { 220.0, 277.18, 329.63 };
    const double gapsHz[3] = { 245.0, 300.0, 370.0 };
    worst = 1e9;
    for (int k = 0; k < 3; ++k)
        worst = std::fmin(worst, db(band(p, notesHz[k] - 2.0, notesHz[k] + 2.0, df)) - db(band(p, gapsHz[k] - 2.0, gapsHz[k] + 2.0, df)));
    std::snprintf(what, sizeof(what), "chord A major: A3, C#4, E4 at least %.1f dB above the gaps (> 15)", worst);
    check(worst > 15.0, what);

    s.tune = 1;
    s.length = 10.0;
    s.lfoRate = 0.1;   // period 10 s: highest at 2.5 s, lowest at 7.5 s
    s.lfoPitch = 1.0;
    s.resonance = 60.0;
    const std::vector<Response> v = draws(s);
    const int w = static_cast<int>(0.2 * s.fs);
    const double hi = peakFrequency(v, static_cast<size_t>(2.5 * s.fs) - w / 2, w, s.fs, 180.0, 260.0);
    const double lo = peakFrequency(v, static_cast<size_t>(7.5 * s.fs) - w / 2, w, s.fs, 180.0, 260.0);
    std::snprintf(what, sizeof(what), "LFO pitch +-1 semitone: peak %.1f Hz at 2.5 s, %.1f Hz at 7.5 s (233.1 and 207.7, +-2)", hi, lo);
    check(std::fabs(hi - 233.08) < 2.0 && std::fabs(lo - 207.65) < 2.0, what);
}

// responses of 8 seeds (both channels used): measurements on noise are averaged over 16 draws
static std::vector<Response> draws(Settings s)
{
    std::vector<Response> r;
    for (int seed = 1; seed <= 8; ++seed)
    {
        s.seed = seed;
        r.push_back(generate(s));
    }
    return r;
}

// level of a band in a window of 0.2 s centred on t, dB, averaged over the draws
static double windowBand(const std::vector<Response>& r, const double fs, const double t, const double f0, const double f1)
{
    const int n = static_cast<int>(0.2 * fs), nfft = 1 << 14;
    const size_t a = static_cast<size_t>(std::fmax(t * fs - n / 2, 0.0));
    double q = 0.0;
    for (const Response& x : r)
        for (int c = 0; c < 2; ++c)
            q += band(spectrum(x.h[c][c], a, n, nfft), f0, f1, fs / nfft);
    return db(q);
}

static void testSpectral()
{
    std::printf("5. Damping, bloom, LFO on the tone\n");
    Settings s = base();
    s.colour = 0.0;
    s.length = 4.0;
    s.swell = 0.0;
    s.decay = 0.0;
    s.damping = 6.0;
    std::vector<Response> r = draws(s);
    // law: at t, the level of f relative to t = 0 is -damping log2(f / 250) t / L (above 250 Hz)
    double worst = 0.0;
    for (const double t : { 1.0, 2.0, 3.0 })
        for (const double f : { 1000.0, 4000.0, 8000.0 })
        {
            const double measured = (windowBand(r, s.fs, t, f / 1.2, f * 1.2) - windowBand(r, s.fs, t, 100.0, 200.0))
                                  - (windowBand(r, s.fs, 0.2, f / 1.2, f * 1.2) - windowBand(r, s.fs, 0.2, 100.0, 200.0));
            const double law = -s.damping * std::log2(f / 250.0) * (t - 0.2) / s.length;
            worst = std::fmax(worst, std::fabs(measured - law));
            if (std::getenv("VERBOSE"))
                std::printf("    t %.1f f %.0f: measured %.1f law %.1f\n", t, f, measured, law);
        }
    char what[160];
    std::snprintf(what, sizeof(what), "damping 6 dB/octave: highs against 100-200 Hz within %.1f dB of the law (< 1.5)", worst);
    check(worst < 1.5, what);

    s.damping = 0.0;
    s.swell = 0.5;
    s.decay = 1.0;
    s.bloom = 1.0;
    r = draws(s);
    // peak of each band: Ta(f) = 2 s + 2 s u(f)
    double peakLow = 0.0, peakHigh = 0.0, best[2] = { -1e9, -1e9 };
    for (double t = 0.1; t < 3.95; t += 0.05)
    {
        const double low = windowBand(r, s.fs, t, 110.0, 140.0), high = windowBand(r, s.fs, t, 7000.0, 9000.0);
        if (low > best[0]) { best[0] = low; peakLow = t; }
        if (high > best[1]) { best[1] = high; peakHigh = t; }
    }
    const double lawLow = peakTime(s, 125.0), lawHigh = peakTime(s, 8000.0);
    std::snprintf(what, sizeof(what), "bloom +100 %%: peak of 125 Hz at %.2f s (law %.2f), of 8 kHz at %.2f s (law %.2f), +-0.3 s",
                  peakLow, lawLow, peakHigh, lawHigh);
    check(std::fabs(peakLow - lawLow) < 0.3 && std::fabs(peakHigh - lawHigh) < 0.3, what);

    s.bloom = 0.0;
    s.swell = 0.0;
    s.decay = 0.0;
    s.lfoTone = 3.0;
    s.lfoRate = 0.5;
    r = draws(s);
    // tilt between 250 Hz and 4 kHz (4 octaves): +-12 dB around the mean, highest at 0.5 s
    const auto tilt = [&](const double t) {
        return windowBand(r, s.fs, t, 3500.0, 4500.0) - windowBand(r, s.fs, t, 220.0, 280.0);
    };
    const double swing = tilt(0.5) - tilt(1.5);
    std::snprintf(what, sizeof(what), "LFO tone 3 dB/octave: 4 kHz against 250 Hz swings by %.1f dB (law 24, +-2)", swing);
    check(std::fabs(swing - 24.0) < 2.0, what);
}

// contrast of the harmonics 1-6 of 220 Hz against the gaps between them, window of 0.2 s at t, dB
static double combContrast(const std::vector<Response>& r, const double fs, const double t)
{
    double on = 0.0, off = 0.0;
    for (int k = 1; k <= 6; ++k)
    {
        on += windowBand(r, fs, t, 220.0 * k - 4.0, 220.0 * k + 4.0);
        off += windowBand(r, fs, t, 220.0 * (k + 0.5) - 4.0, 220.0 * (k + 0.5) + 4.0);
    }
    return (on - off) / 6.0;
}

static void testLfoTargets()
{
    std::printf("6. Targets and phases of the LFO\n");
    char what[200];
    Settings s = base();
    s.colour = 0.0;
    s.swell = 0.0;
    s.decay = 0.0;

    // phase 180 degrees on the pitch: the vibrato upside down
    {
        Settings v = s;
        v.tune = 1;
        v.length = 10.0;
        v.lfoRate = 0.1;
        v.lfoPitch = 1.0;
        v.resonance = 60.0;
        v.phase[kLfoPitch] = 2;
        const std::vector<Response> r = draws(v);
        const int w = static_cast<int>(0.2 * v.fs);
        const double a = peakFrequency(r, static_cast<size_t>(2.5 * v.fs) - w / 2, w, v.fs, 180.0, 260.0);
        const double b = peakFrequency(r, static_cast<size_t>(7.5 * v.fs) - w / 2, w, v.fs, 180.0, 260.0);
        std::snprintf(what, sizeof(what), "LFO pitch, phase 180: peak %.1f Hz at 2.5 s, %.1f Hz at 7.5 s (207.7 and 233.1, +-2)", a, b);
        check(std::fabs(a - 207.65) < 2.0 && std::fabs(b - 233.08) < 2.0, what);
    }

    // phase 90 degrees on the tone: brightest at 0 and 2 s, darkest at 1 s
    {
        Settings v = s;
        v.length = 4.0;
        v.lfoTone = 3.0;
        v.lfoRate = 0.5;
        v.phase[kLfoTone] = 1;
        const std::vector<Response> r = draws(v);
        const auto tilt = [&](const double t) {
            return windowBand(r, v.fs, t, 3500.0, 4500.0) - windowBand(r, v.fs, t, 220.0, 280.0);
        };
        const double swing = tilt(2.0) - tilt(1.0);
        std::snprintf(what, sizeof(what), "LFO tone, phase 90: tilt at 2 s against 1 s %.1f dB (law 24, +-2)", swing);
        check(std::fabs(swing - 24.0) < 2.0, what);
    }

    // resonance 30 % +- 50 %: clearly pitched at the top of the LFO (80 %), a light tint at the
    // bottom (0 %); phase 180 the other way round
    for (const int phase : { 0, 2 })
    {
        Settings v = s;
        v.tune = 1;
        v.length = 10.0;
        v.lfoRate = 0.1;
        v.resonance = 30.0;
        v.lfoResonance = 50.0;
        v.phase[kLfoResonance] = phase;
        const std::vector<Response> r = draws(v);
        const double top = combContrast(r, v.fs, phase == 0 ? 2.5 : 7.5), bottom = combContrast(r, v.fs, phase == 0 ? 7.5 : 2.5);
        std::snprintf(what, sizeof(what), "LFO resonance 30 +- 50 %%, phase %d: harmonics %.1f dB above the gaps at the top, "
                      "%.1f dB at the bottom (> 15, < 5)", 90 * phase, top, bottom);
        check(top > 15.0 && bottom < 5.0, what);
    }

    // density 40 / s, +- 1 octave: impulses per window = integral of the density
    for (const int phase : { 0, 2 })
    {
        Settings v = s;
        v.length = 4.0;
        v.density = 40.0;
        v.lfoDensity = 1.0;
        v.lfoRate = 0.25;
        v.phase[kLfoDensity] = phase;
        const Response r = generate(v);
        int worst = 0;
        for (const double a : { 0.2, 1.2, 2.2 })
        {
            int count = 0;
            for (int i = static_cast<int>(a * v.fs); i < static_cast<int>((a + 1.0) * v.fs); ++i)
                count += r.h[0][0][i] != 0.0f;
            double law = 0.0;
            for (double t = a; t < a + 1.0; t += 1e-4)
                law += densityAt(v, t) * 1e-4;
            worst = std::max(worst, static_cast<int>(std::lround(std::fabs(count - law))));
        }
        std::snprintf(what, sizeof(what), "LFO density 40 /s +- 1 octave, phase %d: impulses per second within %d of the law (<= 2)",
                      90 * phase, worst);
        check(worst <= 2, what);
    }

    // swept filters: levels per hertz against the law of sweptFilters (Butterworth magnitudes)
    const auto density = [](const std::vector<Response>& r, const double fs, const double t, const double f0, const double f1) {
        return windowBand(r, fs, t, f0, f1) - 10.0 * std::log10(f1 - f0);
    };
    {
        Settings v = s;
        v.length = 4.0;
        v.lfoRate = 0.25;   // top at 1 s, bottom at 3 s
        v.highCut = 2000.0;
        v.lfoHighCut = 2.0;   // 8 kHz at 1 s, 500 Hz at 3 s
        std::vector<Response> r = draws(v);
        double worst = 0.0;
        for (const double t : { 1.0, 3.0 })
            for (const double f : { 1000.0, 4000.0 })
            {
                const double measured = density(r, v.fs, t, f / 1.1, f * 1.1) - density(r, v.fs, t, 100.0, 200.0);
                const double law = 20.0 * std::log10(sweptFilters(v, f, t) / sweptFilters(v, 150.0, t));
                worst = std::fmax(worst, std::fabs(measured - law));
                if (std::getenv("VERBOSE"))
                    std::printf("    high cut t %.1f f %.0f: measured %.1f law %.1f\n", t, f, measured, law);
            }
        std::snprintf(what, sizeof(what), "LFO high cut 2 kHz +- 2 octaves: 1 and 4 kHz within %.1f dB of the law (< 2)", worst);
        check(worst < 2.0, what);

        v.highCut = kHighCutOff;
        v.lfoHighCut = 0.0;
        v.lowCut = 200.0;
        v.lfoLowCut = 2.0;   // 800 Hz at 1 s, 50 Hz at 3 s
        r = draws(v);
        worst = 0.0;
        for (const double t : { 1.0, 3.0 })
            for (const double f : { 200.0, 400.0 })
            {
                const double measured = density(r, v.fs, t, f / 1.1, f * 1.1) - density(r, v.fs, t, 3000.0, 5000.0);
                const double law = 20.0 * std::log10(sweptFilters(v, f, t) / sweptFilters(v, 4000.0, t));
                worst = std::fmax(worst, std::fabs(measured - law));
                if (std::getenv("VERBOSE"))
                    std::printf("    low cut t %.1f f %.0f: measured %.1f law %.1f\n", t, f, measured, law);
            }
        std::snprintf(what, sizeof(what), "LFO low cut 200 Hz +- 2 octaves: 200 and 400 Hz within %.1f dB of the law (< 2)", worst);
        check(worst < 2.0, what);
    }

    // settings without effect: no other response
    float p[kParamFirstOutput];
    for (int i = 0; i < kParamFirstOutput; ++i)
        p[i] = kPadifierRanges[i].def;
    const Settings d = fromParams(p, 48000.0);
    p[kParamLowCut] = 300.0f;            // fixed filter, not in the response
    p[kParamHighCut] = 3000.0f;
    p[kParamLfoResonance] = 40.0f;       // tune off
    p[kParamLfoDensity] = 2.0f;          // smooth noise
    p[kParamPhaseTone] = 2.0f;           // no depth
    p[kParamLfoRate] = 3.0f;             // no target
    check(fromParams(p, 48000.0) == d, "filters without LFO, resonance with Tune off, density of smooth noise, phase and "
                                       "rate without depth: the same settings");
}

static void testStereo()
{
    std::printf("7. Stereo motion, cross\n");
    Settings s = base();
    s.swell = 0.0;
    s.decay = 0.0;
    s.panDepth = 1.0;
    s.panRate = 1.0;   // left silent at 0.25 s, right silent at 0.75 s
    Response r = generate(s);
    const size_t q = static_cast<size_t>(0.25 * s.fs), h = static_cast<size_t>(0.75 * s.fs), w = 240;
    const double l1 = energy(r.h[0][0], q - w, q + w), r1 = energy(r.h[1][1], q - w, q + w);
    const double l2 = energy(r.h[0][0], h - w, h + w), r2 = energy(r.h[1][1], h - w, h + w);
    char what[160];
    std::snprintf(what, sizeof(what), "pan depth 100 %%, 1 Hz: left/right %.1f dB at 0.25 s, %.1f dB at 0.75 s (< -30, > 30)",
                  db(l1 / r1), db(l2 / r2));
    check(db(l1 / r1) < -30.0 && db(l2 / r2) > 30.0, what);

    s.panDepth = 0.0;
    s.cross = 1.0;
    r = generate(s);
    const double e0 = energy(r.h[0][0]) + energy(r.h[1][0]), e1 = energy(r.h[1][1]) + energy(r.h[0][1]);
    const double ratio = energy(r.h[1][0]) / energy(r.h[0][0]);
    double corr = 0.0;
    const std::vector<float>* all[4] = { &r.h[0][0], &r.h[1][1], &r.h[1][0], &r.h[0][1] };
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
            corr = std::fmax(corr, std::fabs(dot(*all[a], *all[b]) / std::sqrt(energy(*all[a]) * energy(*all[b]))));
    std::snprintf(what, sizeof(what), "cross 100 %%: energy per output %.6f, %.6f; cross / direct %.3f (1); largest correlation %.3f",
                  e0, e1, ratio, corr);
    check(r.cross && std::fabs(e0 - 1.0) < 1e-5 && std::fabs(e1 - 1.0) < 1e-5 && std::fabs(ratio - 1.0) < 1e-3 && corr < 0.05, what);
}

static void cost()
{
    std::printf("8. Cost of a rebuild (all treatments, 30 s, chord of 4 notes, cross)\n");
    for (const double fs : { 48000.0, 96000.0, 192000.0 })
    {
        Settings s = base(fs);
        s.length = 30.0;
        s.tune = 2;
        s.chord = 5;
        s.resonance = 100.0;
        s.lfoPitch = 2.0;
        s.damping = 6.0;
        s.bloom = 0.5;
        s.lfoTone = 3.0;
        s.lfoResonance = 30.0;
        s.lfoDensity = 1.0;
        s.lfoLowCut = 1.0;
        s.lowCut = 200.0;
        s.lfoHighCut = 2.0;
        s.highCut = 3000.0;
        s.cross = 1.0;
        s.panDepth = 1.0;
        s.density = 100.0;
        const auto t0 = std::chrono::steady_clock::now();
        const Response r = generate(s);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %3.0f kHz: %.2f s (%d samples)\n", fs / 1000.0, sec, r.length);
    }
    const Settings d = base();
    const auto t0 = std::chrono::steady_clock::now();
    generate(d);
    std::printf("  defaults, 48 kHz: %.3f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
}

int main(int argc, char** argv)
{
    testDefaults();
    testColour();
    testEnvelope();
    testDensity();
    testTune();
    testSpectral();
    testLfoTargets();
    testStereo();
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0)
        cost();
    std::printf("%s (%d failure%s)\n", gFailures == 0 ? "all impulse response tests passed" : "IMPULSE RESPONSE TESTS FAILED",
                gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
