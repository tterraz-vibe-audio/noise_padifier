#!/usr/bin/env python3
"""Noise Padifier - figures of the user manual (doc/manuel.tex).

python3 doc/figures.py  ->  doc/fig_*.pdf (French, manuel.tex) and doc/fig_*_en.pdf (English, manual.tex)

Impulse responses: the code of the plugin itself (plugin/NoiseIR.hpp), through tools/ir.cpp,
compiled into build/ by this script (gcc, g++).  Envelope, mix law and filters: the formulas of
plugin/NoiseIR.hpp, PadifierParams.hpp and PadifierPlugin.cpp, written again here in NumPy.
Convolutions: NumPy FFT (the plugin gives the same result within 5e-7 of the peak,
tests/test_dsp.cpp).
Copyright (C) 2026 Padifier authors - SPDX-License-Identifier: GPL-3.0-or-later
"""
import os
import subprocess

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VIOLET, AMBER, DARK, GREY, ROSE = "#6246aa", "#96691a", "#26203a", "#8c8a9e", "#b5476a"
plt.rcParams.update({"font.size": 9, "axes.grid": True, "grid.alpha": 0.3, "axes.spines.top": False,
                     "axes.spines.right": False, "legend.frameon": False, "figure.dpi": 150})
FS = 48000.0
LANG = "fr"   # set by main: "fr" or "en"

TEXT = {
    "envelope_band": ("enveloppe (± 3 écarts-types)", "envelope (± 3 standard deviations)"),
    "amplitude": ("amplitude", "amplitude"),
    "pink": ("bruit rose", "pink noise"),
    "white": ("bruit blanc", "white noise"),
    "time": ("temps (s)", "time (s)"),
    "ir_title": ("Réponse impulsionnelle, canal gauche (Default : 3 s, Swell 25 %, Decay −30 dB)",
                 "Impulse response, left channel (Default: 3 s, Swell 25 %, Decay −30 dB)"),
    "time_length": ("temps (s), Length 3 s", "time (s), Length 3 s"),
    "env_level": ("niveau de l'enveloppe (dB)", "level of the envelope (dB)"),
    "measured": (" (mesuré, 1/6 d'octave)", " (measured, 1/6 octave)"),
    "pink_filtered": ("bruit rose, Low cut 80 Hz, High cut 8 kHz", "pink noise, Low cut 80 Hz, High cut 8 kHz"),
    "frequency": ("fréquence (Hz)", "frequency (Hz)"),
    "db_1k": ("dB (0 à 1 kHz)", "dB (0 at 1 kHz)"),
    "dry": ("sec", "dry"),
    "total": ("puissance totale (sources indépendantes)", "total power (independent sources)"),
    "gain": ("gain (dB)", "gain (dB)"),
    "input_notes": ("entrée : deux notes brèves (220 et 330 Hz)", "input: two short notes (220 and 330 Hz)"),
    "output_pad": ("sortie, pad seul (Default : bruit rose, 3 s, Swell 25 %, Decay −30 dB)",
                   "output, pad only (Default: pink noise, 3 s, Swell 25 %, Decay −30 dB)"),
    "output_chord": ("sortie, pad seul (Chord pad : accord ré mineur 7, résonance 60 %)",
                     "output, pad only (Chord pad: D minor 7 chord, resonance 60 %)"),
    "colour": ("Colour %+d dB/oct", "Colour %+d dB/oct"),
    "t_damping": ("Damping 6 dB/oct, Bloom −60 %", "Damping 6 dB/oct, Bloom −60 %"),
    "t_chord": ("Chord ré min7, Resonance 60 %, Vibrato 0,5 demi-ton", "Chord D min7, Resonance 60 %, Vibrato 0.5 st"),
    "t_rhythm": ("Rhythm 1/8 à 120 BPM, Duty 40 %", "Rhythm 1/8 at 120 BPM, Duty 40 %"),
    "t_density": ("Density 40 impulsions/s", "Density 40 impulses/s"),
    "level_db": ("niveau (dB)", "level (dB)"),
    "left": ("sortie gauche", "left output"),
    "right": ("sortie droite", "right output"),
    "pan_title": ("Pan depth 100 %, Pan rate 1 Hz (enveloppe plate, moyenne sur 20 ms)",
                  "Pan depth 100 %, Pan rate 1 Hz (flat envelope, mean over 20 ms)"),
}


def tr(key):
    return TEXT[key][0 if LANG == "fr" else 1]


def db(x):
    return 20.0 * np.log10(np.maximum(np.abs(x), 1e-12))


def save(fig, name):
    fig.tight_layout()
    if LANG != "fr":
        name = name.replace(".pdf", "_%s.pdf" % LANG)
    fig.savefig(os.path.join(HERE, name))
    plt.close(fig)


# ---------------------------------------------------------------------------------- the plugin

def build_tool():
    b = os.path.join(ROOT, "build")
    os.makedirs(b, exist_ok=True)
    subprocess.run(["gcc", "-O2", "-c", os.path.join(ROOT, "plugin/pffft/pffft.c"), "-o",
                    os.path.join(b, "pffft_fig.o")], check=True)
    subprocess.run(["g++", "-O2", "-std=gnu++17", "-I" + os.path.join(ROOT, "plugin"),
                    os.path.join(ROOT, "tools/ir.cpp"), os.path.join(b, "pffft_fig.o"), "-o",
                    os.path.join(b, "ir")], check=True)
    return os.path.join(b, "ir")


IR_TOOL = build_tool()


def ir(fs=FS, **settings):
    """impulse responses of the plugin, settings by parameter symbol (defaults otherwise):
    array (N, 4): left -> left, right -> right, right -> left, left -> right"""
    args = [IR_TOOL, str(fs)] + ["%s=%s" % (k, v) for k, v in settings.items()]
    out = subprocess.run(args, check=True, capture_output=True).stdout
    return np.frombuffer(out, dtype=np.float32).reshape(-1, 4).astype(float)


def stft_db(h, n=2048, hop=256):
    """level of h by frame and frequency (Hann), dB; times and frequencies"""
    w = np.hanning(n)
    frames = np.array([h[i:i + n] * w for i in range(0, len(h) - n, hop)])
    p = np.abs(np.fft.rfft(frames, axis=1)) ** 2
    t = (np.arange(len(frames)) * hop + n / 2) / FS
    f = np.fft.rfftfreq(n, 1 / FS)
    return t, f, 10 * np.log10(np.maximum(p, 1e-30)).T


def envelope(t, length, swell, decay_db):
    """NoiseIR.hpp, envelope()"""
    t = np.asarray(t, dtype=float)
    L, ta, tf = length, swell * length, min(0.02, 0.1 * length)
    e = np.ones_like(t)
    if ta > 0:
        rise = t < ta
        e[rise] *= np.sin(0.5 * np.pi * t[rise] / ta) ** 2
    after = t >= ta
    if L - ta > 0:
        e[after] *= 10.0 ** (decay_db / 20.0 * (t[after] - ta) / (L - ta))
    end = t > L - tf
    e[end] *= np.cos(0.5 * np.pi * (t[end] - (L - tf)) / tf) ** 2
    e[(t < 0) | (t >= L)] = 0.0
    return e


def butterworth2(f, fc, high):
    r4 = (np.asarray(f) / fc) ** 4
    return r4 / (1 + r4) if high else 1 / (1 + r4)   # power gain


def smooth_octave(f, p, fraction=6):
    """power spectrum averaged over 1/fraction octave around each frequency"""
    c = np.cumsum(p)
    out = np.empty_like(p)
    k = 2 ** (0.5 / fraction)
    for i, fi in enumerate(f):
        a = np.searchsorted(f, fi / k)
        b = max(np.searchsorted(f, fi * k), a + 1)
        out[i] = (c[min(b, len(c)) - 1] - (c[a - 1] if a > 0 else 0)) / (min(b, len(c)) - a)
    return out


# ---------------------------------------------------------------------------------- figures

def fig_reponse():
    fig, axes = plt.subplots(2, 1, figsize=(6.6, 4.2), sharex=True)
    for ax, pink in zip(axes, (0, 1)):
        h = ir(colour=-3 if pink else 0)[:, 0]
        t = np.arange(len(h)) / FS
        e = envelope(t, 3.0, 0.25, -30.0)
        k = np.sqrt(np.sum(h ** 2) / np.sum(e ** 2))
        ax.plot(t, h, color=VIOLET if pink else GREY, lw=0.3, rasterized=True)
        ax.plot(t, 3 * k * e, color=AMBER, lw=1.2, label=tr("envelope_band"))
        ax.plot(t, -3 * k * e, color=AMBER, lw=1.2)
        ax.set_ylabel(tr("amplitude"))
        ax.set_title(tr("pink") if pink else tr("white"), fontsize=9, loc="left")
    axes[0].legend(loc="upper right")
    axes[1].set_xlabel(tr("time"))
    axes[1].set_xlim(0, 3.0)
    fig.suptitle(tr("ir_title"), fontsize=9)
    save(fig, "fig_reponse.pdf")


def fig_enveloppes():
    t = np.linspace(0, 3.0, 3001)
    fig, axes = plt.subplots(1, 2, figsize=(6.6, 2.6), sharey=True)
    cols = [GREY, VIOLET, AMBER, ROSE]
    for c, sw in zip(cols, (0, 25, 50, 100)):
        axes[0].plot(t, db(envelope(t, 3.0, sw / 100, -30.0)), color=c, lw=1.4, label="Swell %d %%" % sw)
    axes[0].set_title("Decay −30 dB", fontsize=9, loc="left")
    for c, de in zip(cols, (0, -20, -40, -60)):
        axes[1].plot(t, db(envelope(t, 3.0, 0.25, de)), color=c, lw=1.4, label="Decay %d dB" % de)
    axes[1].set_title("Swell 25 %", fontsize=9, loc="left")
    for ax in axes:
        ax.set_xlabel(tr("time_length"))
        ax.set_ylim(-62, 3)
        ax.legend(fontsize=7.5, loc="lower center")
    axes[0].set_ylabel(tr("env_level"))
    save(fig, "fig_enveloppes.pdf")


def fig_spectre():
    fig, ax = plt.subplots(figsize=(6.6, 2.9))
    for c, colour in zip((ROSE, VIOLET, GREY, AMBER), (-6, -3, 0, 3)):
        h = ir(length=4.0, swell=0, decay=0, colour=colour)
        p = 0.5 * (np.abs(np.fft.rfft(h[:, 0])) ** 2 + np.abs(np.fft.rfft(h[:, 1])) ** 2)
        f = np.fft.rfftfreq(len(h), 1 / FS)
        sel = (f >= 15) & (f <= 22000)
        ps = smooth_octave(f[sel], p[sel])
        ref = np.interp(1000, f[sel], ps)
        ax.semilogx(f[sel], 10 * np.log10(ps / ref), color=c, lw=1.3, label=tr("colour") % colour)
        if colour == -3:
            filt = butterworth2(f[sel], 80, True) * butterworth2(f[sel], 8000, False)
            ax.semilogx(f[sel], 10 * np.log10(ps / ref * filt), color=DARK, lw=1.0, ls="--",
                        label=tr("pink_filtered"))
    ax.set_xlim(15, 22000)
    ax.set_ylim(-36, 48)
    ax.set_xlabel(tr("frequency"))
    ax.set_ylabel(tr("db_1k"))
    ax.legend(fontsize=7, loc="upper center", ncol=3)
    save(fig, "fig_spectre.pdf")


def fig_traitements():
    """time-frequency maps of responses with the treatments of the noise"""
    cases = (("t_damping", dict(colour=0, length=4, swell=30, decay=-24, damping=6, bloom=-60)),
             ("t_chord", dict(colour=0, length=4, swell=10, decay=-12, tune=2, note=50, chord=6, resonance=60,
                              lforate=0.5, lfopitch=0.5)),
             ("t_rhythm", dict(colour=0, length=3, swell=5, decay=-24, rhythm=4, duty=40)),
             ("t_density", dict(colour=0, length=3, swell=5, decay=-24, density=40)))
    fig, axes = plt.subplots(2, 2, figsize=(6.6, 5.0), sharey=True)
    for ax, (key, settings) in zip(axes.flat, cases):
        h = ir(**settings)[:, 0]
        t, f, d = stft_db(h)
        sel = (f >= 20) & (f <= 20000)
        d = d[sel] - d[sel].max()
        ax.grid(False)
        m = ax.pcolormesh(t, f[sel], d, vmin=-60, vmax=0, cmap="magma", shading="auto", rasterized=True)
        ax.set_yscale("log")
        ax.set_ylim(40, 20000)
        ax.set_title(tr(key), fontsize=8.5, loc="left")
        ax.grid(False)
    for ax in axes[1]:
        ax.set_xlabel(tr("time"))
    for ax in axes[:, 0]:
        ax.set_ylabel(tr("frequency"))
    fig.colorbar(m, ax=axes, shrink=0.8, label=tr("level_db"))
    fig.savefig(os.path.join(HERE, "fig_traitements.pdf" if LANG == "fr" else "fig_traitements_%s.pdf" % LANG))
    plt.close(fig)


def fig_stereo():
    h = ir(length=3, swell=0, decay=0, pandepth=100, panrate=1)
    n = int(0.02 * FS)
    fig, ax = plt.subplots(figsize=(6.6, 2.2))
    for col, c, key in ((0, VIOLET, "left"), (1, AMBER, "right")):
        e = np.convolve(h[:, col] ** 2, np.ones(n) / n, mode="same")
        t = np.arange(len(e)) / FS
        ax.plot(t, 10 * np.log10(np.maximum(e, 1e-30) / np.mean(e)), color=c, lw=1.2, label=tr(key))
    ax.set_ylim(-30, 8)
    ax.set_xlim(0, 3)
    ax.set_xlabel(tr("time"))
    ax.set_ylabel(tr("level_db"))
    ax.set_title(tr("pan_title"), fontsize=8.5, loc="left")
    ax.legend(fontsize=7.5, loc="lower right", ncol=2)
    save(fig, "fig_stereo.pdf")


def fig_mix():
    m = np.linspace(0, 100, 201)
    wet = np.sin(0.5 * np.pi * m / 100)
    dry = np.cos(0.5 * np.pi * m / 100)
    fig, ax = plt.subplots(figsize=(4.4, 2.5))
    ax.plot(m, db(dry), color=GREY, lw=1.4, label=tr("dry"))
    ax.plot(m, db(wet), color=VIOLET, lw=1.4, label="pad")
    ax.plot(m, 10 * np.log10(dry ** 2 + wet ** 2 + 1e-12), color=AMBER, lw=1.0, ls="--",
            label=tr("total"))
    ax.set_ylim(-40, 3)
    ax.set_xlabel("Mix (%)")
    ax.set_ylabel(tr("gain"))
    ax.legend(fontsize=7.5, loc="lower center")
    save(fig, "fig_mix.pdf")


def fig_pad():
    """two plucked notes, then the pad (Default preset, mix 100 %)"""
    n = int(5.0 * FS)
    t = np.arange(n) / FS
    x = np.zeros(n)
    for t0, f0 in ((0.2, 220.0), (1.6, 329.6)):
        tt = t - t0
        on = tt >= 0
        note = sum(np.sin(2 * np.pi * k * f0 * tt) / k for k in range(1, 9))
        x[on] += 0.3 * note[on] * np.exp(-tt[on] / 0.12)
    def convolve(h):
        nf = 1 << int(np.ceil(np.log2(n + len(h))))
        return np.fft.irfft(np.fft.rfft(x, nf) * np.fft.rfft(h, nf), nf)[:n]
    y = convolve(ir()[:, 0])
    z = convolve(ir(length=6, swell=45, decay=-18, tune=2, note=50, chord=6, resonance=60, lforate=0.2,
                    lfopitch=0.15)[:, 0])
    fig, axes = plt.subplots(3, 1, figsize=(6.6, 6.2), sharex=True)
    for ax, sig, name in ((axes[0], x, tr("input_notes")),
                          (axes[1], y, tr("output_pad")),
                          (axes[2], z, tr("output_chord"))):
        ax.specgram(sig + 1e-9 * np.random.default_rng(0).standard_normal(len(sig)), NFFT=2048, Fs=FS, noverlap=1536, cmap="magma", vmin=-110, vmax=-20, rasterized=True)
        ax.set_ylim(0, 3000)
        ax.set_ylabel(tr("frequency"))
        ax.set_title(name, fontsize=9, loc="left")
        ax.grid(False)
    axes[2].set_xlabel(tr("time"))
    save(fig, "fig_pad.pdf")


if __name__ == "__main__":
    for LANG in ("fr", "en"):
        fig_reponse()
        fig_enveloppes()
        fig_spectre()
        fig_mix()
        fig_pad()
        fig_traitements()
        fig_stereo()
    print("figures written in", HERE)
