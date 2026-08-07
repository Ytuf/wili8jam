# wili8jam

A PICO-8-compatible fantasy console packaged as a loadable FreeWili 2 DISPLAY app.

This migration builds a contract-valid PSRAM UF2. The inherited DVI renderer compiles for the FreeWili 2 HSTX pinout, but the Fruit Jam audio, direct-SD, and PIO-USB hardware paths have not yet been ported or verified on FreeWili 2 hardware. Do not treat a successful build as peripheral verification.

Hold HOME for five seconds to return to the DISPLAY recovery loader. Run `info` in the on-device console for the app version and source repository.

## Features

- **PICO-8 cartridge runner** -- loads `.p8` and `.p8.png` files with full game loop (`_init`/`_update`/`_draw`)
- **100+ PICO-8 API functions** -- graphics, audio, input, math, strings, tables, memory, coroutines, cartdata
- **8-channel audio** -- 4-channel SFX engine (8 waveforms, 7 effects) + music pattern sequencer + 4-channel basic synth
- **PICO-8 syntax preprocessor** -- `!=`, `+=`, short-form `if`/`while`, `?print`, `//` comments, P8SCII glyphs, `0b` literals
- **128x128 DVI display** -- 4-bit indexed framebuffer with PICO-8 16-color palette, 3x scaled to 384x384
- **Interactive Lua 5.4 REPL** -- serial terminal over USB CDC and on-screen console
- **On-device code editor** -- syntax highlighting, copy/paste, load/save `.p8` files
- **Cart picker UI** -- browse and launch cartridges from the SD card
- **USB input** -- keyboard, mouse, and gamepad via PIO-USB host (simultaneous with USB serial)
- **8 MB PSRAM heap** -- Lua allocations backed by TLSF allocator on external PSRAM

## Hardware

Target: FreeWili 2 DISPLAY CPU (RP2350B), built as a PSRAM-resident FW2App. See `wilibsp/AGENTS.md` for the authoritative board and app contract.

## Building

### Prerequisites

- Pico SDK 2.3.0
- ARM GNU toolchain 14_2_Rel1
- CMake and Ninja
- Initialized submodules: `git submodule update --init --recursive`

### Build

```bash
cmake -S . -B build -G Ninja
cmake --build build --target wili8jam
```

The build validates `build/wili8jam.uf2` as a PSRAM-only FW2App image. Install it with the BSP launcher: `wilibsp/tools/fw install-app build/wili8jam.uf2`. Every published release must attach this validated UF2 as a downloadable release artifact.

### Tests

The PICO-8 preprocessor has a host-side test suite that runs natively:

```bash
gcc -o test_preprocess src/test_p8_preprocess.c src/p8_preprocess.c tlsf/tlsf.c -Isrc -Itlsf -lm -DTEST_HOST
./test_preprocess
```

## Usage

### Serial REPL

Connect to the USB-C serial port (115200 baud). You get a Lua 5.4 REPL with PICO-8 API functions available as globals.

```
> print("hello")
hello
> circfill(64,64,20,8)
> flip()
```

Built-in commands: `ls`, `cd`, `load`, `run`, `edit`, `save`, `cls`, `help`, `info`, `reboot`

### Running Cartridges

Place `.p8` or `.p8.png` files on the SD card root.

- **Auto-run:** Name a file `main.p8` to run it on boot
- **REPL:** Type `load game` then `run`, or `load("game.p8")`
- **Cart picker:** Boot without `main.p8` on the SD to get a file browser

Press **ESC** to exit a running cartridge back to the REPL. Type `resume` to re-enter without resetting.

### Code Editor

Press **ESC** from the REPL to enter the editor. Press **ESC** again to return to the REPL. Editor state persists between switches.

- Arrow keys, Home/End, PgUp/PgDn for navigation
- Shift+arrows for selection, Ctrl+C/X/V for clipboard, Ctrl+A to select all
- Syntax highlighting for Lua keywords, strings, numbers, comments
- Save from the REPL with `save filename` (`.p8` extension added automatically)
- If a cart is loaded, `edit` opens its code automatically

## Source Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | Entry point, init sequence, REPL loop, Lua library registration |
| `src/dvi.c/h` | HSTX DVI driver: 640x480@60Hz timing, DMA command list, TMDS encoding |
| `src/gfx.c/h` | 128x128 4-bit indexed framebuffer, drawing primitives, font rendering |
| `src/audio.c/h` | I2S codec init (I2C), PIO program, DMA ISR, 4-channel basic synth |
| `src/audio_i2s.pio` | PIO I2S transmitter program |
| `src/input.c/h` | HID keyboard state, PICO-8 button mapping, gamepad parsing, mouse |
| `src/sdcard.c/h` | SD card SPI driver |
| `src/psram.h` | PSRAM initialization via QMI hardware registers |
| `src/p8_api.c/h` | Full PICO-8 API: 100+ Lua globals |
| `src/p8_cart.c/h` | .p8 file parser, game loop runner, cart picker UI |
| `src/p8_png.c/h` | .p8.png loader: PNG decoder, DEFLATE decompressor, steganography, PXA |
| `src/p8_preprocess.c/h` | PICO-8 syntax preprocessor |
| `src/p8_sfx.c/h` | SFX engine + music pattern sequencer |
| `src/p8_console.c/h` | On-screen text console |
| `src/p8_editor.c/h` | On-device code editor |
| `src/test_p8_preprocess.c` | 54 host-side preprocessor tests |

### Vendored Libraries

| Directory | Source |
|-----------|--------|
| `lua-5.4.7/src/` | [Lua 5.4.7](https://www.lua.org/) (LUA_32BITS=1, patched luaconf.h) |
| `tlsf/` | [TLSF v3.1](http://www.gii.upv.es/tlsf/) allocator for PSRAM |
| `fatfs/` | [FatFS R0.15](http://elm-chan.org/fsw/ff/) + custom diskio with PSRAM bounce buffer |
| `Pico-PIO-USB/` | [Pico-PIO-USB](https://github.com/sekigon-gonnern/Pico-PIO-USB) |
| `usb-host/` | TinyUSB host wrappers (HID keyboard/mouse/gamepad, CDC, MSC) |

## Acknowledgments

- [Lua](https://www.lua.org/) by PUC-Rio
- [PICO-8](https://www.lexaloffle.com/pico-8.php) by Lexaloffle Games
- [yocto-8](https://github.com/yocto-8/yocto-8) -- P8SCII font data (MIT license)
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)

PICO-8 is a trademark of Lexaloffle Games. This project is not affiliated with or endorsed by Lexaloffle.
