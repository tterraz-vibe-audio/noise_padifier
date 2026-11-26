/*
 * Noise Padifier - impulse response of the plugin (plugin/NoiseIR.hpp), for the figures of the
 * manual (doc/figures.py builds and calls it).
 *
 *   gcc -O2 -c plugin/pffft/pffft.c -o build/pffft.o
 *   g++ -O2 -std=gnu++17 -Iplugin tools/ir.cpp build/pffft.o -o build/ir
 *   ./build/ir SR LENGTH SWELL_PERCENT DECAY_DB SEED PINK > ir.f32
 *
 * Output: interleaved L, R as raw 32-bit floats, unit energy per channel.
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "NoiseIR.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 7)
    {
        std::fprintf(stderr, "usage: ir SR LENGTH SWELL_PERCENT DECAY_DB SEED PINK\n");
        return 1;
    }
    float p[kIRParamCount];
    p[kParamLength] = static_cast<float>(std::atof(argv[2]));
    p[kParamSwell] = static_cast<float>(std::atof(argv[3]));
    p[kParamDecay] = static_cast<float>(std::atof(argv[4]));
    p[kParamSeed] = static_cast<float>(std::atof(argv[5]));
    p[kParamNoise] = static_cast<float>(std::atof(argv[6]));
    const PadifierIR::Settings s = PadifierIR::fromParams(p, std::atof(argv[1]));
    std::vector<float> h[2];
    PadifierIR::generate(s, 0, h[0]);
    PadifierIR::generate(s, 1, h[1]);
    std::vector<float> out(2 * h[0].size());
    for (size_t i = 0; i < h[0].size(); ++i)
    {
        out[2 * i] = h[0][i];
        out[2 * i + 1] = h[1][i];
    }
    std::fwrite(out.data(), sizeof(float), out.size(), stdout);
    return 0;
}
