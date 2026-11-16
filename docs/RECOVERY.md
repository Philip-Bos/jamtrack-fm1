# If something goes wrong

Work down this list: each step is for an FM-1 the one before cannot reach.

## 1. The install stopped halfway

The FM-1 stays in its **update loader** (dark screen, or UPDATING). Plug it back in, open the flasher (the companion's **Install / update** button, on a computer),
press **Find the FM-1**: it says "the update loader", and **Resume** finishes the install. Nothing else is needed; the
boot area was never touched.

## 2. JAMTRACK (or another firmware) does not start

After two failed starts the FM-1 opens **SAFE MODE**: no sound, USB on. The flasher finds it: install JAMTRACK again,
or the stock firmware. In SAFE MODE, PLAY tries the firmware once more.

## 3. Back to M-VAVE's firmware

Download M-VAVE's FM-1 firmware (m-vave.com → Download → PC Firmware), then either the flasher's
**Back to the stock firmware**, or M-VAVE's own updater **M-UPGRADE**. Jams kept by JAMTRACK may be lost:
back them up first (companion → Library → Back up all as a file).

## 4. Nothing answers on USB

After four failed starts the chip enters its own update mode (the mask ROM's "UBOOT": it appears on USB as
`WL80UBOOT1.00`, 4C4A:8057). From a Mac or Linux, felucca-bsp's rescue script puts stock firmware back with no extra
hardware:

    python3 -m venv ~/fm1-rescue && ~/fm1-rescue/bin/pip install pyusb libusb-package
    sudo ~/fm1-rescue/bin/python base/bsp/tools/fm1_rescue.py V15.fwsc V16.fwsc            # checks, backs up, writes nothing
    sudo ~/fm1-rescue/bin/python base/bsp/tools/fm1_rescue.py V15.fwsc V16.fwsc --write    # then writes stock

It reads and saves the whole flash before anything, never writes the boot area, writes only the sectors that differ
and reads everything back. Give it the stock packages you have; it picks the one matching the FM-1's boot area.

On a newer FM-1 (the UC25HQ80 flash chip, 0xB36014, in units sold since autumn 2026), use the script from
[felucca-bsp's main branch](https://github.com/hugelton/felucca-bsp/blob/main/tools/fm1_rescue.py): the copy in
JAMTRACK's `base/bsp` predates that chip and refuses it. On Windows, or without Python,
[The Firmware Hub's rescue page](https://thefirmwarehub.com/tools/rescue/#chip) does the same from the browser.

## A newer FM-1 that refuses every install

JAMTRACK knows both flash chips, the first one (Puya P25Q80, 0x856014) and the newer one (UC25HQ80, 0xB36014): it
saves and updates on either. Some other firmwares know only the first chip. On a newer FM-1 they keep saves in RAM
and never start their update loader, so every installer stops before writing anything ("the device stopped after 0
requests"), JAMTRACK's flasher included. Put the FM-1 back to stock through the chip's own update mode (step 4), then
install JAMTRACK.

## 5. Last resort

[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) with an RP2040 board reads and writes the whole
flash. Not needed so far by any FM-1 running a firmware built on felucca-bsp's update path.
