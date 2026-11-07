#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Philip Bos
"""JAMTRACK's factory drum samples, made from nothing: modal membranes and shells, snare wires, metal partials for the
cymbals and hats, beaters, sticks and brushes, a little room. Every sound here is synthesized (CC0); none is recorded.

  .venv/bin/python tools/make_kits.py [--wav DIR]    -> build/kits.json (int16 PCM, base64), then
  node tools/encode_kits.mjs                         -> app/dsp/kits_data.h (EFB1 ADPCM, the firmware's)

--wav writes every sample as a WAV too, to listen to.
"""
import base64, json, os, sys, wave
import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
RNG = np.random.default_rng(20261107)

# ---------------------------------------------------------------- building blocks
def t_axis(sr, dur):
    return np.arange(int(sr * dur)) / sr

def noise(n):
    return RNG.standard_normal(n)

def fft_filter(x, sr, lo=None, hi=None, order=2, peaks=()):
    """zero-phase shaping in the frequency domain: high-pass at lo, low-pass at hi (Butterworth-like magnitudes of
    `order`), and peaks [(f, gain_db, q)]"""
    n = len(x)
    N = 1 << int(np.ceil(np.log2(n * 2)))
    X = np.fft.rfft(x, N)
    f = np.fft.rfftfreq(N, 1 / sr)
    H = np.ones_like(f)
    if lo:
        H *= 1 / np.sqrt(1 + (lo / np.maximum(f, 1e-3)) ** (2 * order))
    if hi:
        H *= 1 / np.sqrt(1 + (f / hi) ** (2 * order))
    for (fc, g, q) in peaks:
        bw = fc / q
        H *= 1 + (10 ** (g / 20) - 1) * np.exp(-0.5 * ((f - fc) / (bw / 2)) ** 2)
    return np.fft.irfft(X * H, N)[:n]

def env(t, attack, decay):
    a = np.clip(t / max(attack, 1e-5), 0, 1)
    return a * np.exp(-t / decay)

def glide_phase(t, sr, f0, f1, tau):
    """phase (radians) of a sine gliding from f1 down to f0 with time constant tau"""
    f = f0 + (f1 - f0) * np.exp(-t / tau)
    return 2 * np.pi * np.cumsum(f) / sr

def modes(t, sr, f0, ratios, amps, decays, glide=0.0, gtau=0.04, phases=None):
    out = np.zeros_like(t)
    for i, (r, a, d) in enumerate(zip(ratios, amps, decays)):
        ph = glide_phase(t, sr, f0 * r, f0 * r * (1 + glide), gtau)
        p0 = 0 if phases is None else phases[i]
        out += a * np.sin(ph + p0) * np.exp(-t / d)
    return out

def metal(t, sr, n, lo, hi, d_lo, d_hi, tilt=0.5, seed=0):
    """n inharmonic partials between lo and hi Hz, the high ones dying faster"""
    r = np.random.default_rng(seed)
    fs = np.exp(r.uniform(np.log(lo), np.log(hi), n))
    out = np.zeros_like(t)
    for f in fs:
        k = (np.log(f) - np.log(lo)) / (np.log(hi) - np.log(lo))
        d = d_lo * (d_hi / d_lo) ** k
        a = (f / lo) ** (-tilt) * r.uniform(0.5, 1.0)
        out += a * np.sin(2 * np.pi * f * t + r.uniform(0, 2 * np.pi)) * np.exp(-t / d)
    return out / np.sqrt(n)

def squares(t, freqs, sr):
    out = np.zeros_like(t)
    for f in freqs:
        k = 1
        while f * k < sr * 0.45:
            out += np.sin(2 * np.pi * f * k * t) / k
            k += 2
    return out

def room(x, sr, size=0.25, mix=0.25, dark=6000):
    """a small synthetic room: early reflections and a short diffuse tail"""
    n = int(sr * (0.05 + size))
    tt = np.arange(n) / sr
    ir = noise(n) * np.exp(-tt / (size * 0.3))
    ir = fft_filter(ir, sr, lo=150, hi=dark)
    for d, g in ((0.011, 0.5), (0.017, 0.35), (0.023, 0.3), (0.031, 0.2)):
        k = int(d * size * 4 * sr)
        if k < n:
            ir[k] += g * 8
    ir /= np.sqrt(np.sum(ir ** 2)) + 1e-9
    N = 1 << int(np.ceil(np.log2(len(x) + n)))
    wet = np.fft.irfft(np.fft.rfft(x, N) * np.fft.rfft(ir, N), N)[:len(x)]
    wet /= np.max(np.abs(wet)) + 1e-9
    return x / (np.max(np.abs(x)) + 1e-9) + mix * wet

def sat(x, drive):
    return np.tanh(x * drive) / np.tanh(drive)

def crush(x, bits, sr, to_sr=None):
    if to_sr:                            # sample and hold at a lower rate (an old sampler's)
        idx = (np.floor(np.arange(len(x)) * to_sr / sr) * sr / to_sr).astype(int)
        x = x[np.clip(idx, 0, len(x) - 1)]
    q = 2 ** (bits - 1)
    return np.round(x * q) / q

def mulaw8(x):                           # an 8-bit companded sampler
    mu = 255.0
    y = np.sign(x) * np.log1p(mu * np.abs(np.clip(x, -1, 1))) / np.log1p(mu)
    y = np.round(y * 127) / 127
    return np.sign(y) * (np.expm1(np.abs(y) * np.log1p(mu)) / mu)

def finish(x, sr, fade=0.02, gate=None):
    x = x - np.mean(x[: min(len(x), 64)]) * 0
    if gate:
        g = int(gate * sr)
        if g < len(x):
            f = np.linspace(1, 0, min(len(x) - g, int(0.01 * sr)))
            x = x.copy()
            x[g:g + len(f)] *= f
            x[g + len(f):] = 0
    nf = int(fade * sr)
    if nf and nf < len(x):
        x = x.copy()
        x[-nf:] *= np.linspace(1, 0, nf) ** 2
    # trim the silent tail
    a = np.abs(x)
    thr = np.max(a) * 10 ** (-58 / 20)
    idx = np.nonzero(a > thr)[0]
    end = min(len(x), (idx[-1] + int(0.005 * sr)) if len(idx) else len(x))
    x = x[:max(end, 256)]
    x = x / (np.max(np.abs(x)) + 1e-9) * 0.89
    return x

def shaped(sr, dur, center, width, tau_lo, tau_hi, attack=0.001, f_lo=300.0, f_hi=None, seed=0, early=0.0,
           shimmer=0.25, frame=1024, hop=256):
    """noise with a spectrum that lives and fades as a cymbal's does (overlap-added short spectra): a broad hump around
    `center` Hz, `width` octaves either side; each frequency decays with its own time, tau_lo at f_lo .. tau_hi at
    f_hi (the highs die first, the sound darkens as it rings); `early`: extra highs in the first 40 ms (the stick's
    splash); `shimmer`: slow random wobble of each band (the plate's modes beating)"""
    r = np.random.default_rng(seed)
    f_hi = f_hi or sr * 0.47
    n = int(sr * dur)
    nfr = n // hop + 2
    f = np.fft.rfftfreq(frame, 1 / sr)
    lf = np.log2(np.maximum(f, 20) / center)
    S = np.exp(-0.5 * (lf / width) ** 2)
    S *= 1 / np.sqrt(1 + (f_lo / np.maximum(f, 1)) ** 6) / np.sqrt(1 + (f / f_hi) ** 8)
    k = np.clip((np.log2(np.maximum(f, f_lo)) - np.log2(f_lo)) / (np.log2(f_hi) - np.log2(f_lo)), 0, 1)
    tau = tau_lo * (tau_hi / tau_lo) ** k
    wob = np.ones((nfr, len(f)))
    if shimmer:                          # slow band wobble: smoothed noise along time and frequency
        w = r.standard_normal((nfr, len(f)))
        for _ in range(6):
            w = 0.5 * w + 0.25 * (np.roll(w, 1, 0) + np.roll(w, -1, 0))
        for _ in range(3):
            w = 0.5 * w + 0.25 * (np.roll(w, 1, 1) + np.roll(w, -1, 1))
        wob = 1 + shimmer * w / (np.std(w) + 1e-9)
    out = np.zeros(nfr * hop + frame)
    win = np.hanning(frame)
    for i in range(nfr):
        t = i * hop / sr
        mag = S * np.exp(-t / tau) * min(1.0, t / attack if attack > 0 else 1.0) * np.clip(wob[i], 0, None)
        if early:
            mag = mag * (1 + early * np.exp(-t / 0.04) * k)
        spec = mag * np.exp(2j * np.pi * r.random(len(f)))
        out[i * hop:i * hop + frame] += np.fft.irfft(spec, frame) * win
    return out[:n] / (np.max(np.abs(out[:n])) + 1e-12)

S = {}                                   # name -> (rate, samples)

def add(name, sr, x, **kw):
    S[name] = (sr, finish(x, sr, **kw))

# ---------------------------------------------------------------- kicks (16 kHz)
def kick(sr, f0, f1, ptau, decay, click, click_hi, shell=0.25, dur=0.6, roomy=0.0, drive=1.5, beater=0.004):
    t = t_axis(sr, dur)
    body = np.sin(glide_phase(t, sr, f0, f1, ptau)) * env(t, 0.0015, decay)
    body += shell * np.sin(glide_phase(t, sr, f0 * 1.58, f1 * 1.4, ptau)) * env(t, 0.001, decay * 0.35)
    cl = fft_filter(noise(len(t)), sr, lo=800, hi=click_hi) * np.exp(-t / beater)
    cl += 0.6 * np.sin(2 * np.pi * min(click_hi * 0.4, 1600) * t) * np.exp(-t / (beater * 0.8))
    x = body + click * cl
    x = sat(x, drive)
    if roomy:
        x = room(x, sr, 0.22, roomy, 3500)
    return x

def make_kicks():
    sr = 16000
    add("KICK_VINTAGE", sr, kick(sr, 56, 118, 0.028, 0.13, 0.35, 2200, shell=0.35, dur=0.45, drive=1.8, beater=0.006))
    add("KICK_STUDIO", sr, kick(sr, 52, 135, 0.03, 0.2, 0.55, 5000, dur=0.55, drive=1.6))
    add("KICK_ROOM", sr, kick(sr, 50, 125, 0.035, 0.26, 0.5, 4500, dur=0.7, roomy=0.4, drive=1.4))
    add("KICK_SOFT", sr, kick(sr, 68, 110, 0.03, 0.22, 0.15, 1800, shell=0.4, dur=0.55, drive=1.1, beater=0.008))
    add("KICK_ROCK", sr, kick(sr, 50, 150, 0.025, 0.24, 0.9, 6500, dur=0.6, roomy=0.2, drive=2.2, beater=0.003))
    x = kick(sr, 55, 115, 0.03, 0.15, 0.3, 2000, shell=0.3, dur=0.5, drive=2.4)
    add("KICK_LOFI", sr, crush(fft_filter(x, sr, hi=2600), 10, sr, 11000))
    x = kick(sr, 54, 160, 0.02, 0.16, 0.8, 7000, dur=0.45, drive=2.0, beater=0.003)
    add("KICK_LM", sr, mulaw8(fft_filter(x, sr, hi=9000)), gate=0.3)
    t = t_axis(sr, 0.35)                 # the rhythm box's: a damped resonance struck
    add("KICK_BOX", sr, np.sin(glide_phase(t, sr, 78, 92, 0.01)) * env(t, 0.001, 0.07)
        + 0.1 * fft_filter(noise(len(t)), sr, hi=1200) * np.exp(-t / 0.003))
    t = t_axis(sr, 0.9)
    add("KICK_808", sr, sat(np.sin(glide_phase(t, sr, 49, 95, 0.018)) * env(t, 0.001, 0.42), 1.3)
        + 0.08 * np.sin(2 * np.pi * 1200 * t) * np.exp(-t / 0.002))
    t = t_axis(sr, 0.4)                  # the cajon's bass: the front plate's low modes, a palm
    x = modes(t, sr, 82, [1, 1.7, 2.6], [1, 0.4, 0.15], [0.12, 0.06, 0.04], glide=0.12, gtau=0.02)
    x += 0.25 * fft_filter(noise(len(t)), sr, lo=200, hi=900) * np.exp(-t / 0.012)
    add("KICK_CAJON", sr, room(x, sr, 0.12, 0.15, 3000))

# ---------------------------------------------------------------- snares (22.05 kHz)
def snare(sr, f0, body, body_d, wires, wire_d, wire_lo, wire_hi, crack=0.4, dur=0.45, roomy=0.0, drive=1.3, gate=None):
    t = t_axis(sr, dur)
    b = modes(t, sr, f0, [1, 1.59, 2.14, 2.3, 2.65], [1, 0.5, 0.3, 0.25, 0.15],
              [body_d, body_d * 0.6, body_d * 0.45, body_d * 0.4, body_d * 0.35], glide=0.02, gtau=0.015)
    # the wires: a burst that rattles on a little longer, darkening as it goes
    w = shaped(sr, dur, 5000, 1.3, wire_d * 1.3, wire_d * 0.6, attack=0.0015, f_lo=wire_lo, f_hi=wire_hi, seed=int(f0),
               early=0.8, shimmer=0.15)
    w *= 1 + 0.6 * np.exp(-t / 0.012)
    c = fft_filter(noise(len(t)), sr, lo=2000, hi=9000) * np.exp(-t / 0.0025)
    x = body * b / (np.max(np.abs(b)) + 1e-9) + wires * w / (np.max(np.abs(w)) + 1e-9) + crack * c / (np.max(np.abs(c)) + 1e-9)
    x = sat(x, drive)
    if roomy:
        x = room(x, sr, 0.3, roomy, 5000)
    return x

def make_snares():
    sr = 22050
    add("SNARE_VINTAGE", sr, fft_filter(snare(sr, 205, 1.0, 0.07, 0.55, 0.09, 1200, 6000, crack=0.25, dur=0.35, drive=1.6), sr, hi=7500))
    add("SNARE_STUDIO", sr, snare(sr, 190, 0.9, 0.11, 0.9, 0.16, 1500, 9500, crack=0.45))
    add("SNARE_ROOM", sr, snare(sr, 180, 0.9, 0.13, 0.9, 0.2, 1400, 9000, crack=0.4, dur=0.6, roomy=0.45))
    t = t_axis(sr, 0.5)                  # brushes: a slap that spreads, little body
    w = fft_filter(noise(len(t)), sr, lo=900, hi=7000, peaks=((3000, 2, 1),)) * env(t, 0.004, 0.13)
    w += 0.35 * fft_filter(noise(len(t)), sr, lo=2000, hi=6000) * env(t, 0.03, 0.25)
    w += 0.3 * modes(t, sr, 200, [1, 1.59], [1, 0.5], [0.06, 0.04])
    add("SNARE_BRUSH", sr, w)
    add("SNARE_ROCK", sr, snare(sr, 175, 1.1, 0.14, 1.0, 0.22, 1300, 10000, crack=0.7, dur=0.6, roomy=0.3, drive=2.0))
    x = snare(sr, 210, 1.0, 0.08, 0.6, 0.1, 1200, 6000, crack=0.2, dur=0.35, drive=2.0)
    add("SNARE_LOFI", sr, crush(fft_filter(x, sr, lo=150, hi=4200), 10, sr, 12000))
    x = snare(sr, 200, 1.0, 0.09, 1.0, 0.14, 1500, 10000, crack=0.6, dur=0.3, drive=1.8)
    add("SNARE_LM", sr, mulaw8(room(x, sr, 0.15, 0.25, 7000)), gate=0.17)
    t = t_axis(sr, 0.25)
    x = fft_filter(noise(len(t)), sr, lo=1000, hi=8000) * env(t, 0.001, 0.05)
    x += 0.5 * np.sin(2 * np.pi * 250 * t) * np.exp(-t / 0.03)
    add("SNARE_BOX", sr, x)
    t = t_axis(sr, 0.35)
    x = 0.8 * np.sin(2 * np.pi * 180 * t) * np.exp(-t / 0.05) + 0.4 * np.sin(2 * np.pi * 330 * t) * np.exp(-t / 0.035)
    x += fft_filter(noise(len(t)), sr, lo=1500, hi=9000) * env(t, 0.001, 0.09)
    add("SNARE_808", sr, x)
    t = t_axis(sr, 0.35)                 # the cajon's slap: the top edge, its wires inside
    x = modes(t, sr, 380, [1, 2.3, 3.1], [1, 0.6, 0.4], [0.04, 0.025, 0.02])
    x += 0.9 * fft_filter(noise(len(t)), sr, lo=1200, hi=6000) * env(t, 0.001, 0.035)
    x += 0.35 * fft_filter(noise(len(t)), sr, lo=2500, hi=8000) * env(t, 0.004, 0.09)
    add("SNARE_CAJON", sr, room(x, sr, 0.12, 0.15, 5000))

# ---------------------------------------------------------------- rims and claps
def make_rims():
    sr = 22050
    t = t_axis(sr, 0.2)                  # a cross-stick: the stick's wood on the rim, the shell answering
    x = modes(t, sr, 520, [1, 3.1, 5.0, 6.7], [0.5, 1, 0.6, 0.3], [0.03, 0.02, 0.012, 0.01])
    x += 0.6 * fft_filter(noise(len(t)), sr, lo=1500, hi=8000) * np.exp(-t / 0.002)
    add("STICK", sr, room(x, sr, 0.15, 0.2, 6000))
    t = t_axis(sr, 0.3)
    x = np.zeros_like(t)
    for k, d in enumerate((0.0, 0.011, 0.021, 0.034)):
        x += (1 if k == 3 else 0.7) * env(np.maximum(t - d, 0), 0.0005, 0.006 if k < 3 else 0.06) * (t >= d)
    x *= fft_filter(noise(len(t)), sr, lo=900, hi=5000, peaks=((1500, 4, 2),))
    add("CLAP", sr, room(x, sr, 0.2, 0.3, 6000))
    t = t_axis(sr, 0.12)
    x = np.sin(2 * np.pi * 1700 * t) * np.exp(-t / 0.012) + 0.6 * np.sin(2 * np.pi * 480 * t) * np.exp(-t / 0.02)
    add("RIM_BOX", sr, x)
    t = t_axis(sr, 0.18)
    x = fft_filter(noise(len(t)), sr, lo=1500, hi=5000, peaks=((2600, 6, 3),)) * env(t, 0.0008, 0.012)
    x += 0.3 * np.sin(2 * np.pi * 2600 * t) * np.exp(-t / 0.02)
    add("SNAP", sr, room(x, sr, 0.15, 0.25, 7000))

# ---------------------------------------------------------------- hats (32 kHz)
def hat(sr, decay, dur, noise_amt=0.6, lo=6500, seed=1, chick=0.0):
    t = t_axis(sr, dur)
    m = metal(t, sr, 48, 3000, 15000, decay * 1.6, decay * 0.6, tilt=0.2, seed=seed)
    n = fft_filter(noise(len(t)), sr, lo=lo, hi=15000) * np.exp(-t / decay)
    x = m / (np.max(np.abs(m)) + 1e-9) + noise_amt * n / (np.max(np.abs(n)) + 1e-9)
    x *= np.clip(t / 0.0008, 0, 1)
    if chick:                            # the pedal's foot: the plates meeting
        x += chick * fft_filter(noise(len(t)), sr, lo=500, hi=4000) * np.exp(-t / 0.004)
    return fft_filter(x, sr, lo=lo * 0.8)

def ac_hat(sr, decay, dur, center=8500, seed=1, chick=0.25, metal_amt=0.25):
    """an acoustic hi-hat: two plates' noise around 7-10 kHz fading fast, a little of their metal ring, the stick"""
    t = t_axis(sr, dur)
    x = shaped(sr, dur, center, 0.9, decay * 1.6, decay * 0.7, attack=0.0006, f_lo=3500, seed=seed, early=1.5, shimmer=0.3)
    m = fft_filter(squares(t, [317, 461, 553, 637, 811, 997], sr), sr, lo=6000, hi=13000) * np.exp(-t / (decay * 0.8))
    x = x + metal_amt * m / (np.max(np.abs(m)) + 1e-9)
    x += chick * fft_filter(noise(len(t)), sr, lo=2500, hi=9000) * np.exp(-t / 0.0025)
    return fft_filter(x, sr, lo=3000, order=3)

def make_hats():
    sr = 32000
    add("HAT_TIGHT", sr, ac_hat(sr, 0.03, 0.2, seed=3))
    add("HAT_LOOSE", sr, ac_hat(sr, 0.07, 0.32, center=7500, seed=4, chick=0.2))
    add("OPEN_A", sr, ac_hat(sr, 0.28, 0.8, center=7000, seed=5, chick=0.15, metal_amt=0.3), fade=0.12)
    t = t_axis(sr, 0.2)
    x = squares(t, [205.3, 304.4, 369.6, 522.7, 540.0, 800.0], sr)
    x = fft_filter(x, sr, lo=7000, hi=12000) * env(t, 0.0005, 0.035)
    add("HAT_808", sr, x)
    x = hat(sr, 0.04, 0.16, 1.0, lo=7000, seed=7)
    add("HAT_LM", sr, mulaw8(x))
    x = hat(sr, 0.1, 0.5, 1.0, lo=6500, seed=8)
    add("OPEN_LM", sr, mulaw8(x), fade=0.08)
    t = t_axis(sr, 0.1)
    add("HAT_BOX", sr, fft_filter(noise(len(t)), sr, lo=8000, hi=13000, peaks=((10000, 4, 3),)) * env(t, 0.0004, 0.012))

# ---------------------------------------------------------------- cymbals (24 kHz)
def make_cymbals():
    """cymbals: a wash of noise shaped like a cymbal's spectrum (a broad hump at 4-7 kHz) whose highs fade first, with
    the plates' beating; the stick's ping; a ride's bell as a few clear partials. 24 kHz (nothing above 11 kHz),
    stored as 8-bit mu-law (encode_kits.mjs): ADPCM hisses on bright sounds"""
    sr = 24000
    d = 1.25
    t = t_axis(sr, d)
    ping = fft_filter(noise(len(t)), sr, lo=2500, hi=9000, peaks=((4200, 6, 2),)) * np.exp(-t / 0.004)
    bell = metal(t, sr, 5, 2300, 3300, 0.9, 0.7, tilt=0.0, seed=11)
    wash = shaped(sr, d, 5500, 1.1, 1.3, 0.35, attack=0.002, f_lo=800, seed=21, early=0.8, shimmer=0.3)
    x = wash + 0.18 * bell / np.max(np.abs(bell)) + 0.35 * ping
    add("RIDE_ROCK", sr, fft_filter(x, sr, lo=600, order=2), fade=0.25)
    wash = shaped(sr, d, 4200, 1.2, 1.6, 0.4, attack=0.003, f_lo=500, f_hi=9500, seed=22, early=0.5, shimmer=0.35)
    x = wash + 0.06 * bell / np.max(np.abs(bell)) + 0.25 * ping
    add("RIDE_JAZZ", sr, fft_filter(x, sr, lo=400, order=2), fade=0.3)
    d = 1.45
    t = t_axis(sr, d)
    tk = fft_filter(noise(len(t)), sr, lo=2000, hi=10000) * np.exp(-t / 0.003)
    x = shaped(sr, d, 5000, 1.4, 1.1, 0.3, attack=0.004, f_lo=400, seed=23, early=1.2, shimmer=0.4) + 0.3 * tk
    add("CRASH_A", sr, fft_filter(x, sr, lo=350, order=2), fade=0.35)
    x = shaped(sr, d, 3600, 1.4, 1.4, 0.35, attack=0.012, f_lo=300, f_hi=9000, seed=24, early=0.6, shimmer=0.4) + 0.15 * tk
    add("CRASH_DARK", sr, fft_filter(x, sr, lo=250, order=2), fade=0.4)

# ---------------------------------------------------------------- toms (16 kHz)
def tom(sr, f0, decay, dur=0.6, glide=0.12, stick=0.3, roomy=0.0):
    t = t_axis(sr, dur)
    x = modes(t, sr, f0, [1, 1.59, 2.14, 2.3], [1, 0.45, 0.25, 0.2], [decay, decay * 0.6, decay * 0.4, decay * 0.35],
              glide=glide, gtau=0.05)
    x += stick * fft_filter(noise(len(t)), sr, lo=1000, hi=6000) * np.exp(-t / 0.003)
    x = sat(x, 1.3)
    if roomy:
        x = room(x, sr, 0.25, roomy, 4000)
    return x

def make_toms():
    sr = 16000
    add("TOM_VINTAGE", sr, tom(sr, 118, 0.14, 0.45, glide=0.08, stick=0.25))
    add("TOM_STUDIO", sr, tom(sr, 105, 0.24, stick=0.35))
    add("TOM_ROOM", sr, tom(sr, 92, 0.3, 0.8, roomy=0.35))
    x = tom(sr, 125, 0.2, 0.5, glide=0.25, stick=0.4)
    add("TOM_LM", sr, mulaw8(x))
    t = t_axis(sr, 0.6)
    add("TOM_808", sr, np.sin(glide_phase(t, sr, 110, 150, 0.06)) * env(t, 0.001, 0.22))

# ---------------------------------------------------------------- percussion (22.05 kHz)
def make_percs():
    sr = 22050
    t = t_axis(sr, 0.22)                 # a shaker: the beads' shuffle rising and falling
    e = env(t, 0.012, 0.045)
    add("SHAKER", sr, fft_filter(noise(len(t)), sr, lo=3500, hi=12000, peaks=((6500, 3, 1.5),)) * e)
    t = t_axis(sr, 0.4)
    j = metal(t, sr, 24, 4500, 10000, 0.18, 0.08, tilt=0.1, seed=21)
    x = j / np.max(np.abs(j)) + 0.6 * fft_filter(noise(len(t)), sr, lo=5000, hi=11000) * env(t, 0.001, 0.05)
    add("TAMB", sr, x * env(t, 0.0015, 0.2))
    t = t_axis(sr, 0.4)
    x = fft_filter(squares(t, [540, 800], sr), sr, lo=500, hi=3500, peaks=((800, 3, 2),)) * env(t, 0.0005, 0.09)
    add("COWBELL", sr, x)
    t = t_axis(sr, 0.4)
    x = modes(t, sr, 230, [1, 1.5, 2.1], [1, 0.4, 0.2], [0.12, 0.06, 0.04], glide=0.05)
    x += 0.4 * fft_filter(noise(len(t)), sr, lo=1500, hi=6000) * np.exp(-t / 0.004)
    add("CONGA", sr, x)
    t = t_axis(sr, 0.12)
    add("CABASA", sr, fft_filter(noise(len(t)), sr, lo=5000, hi=13000) * env(t, 0.003, 0.025))
    t = t_axis(sr, 0.22)                 # a guiro: a scrape of little clicks
    x = np.zeros_like(t)
    for k in range(14):
        d = 0.006 + k * 0.011
        x += np.exp(-np.maximum(t - d, 0) / 0.003) * (t >= d)
    x *= fft_filter(noise(len(t)), sr, lo=1500, hi=6000, peaks=((3000, 6, 2),))
    add("GUIRO", sr, x * np.exp(-t / 0.15))
    t = t_axis(sr, 0.4)                  # a brush's sweep
    add("SWISH", sr, fft_filter(noise(len(t)), sr, lo=1500, hi=7000) * env(t, 0.05, 0.09))

def main():
    make_kicks(); make_snares(); make_rims(); make_hats(); make_cymbals(); make_toms(); make_percs()
    out = {}
    for name, (sr, x) in S.items():
        pcm = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
        out[name] = {"rate": sr, "pcm": base64.b64encode(pcm.tobytes()).decode()}
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    with open(os.path.join(ROOT, "build", "kits.json"), "w") as f:
        json.dump(out, f)
    if "--wav" in sys.argv:
        d = sys.argv[sys.argv.index("--wav") + 1]
        os.makedirs(d, exist_ok=True)
        for name, (sr, x) in S.items():
            with wave.open(os.path.join(d, name.lower() + ".wav"), "wb") as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
                w.writeframes(np.clip(np.round(x * 32767), -32768, 32767).astype("<i2").tobytes())
    total = sum(len(x) for _, (sr, x) in S.items())
    print(f"{len(S)} samples, {total} frames, about {total // 2 // 1024} KB as ADPCM")

if __name__ == "__main__":
    main()
