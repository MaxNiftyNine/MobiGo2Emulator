# Emulator architecture

This document is the map for changing the emulator without accidentally
changing the machine it models.  `DISCOVERY.md` is the chronological evidence
log; this file describes the current code and its invariants.

## Execution flow

`src/main.cpp` owns the application loop and one emulation session at a time.
It parses the stable command line, resolves packaged firmware, optionally asks
the no-argument desktop launcher for a game/configuration, constructs the
devices, performs the selected boot, then advances the guest in bounded
instruction batches. Between batches it handles host input, audio,
presentation, frame dumps, and real-time pacing. Returning to the
desktop library tears down that session before constructing the next one;
headless and desktop runs still use the same CPU and device path.

The hot path is intentionally simple:

1. `Cpu::step()` checks pending interrupts and fetches one 16-bit instruction.
2. The instruction interpreter reads and writes through `Bus`.
3. The instruction's guest cycle cost advances `Bus::cycles`.
4. `Cpu::step()` compares the new cycle count with the cached device deadline
   and calls `Bus::update_periodic_events(false)` only when work is due or the
   deadline was invalidated.
5. The host loop completes due PPU work and periodically services presentation
   and audio.

Do not move guest-visible work from this path to a wall-clock timer.  Headless
tests, uncapped runs, and slow hosts must observe the same guest state.

## Source ownership

| File | Responsibility |
| --- | --- |
| `common.hpp` | Shared constants, CLI options, file helpers, and input names |
| `boot.hpp` | Reset-vector boot sequence |
| `cpu.hpp` | unSP register state, decoder, interpreter, interrupts, and cycle costs |
| `bus.hpp` | Address map, MMIO, DMA, timers, GPIO, interrupt latches, and device scheduling |
| `devices.hpp` | NAND and SPI flash transaction state |
| `video.hpp` | LCD scanout and GPL16250 page/sprite/PPU rendering |
| `audio.hpp` | DAC/SPU mixing and SDL host playback |
| `accelerometer.hpp` | Board accelerometer and its bit-banged I2C behavior |
| `desktop_frontend.cpp` | SDL launcher, native file dialogs, library, settings, and control mapping |
| `desktop_frontend_model.cpp` | Persistent frontend configuration and CLI-option mapping |
| `game_controller.hpp` | Hot-plugged SDL controller input and host commands |
| `ui_font.hpp` | Dependency-free bitmap text used only by the launcher |
| `realtime_throttle.hpp` | Guest-cycle to host-time pacing |

Keep hardware rules in the relevant device or bus implementation.  Frontend
code may request input, reset, pause, or presentation changes, but it must not
special-case a title or patch guest memory to make software advance.

## Address and time units

The unSP CPU uses 22-bit **word addresses**.  `kAddrMask`, CPU registers, bus
addresses, DMA addresses, and PPU source addresses are word-based unless a
function explicitly says `byte` or `bytes`.  NAND/SPI containers and host files
are byte-based.  Mixing these units is a common source of plausible-looking
off-by-two bugs.

`Bus::cycles` is the emulated system-cycle timeline.  Peripheral clocks retain
fractional phase so changing the PLL/divider does not create or discard ticks.
The event scheduler stores the next guest-cycle deadline and avoids running the
full timer/video calculation after every instruction.  MMIO writes that change
a clock or timer must invalidate or recompute that deadline.

## Memory map

- `0x000000..0x006fff`: internal RAM
- `0x007000..0x007fff`: MMIO and dedicated internal RAM windows
- `0x008000..0x027fff`: internal ROM aperture
- `0x030000+`: external chip-select space (SDRAM and cartridge NOR)
- `0x200000+`: banked external chip-select window

The fitted SDRAM is 4M x 16-bit and wraps on its connected address lines.
Cartridges occupy the CS3 NOR aperture selected by the MCS registers; ordinary
stores must never mutate cartridge data.  Instruction fetch and data access are
separate because the internal ROM can appear in fetch-only mappings used by
diagnostic boot modes.

## Accuracy rules

- Preserve the distinction between a hardware status latch and its interrupt
  enable.  A disabled IRQ does not necessarily suppress the status bit.
- A register write may acknowledge bits, trigger work, or expose read-only
  fields.  Do not replace MMIO handlers with plain array assignments without
  checking those semantics.
- DMA and PPU access external memory through the bus mapping, including SDRAM
  wrap and cartridge apertures.
- Real-time pacing and presentation caps are host behavior.  They must not
  change instruction results or peripheral state.
- Keep the documented cartridge, firmware, headless, and evidence-output CLI
  stable. Retired MBA/USB launcher options are intentionally absent.

When evidence is incomplete, use the labels from `DISCOVERY.md`: **Verified**,
**Emulator-inferred**, and **Unknown**.  Title-specific observations can motivate
a hardware fix, but the implementation and regression should express the
general hardware behavior.

## Tests and performance

Tests use synthetic state and firmware-free fixtures.  Retail firmware and
cartridge bytes do not belong in test sources.  Add the smallest regression at
the layer that owns a fix: CPU decoding in `hardware_accuracy_test`, CLI/config
behavior in `configuration_test`, and device-specific behavior in the matching
test executable.

For a normal release check:

```sh
python3 scripts/assemble_firmware.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Use `scripts/benchmark.py` for repeatable throughput comparisons.  Compare the
same binary mode, instruction count, firmware, final PC, and host power state;
a faster run that reaches different guest state is a correctness regression.
Profile-guided release builds use `pgo_build.sh`.  Optimizations in `Cpu::step`,
`Bus::read`/`write`, the event deadline check, or framebuffer access need both a
benchmark and the full correctness suite because tiny changes there multiply
across millions of calls.
