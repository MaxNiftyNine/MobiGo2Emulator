# MobiGo 2 Emulator

A C++20 emulator for the VTech MobiGo 2 and its GPL16250-class unSP system.
It boots the included retail ROM, SPI, and NAND images and models the CPU,
display/PPU, DMA, matrix keyboard, touch panel, motion sensor, timers,
watchdog, audio, storage, USB, and role-aware `.MBA` launch paths.

## Download and run

Download the archive for your operating system from GitHub Releases, extract
it, and double-click **MobiGo2Emulator**. Firmware is bundled beside the app;
there are no command-line setup steps.

Host controls:

| MobiGo input | Computer key |
| --- | --- |
| D-pad | Arrow keys |
| Primary | Left or right Ctrl |
| Exit / Help / Off | Escape / F1 / F2 |
| Brightness | F6 |
| Volume down / up | F7 / F8 |
| Keyboard letters | Matching letters |
| Keyboard left / right | `[` / `]` |
| Enter / Delete / Space / Question | Return / Backspace / Space / `/` |
| Motion left/right/up/down | Home / End / Page Up / Page Down |
| Touch screen | Left mouse button or drag |
| Open USB panel | U |
| Close emulator | F12 |

Arrows always mean D-pad; motion uses its own keys in both emulator modes.

## Build from source

The Git checkout stores the NAND in two parts because the complete image is
larger than GitHub's per-file limit:

```sh
python3 scripts/assemble_firmware.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Install CMake, a C++20 compiler, and SDL2 first. `pkg-config` is used when an
SDL2 CMake package is unavailable. Portable release builds use compiler
optimization and link-time optimization without `-march=native`; local builds
can opt in with `-DMOBIGO2_NATIVE_OPTIMIZATIONS=ON`. `pgo_build.sh` provides a
Clang profile-guided build.

## Command-line development

With no arguments the app locates firmware beside the executable and starts
the system menu with video, sound, and accurate pacing. The complete CLI is
still available for SDK integration and deterministic tests:

```sh
build/mobigo2_emu \
  --rom firmware/internalrom.bin \
  --spi firmware/spi.bin \
  --nand firmware/nand.bin \
  --mba HomebrewLauncher.MBA \
  --mba-target auto \
  --open-window-on-mba \
  --mode accurate
```

`--mba-target` accepts `auto`, `system`, `g1`, and `menu`. The overlay is
transient and the source NAND is not changed. `--mode fast` removes host pacing
and diagnostic-history overhead but executes the same guest CPU/peripheral
behavior and input mapping. Headless validation supports `--steps`,
`--key-event`, `--touch-event`, frame/memory dumps, and filtered traces.

Use `--usb` only with a disposable NAND copy. Firmware-mediated USB writes are
saved only for non-overlay sessions that exit cleanly.

## Accuracy boundaries

Passing emulator tests proves behavior against the implemented model, not
cycle-perfect or electrical equivalence with every physical console revision.
See [DISCOVERY.md](DISCOVERY.md) for chronological reverse-engineering evidence.

The emulator code is GPL-2.0-or-later. Device-derived firmware in `firmware/`
is not covered by that license; see [firmware/README.md](firmware/README.md).
