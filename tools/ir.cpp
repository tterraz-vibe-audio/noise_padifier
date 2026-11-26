/*
 * Noise Padifier - impulse responses of the plugin (plugin/NoiseIR.hpp), for the figures of the
 * manual (doc/figures.py builds and calls it).
 *
 *   gcc -O2 -c plugin/pffft/pffft.c -o build/pffft.o
 *   g++ -O2 -std=gnu++17 -Iplugin tools/ir.cpp build/pffft.o -o build/ir
 *   ./build/ir SR [symbol=value ...] > ir.f32
 *
 * Settings: symbols of the parameters (colour, length, swell, decay, seed, density, tune, note,
 * chord, resonance, damping, bloom, lforate, lfopitch, lfotone, panrate, pandepth, cross, lowcut,
 * highcut, lforesonance, lfodensity, lfolowcut, lfohighcut, phasepitch, phasetone, phaseresonance,
 * phasedensity, phaselowcut, phasehighcut); the others keep their defaults.  Low cut and High cut
 * only act on the response through their LFO.
 * Output: raw 32-bit floats, interleaved L->L, R->R, R->L, L->R (the last two 0 without Cross).
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "NoiseIR.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const struct { const char* key; int param; } kKeys[] = {
    { "colour", kParamColour }, { "length", kParamLength }, { "swell", kParamSwell }, { "decay", kParamDecay },
    { "seed", kParamSeed }, { "density", kParamDensity }, { "tune", kParamTune }, { "note", kParamNote },
    { "chord", kParamChord }, { "resonance", kParamResonance }, { "damping", kParamDamping },
    { "bloom", kParamBloom }, { "lforate", kParamLfoRate }, { "lfopitch", kParamLfoPitch },
    { "lfotone", kParamLfoTone }, { "panrate", kParamPanRate }, { "pandepth", kParamPanDepth },
    { "cross", kParamCross }, { "lowcut", kParamLowCut }, { "highcut", kParamHighCut },
    { "lforesonance", kParamLfoResonance }, { "lfodensity", kParamLfoDensity }, { "lfolowcut", kParamLfoLowCut },
    { "lfohighcut", kParamLfoHighCut }, { "phasepitch", kParamPhasePitch }, { "phasetone", kParamPhaseTone },
    { "phaseresonance", kParamPhaseResonance }, { "phasedensity", kParamPhaseDensity },
    { "phaselowcut", kParamPhaseLowCut }, { "phasehighcut", kParamPhaseHighCut },
};

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: ir SR [symbol=value ...]\n");
        return 1;
    }
    float p[kParamFirstOutput];
    for (int i = 0; i < kParamFirstOutput; ++i)
        p[i] = kPadifierRanges[i].def;
    for (int a = 2; a < argc; ++a)
    {
        const char* const eq = std::strchr(argv[a], '=');
        bool known = false;
        if (eq != nullptr)
        {
            const std::string key(argv[a], static_cast<size_t>(eq - argv[a]));
            for (const auto& k : kKeys)
                if (key == k.key)
                {
                    p[k.param] = static_cast<float>(std::atof(eq + 1));
                    known = true;
                }
        }
        if (!known)
        {
            std::fprintf(stderr, "unknown setting: %s\n", argv[a]);
            return 1;
        }
    }
    const PadifierIR::Settings s = PadifierIR::fromParams(p, std::atof(argv[1]));
    const PadifierIR::Response r = PadifierIR::generate(s);
    std::vector<float> out(4 * static_cast<size_t>(r.length), 0.0f);
    const std::vector<float>* h[4] = { &r.h[0][0], &r.h[1][1], &r.h[1][0], &r.h[0][1] };
    for (int k = 0; k < 4; ++k)
        if (!h[k]->empty())
            for (int i = 0; i < r.length; ++i)
                out[4 * static_cast<size_t>(i) + k] = (*h[k])[i];
    std::fwrite(out.data(), sizeof(float), out.size(), stdout);
    return 0;
}
