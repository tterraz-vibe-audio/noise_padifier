#!/usr/bin/make -f
# Noise Padifier (DPF) - see ../build.sh
# Copyright (C) 2026 Padifier authors - SPDX-License-Identifier: GPL-3.0-or-later

NAME = noise_padifier

FILES_DSP = PadifierPlugin.cpp pffft/pffft.c
FILES_UI  = PadifierUI.cpp

DPF_PATH       = ../dpf
DPF_TARGET_DIR ?= ../bin
DPF_BUILD_DIR  = ../build

include ../dpf/Makefile.plugins.mk

TARGETS += vst3 clap
ifneq ($(MACOS_OR_WINDOWS),true)
TARGETS += lv2_sep
endif

all: $(TARGETS)
