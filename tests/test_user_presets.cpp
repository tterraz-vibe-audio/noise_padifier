/*
 * Noise Padifier - tests of the presets saved by the user.
 * Run with HOME / XDG_CONFIG_HOME (Linux) or APPDATA (Windows) pointing to an empty folder.
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "PadifierUserPresets.hpp"

#include <clocale>
#include <cstdio>

using namespace PadifierUserPresets;

static int failures = 0;

static void check(const bool ok, const char* const what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

// the bypass is not saved
static bool sameValues(const float* const a, const float* const b)
{
    for (int i = 0; i < kPresetParamCount; ++i)
        if (isPresetParam(i) && a[i] != b[i])
            return false;
    return true;
}

int main()
{
    // a host may set a locale with a decimal comma: the files must not depend on it
    const bool comma = std::setlocale(LC_ALL, "fr_FR.UTF-8") != nullptr;
    std::printf("locale with a decimal comma: %s\n", comma ? "yes" : "not available");

    check(list().empty(), "no preset at first");

    check(validName("My master 2"), "valid name");
    check(!validName("") && !validName(" lead") && !validName("lead ") && !validName("lead."),
          "empty name, leading or trailing space or dot refused");
    check(!validName("a/b") && !validName("a:b") && !validName("a?") && !validName("tab\tname"),
          "forbidden characters refused");
    check(!validName("CON") && !validName("com1") && !validName("nul.txt") && validName("Console"),
          "reserved Windows names refused");
    check(!validName("abcdefghijklmnopqrstuvwxy"), "name longer than 24 characters refused");
    check(!save("Default", kPadifierPresets[0].values) && !save("soft PAD", kPadifierPresets[0].values),
          "factory names refused");

    // every parameter away from its default, odd values
    float a[kPresetParamCount], b[kPresetParamCount];
    for (int i = 0; i < kPresetParamCount; ++i)
    {
        const PadifierRange& q = kPadifierRanges[i];
        a[i] = q.min + (q.max - q.min) * (0.123457f + 0.031f * i);
        b[i] = q.min;   // the other end
    }
    a[kParamTune] = 2.0f;
    a[kParamChord] = 6.0f;
    a[kParamRhythm] = 9.0f;
    a[kParamBypass] = b[kParamBypass] = 0.0f;
    check(save("Bright", a), "save (all the parameters of 1.1)");
    float r[kPresetParamCount] = { 0 };
    check(load("Bright", r) && sameValues(a, r), "read back exactly");

    check(save("aggressive", b) && save("Calm", kPadifierPresets[2].values), "save two more");
    const std::vector<std::string> names = list();
    check(names.size() == 3 && names[0] == "aggressive" && names[1] == "Bright" && names[2] == "Calm",
          "list sorted without case");

    check(save("bright", b) && list().size() == 3 && list()[1] == "bright" && load("bright", r)
          && sameValues(b, r), "same name with another case replaces the preset");

    check(erase("Calm") && list().size() == 2 && !load("Calm", r), "delete");
    check(!erase("Calm"), "delete a missing preset fails");

    // hand-written file: missing keys take the default, unknown lines are ignored, values are
    // clamped, CRLF ends of line
    std::FILE* const f = openPreset("Hand", true);
    check(f != nullptr, "open for writing");
    if (f != nullptr)
    {
        std::fputs("# comment\r\nvolume=11\r\nlowcut=5000\r\nwidth=40.5\r\nlength=x\r\n", f);
        std::fclose(f);
    }
    bool ok = load("Hand", r) && r[kParamLowCut] == 1000.0f && r[kParamWidth] == 40.5f;
    for (int i = 0; i < kPresetParamCount; ++i)
        if (i != kParamLowCut && i != kParamWidth)
            ok = ok && r[i] == kPadifierRanges[i].def;
    check(ok, "missing keys: default, unknown lines ignored, clamped");

    // a preset of 1.0: "noise" ignored, the colour and the parameters of 1.1 take their defaults
    std::FILE* const old = openPreset("Old", true);
    if (old != nullptr)
    {
        std::fputs("# Noise Padifier preset\nversion=1\nnoise=0\nlength=4.5\nswell=40\nmix=70\n", old);
        std::fclose(old);
    }
    ok = load("Old", r) && r[kParamLength] == 4.5f && r[kParamSwell] == 40.0f && r[kParamMix] == 70.0f;
    for (int i = 0; i < kPresetParamCount; ++i)
        if (i != kParamLength && i != kParamSwell && i != kParamMix)
            ok = ok && r[i] == kPadifierRanges[i].def;
    check(ok, "preset of 1.0: read, colour and new parameters at their defaults");
    erase("Old");

    // files that are not presets are not listed
#ifdef _WIN32
    std::FILE* const g = _wfopen((folder() + L"\\notes.txt").c_str(), L"wb");
#else
    std::FILE* const g = std::fopen((folder() + "/notes.txt").c_str(), "wb");
#endif
    if (g != nullptr)
        std::fclose(g);
    check(list().size() == 3, "other files ignored");

    std::printf("%s\n", failures == 0 ? "all user preset tests passed" : "USER PRESET TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
