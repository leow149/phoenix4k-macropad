# Phoenix4K

Open-source firmware for a 4-key USB macropad built around the WCH CH551G (an 8051-core USB
microcontroller). Started life as a commercial SayoDevice macropad; this replaces its stock
firmware entirely with a small, fully open, dynamically-reconfigurable one — no vendor tools,
no cloud account, no proprietary config app required.

## Features

- Standard USB HID keyboard — works everywhere, no drivers, no special software needed to just
  use it as a keyboard.
- 2 layers, with a configurable key that momentarily shifts to the second layer while held.
- Modifier support (Ctrl / Shift / Alt / GUI combos, not just plain keys).
- Per-key macros, up to 6 keystrokes, firing once per press.
- Media/consumer keys (volume, play/pause, track skip, etc.), independent per layer.
- **Fully live reconfiguration** — remap any key, change layers, edit macros, from the included
  web app, with no reflashing. Settings persist in the chip's own EEPROM.
- **Zero-driver on every OS**, including Windows — the config channel is implemented as a second
  HID collection (a vendor-defined usage page, the same approach SayoDevice's own firmware uses),
  not a raw vendor-class USB interface. Every OS already has a generic HID driver built in.

## Repository layout

```
firmware/     The actual macropad firmware (main.c, Makefile, a prebuilt .bin/.hex)
config-app/   phoenix4k-config.html — the live config tool (WebHID, open it directly in Chrome/Edge)
pinscan/      A small bring-up/diagnostic tool used to map which physical key is on which
              chip pin during development. Not needed to use the macropad; kept as a reference
              for anyone porting this to different hardware.
```

## Hardware

- Chip: WCH CH551G (8051 core, 10 KB flash, 128 B data-flash/EEPROM, built-in USB ISP bootloader).
- This specific board's 4 keys map to pins P3.2, P3.4, P3.3, P3.1 (left to right) — confirmed by
  directly testing each key with `pinscan/`. A different board will need remapping; see
  `firmware/main.c`'s `KeyBit[]` table.
- Entering the bootloader: this board's `DOWNLOAD_CFG` fuse selects P1.5 (pulled low briefly at
  power-up) as the ISP entry pin, rather than the chip's other option (P3.6, which is also the
  USB D+ line and awkward to access). Check `wchisp info` on your own board to confirm which pin
  it actually uses before assuming it's the same.

## Building

The firmware needs WCH's own SDK, kept as a separate clone rather than vendored into this repo
(it carries no explicit license, so it isn't republished here):

```sh
git clone https://github.com/Blinkinlabs/ch554_sdcc.git
# commit 6d7aa8f (2023-11-17) is what this firmware was built and tested against
```

Clone it as a sibling of `firmware/` and `pinscan/` (i.e. `ch554_sdcc/` sits at the root of this
repo, next to them) — the Makefiles expect it at `../ch554_sdcc`.

Build with SDCC 4.2+ (tested via a plain Debian container, no native install needed):

```sh
cd firmware
docker run --rm -v "$PWD/..":/work -w /work/firmware debian:bookworm \
  bash -c "apt-get update -qq && apt-get install -y -qq sdcc binutils make && make"
```

This produces `phoenix4k.bin`. A prebuilt copy is already included if you just want to flash it.

## Flashing

Uses [`wchisp`](https://github.com/ch32-rs/wchisp), an open-source flasher for WCH's ISP
bootloader:

1. Put the chip in bootloader mode (see the pin note above) and plug it in.
2. `wchisp flash firmware/phoenix4k.bin`

The bootloader lives in protected ROM and can't be overwritten, so this is hard to brick — if a
flash goes wrong, just re-enter the bootloader and try again.

## Using the config app

Open `config-app/phoenix4k-config.html` directly in Chrome or Edge (just double-click it, or
`open`/your file manager — no server needed) and click Connect. Click a key tile to edit it:
plain key, macro, or media key, with a "click then press a key" capture for picking keycodes.
Changes save instantly to the device over WebHID; nothing needs reflashing.

(It can't be hosted as a web page on most sites — browsers only grant raw HID device access to
pages loaded directly, not pages embedded in another site's frame. Keep it as a local file.)

## Config protocol

For anyone writing their own tooling: the device exposes a second HID collection (vendor usage
page `0xFF00`, report ID 3, 64-byte reports) alongside the normal keyboard collection. Commands,
sent as an output report and answered with an input report of the same report ID:

| Command | Request bytes | Reply | Meaning |
|---|---|---|---|
| `0xB0` | `addr, len` | `len` bytes | Read `len` bytes from the config EEPROM at `addr` |
| `0xB1` | `addr, len, data...` | `0x01`/`0x00` | Write bytes, applied immediately, persisted |
| `0xB2` | — | `0x01` | Factory reset |
| `0xB3` | — | 7 bytes | Device info: keys, layers, macro depth, EEPROM size, layout version, firmware version |

EEPROM layout (128 bytes total, see `firmware/main.c` for the authoritative version):

```
0        magic byte (0xA5)
1        layout version
2        Fn key index (0-3, 0xFF = no Fn / single layer)
3        reserved
4-19     consumer/media key table: 4 keys x 2 layers x 2 bytes
20-123   keyboard action slots: 4 keys x 2 layers x 13 bytes
         [length 0-6][mod0,key0]..[mod5,key5]
         length 0 = inactive, 1 = normal held key, 2-6 = one-shot macro
124-127  spare
```

## Credits

- [ch554_sdcc](https://github.com/Blinkinlabs/ch554_sdcc) — the SDCC port of WCH's CH55x SDK this
  firmware is built against, including its USB device boilerplate.
- [wchisp](https://github.com/ch32-rs/wchisp) — open-source flashing tool for WCH's ISP bootloader.
- [wagiminator/CH552-MacroPad-plus](https://github.com/wagiminator/CH552-MacroPad-plus) — an open
  CH552-based macropad project referenced during early research.

## License

MIT — see `LICENSE`. (The vendored dependencies above are not included in this repo; see their
own licenses.)
