# Included firmware input set

The emulator expects the included 128 KiB internal ROM, 2 MiB SPI image, and
reassembled 132 MiB stitched NAND. The two Git-tracked NAND parts combine to
138,412,032 bytes with SHA-256
`66e686225f709e07ca0d76b78b82374cb6fd27296c7a3d8b98c765da66442e7a`.

From the emulator repository root:

```sh
python3 scripts/assemble_firmware.py
```

This creates `firmware/nand.bin`, verifies its size and
hash, and leaves the numbered inputs unchanged. The assembled image is ignored
by Git and must not be committed.

The included `mobigo.bin` is the 8 MiB US MobiGo 1 external NOR BIOS/OS.
CMake copies it into the executable's `firmware` directory automatically, and
release packages include it for the **System (MobiGo 1)** launcher button.
It is independent of the MobiGo 2 firmware set above.

Official release archives already include the assembled `nand.bin`; no setup
step is needed before double-clicking the packaged emulator.

Firmware and device-derived images are not covered by the emulator's GPL
license. Keep an untouched backup before any storage experiment.
