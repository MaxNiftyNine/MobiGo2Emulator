#!/usr/bin/env python3
"""Run a repeatable, headless emulator throughput benchmark.

The benchmark deliberately uses the public CLI. That keeps it useful for
comparing packaged binaries as well as local builds, while its fixed-step PC
checkpoint catches gross execution drift during performance work.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_STEPS = 100_000_000
DEFAULT_STOCK_PC = 0x536DA
MIPS_RE = re.compile(r"Emulation time [0-9.]+ s \(([0-9.]+) MIPS\)")
STOP_RE = re.compile(r"Stopped after (\d+) instructions at PC=0x([0-9a-fA-F]+)")


@dataclass(frozen=True)
class Result:
    mips: float
    instructions: int
    pc: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--emulator",
        type=Path,
        default=ROOT
        / "build"
        / ("mobigo2_emu.exe" if os.name == "nt" else "mobigo2_emu"),
        help="emulator binary to benchmark (default: build/mobigo2_emu[.exe])",
    )
    parser.add_argument("--steps", type=int, default=DEFAULT_STEPS)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--cart", type=Path, help="optional cartridge image")
    parser.add_argument(
        "--expect-pc",
        type=lambda value: int(value, 0),
        help=(
            "require this final program counter (decimal or 0x-prefixed); "
            "the stock 100M-instruction workload checks 0x536da automatically"
        ),
    )
    return parser.parse_args()


def run_once(command: list[str]) -> Result:
    completed = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout)
        raise RuntimeError(f"emulator exited with status {completed.returncode}")
    mips_match = MIPS_RE.search(completed.stdout)
    stop_match = STOP_RE.search(completed.stdout)
    if not mips_match or not stop_match:
        sys.stderr.write(completed.stdout)
        raise RuntimeError("emulator output did not contain benchmark summary")
    return Result(
        mips=float(mips_match.group(1)),
        instructions=int(stop_match.group(1)),
        pc=int(stop_match.group(2), 16),
    )


def main() -> int:
    args = parse_args()
    if args.steps <= 0 or args.repeats <= 0 or args.warmups < 0:
        raise SystemExit("steps/repeats must be positive and warmups cannot be negative")

    required = {
        "emulator": args.emulator,
        "internal ROM": ROOT / "firmware" / "internalrom.bin",
        "SPI": ROOT / "firmware" / "spi.bin",
        "NAND": ROOT / "firmware" / "nand.bin",
    }
    if args.cart:
        required["cartridge"] = args.cart
    missing = [f"{label}: {path}" for label, path in required.items() if not path.is_file()]
    if missing:
        raise SystemExit("missing benchmark input(s):\n  " + "\n  ".join(missing))

    command = [
        str(args.emulator.resolve()),
        "--rom", str(required["internal ROM"]),
        "--spi", str(required["SPI"]),
        "--nand", str(required["NAND"]),
        "--no-cap",
        "--no-window",
        "--steps", str(args.steps),
    ]
    if args.cart:
        command.extend(("--cart", str(args.cart.resolve())))

    for _ in range(args.warmups):
        run_once(command)
    results = [run_once(command) for _ in range(args.repeats)]
    early = [result.instructions for result in results if result.instructions != args.steps]
    if early:
        raise RuntimeError(
            f"benchmark stopped before --steps={args.steps}: observed {sorted(set(early))}"
        )
    final_checkpoints = {(result.instructions, result.pc) for result in results}
    if len(final_checkpoints) != 1:
        checkpoints = ", ".join(
            f"{instructions} instructions/PC=0x{pc:x}"
            for instructions, pc in sorted(final_checkpoints)
        )
        raise RuntimeError(f"benchmark final checkpoints diverged: {checkpoints}")

    expected_pc = args.expect_pc
    if (
        expected_pc is None
        and args.cart is None
        and args.steps == DEFAULT_STEPS
    ):
        expected_pc = DEFAULT_STOCK_PC
    state = results[0]
    if expected_pc is not None and state.pc != expected_pc:
        raise RuntimeError(
            f"benchmark final PC mismatch: expected 0x{expected_pc:x}, "
            f"observed 0x{state.pc:x}"
        )

    samples = [result.mips for result in results]
    print(f"binary: {args.emulator}")
    print(f"workload: {args.steps} instructions, uncapped, repeats={args.repeats}")
    print(f"final state: PC=0x{state.pc:x}")
    print(
        "throughput: "
        f"median={statistics.median(samples):.3f} MIPS "
        f"min={min(samples):.3f} max={max(samples):.3f}"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        raise SystemExit(f"benchmark failed: {error}") from error
