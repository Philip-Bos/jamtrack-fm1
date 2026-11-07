#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Record the FM-1's USB audio and report its level and spectrum:  .venv/bin/python tools/rec.py SECONDS OUT.wav"""
import sys, wave
import numpy as np
import sounddevice as sd
sec = float(sys.argv[1]); out = sys.argv[2]
dev = [i for i, d in enumerate(sd.query_devices()) if "FM-1" in d["name"] and d["max_input_channels"] >= 2][0]
x = sd.rec(int(sec * 44100), samplerate=44100, channels=2, device=dev, dtype="float32"); sd.wait()
with wave.open(out, "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())
m = x.mean(1)
rms = 20 * np.log10(np.sqrt(np.mean(m ** 2)) + 1e-12)
X = np.abs(np.fft.rfft(m * np.hanning(len(m)))) ** 2
f = np.fft.rfftfreq(len(m), 1 / 44100)
bands = [(20, 200), (200, 1000), (1000, 4000), (4000, 8000), (8000, 12000), (12000, 20000)]
tot = X.sum() + 1e-20
print(f"rms {rms:.1f} dBFS peak {20*np.log10(np.abs(x).max()+1e-12):.1f} dBFS  " +
      " ".join(f"{a}-{b}:{10*np.log10(X[(f>=a)&(f<b)].sum()/tot+1e-20):.0f}dB" for a, b in bands))
