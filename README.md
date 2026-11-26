# Noise Padifier

A convolution pad effect for VST3, CLAP and LV2: the input is convolved with **white or pink
noise under an envelope** (0.1 to 10 s), so that every note and every attack is smeared into a
pad. The timbre of the source stays (its harmonics keep their place), its shape in time is
replaced by the envelope.

![Noise Padifier, Dusk theme](doc/captures/capture_dusk.png)

## Features

- **Noise**: white (flat, bright, airy) or pink (-3 dB/octave from 20 Hz, warm and full);
  **Seed** picks another draw of the same noise, and the same seed always gives the same
  response on every computer.
- **Envelope**: **Length** (0.1-10 s), **Swell** (rise time, % of the length; 100 % gives a
  reverse swell), **Decay** (level at the end, -60 to 0 dB; 0 dB gives a held pad), shown as a
  curve.
- **True stereo width**: independent noises left and right, so even a mono source comes out wide;
  **Width** 0-200 %.
- **Low cut** and **High cut** on the pad (12 dB/octave), **Mix** at constant power, **Output**.
- Unit-energy responses: a broadband input keeps its level in the pad, whatever the settings.
- **Low-latency, low-CPU convolution**: two-stage uniformly partitioned FFT convolution (pffft),
  work spread evenly over the blocks, no allocation in the audio thread; 256 samples of latency,
  reported to the host. About 2.4 % of one core at 48 kHz with a 10 s response.
- Changing the response (noise, seed, envelope) rebuilds it in a worker thread and crossfades it
  in without a click; the new response gets the whole past of the input at once.
- **Presets**: 8 factory presets and your own (save, replace, delete), shared by all projects.
- **A/B comparison** saved with the project; dark (Dusk) and light (Pearl) themes; tooltips
  everywhere; keyboard entry of values (right-click a knob); resizable HiDPI interface.
- Bypass declared to the host (delayed dry signal: constant latency), smoothed controls, no
  denormal slowdown.

![Pearl theme](doc/captures/capture_pearl.png)

## Formats

| Platform | Formats |
|---|---|
| Windows 64-bit | VST3, CLAP (installer included) |
| Linux x86-64 | VST3, CLAP, LV2 |

## Installation

**Windows**: run `Noise-Padifier-1.0.0-setup.exe` (VST3, optional CLAP and user manual).
Manually: copy the `noise_padifier.vst3` folder to `C:\Program Files\Common Files\VST3\` and
`noise_padifier.clap` to `C:\Program Files\Common Files\CLAP\`.

**Linux**: copy `noise_padifier.vst3` to `~/.vst3/`, `noise_padifier.clap` to `~/.clap/` or
`noise_padifier.lv2` to `~/.lv2/`.

## Documentation

- **User manual** (French): [`doc/manuel.pdf`](doc/manuel.pdf): every setting, the presets, tips,
  and a description of the algorithm (impulse response, partitioned convolution, crossfade of a
  new response, mix law, measured cost), with figures computed from the plugin's own code
  (`doc/figures.py`; impulse responses through `tools/ir.cpp`).
- Developer notes (French): [`LISEZMOI.md`](LISEZMOI.md).

## Building

From Linux with MinGW-w64 (`-posix`), git, and optionally NSIS: `./build.sh` clones DPF at the
tested commit, applies `patches/`, runs the tests, and builds Windows and Linux binaries and the
installer into `bin/` (`BENCH=1 ./build.sh` also prints the cost of the DSP).
The manual needs `gcc`, `g++`, `python3-numpy`, `python3-matplotlib` and `pdflatex`:

```sh
cd doc && python3 figures.py && pdflatex manuel.tex && pdflatex manuel.tex && pdflatex manuel.tex
```

## Tests

`tests/test_dsp.cpp`, built twice (strict, and with the plugin's `-O3 -ffast-math`):
convolution against a direct double-precision convolution (within 4.4e-7 of the peak, responses
from 1 sample to five tail blocks, 48, 96 and 192 kHz, odd host block sizes), exact crossfade
between two responses, impulse responses (unit energy, reproducible, decorrelated channels,
envelope, white and pink spectra), and the whole plugin (latency, bit-exact mix 0 % and bypass,
filters, width, presets, meters, FPU state). Also `tests/test_prefs.cpp` and
`tests/test_user_presets.cpp`. Steinberg's VST3 validator passes 47/47 on the Linux build.

## Licence

GPL-3.0-or-later (`COPYING`). `plugin/pffft/` holds the unmodified pffft by Julien Pommier
(FFTPACK licence, BSD-like; `PROVENANCE.txt`). The binaries also contain DPF (ISC), NanoVG (zlib),
pugl (ISC), the CLAP headers (MIT) and the DejaVu Sans font: their notices are in
`THIRD_PARTY_LICENSES.txt`, shipped with every download.
