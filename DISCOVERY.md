# GPL16250 / unSP MobiGo 2 Emulator Notes

This file records evidence, implementation choices, and uncertainty while building
the emulator. Anything marked `ASSUMPTION` or `TODO` should be treated as
incomplete hardware knowledge, not as verified behavior.

This is a chronological evidence log, not an application or emulator usage
guide. Current commands live in [README.md](README.md), the implementation map
and accuracy rules live in [ARCHITECTURE.md](ARCHITECTURE.md), and executable
coverage lives under `tests/`. Bare capture names and paths below are historical
labels. Maintained standalone firmware paths are under `firmware/`; the
assembled NAND is `firmware/nand.bin`.

## Evidence inventory

- `spi.bin`: 2 MiB SPI flash image. Starts with bytes spelling a `PGpssiipp`
  style signature, then unSP-looking 16-bit code begins around offset `0x40`.
- `nand.bin`: 132 MiB NAND image. Starts with bytes spelling `gM_BaNdn`,
  likely a NAND boot/container signature rather than a raw CPU reset vector.
- `rom.bin`: earlier 128 KiB internal-ROM dump. SHA-256:
  `3525cf0ab41686fd7b192953e768b7176e7f8387115312bf398a21298e846a63`.
- `internalrom.bin`: newer 128 KiB internal-ROM dump. SHA-256:
  `3ad4d81f3871b55a32642d9c501e8ec0f4f4ca4e77b27ab57c6ac92f0b89fadf`.
  This is still only 64 Kwords, not the 128 Kwords described in MAME's
  GPAC800 notes. The dump appears to be stored big-endian by word for the reset
  vector window: interpreting it little-endian makes the reset vector
  `0x4000`, while big-endian makes `[0x00fff7] == 0x0040`.
  Important dump-quality warning: byte range `0x008000..0x00ffff` is all zero,
  and `0x010000..0x01ffff` is mostly a repeating low-entropy pattern
  (`0100/0200/0400/0600`). That looks like an unmapped/windowed dump artifact,
  not executable ROM.
- `internalromfixed.bin`: later 128 KiB candidate dump. SHA-256:
  `6ac6671a7b72d7dd03febedae6de785f6972da6c58f7eb6c3b35588466fe62a2`.
  It is not accepted as the default dump:
  - It differs from `internalrom.bin` in only 139 bytes, all within file offsets
    `0x1000..0x10e1` (CPU word addresses approximately `0x008800..0x008870`).
  - Those changes clear exactly 199 bits (`1 -> 0`) and set zero bits.
  - Of those 199 changed bits, the oldest `rom.bin` agrees with
    `internalrom.bin` on 195 and with `internalromfixed.bin` on only 4.
  - The suspicious upper 64 KiB is byte-for-byte identical across all three
    dumps and still contains only byte values `0x00..0x07`.
  This pattern looks like a clearing/AND operation or failed bits, not two
  independent matching reads.
  The all-`1 -> 0` change pattern has a likely SPI NOR explanation: page
  programming was performed over the previous contents without first erasing
  every destination sector/block. NOR page program can clear bits but cannot
  change `0` back to `1`; a fresh erase to `0xff`, WIP polling, page-bounded
  programming, and readback verification are required for every run.
- `mobigo2_pinstream_a_first512_128k.bin`: 128 KiB internal-ROM candidate.
  SHA-256:
  `883e2d2111bf978af1b98fcf34f577c46739da8778c1cec592be79a6f6b4d5d5`.
  This is now the primary dump because it has multiple independent structural
  and execution confirmations:
  - It is exactly 64K 16-bit words, matching the GPL16250 datasheet.
  - Words are little-endian in the file.
  - With the ROM mapped at CPU word `0x008000`, CPU vector locations contain a
    coherent vector table: nearby handlers include `0x95ca`, `0x95ce`, reset is
    `0xf000`, followed by `0x95d2`, `0x95d6`, `0x95da`, and others.
  - Reset target `0x00f000` lies directly inside the documented ROM window, so
    no low-address ROM shadow or high-segment mirror is required.
  - Strict execution runs for at least 500,000 instructions without an unknown
    opcode. It initializes system-control MMIO around `0x7800`, sets SP to
    `0x3fff`, and enters a deliberate power-control loop.
  - Regular-looking regions such as little-endian pairs `0xfe81, address` are
    valid far-jump table entries, not evidence of a bad capture.
- `docs/`: GPL16250 datasheet, unSP ISA/programming manuals, a related GPL16200
  code reference, and unofficial links to prior emulator/reference work.

## Board observations

- SoC: Generalplus GPL16250, unSP-based.
- External RAM: EtronTech `EM638165TS-6G`.
- Datasheet evidence identifies `EM638165TS-6G` as 64 Mbit SDRAM organized as
  4M x 16-bit. The emulator models this as `0x400000` external 16-bit words
  (8 MiB), not the earlier incorrect 8M-word assumption.
- SPI flash: Macronix/NXIC-compatible `MX25L1606E`, 16 Mbit / 2 MiB, matching
  the size of `spi.bin`.
- NAND: Toshiba-marked `143191 12429AE A`. Geometry supplied from board/dump:
  2048 data bytes/page, 64 spare bytes/page, 64 pages/block, 1024 blocks. Total
  raw dump size is 1024 * 64 * (2048 + 64) = 132 MiB, matching `nand.bin`.
- LCD: 3 inch, 320x240, 32-pin connector. Touch wiring/controller remains
  unknown. Digital audio is implemented; analog output characteristics remain
  unknown.

## Initial boot hypothesis

- The first executable firmware path should include the SPI flash. The SPI image
  contains a compact header at offset `0x00000000` and apparent code at
  `0x00000040`.
- The SPI header words match the documented SPI NOR tag:
  `0x4750 0x7370 0x6973 0x7069 0x7370` (`GPspispisp` in the Generalplus boot
  reference text).
- SPI header decode from `spi.bin`:
  - MCS0..MCS4 setup: `0x0044 0x7c47 0x3fc4 0xff87 0x0044`
  - Destination address: `0x001000` words
  - Sector count: `0x0020` sectors, i.e. 16 KiB / 8192 words
  - Documented ROM handoff target: destination + `0x20`, so `0x001020`
- The NAND image must remain present in the emulated system. It will initially be
  exposed through a documented-or-best-effort NAND controller model once register
  addresses are confirmed.
- MAME's GPL16250 notes identify the GPAC800/GCM394 NAND memory map:
  - `0x000000..0x006fff`: internal RAM
  - `0x007000..0x007fff`: internal peripherals
  - `0x008000..0x027fff`: 128 Kword internal ROM region
  - `0x030000+`: external chip-select space
- With `mobigo2_pinstream_a_first512_128k.bin` present, the emulator uses the
  internal-ROM reset-vector boot sequence rather than the old synthetic SPI copy.
- `vendor/firmware/internalrom.bin` currently matches the verified pinstream ROM
  SHA-256 (`883e2d2111bf978af1b98fcf34f577c46739da8778c1cec592be79a6f6b4d5d5`),
  and the maintained launcher supplies that repository path explicitly rather
  than depending on a process-local firmware filename.
- `--auto-app-handoff` and `--auto-menu-handoff` are debug-only control-flow
  shortcuts. They are disabled by default because the retail boot should advance
  by emulating the firmware-visible hardware state, not by forcing the PC.
- Current ROM mapping evidence:
  - The documented reset vector is word address `0x00fff7`.
  - The primary dump is loaded little-endian at `0x008000`; reset vector
    `[0x00fff7] == 0xf000`.
  - The primary path does not use the old low boot shadow assumption.
  - Mapping `internalrom.bin` big-endian at `0x008000` makes
    `[0x00fff7] == 0x0040`.
  - The emulator models a fetch-only low boot shadow:
    instruction/immediate fetches from low addresses can see the internal ROM,
    but normal low data reads still see RAM. This is an
    `ASSUMPTION`; making all low reads return ROM made the boot code load ROM
    bytes as data and jump into obvious tables.
  - Strict ISA 1.2 decoding stops immediately at reset target `0x0040`, opcode
    `0x5830`. It occupies an invalid ISA 1.2 ALU slot, but this is no longer
    sufficient evidence that the dump is byte-swapped or corrupt: the unSP 1.3
    byte indexed-address diagrams intentionally hide their identifying opcode
    fields with `x` placeholders, and `0x5830` fits the diagram's general field
    shape. The GPL16250 may be using byte-memory instructions that MAME and the
    emulator do not decode.
  - New endian evidence: loading `internalromfixed.bin` as little-endian words
    and explicitly starting at `0x0040` executes 14 documented instructions
    before reaching opcode `0x0081`. Its bit layout matches the unSP 1.3
    byte-register-indirect instruction diagram. This strongly suggests the
    payload serialized normal ROM words least-significant byte first, while
    the vector word or reset-shadow interpretation is still unresolved.
  - `0x0081` is not treated as corruption. The emulator now logs it as an
    unimplemented unSP 1.3 byte-memory instruction. The supplied manual
    describes the addressing modes but replaces the identifying opcode fields
    with `x`, and MAME does not implement this instruction group.
  - Dump-addressing requirement: the ROM range is 64K words, from word
    `0x008000` through `0x017fff`. A 16-bit pointer cannot traverse this range
    linearly. The lower half must read DS=0, offsets `0x8000..0xffff`; the upper
    half must read DS=1, offsets `0x0000..0x7fff`. Merely resetting a 16-bit
    pointer for the upper output file without changing DS will read low memory
    rather than the upper ROM half.
  - If the explicit diagnostic workaround `--allow-invalid-alu-nop` is enabled,
    the ROM later changes the code segment through `SR` so `PC=0x0087` becomes
    logical address `0x100087`. The supplied dump has no real code there.
  - A debug-only `--rom-fetch-mirror64` experiment mirrors low 64K ROM fetches
    into higher code segments. Combined with `--allow-invalid-alu-nop`, the new
    dump reaches `0x100185`, opcode `0x2084`, which is the same undocumented
    `op1 == 2` instruction class already blocking the SPI-shim path. This is
    not kept as default because both the mirror and invalid-ALU no-op behavior
    are undocumented.

## Implemented first pass

- Standalone C++20/SDL2 project in `src/main.cpp`.
- GPL16250 audio model based on the supplied GPF16001A SDK and Generalplus
  programming guides: two 16-word DAC FIFOs with SRC/Timer-E timing and
  IRQ/FIQ refill signaling, plus a 32-channel SPU supporting PCM8, PCM16, IMA
  ADPCM, ADPCM36, pitch, pan, volume/envelopes, loop/end markers, and SDL2
  stereo output. The mixer iterates active channels only, caches stable state
  per render block, and batches host queue writes.
- unSP interpreter covers the documented base ALU modes, branches, push/pop,
  far call/jump, DS/FR access, bit operations, multiplication, division quotient
  step, and a subset of unSP 2.0 extended operations.
- Implemented a first MULS/inner-product instruction model from the unSP ISA
  text: signedness bits, count 0 as 16 taps, fraction-mode shift, R4:R3 result,
  and basic pointer increments. FIR_MOVE memory shifting remains an
  `ASSUMPTION`/TODO.
- The interpreter still lacks at least one `execute_remaining` addressing form:
  `op1 == 2` / `lower_op == 0x28`. The SPI-shim path now reaches opcode
  `0x8082` at `0x120d`, which uses this form. The extracted MAME
  `unsp_other.cpp` also lacks an `op1 == 2` case, and the supplied text docs do
  not expose a clear encoding row for it yet, so this is not guessed in code.
- Internal ROM boot path:
  - `--rom mobigo2_pinstream_a_first512_128k.bin`
  - `--rom-endian be|le` (default `le` for the primary dump)
  - `--rom-base ADDR` (default `0x8000`)
  - `--rom-shadow-low` enables the old fetch-only low boot shadow experiment;
    it is disabled by default
  - `--rom-fetch-mirror64` enables a rejected debug experiment that mirrors low
    ROM fetches into higher segments
  - `--allow-invalid-alu-nop` enables a temporary diagnostic workaround for
    documented-invalid ALU opcodes. This is intentionally off by default.
  - `--start-pc ADDR` can override the reset vector for experiments
- SPI NOR controller registers `0x7940..0x7945` now have a first transaction
  model for JEDEC ID (`0xc2 0x20 0x15` for MX25L1606E-compatible flash), status
  reads, and `0x03`/`0x0b` flash reads from `spi.bin`. The ROM has not reached
  this yet because the internal-ROM mapping/dump issue blocks earlier.
- MMIO unknown reads/writes are stored and logged to `emulator.log`.
- The SPI flash remains the boot-copy source only. It is not memory-mapped into
  external CS space because the MobiGo 2 is a NAND+RAM configuration and MAME's
  notes map `0x030000+` as external chip-select RAM. An earlier test mapping SPI
  at `0x030000` caused firmware to execute the SPI header as code, which was
  incorrect.
- External CS SDRAM is now a separate memory array from internal RAM. GPL16250
  chip-select accesses follow MAME's GPAC800/GPL16250 map:
  `0x020000..0x1fffff` maps directly to CS offset `addr - 0x020000`, and
  `0x200000..0x3fffff` maps through bank register `0x7810`.
- NAND command registers `0x7850..0x785f` are modeled from MAME's GPAC800 notes:
  command, low/high address, data reads, ready status, ECC status stubs.
- Firmware later uses large-page NAND command `0x30` after address setup. The
  emulator treats `0x30` as the read-confirm/data phase for the current
  effective address.
- NAND ID now reports Toshiba `0x98,0xf1`, because the related Generalplus boot
  reference lists this as NAND1: 128 MiB, 2048+64 bytes/page, 64 pages/block.
  `ASSUMPTION`: the extra ID bytes are currently `80 15 40` until the exact
  Toshiba part behind the board marking is identified.
- NAND address translation now models NAND1 as four 512-byte logical sectors per
  2048-byte page, with 16 spare/ECC bytes per sector stored in the physical
  64-byte spare area after the page data. This replaces the older `sector * 528`
  shortcut, which displaced sectors 1..3 into spare bytes.
- A first GPL16250 video/PPU model is implemented:
  - palette RAM `0x7300..0x73ff` with `0x703a` banking;
  - sprite RAM `0x7400..0x77ff` with `0x707e` banking;
  - PPU/TFT status reads such as `0x7072` not-busy and `0x707c == 0x8000`;
  - a conservative tile/sprite renderer based on MAME's GPL renderer register
    layout. It is incomplete but can render firmware-programmed tilemaps,
    simple sprites, and RGB565 frame buffers.
- Video status register `0x7063` now pulses TFT/frame-end bit `0x0800` on an
  approximate scanline-0 event and clears it on the opposite edge if firmware
  has not already acknowledged it. Bit `0x0001` is not asserted with this event:
  when the emulator returned `0x0801`, later firmware treated the status as an
  error path and explicitly jumped through the watchdog reset routine at
  `0x30043..0x3004a`.
- A first unSP interrupt service path is implemented. Video/PPU IRQ is wired to
  IRQ5, matching MAME's GPL162xx notes, with IRQ vectors fetched from the
  documented unSP table (`IRQ5` vector word at `0x00fffd`) and RETI restoring
  interrupt state. Current firmware reaches the terminal loop with IRQ/FIQ
  disabled, so this does not yet change the halt.
- Firmware programs DMA channel 0 through `0x7a80..0x7a86` and polls `0x7abf`
  (`DMA_INT`). The emulator performs a word-copy DMA and now honors documented
  source/destination increment/decrement bits (`SF/DF/SD/DD`) from MAME's
  Generalplus DMA device. This fixed the earlier deterministic long-run crash:
  the old DMA repeatedly copied the same 256-word source block, causing execution
  through repeated `0x001f` data and eventually `0xffff`.
  `ASSUMPTION`: this firmware appears to trigger DMA with `0x0200`; MAME names
  bit 0 as channel enable and bit 9 as reset, so the emulator currently treats
  either bit as a software trigger and logs this uncertainty in source.
- DMA high address words are full linear DMA address bits, not 6-bit CPU segment
  values. Masking them to 6 bits caused destination `0x400000` to wrap to low
  internal RAM and corrupt the boot stack/code. Removing that mask fixed a false
  unknown-opcode failure.
- First execution trace reached a wait loop at `0x001061..0x001064` polling
  MMIO `0x780f` (`Power_State`) and masking with `0x0007`. A later loop at
  `0x00106f..0x001073` waits for those same low bits to equal `0b010`. The
  emulator returns low bits `2` as an `ASSUMPTION` so the firmware can progress
  past the power/clock-ready wait.
- A separate `0x7807` (`Clock_Ctrl`) read path also returns low bits `2` as an
  `ASSUMPTION` for PLL/clock-ready state, based on the register name and nearby
  clock setup writes.
- Correction from the GPL1625x register list: `0x7ae2` is `E-Fuse2`, not a
  reset/wake-cause register. The internal ROM masks this register with `0x0300`
  at reset and only continues boot-device probing when both bits are set. The
  emulator now gives `E-Fuse2` the evidence-based MobiGo 2 value `0x0300` at
  cold reset. Returning zero was what caused the earlier sleep-key path; the
  automatic sleep/wake reset is no longer part of normal boot.
- The internal ROM's SPI routines use GPIO-B bit 4 (`0x7869`) as active-low
  chip select. Calls around `0x1588d` assert the line, exchange command bytes
  through `0x7942/0x7944`, then deassert it. The SPI NOR model now starts and
  ends transactions on these GPIO edges. Previously every command was folded
  into stale transaction state, causing valid flash probes to fail and the ROM
  to fall through toward USB-device recovery.
- SPI receive timing is full-duplex: bytes sampled while sending the command
  and address are dummy `0xff`, and JEDEC/read data begins only on subsequent
  clocked transfers. The previous model returned the first response during the
  command byte, shifting `C2 20 15` into `20 15 15` at the ROM's JEDEC probe.
- After accepting the JEDEC ID, the ROM first issues WRDI (`0x04`) and polls
  read-status (`0x05`). The ROM reads status immediately after the opcode
  transfer without sending a separate dummy byte, so the GPL16250 SPI receive
  register must expose the status for that transaction. This differs from the
  straightforward wire-level byte timing used for JEDEC ID and is modeled as
  a command-specific controller behavior.
- Earlier notes identified the subsequent bulk path as system DMA from
  `SPI_RXData`; trace disproved that conclusion. If the SPI status stage fails,
  the ROM falls through to a separate SD2-controller boot probe at
  `0x79e0..0x79ea`. The actual SPI `0x03` read path remains to be observed
  after the status transaction succeeds.
- The actual ROM boot-source selector is the low three bits of `E-Fuse0`
  (`0x7ae0`). Code at `0xf16c` masks the register with `7` and dispatches
  values 1 through 6 to distinct loader paths; zero reaches USB ISP/device
  recovery. The emulator exposes all three observed fuse words through
  `--efuse0`, `--efuse1`, and `--efuse2` while the exact board-programmed
  `E-Fuse0` value is determined from successful recognition of the supplied
  SPI and NAND images.
- USB-device interrupt register `0x7a3a` is polled by the ROM routine at
  `0x175c2`. The handler recognizes individual event bits including `0x20`,
  and clears them by writing the same bit back, establishing write-one-to-clear
  semantics. With the device controller enabled at `0x7a30` and no USB host
  attached, the emulator now latches a one-shot `0x20` suspend event after a
  short delay. This timing and bit interpretation are an `ASSUMPTION` based on
  standard USB suspend behavior plus the ROM's explicit event decoding; it is
  not a hardcoded program-counter escape.
- At reset, code around `0xf015` samples GPIO-E data (`0x7880`) bit 15. Forcing
  it low initially appeared to reach a separate external-CS path that jumped
  to empty address `0x020000`. That result was contaminated by `Clock_Ctrl`
  incorrectly reporting ready state at cold reset.
  GPIO-C bit 8 at `0x7870` separately gates the full SPI JEDEC/header probe
  around `0x1588d`; the former all-high input stub skipped that probe. The
  emulator now models GPIO-C bit 8 low for the populated MobiGo 2 SPI flash,
  GPIO-E is now low for the cold-bootstrap experiment. Exact GPIO/boot-strap
  polarity remains an
  `ASSUMPTION` pending board measurements or documentation.
- `Clock_Ctrl` (`0x7807`) must read zero at cold reset. The ROM checks it in
  its reset/recovery decision at `0xf049`; the prior stub always forced low
  bits to `2`, falsely selecting recovery. The ready value is now exposed only
  after firmware writes a nonzero clock configuration, preserving the later
  PLL-ready poll behavior.
- With that correction and GPIO-E low, the ROM performs NAND `0x90` ID reads,
  issues large-page `0x00/0x30` reads, and scans multiple block candidates
  before falling back to USB. At this investigation stage, the failure was
  narrowed to NAND identification/geometry/address translation rather than
  CPU execution or the USB wait itself.
- The SPI bootstrap uses two different on-chip interfaces. It identifies the
  MX25L1606E through the byte-oriented SPI registers at `0x7940..0x7945`, then
  configures the second SD controller at `0x79e0..0x79ea` for the bulk transfer.
  The register names are confirmed by the current MAME GPL1625x map:
  `SD2_DataTX`, `SD2_DataRX`, `SD2_CMD`, argument/response words,
  `SD2_Status`, `SD2_Ctrl`, `SD2_BLKLEN`, and `SD2_INT`.
- The ROM clears `SD2_Status` by writing `0xffff`, writes command `0x40`, and
  waits while status bit 0 is set. The emulator now treats `SD2_Status` as
  write-one-to-clear. The bit-level meaning remains an `ASSUMPTION` from the
  ROM access sequence because the available register list has names but no
  field definitions.
- SPI command `0x04` is the MX25L1606E standard WRDI command. It is now modeled
  as a non-data command instead of being logged as unknown.
- The GPL162002A/162003A programming guide documents the same timer register
  addresses used by GPL16250: Timer A control is `0x78c0`, with bit 15 as the
  overflow/event flag and write-one-to-clear. The internal ROM writes
  `0x8062` while setting up its SPI timeout. Storing that value verbatim made
  the next read falsely report an immediate timeout; the readable control
  value is `0x0062` until a real timer overflow occurs. This W1C behavior is
  now applied to Timer A through Timer F.
- Current MAME GPL162xx source models `P_INT_Status2` (`0x78a1`) as a derived
  TimeBase status register rather than ordinary writable storage: TimeBase A,
  B, and C report as bits `0x0100`, `0x0200`, and `0x0400` respectively, and
  writes to `P_INT_Status2` are ignored. The emulator now derives reads from
  the TimeBase control words instead of retaining unrelated high bits from
  programmable timer overflow bookkeeping.
- With the timer flag corrected, the ROM issues SPI read command `0x03` for
  address zero and reaches its bulk-transfer setup at `0x15c50`. Opcode
  `0xf258 0x7945` is the unSP 2.0 direct-memory bit operation
  `SETB [0x7945],8`, enabling the documented SPI `SMART` FIFO mode. The CPU
  core now implements all four direct-memory bit operations, with optional
  DS addressing, following MAME's unSP 2.0 decode.
- The related programming guide fully documents the DMA control layout used
  here (GPL16250 moves the block from `0x7b80` to `0x7a80`). Control bit 9 is
  `RS`, a write-only software reset; bit 0 is `CE`, channel enable, and bit 1
  is read-only busy status. The prior emulator treated either bit 9 or bit 0
  as a start request. That caused a reset write of `0x0200` to launch a bogus
  transfer and increment the fixed SPI MMIO source through unrelated
  registers. DMA now resets on bit 9 and starts only with `CE=1` and a
  nonzero terminal count.
- The SPI bulk read uses two demand-mode DMA channels selected as SPI TX and
  SPI RX. Channel 1 repeatedly writes a fixed RAM value to `SPI_TXData`;
  channel 2 reads byte-wide `SPI_RXData` into 16-bit RAM. The DMA guide states
  that when source and target widths differ, the word-side address advances
  after every two byte requests. The engine now packs received bytes low-first
  into each destination word instead of leaving a zero byte between every
  flash byte.
- The external SPI loader's NAND routine provides direct address-format
  evidence. For each physical page it reads main-data columns `0x000`,
  `0x200`, `0x400`, and `0x600`, then spare columns `0x800`, `0x810`,
  `0x820`, and `0x830`; `AddrH` advances as page number `0`, `1`, `2`, etc.
  The NAND image repeats its opening structure at raw offset `0x21000`, which
  is exactly one block (`64 * (2048 + 64)`). The emulator therefore maps
  `AddrH` as the physical page and `AddrL` as a large-page byte column, with
  columns `0x800..0x83f` selecting the 64-byte spare area. This replaces the
  earlier incorrect flat 512-byte-sector interpretation.
- The NAND controller has a second address convention used by the NAND-loaded
  physical-page driver. With `P_NF_Type=0x27`, firmware transfers one complete
  `0x840`-byte raw page and supplies the page number across `P_NF_AddrL/H`
  (`0x400`, `0x401`, `0x440`, `0x441`, etc. at 64-page block boundaries).
  These values are not byte columns. The emulator now selects this packed
  row-address mode only for the observed `0x27` configuration while retaining
  the ROM loader's separately observed column/page convention.
- NAND status command `0x70` previously returned `0xffff`, which incorrectly
  asserted the standard bit-0 failure flag for every firmware erase/program
  probe. It now reports ready/pass (`0x40`) and supports in-memory 64-page
  block erase plus one-to-zero data programming. This models actual flash
  semantics without removing NAND or fabricating filesystem contents.
- With corrected NAND translation, execution leaves the SPI loader, enters
  NAND-loaded firmware at CPU word address `0x030000`, and later reaches its
  cache initialization at `0x006c00`. The firmware writes `0x001c` to
  `Cache_Ctrl` (`0x7819`), writes command `0x0002`, waits four instructions,
  and polls until `0x0002` clears. Because emulator memory accesses are not
  host-cached, cache maintenance completes immediately and the command bit is
  now read as self-cleared. The register name is documented by MAME's
  GPL16250 map; the bit interpretation is firmware-derived and marked as such
  in source.
- `P_NF_AddrL` is documented as the raw first and second NAND address cycles.
  The firmware deliberately probes invalid columns such as `0x0ac0` while
  detecting geometry. Invalid columns now return `0xff` and cannot alias the
  first byte of the following physical page.
- The documented ECC no-error encoding is `FAILBIT=3, FAILLINE=0xff`
  (`0x03ff`). Both low- and high-byte ECC error aliases now return that value
  until full ECC calculation is implemented.
- The NAND-loaded firmware performs a later SPI read beginning at byte offset
  `0x45000`. It loads a `bM_gdSQl` module at CPU word address `0x052200`.
  Full-range comparison shows that the transfer is correct: the package stores
  512 bytes of executable data followed by a 4-byte record, and firmware
  removes each record while loading. This accounts exactly for the increasing
  source/runtime offset and rules out byte packing, endianness, or DMA
  corruption as the immediate handoff failure.
- The module header declares entrypoint `0x05:0x2220`. Its entry code disables
  IRQ/FIQ and jumps to `0x06fe7a`, which is a compiler-style epilogue
  (`SP += 3`, then pop `BP,SR,PC`). Generic startup reaches it without a return
  frame above `SP=0x6bff`, so it pops copied cache-routine words and fetches
  unmapped `0xffff` code. JMPR, JMPF, push, and pop behavior has been checked
  against MAME and the unSP documentation; no crash-skipping workaround was
  added.
- GPIO-A bit 15 is the only input bit read during final initialization.
  Modeling it active-low/pressed changes a branch at `0x0368bf` but reaches the
  same module entrypoint, ruling controls out as the blocker observed then.
- GPIO-A, GPIO-B, and GPIO-C input levels are exposed as `--gpio-a`,
  `--gpio-b`, and `--gpio-c`. This permits boot-selection experiments without
  changing source or silently treating an undocumented board strap as
  established behavior. GPIO-B now defaults to `0xfffe`: the GPL16250
  datasheet requires BM0/IOB0 low, BM1/IOB1 high selects internal-ROM boot,
  and BM2/IOB2 high selects the internal PLL normally used by handhelds.
- GPIO-D and GPIO-E are now also exposed as `--gpio-d` and `--gpio-e`.
  Their prior fixed idle values were undocumented assumptions, and the stock
  firmware reads board-state inputs near the final overlay selection path.
- A controlled GPIO-D/E sweep shows constant GPIO-E high selects the internal
  ROM's USB-device path and waits for a host, while `0x0000` selects normal
  NAND loading. GPIO-D has no observed effect. An attempted automatic
  low-to-high transition did not alter the later overlay selection, so that
  hypothesis was rejected rather than retained as a workaround. The default
  remains the evidence-backed NAND-boot value `0x0000`.
- A second `bM_gdSQl` module at SPI offset `0x8cbc0` has the same load address
  but a different payload. Substituting it into a temporary diagnostic flash
  copy leaves execution looping at `0x052220`; it is not a valid replacement.
- Raw NAND contains corresponding `bM_gdSQl` package copies. The first cooked
  NAND package has the same `0x05:0x2220` load address but jumps to
  `0x06e701`, while the SPI package jumps to the empty epilogue at
  `0x06fe7a`. Replacing only the first SPI package in a temporary diagnostic
  image with the cooked NAND package still eventually returns through an
  epilogue and fetches an invalid top-level return address. It does not program
  further graphics before doing so. The remaining failure is therefore shared
  overlay-entry/return context or reset behavior, not that one SPI target.
- The supplied unSP 1.2 and unSP 2.0 manuals confirm opcode `0xfec0` is
  `GOTO MR`, while `CALL MR` uses the `0xf161` family. The transfer at
  `0x000656` must not push a return frame. The alternate NAND package performs
  no MMIO after entry and reaches its epilogue in three instructions, so an
  inaccurate peripheral status inside the package cannot explain that return.
  The unresolved question is why firmware selects a deliberately empty
  tail-called overlay, or what external reset/slot contract is expected after
  it returns.
- `E-Fuse2` bit sweeps confirm that bits `0x0100` and `0x0200` must both be set
  to avoid the ROM's sleep path. Varying `E-Fuse0`, `E-Fuse1`, or lower
  `E-Fuse2` bits did not alter the selected deep boot path.
- Runtime ranges can now be captured with
  `--dump-memory PATH --dump-memory-base ADDR --dump-memory-words N`.
  `--dump-memory-dma` reads the same range through the DMA/CS path instead of
  the CPU banked data path, which is necessary for physical framebuffer
  addresses such as `0x3fd400`. `--trace-start-insn N` gates instruction and
  transition traces until late boot so they are not buried under ROM startup
  flow.
- The dedicated current MAME GPAC800/MobiGo 2 map confirms that the NAND
  variant's external chip-select window starts at CPU word address `0x030000`,
  not `0x020000`. CS0 is a separate 64K-word window; CS1 contains the fitted
  EtronTech EM638165TS-6G 4M x16 SDRAM and wraps within the larger window
  programmed by firmware. The emulator now models those two regions
  separately.
- Firmware configures frame-base video by writing `FBI_ADDR=0x3fd400` through
  `0x7078/0x7079`, then writes `0x0080` to `PPU_Enable`. The GPL16250 bit
  decode identifies bit 7 as `FB_EN`; the old renderer incorrectly tested
  `PPU_EN` bit 0 and read `FBO_ADDR` from `0x707a/0x707b`. MAME also does not
  implement this frame-base path. The emulator now uses `FBI_ADDR` in this
  mode. Later execution provides definitive pointer-direction evidence:
  firmware issues a `0x12c00`-word DMA (exactly `320*240`) with destination
  `0x3fd400`. `FBI_ADDR` is therefore the first framebuffer word. The transfer
  crosses `0x3fffff` through the external SDRAM bus window, so rendering uses
  the same physical DMA memory view rather than wrapping into low internal
  RAM.
- Post-logo execution shows the frame-base registers are physical DMA/CS
  addresses, not CPU bank-window addresses. With `P_Bank` selecting CS bank 1,
  bank-resolving an `FBI_ADDR` such as `0x1d400` incorrectly points at
  structured package/NAND data around `0x21d400` and renders that data as
  pixels. The renderer now consumes `0x7078..0x707b` directly through
  `dma_read`.
- MAME's GPL1625x/GPL162xx GPIO map identifies the Port B group as
  `0x7868=IOB_Data`, `0x7869=IOB_Buffer`, `0x786a=IOB_Dir`,
  `0x786b=IOB_Attrib`, `0x786c=IOB_Latch/Wakeup`, and `0x786d=IOB_Drv`.
  The post-logo wait around `0x693df..0x69403` reads `0x786b` and `0x786a`,
  so those accesses are direction/attribute readback, not direct pin sampling.
  They should continue to return retained register writes rather than being
  substituted with the external `--gpio-b` input level.

## Documentation limitations

- `docs/reference/GPL16250VAV10_ds.pdf` is encrypted with copy disabled, but current
  `pypdf` plus AES support can extract its text locally. It provides high-level
  peripheral and boot-pin behavior, not full register-level PPU semantics.
- The supplied unSP 2.0 programmer's guide confirms that GPL16250-era ISA 1.3
  has a separate 6-bit Stack Segment register (`SS`). PUSH/POP use the 22-bit
  address `{SS:Rs}` and carry/borrow into SS; every `[BP+IM6]` access also uses
  SS. The previous CPU model omitted SS entirely. The guide's opcode-table
  image masks the fixed bits as `x`, but its contiguous special-register table
  and the established DS/FR encodings place MDS access at `F000/F008` and SS
  access at `F010/F018`. This mapping is evidence-based but remains explicitly
  marked as an inference until confirmed against an assembler or hardware
  trace. SS writes are logged.

## Verification

- `audio_test` verifies direct-DAC FIFO drain/underflow and FIQ signaling,
  interrupt-enable gating, shared stereo/mono FIFO routing, and SPU PCM16
  one-shot output/end status.
- `cmake --build build -j4` succeeds with SDL2 from Homebrew/pkg-config after
  selecting the pin-stream-derived ROM as the default.
- `./build/mobigo2_emu --no-window --steps 500000` starts with:
  `ROM boot: rom_base=0x8000 words=0x10000 reset_vector=0xf000 start=0xf000
  shadow_low=0 fetch_mirror64=0`.
  It executes the full step count without an unknown opcode.
- With an incorrect zero value for `E-Fuse2`, the ROM initialized system
  control registers and reached a loop at `0xf864..0xf876`. Immediately before
  the loop it:
  - reads `0x7808`, sets bit `0x20`, and writes it back;
  - writes the documented sleep key `0xa00a` to `0x780e`;
  - executes NOP and a backward branch while hardware should enter sleep.
  The related Generalplus programming guide documents `0x780e` as the sleep
  entrance register and says wake from sleep resets the system. This is a real
  ROM power-down path, but it is not the normal MobiGo 2 path once `E-Fuse2`
  is initialized to the required `0x0300`.
- `./build/mobigo2_emu --allow-invalid-alu-nop --rom-fetch-mirror64 --no-window --steps 500000`
  is a diagnostic-only run. It reaches `PC=0x100185`, opcode `0x2084`, after
  many logged invalid-ALU no-op workarounds and mirrored high-segment fetches.
  This confirms the new dump changes behavior compared with the old dump, but
  the path is not a valid emulation result.
- Before correcting NAND page/column translation,
  `./build/mobigo2_emu --no-window --steps 30000000 --dump-frame
  build/frame_30m_irq.bmp` stopped at `PC=0x1160`, the external loader's
  failure loop.
- The frame dump still shows the emulator fallback/debug image, not firmware
  graphics. There are no video DMA logs before the `0x1160` halt, so the
  firmware has not yet programmed a visible framebuffer/PPU state in the path
  reached by the current boot model.
- Focused trace:
  `./build/mobigo2_emu --no-window --steps 30000000 --trace-pc 0x1140 0x1162 --trace-limit 200`
  shows the caller executing setup calls at `0x1152`, `0x1154`, `0x1159`, and
  `0x115e -> 0x19a1`, then entering `0x1160: ee41`.
- `0xee41` is an unconditional `JMP -1` under MAME's unSP decode order
  (`op0=0xe`, `opa=7`, `op1=1`). It is not a bit-operation decode bug.
- At the `0x1160` loop, trace shows `FR=0x0508`; IRQ and FIQ enable bits are
  clear, and `virq=0`. This historical trace showed an explicit terminal loop
  after the loader/setup path, not an unhandled active interrupt.
- The preceding `0x19a1` routine performs the large DMA sweep into SDRAM and
  polls video status `0x7063` at `0x19eb..0x19ee`; that poll now exits after the
  emulated frame-end status is latched.
- The apparent SPI-overlay crash was a downstream symptom, not a bad SPI dump.
  The NAND physical-page driver had been reading every packed page address as
  a column in page zero, and NAND status reported every operation as failed.
  After correcting packed page addressing and ready/pass status, firmware does
  not enter the invalid overlay return. It streams stock NAND pages into SDRAM
  and performs the expected framebuffer DMA.

## Working display milestone

- On June 6, 2026, the stock `mobigo2_pinstream_a_first512_128k.bin`,
  `spi.bin`, and `nand.bin` booted through the real internal ROM, external
  loader, and NAND firmware without an opcode crash.
- Firmware DMA copies exactly `0x12c00` words from SDRAM to
  `FBI_ADDR=0x3fd400`. Rendering that physical SDRAM window as RGB565 produces
  the real 320x240 blue VTech logo boot screen.
- Deterministic verification:
  `./build/mobigo2_emu --no-window --steps 50000000 --dump-frame build/frame.bmp`
  produced SHA-256
  `df2773fb9bf81c16c9fd62dbcd4d4817aa76694f0831cdf39acefc047341b9a0`.
  The same hash was produced at 120 million instructions.
- Native SDL verification also succeeded. The visible window titled
  `MobiGo 2 GPL16250 Emulator` displayed the same firmware-generated image;
  the verification screenshot is `build/sdl_window_front.png`.
- Post-logo progress as of the Timer A/PPU status and IRQ-return fixes:
  - The old app wait at `0x693f0..0x69403` is no longer stuck. Timer A is
    configured as `0x6064`; treating nonzero timer mode bits as "running" lets
    it overflow to `0xe064`, assert IRQ4, and drive the firmware tick counter at
    `0x01d8/0x01d9`.
  - The watchdog reset path at `0x30043..0x3004a` is avoided when `0x7063`
    reports only frame-end bit `0x0800`, not `0x0801`.
  - The `0x21f298..0x21f29e` video-status loop is passed when `0x0800` is a
    pulse instead of a permanently latched status bit.
  - The later `0x63aa7..0x63aaf` wait is also no longer a permanent stop. That
    loop clears and polls the firmware RAM counter pair `0x09b7/0x09b8`; the
    missing piece was CPU IRQ-return bookkeeping. The video IRQ dispatcher exits
    through opcode `0x9a90` at `0x3a19b`, not only the `0x9a98` return form, so
    the emulator's IRQ-active latch stayed set and blocked later video IRQs.
    Clearing the latch at that dispatcher epilogue lets execution advance past
    the old wait. Current checks reached `PC=0x30400` at 600M instructions,
    `PC=0x30843` at 900M, and `PC=0x302aa` at 1.22B.
  - The active frame-base window later changes to `0x0ad400`. That framebuffer
    is still all black, but the firmware simultaneously enables PPU text/tile
    layers (`PPU_Enable=0x00cb`). MAME's GPL renderer draws PPU layers
    regardless of frame-base mode, so the emulator now composes the PPU layer
    over the frame-base background instead of treating frame-base mode as
    mutually exclusive with tile rendering. This exposes a real firmware
    tilemap copied to `0x2800/0x2000` from source rows starting at `0x109810`.
- The post-logo visible state is now a stable white loading-style tile screen
    with small blue glyph fragments and a lower-right swirl, sourced from page 2
    registers `attr=0x1052`, `ctrl=0xfc2a`, tilemap `0x2800`, and graphics base
    `0x10cd98`. The same frame hash is produced from 1B through 8B
    instructions, so the remaining blocker to the game-select screen is not a
    black-frame renderer failure. Firmware continues executing loader/NAND
    parsing code while the display remains static.
  - `P_INT_Status2` (`0x78a1`) is a derived status register. The verified
    GPL162002A/162003A programming guide documents TimerD/C/B/A status in bits
    15/14/13/12, TimeBaseC/B/A in bits 10/9/8, and scheduler in bit 2. The
    emulator now reports those source bits from their backing timer/timebase/RTC
    flags instead of exposing only TimeBaseA-C. This is a hardware-accuracy fix,
    but a 2.2B-instruction verification still reaches `PC=0x3057f` with only
    one `P_FB_PPU_GO` render and the same static MobiGo animation frame hash
    (`d7ef254b85eeceb2c75c8abb3fc1e56e265b2c45f5b5f76ddcb8fc7d6ae8a14d`).
  - A more literal timer-source experiment was rejected. The verified guide
    shows `P_TimerX_Ctrl.bit13` as module enable, bit14 as interrupt enable,
    bit15 as the W1C event flag, and the low nibble as clock-source select.
    Modeling source periods directly against the current coarse emulator cycle
    base made the boot regress to the old `PC=0x63aa9` wait through 900M
    instructions. The frame still rendered, but the firmware's wait pair at
    `0x09b7/0x09b8` stopped advancing. Until the SoC clock tree is tied down
    more accurately, the earlier coarse timer tick is retained because it is the
    observed path that gets past this firmware wait without synthetic PC jumps.
  - A temporary alternate CS-base graphics test was rejected: interpreting the
    page graphics address as if external CS began at `0x020000` maps
    `0x10cd98` to the current-bus address `0x11cd98`, but false-colour
    rendering of that dump produces repeated speckle rather than coherent art.
    This supports retaining the verified MobiGo 2 `0x030000` external CS base.
- Synthetic video fallback was removed from frame composition. If no plausible
  framebuffer or PPU source is active, the emulator now displays black instead
  of debug-generated colour noise.

## Real-hardware MBA lifecycle and display findings (2026-07-22)

- A real MobiGo 2 confirmed that a G1 MBA entry is an application launch
  routine. A top-level `RETF` exits to LD; it does not leave the program
  resident. A test that returned after drawing therefore repeated
  Loading → one colour → Loading. The emulator now recognizes verified MBA
  headers loaded by system DMA, records entry with its caller stack, and marks
  the matching top-level return as an application exit.
- The earlier resident test stub began with unSP opcode `0xf140` (`INT OFF`).
  The emulator's host renderer had read framebuffer RAM unconditionally, so it
  displayed the test even though the real unit remained frozen white. The
  working hardware build preserves inherited IRQ/FIQ, services the watchdog,
  and stays resident. While a DMA-discovered MBA owns LD's inherited display,
  the emulator now preserves the previously latched frame if both interrupt
  classes are disabled. Standalone SDK programs that own the display are not
  subjected to this LD-specific contract.
- The hardware-working program reads live FBI/FBO from `0x7078..0x707b`,
  because retail firmware rotates among addresses including `0x3fd400`,
  `0x41d400`, and `0x0ad400`. It fills both buffers using the documented
  system-DMA control value `0x0089`, waits for `P_DMA_INT` bit 0, clears that
  flag W1C, and uses frame-base PPU mode `0x0088`.
- The verified GPL16250VA SDK definitions show that `P_PPU_Enable` bit 0 is not
  a global enable field. The renderer no longer gates all page/sprite output on
  that bit. The SDK-defined RTC HMS registers are now readable/writable and
  advance from emulated time under `C_RTCEN`; previously only scheduler
  interrupt timing existed.
- Regression coverage in `hardware_accuracy_test` checks the application
  entry/return lifetime, loader-owned scanout stall, corrected PPU bit-0
  behavior, RTC rollover/status, MBA-header discovery, and the official
  fixed-source DMA completion/W1C path.

## Event scheduler and PPU effects (2026-08-01)

- Peripheral state is synchronized once at the end of each CPU instruction.
  Timer, timebase, RTC, watchdog, ADC, USB-suspend, SPU-beat, and video-edge
  deadlines let the common path return immediately until an event is due.
  Live MMIO counter reads still force synchronization at the read cycle.
- Counter timestamps use `UINT64_MAX` as their uninitialized state. Cycle zero
  is therefore a valid timestamp and no longer discards the first interval
  after reset.
- Video frame comparison and wrap status are scheduled at programmed TFT cycle
  boundaries instead of recomputing division/modulo state every instruction.
- GPL16250 PPU compositing now applies documented 25/50/75/100-percent global
  blending, individual 6-bit sprite alpha, RGB1555 transparency, output fade,
  and saturation. The blend equation and register interpretation were checked
  against the GPL16250 hardware documentation and MAME's GPL renderer.
- Real-hardware comparison of SY found that a centered Family-B object leaving
  the top edge must be clipped; treating its coordinate as a 9-bit ring made it
  reappear at the bottom. Live SY sprite tables independently show ten-bit
  signed coordinates (`0x39a == -102`, for example). The renderer now decodes
  that signed field before the centered transform and clips the transformed
  sprite on both LCD axes.
- Regression tests cover exact timer overflow/reload, lazy counter reads,
  exact video edges, blend values, direct-color transparency, fade, and SY's
  signed/clipped centered-sprite behavior.
- A deterministic 10-million-instruction retail boot run retained the same
  23,405,111 cycles, PC, and register state while improving locally from 26.7
  to 35.3 MIPS. A 10-million-instruction Clang PGO training profile reached
  about 44 MIPS. These are machine-specific development measurements.

## Retail cartridge SPU completion (2026-08-01)

- Cartridge `mobigo_252800.bin` accepted touch-down and touch-up on both its
  saved-profile and Guest buttons but remained on the profile overlay. The CPU
  was still running; this was a game state wait rather than a crash or halt.
- Decompilation of the cartridge's profile update at `0x21b528` showed that the
  selection was accepted and its advance flag was set. The update then waited
  on the resident sound query at `0x27ba6c` before invoking the next scene.
- The query distinguishes the programmed `P_SPU_CH_ENABLE` mask from live
  `P_SPU_CH_STATUS`. A naturally completed one-shot clears `CH_STATUS` but
  leaves `CH_ENABLE` programmed. The emulator incorrectly cleared both, which
  made the resident report the voice as permanently stopping.
- Natural channel completion now clears only live status, latches the channel
  end/stop events, and routes enabled SPU channel, envelope, and beat sources
  through IRQ4 or FIQ according to `P_INT_Priority3`. `audio_test` covers the
  enable/status distinction, W1C channel event, and both interrupt routes.
- Deterministic integration runs now advance Guest to the title screen and the
  saved profile into the following animated scene.

## Three supplied retail cartridge regressions (2026-08-23)

- **Emulator-observed:** Toy Story 3 stopped at instruction `476,622,489`
  (`PC=0x28ae13`) and Shrek Forever After stopped at `476,622,579`
  (`PC=0x276948`). Both reached the same legacy runtime initialization that
  programs all of GPIO-D (`Buffer=0`, `Direction=Attribute=0xffff`). The old
  power-latch model treated that ordinary whole-port clear as a terminal D4
  falling edge because firmware had previously held D4 high.
- The resident Off routine uses a distinct D4-only terminal sequence
  (`Direction=Attribute=0x0010` and a Buffer write that releases D4). Power-off
  detection now recognizes that deliberate configuration instead of treating
  any all-port initialization as shutdown. A counter-test still reaches the
  real Off state at approximately 437.6 million instructions, `PC=0x3a500`;
  the exact boundary varies with the scripted key-hold duration.
- Mickey remained CPU-active beyond 800 million instructions; its apparent
  freeze was display selection. All three cartridges switch
  `P_PPU_Enable` to direct page/sprite mode (`0x0063`, frame-base bit `0x0080`
  clear) while nonzero FBI/FBO values inherited from the resident remain in
  their registers. `Video::compose()` had used pointer plausibility to suppress
  direct rendering and then restored the stale resident framebuffer. Direct
  PPU output now takes precedence whenever frame-base mode is clear, while the
  separate inherited-MBA interrupt guard is checked before any pixels mutate.
- Integrated scripted runs select each cartridge at 410 million instructions,
  select Guest at 530 million, and reach 750 million without a halt. Toy Story
  advances through Disney/Pixar/title art (`PC=0x242c0a`), Shrek reaches title
  art (`PC=0x2762ea`), and Mickey advances through its Disney fade
  (`PC=0x305e7`). Before the scanout fix all three current frames shared the
  stale hash prefix `67cc3d865efa`; their corrected final frame hash prefixes
  are respectively `c6ac4e7fbf88`, `740c1f4d32e8`, and `15d8bcf3a60a`.
- Synthetic regressions express the GPIO-D and PPU mode rules without embedding
  cartridge bytes or matching title names.

- Remaining accuracy work includes exact E-Fuse meanings, complete
  timer/counter slot behavior, row-zoom/transform effects, exact SPU
  ADPCM36/envelope edge behavior, analog output characteristics, physical touch
  calibration, and hardware-revision coverage. Standard matrix controls and
  scripted touch behavior are modeled; their present evidence boundaries are
  tracked in the capability matrix rather than this chronology.

## Uncertainty policy

- Unknown MMIO reads/writes are logged with address, value, and current PC.
- Any behavior inferred from firmware access patterns instead of documentation is
  marked in source comments with `ASSUMPTION`.
- Temporary workarounds must be labelled `WORKAROUND` in code and notes.
