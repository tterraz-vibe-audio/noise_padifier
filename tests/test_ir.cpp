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
 * 6. Rhythm: zero where the gate is closed, the period follows the tempo.
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
    check(!spectralEnvelope(s) && s.tune == 0 && s.rhythm == 0 && s.cross == 0.0, "defaults: no treatment on");
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
    s.decayDb = 0.0;
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

static void testDensity()
{
    std::printf("3. Density\n");
    Settings s = base();
    s.colour = 0.0;
    s.swell = 0.0;
    s.decayDb = 0.0;
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
    s.decayDb = 0.0;
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
    s.decayDb = 0.0;
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
    s.decayDb = -30.0;
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
    s.decayDb = 0.0;
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

static void testRhythm()
{
    std::printf("6. Rhythm\n");
    Settings s = base();
    s.swell = 0.0;
    s.rhythm = 4;   // 1/8
    s.duty = 0.4;
    s.attack = 0.1;
    s.release = 0.2;
    bool ok = true;
    for (const double tempo : { 120.0, 90.0 })
    {
        s.tempo = tempo;
        const Response r = generate(s);
        const double period = 0.5 * 60.0 / tempo;
        int closedNonZero = 0, openZero = 0;
        for (int i = 0; i < r.length - static_cast<int>(0.03 * s.fs); ++i)
        {
            const double tau = std::fmod(i / s.fs, period) / period;
            if (tau > 0.61 && tau < 0.99)   // after duty + release
                closedNonZero += r.h[0][0][i] != 0.0f;
            if (tau > 0.12 && tau < 0.38)   // held open
                openZero += r.h[0][0][i] == 0.0f;
        }
        ok = ok && closedNonZero == 0 && openZero < 5 && std::fabs(energy(r.h[0][0]) - 1.0) < 1e-5;
    }
    check(ok, "1/8 at 120 and 90 BPM, duty 40 %, attack 10 %, release 20 %: zero after the release, open while held, energy 1");

    // the law of one step: rise sin^2 over the attack, held, fall cos^2 over the release
    s.tempo = 120.0;
    const double T = 0.25;
    const double g1 = gate(s, 0.05 * T), g2 = gate(s, 0.1 * T), g3 = gate(s, 0.3 * T), g4 = gate(s, 0.5 * T),
                 g5 = gate(s, 0.6 * T), g6 = gate(s, 3.0 * T + 0.05 * T);
    char what[200];
    std::snprintf(what, sizeof(what), "gate: %.3f at half the attack (0.5), %.3f at its end (1), %.3f held (1), %.3f at half the "
                  "release (0.5), %.3f after (0); next steps alike (%.3f)", g1, g2, g3, g4, g5, g6);
    check(std::fabs(g1 - 0.5) < 1e-9 && std::fabs(g2 - 1.0) < 1e-9 && g3 == 1.0 && std::fabs(g4 - 0.5) < 1e-9 && g5 < 1e-9
          && std::fabs(g6 - 0.5) < 1e-9, what);

    // a release longer than the closed part runs into the next step: no step down
    s.duty = 0.8;
    s.release = 0.5;
    double jump = 0.0, prev = gate(s, 0.0);
    for (int i = 1; i < 48000; ++i)
    {
        const double g = gate(s, i / 48000.0);
        jump = std::fmax(jump, std::fabs(g - prev));
        prev = g;
    }
    std::snprintf(what, sizeof(what), "duty 80 %%, release 50 %% (into the next step): largest change per sample %.4f (< 0.01)", jump);
    check(jump < 0.01, what);
    s.duty = 1.0;
    s.attack = 0.0;
    check(gate(s, 0.1) == 1.0 && gate(s, 0.25) == 1.0, "duty 100 %, no attack: always open");
}

static void testStereo()
{
    std::printf("7. Stereo motion, cross\n");
    Settings s = base();
    s.swell = 0.0;
    s.decayDb = 0.0;
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
    std::printf("8. Cost of a rebuild (all treatments, 10 s, chord of 4 notes, cross)\n");
    for (const double fs : { 48000.0, 96000.0, 192000.0 })
    {
        Settings s = base(fs);
        s.length = 10.0;
        s.tune = 2;
        s.chord = 5;
        s.resonance = 100.0;
        s.lfoPitch = 2.0;
        s.damping = 6.0;
        s.bloom = 0.5;
        s.lfoTone = 3.0;
        s.rhythm = 5;
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
    testDensity();
    testTune();
    testSpectral();
    testRhythm();
    testStereo();
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0)
        cost();
    std::printf("%s (%d failure%s)\n", gFailures == 0 ? "all impulse response tests passed" : "IMPULSE RESPONSE TESTS FAILED",
                gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
