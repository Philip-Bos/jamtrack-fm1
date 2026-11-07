#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""stdin hex lines -> the FM-1 (USB-MIDI); the FM-1's SysEx -> stdout hex lines. For node tests of the companion's
protocol client against a real FM-1 (app/tests/device_test.mjs)."""
import sys, threading, mido
ins = [n for n in mido.get_input_names() if "Felucca" in n or "FM-1" in n or "JAMTRACK" in n]
if not ins:
    sys.exit("no FM-1")
inp, out = mido.open_input(ins[0]), mido.open_output([n for n in mido.get_output_names() if n.split(" ")[0] == ins[0].split(" ")[0]][0])
def rx():
    for m in inp:
        if m.type == "sysex":
            sys.stdout.write("f0" + bytes(m.data).hex() + "f7\n"); sys.stdout.flush()
threading.Thread(target=rx, daemon=True).start()
print("ready", flush=True)
for line in sys.stdin:
    b = bytes.fromhex(line.strip())
    if b and b[0] == 0xF0:
        out.send(mido.Message("sysex", data=list(b[1:-1])))
    elif b:
        out.send(mido.Message.from_bytes(list(b)))
