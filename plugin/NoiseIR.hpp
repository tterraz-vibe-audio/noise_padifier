/*
 * Noise Padifier - the impulse response: white or pink noise under an envelope.
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * h_c(n) = k_c e(n / fs) w_c(n), n = 0 .. N - 1, N = round(length fs), c = channel (0 left,
 * 1 right):
 * - w_c: Gaussian noise (Box-Muller on a SplitMix64 generator seeded by (seed, c): the same
 *   samples on every computer and compiler); pink: its spectrum (FFT over the next power of two
 *   >= N, circular) multiplied by 1 / sqrt(max(f, 20 Hz)), DC removed, i.e. -3 dB per octave
 *   from 20 Hz up;
 * - e: envelope, product of
 *     rise   sin^2(pi/2 t / Ta) for t < Ta = swell x length (Ta = 0: none),
 *     decay  10^(decay_dB / 20 x (t - Ta) / (length - Ta)) after Ta (straight line in dB),
 *     fade   cos^2 over the last Tf = min(20 ms, length / 10), so that the response never ends
 *            on a step;
 * - k_c: sum of h_c^2 = 1 (unit energy): a broadband input keeps its level in the wet signal.
 * The two channels have independent noises: a mono input comes out wide.
 */
#ifndef PADIFIER_NOISE_IR_HPP_INCLUDED
#define PADIFIER_NOISE_IR_HPP_INCLUDED

#include "PadifierParams.hpp"
#include "pffft/pffft.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace PadifierIR {

struct Settings {
    bool pink = true;
    double length = 3.0;    // s
    double swell = 0.25;    // fraction of the length
    double decayDb = -30.0; // level at the end, dB
    int seed = 1;
    double fs = 48000.0;
};

static inline Settings fromParams(const float* const params, const double fs)
{
    Settings s;
    s.pink = PadifierLaw::pink(params[kParamNoise]);
    s.length = std::fmin(std::fmax(params[kParamLength], kPadifierRanges[kParamLength].min), kPadifierRanges[kParamLength].max);
    s.swell = std::fmin(std::fmax(params[kParamSwell] * 0.01, 0.0), 1.0);
    s.decayDb = std::fmin(std::fmax(params[kParamDecay], kPadifierRanges[kParamDecay].min), 0.0f);
    s.seed = PadifierLaw::seed(params[kParamSeed]);
    s.fs = fs;
    return s;
}

static inline bool operator==(const Settings& a, const Settings& b)
{
    return a.pink == b.pink && a.length == b.length && a.swell == b.swell && a.decayDb == b.decayDb
        && a.seed == b.seed && a.fs == b.fs;
}

static inline int lengthSamples(const Settings& s)
{
    return std::max(1, static_cast<int>(std::lround(s.length * s.fs)));
}

// envelope at time t (s), 0 .. 1 before normalisation; also used by the interface
static inline double envelope(const Settings& s, const double t)
{
    const double L = s.length, ta = s.swell * L, tf = std::fmin(0.02, 0.1 * L);
    if (t < 0.0 || t >= L)
        return 0.0;
    double e = 1.0;
    if (t < ta)
    {
        const double r = std::sin(0.5 * M_PI * t / ta);
        e *= r * r;
    }
    else if (L - ta > 0.0)
        e *= std::pow(10.0, s.decayDb / 20.0 * (t - ta) / (L - ta));
    if (t > L - tf)
    {
        const double c = std::cos(0.5 * M_PI * (t - (L - tf)) / tf);
        e *= c * c;
    }
    return e;
}

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

// Noise (unit variance) for one channel, N samples, before the envelope.
static inline void noise(const Settings& s, const int channel, const int n, std::vector<float>& w)
{
    const int nf = s.pink ? nextPowerOf2(n) : n;
    std::vector<double> g(static_cast<size_t>(nf));
    Random rng(static_cast<uint64_t>(s.seed) * 0x100000001B3ull + static_cast<uint64_t>(channel) + 1u);
    for (int i = 0; i < nf; i += 2)
    {
        const double r = std::sqrt(-2.0 * std::log(rng.uniform())), a = 2.0 * M_PI * rng.uniform();
        g[i] = r * std::cos(a);
        if (i + 1 < nf)
            g[i + 1] = r * std::sin(a);
    }
    w.resize(static_cast<size_t>(n));
    if (!s.pink)
    {
        for (int i = 0; i < n; ++i)
            w[i] = static_cast<float>(g[i]);
        return;
    }

    // pink: spectrum x 1 / sqrt(max(f, 20 Hz)), DC removed (ordered layout of pffft:
    // [DC, Nyquist, re1, im1, re2, im2, ...])
    PFFFT_Setup* const setup = pffft_new_setup(nf, PFFFT_REAL);
    float* const buf = static_cast<float*>(pffft_aligned_malloc(sizeof(float) * nf));
    float* const work = static_cast<float*>(pffft_aligned_malloc(sizeof(float) * nf));
    for (int i = 0; i < nf; ++i)
        buf[i] = static_cast<float>(g[i]);
    pffft_transform_ordered(setup, buf, buf, work, PFFFT_FORWARD);
    const double df = s.fs / nf;
    const auto gain = [&](const int k) { return 1.0 / std::sqrt(std::fmax(k * df, 20.0)); };
    buf[0] = 0.0f;
    buf[1] *= static_cast<float>(gain(nf / 2));
    for (int k = 1; k < nf / 2; ++k)
    {
        const float a = static_cast<float>(gain(k));
        buf[2 * k] *= a;
        buf[2 * k + 1] *= a;
    }
    pffft_transform_ordered(setup, buf, buf, work, PFFFT_BACKWARD);
    for (int i = 0; i < n; ++i)
        w[i] = buf[i];
    pffft_aligned_free(work);
    pffft_aligned_free(buf);
    pffft_destroy_setup(setup);
}

// The impulse response of one channel, unit energy.
static inline void generate(const Settings& s, const int channel, std::vector<float>& h)
{
    const int n = lengthSamples(s);
    noise(s, channel, n, h);
    double energy = 0.0;
    std::vector<double> d(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        d[i] = h[i] * envelope(s, i / s.fs);
        energy += d[i] * d[i];
    }
    const double k = energy > 0.0 ? 1.0 / std::sqrt(energy) : 0.0;
    for (int i = 0; i < n; ++i)
        h[i] = static_cast<float>(d[i] * k);
}

}   // namespace PadifierIR

#endif
