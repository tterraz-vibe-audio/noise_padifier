/*
 * Noise Padifier - tests of the preferences file (theme of the interface).
 * Run with HOME / XDG_CONFIG_HOME (Linux) or APPDATA (Windows) pointing to an empty folder.
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "PadifierPrefs.hpp"

#include <cstdio>

using namespace PadifierPrefs;

static int failures = 0;

static void check(const bool ok, const char* const what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

int main()
{
    check(loadTheme() == kThemeDusk, "no file: Dusk");
    check(saveTheme(kThemePearl), "save Pearl");
    check(loadTheme() == kThemePearl, "read back Pearl");
    check(saveTheme(kThemeDusk) && loadTheme() == kThemeDusk, "save and read back Dusk");
    check(!saveTheme(kThemeCount), "invalid theme refused");

    // unknown keys and values are ignored, the last valid line wins
    std::FILE* const f = openFile("w");
    check(f != nullptr, "open for writing");
    if (f != nullptr)
    {
        std::fputs("# comment\r\nvolume=11\r\ntheme=pearl\r\ntheme=purple\r\n", f);
        std::fclose(f);
    }
    check(loadTheme() == kThemePearl, "unknown lines ignored (CRLF)");

    std::printf("%s\n", failures == 0 ? "all prefs tests passed" : "PREFS TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
