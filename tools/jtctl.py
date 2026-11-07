#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Philip Bos
"""JAMTRACK from the command line, over USB-MIDI (the companion's protocol, app/proto.c):

  .venv/bin/python tools/jtctl.py hello | status | stats | play | stop | part | fill | load SLOT | gen GENRE
                                 watch SECONDS   (status and CPU once a second)
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "base", "tools"))
import fm1_install as I  # noqa: E402


def pack7(b):
    out, acc, nb = [], 0, 0
    for x in b:
        acc |= x << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return out


def unpack7(b):
    out, acc, nb = [], 0, 0
    for x in b:
        acc |= x << nb
        nb += 7
        if nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


class Link:
    def __init__(self):
        import mido
        self.mido = mido
        ins = [n for n in mido.get_input_names() if I.PORT_RE.search(n)] if hasattr(I, "PORT_RE") else []
        if not ins:
            ins = [n for n in mido.get_input_names() if "FM-1" in n or "Felucca" in n or "JAMTRACK" in n]
        if not ins:
            sys.exit("no FM-1 found")
        self.inp = mido.open_input(ins[0])
        outs = [n for n in mido.get_output_names() if n.split(" ")[0] == ins[0].split(" ")[0]] or mido.get_output_names()
        self.out = mido.open_output(outs[0])

    def cmd(self, cmd, body=b"", timeout=1.5):
        msg = bytes([0, 0x59, cmd, len(body) & 0xFF, (len(body) >> 8) & 0xFF, 0]) + bytes(body)
        msg += bytes([(~sum(body)) & 0xFF])
        while self.inp.poll():
            pass
        self.out.send(self.mido.Message("sysex", data=pack7(msg)))
        t0 = time.time()
        while time.time() - t0 < timeout:
            m = self.inp.poll()
            if not m:
                time.sleep(0.002)
                continue
            if m.type != "sysex":
                continue
            d = unpack7(m.data)
            if len(d) >= 7 and d[0] == 0 and d[1] == 0x59 and d[2] == cmd:
                n = d[3] | d[4] << 8
                return d[6:6 + n]
        return None


def status(L):
    s = L.cmd(0x75)
    if not s:
        return "no answer"
    bpm = (s[11] | s[12] << 8) / 10
    return (f"playing {s[1]} part {'AB'[s[2] & 1]}{' queued' if s[3] else ''} chord {s[4] + 1} beat {s[5] + 1} "
            f"bar {s[9] | s[10] << 8} bpm {bpm:.1f}{' EXT' if s[7] else ''} cpu {s[15]}% peak {s[16] * 4 / 10:.0f}% "
            f"levels {list(s[18:22])}")


def stats(L):
    s = L.cmd(0x7D)
    if not s:
        return "no answer"
    v = [int.from_bytes(s[1 + 4 * i:5 + 4 * i], "little") for i in range(6)]
    return f"render peak {v[0]} us of 5805, voices {v[1]} us, cpu {v[2]}%, xruns {v[3]}"


def main():
    a = sys.argv[1:] or ["status"]
    L = Link()
    while a:
        c = a.pop(0)
        if c == "hello":
            h = L.cmd(0x70)
            print(h[1:5], "proto", h[5], "version", bytes(h[6:22]).split(b"\0")[0].decode() if h else "")
        elif c == "status":
            print(status(L))
        elif c == "stats":
            print(stats(L))
        elif c == "play":
            L.cmd(0x76, [0])
        elif c == "stop":
            L.cmd(0x76, [2])
        elif c == "part":
            L.cmd(0x76, [3, 2])
        elif c == "fill":
            L.cmd(0x76, [4])
        elif c == "load":
            L.cmd(0x76, [10, int(a.pop(0), 0)])
        elif c == "gen":
            L.cmd(0x76, [9, int(a.pop(0)), 0])
        elif c == "wait":
            time.sleep(float(a.pop(0)))
        elif c == "watch":
            for _ in range(int(a.pop(0))):
                print(status(L), "|", stats(L))
                time.sleep(1)
        else:
            sys.exit(f"unknown: {c}")


if __name__ == "__main__":
    main()
