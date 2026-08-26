# MobiGo 2 Emulator

A C++20 emulator for the VTech MobiGo 2 and its GPL16250-class unSP system.
It models the CPU, display/PPU, DMA, keyboard, touch panel, motion sensor,
timers, watchdog, audio, and storage hardware.

## Start playing

Download a release, extract it, and open **MobiGo2Emulator**. Starting the app
without arguments opens the desktop library. From there you can:

- boot the MobiGo 2 system menu;
- choose a cartridge with the normal Finder/File Explorer dialog;
- choose an `.MBA` application with the same dialog;
- drag a cartridge file onto the launcher;
- drag an `.MBA` file onto the launcher;
- reopen recent games from the library; and
- configure firmware, display, audio, capped or uncapped speed, controls, and advanced
  diagnostics without editing command lines.

Closing a running game or pressing F10 returns to the library, so another title
can be started without reopening the app. Settings and the recent-game library
are saved in the operating system's per-user application-data folder. The
frontend is implemented with SDL2 and does not use accounts, telemetry, or the
network.

The macOS release requires **macOS 15 or later**. It is ad-hoc signed but not
Apple-notarized, so Gatekeeper may block the first launch. In Finder,
Control-click `MobiGo2Emulator.app`, choose **Open**, then confirm **Open**. This
approves only this app; do not disable Gatekeeper globally. If macOS still
reports that the verified GitHub release is damaged, remove quarantine from
that app only:

```sh
xattr -dr com.apple.quarantine /path/to/MobiGo2Emulator.app
```

### Firmware setup

Release archives include the required firmware. If the app was moved, or you
are using a source build, open **Settings** and select these three images:

| Image | Exact expected size |
| --- | ---: |
| Internal ROM (`internalrom.bin`) | 131,072 bytes (128 KiB) |
| SPI image (`spi.bin`) | 2,097,152 bytes (2 MiB) |
| NAND image (`nand.bin`) | 138,412,032 bytes (132 MiB) |

The launcher distinguishes a missing file from a file of the wrong size and
will not start a session until all three images are valid. When a newer release
is moved or extracted elsewhere, valid firmware beside that new executable is
preferred over a stale saved path.

### Controls

SDL's controller mapping supports hot-plugged Xbox, PlayStation, Nintendo, and
compatible gamepads. Keyboard bindings are customizable from the launcher's
**Controls** page and are saved with the rest of the local emulator settings.

| MobiGo input | Keyboard | Controller |
| --- | --- | --- |
| D-pad | Arrow keys | D-pad or left stick |
| Primary | Left or right Ctrl | A / south face button |
| Exit | Escape | B / east face button |
| Help | F1 | X / west face button |
| Power | F2 | Back / Select |
| Motion | Home / End / Page Up / Page Down | Right stick |
| Touch screen | Left click or drag | — |
| Brightness | F6 | — |
| Volume down / up | F7 / F8 | — |
| Keyboard letters | Matching letters | — |
| Keyboard left / right | `[` / `]` | — |
| Enter / Delete / Space / Question | Return / Backspace / Space / `/` | — |

Host shortcuts during emulation:

| Action | Keyboard | Controller |
| --- | --- | --- |
| Pause / resume | F3 | Start |
| Mute / unmute | F4 | — |
| Reset game/system | F5 | Guide / Home |
| Save screenshot | F9 | Y / north face button |
| Return to library | F10 | — |
| Toggle fullscreen | F11 | — |
| Exit the desktop app | F12 | — |

Screenshots are BMP files in the `screenshots` folder inside the same per-user
application-data folder as `frontend.conf`; the full saved path is also printed
to the console.

Advanced pages expose ROM mapping, eFuse, GPIO, battery, logging, trace,
deferred-window, frame, memory, and code-dump settings. Repeatable
`--touch-event` and `--key-event` sequences remain CLI-only because they are
deterministic test automation rather than interactive input.

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
SDL2 CMake package is unavailable. No additional GUI dependency is downloaded
at configure or build time.

Portable release builds use compiler optimization and link-time optimization
without `-march=native`. Local builds can opt in with
`-DMOBIGO2_NATIVE_OPTIMIZATIONS=ON`. The release-oriented Clang
profile-guided build is:

```sh
./pgo_build.sh
# Final executable: build/pgo/mobigo2_emu
```

Release CI runs the same PGO flow on macOS, Linux, and Windows (through
MSYS2). For repeatable headless throughput measurement with a final-PC
correctness check:

```sh
python3 scripts/benchmark.py --emulator build/mobigo2_emu
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for the execution model and contributor
map, and [DISCOVERY.md](DISCOVERY.md) for reverse-engineering evidence and
accuracy boundaries.

## Stable command line

Passing **any** argument bypasses the launcher and uses the existing CLI path.
The CMake target and source-build output remain `mobigo2_emu`
(`mobigo2_emu.exe` on Windows) for full compatibility with the
[MobiGo 2 Starter Project](https://github.com/MaxNiftyNine/MobiGo2StarterProject).

```sh
build/mobigo2_emu \
  --rom firmware/internalrom.bin \
  --spi firmware/spi.bin \
  --nand firmware/nand.bin \
  --cart MyGame.bin \
  --speed-percent 100
```

Launch an MBA through the firmware's normal loader with a transient in-memory
NAND overlay:

```sh
build/mobigo2_emu \
  --rom firmware/internalrom.bin \
  --spi firmware/spi.bin \
  --nand firmware/nand.bin \
  --mba MyApplication.MBA \
  --mba-target auto \
  --open-window-on-mba
```

Automatic targeting always replaces the bootable system-menu (SY) slot. The
MBA keeps its own declared entry address, so SY, G1, and nonstandard applications
all follow the same direct-launch path without a role-versus-slot rejection.
Explicit `system`, `g1`, and `menu` targets remain available for filesystem
research. The emulator modifies only its in-memory NAND copy; the file passed
to `--nand` remains byte-for-byte unchanged.

Use `mobigo2_emu --help` for every development, headless, dump, and trace
option. The emulator always uses its accurate execution model. `--speed-percent`
adjusts the requested real-time speed, while `--no-cap` is available for
headless benchmarks and automated verification.

The no-argument launcher uses native Finder/File Explorer dialogs, stores a
recent software list, lets each console control be rebound, and shows the
current percentage of real MobiGo speed in the emulation window title. USB
device emulation is not exposed by the launcher or CLI.

To retain the historical argument-free direct-boot behavior for a script, set
`MOBIGO2_NO_LAUNCHER=1`. Emscripten/browser builds also keep their direct boot
path.

## License and accuracy

Passing the emulator tests proves behavior against the implemented model, not
cycle-perfect or electrical equivalence with every physical console revision.

The emulator code is GPL-2.0-or-later. Device-derived firmware in `firmware/`
is not covered by that license; see
[firmware/README.md](firmware/README.md).
