/*
 * Noise Padifier - DPF plugin description.
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Padifier"
#define DISTRHO_PLUGIN_NAME    "Noise Padifier"
#define DISTRHO_PLUGIN_URI     "urn:noise-padifier"
#define DISTRHO_PLUGIN_CLAP_ID "noise-padifier"

#define DISTRHO_PLUGIN_BRAND_ID  NsPd
#define DISTRHO_PLUGIN_UNIQUE_ID NPad

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_LATENCY  1   // 256 samples (convolution in blocks)
#define DISTRHO_PLUGIN_WANT_PROGRAMS 1
#define DISTRHO_PLUGIN_WANT_STATE    1   // A/B comparison of the interface saved with the project
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1 // the plugin keeps the state strings (getState)
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_USER_RESIZABLE    1
#define DISTRHO_UI_DEFAULT_WIDTH     1200
#define DISTRHO_UI_DEFAULT_HEIGHT    760

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Reverb"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "reverb", "stereo"
#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:ReverbPlugin"

#endif
