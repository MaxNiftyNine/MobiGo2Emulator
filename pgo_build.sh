#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR="$SCRIPT_DIR"
GENERATE_DIR="$REPO_DIR/build/pgo-generate"
FINAL_DIR="$REPO_DIR/build/pgo"
PROFILE_DIR="$REPO_DIR/build/pgo-data"
RAW_PROFILE="$PROFILE_DIR/mobigo2.profraw"
MERGED_PROFILE="$PROFILE_DIR/mobigo2.profdata"
TRAIN_STEPS=${TRAIN_STEPS:-100000000}

find_pkg_config() {
    if [ -n "${MOBIGO_PKG_CONFIG:-}" ] && [ -x "$MOBIGO_PKG_CONFIG" ]; then
        printf '%s\n' "$MOBIGO_PKG_CONFIG"
        return
    fi
    for candidate in /opt/homebrew/bin/pkg-config /usr/local/bin/pkg-config "$(command -v pkg-config 2>/dev/null || true)"; do
        if [ -x "$candidate" ] && "$candidate" --exists sdl2; then
            printf '%s\n' "$candidate"
            return
        fi
    done
    return 1
}

find_llvm_profdata() {
    if command -v xcrun >/dev/null 2>&1 && xcrun --find llvm-profdata >/dev/null 2>&1; then
        xcrun --find llvm-profdata
    elif command -v llvm-profdata >/dev/null 2>&1; then
        command -v llvm-profdata
    else
        return 1
    fi
}

PKG_CONFIG_BIN=$(find_pkg_config) || {
    echo "SDL2 and pkg-config are required (for Homebrew: brew install sdl2 pkg-config)." >&2
    exit 1
}
LLVM_PROFDATA=$(find_llvm_profdata) || {
    echo "llvm-profdata is required for profile-guided optimization." >&2
    exit 1
}

for firmware in internalrom.bin spi.bin nand.bin; do
    if [ ! -f "$REPO_DIR/firmware/$firmware" ]; then
        echo "Missing training firmware: firmware/$firmware" >&2
        exit 1
    fi
done

mkdir -p "$PROFILE_DIR"
cmake -S "$SCRIPT_DIR" -B "$GENERATE_DIR" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DMOBIGO2_PGO_GENERATE=ON \
    -DPKG_CONFIG_EXECUTABLE="$PKG_CONFIG_BIN"
cmake --build "$GENERATE_DIR" --config Release --parallel

LLVM_PROFILE_FILE="$RAW_PROFILE" "$GENERATE_DIR/mobigo2_emu" \
    --mode fast --no-window --steps "$TRAIN_STEPS" \
    --rom "$REPO_DIR/firmware/internalrom.bin" \
    --spi "$REPO_DIR/firmware/spi.bin" \
    --nand "$REPO_DIR/firmware/nand.bin"
"$LLVM_PROFDATA" merge -output="$MERGED_PROFILE" "$RAW_PROFILE"

cmake -S "$SCRIPT_DIR" -B "$FINAL_DIR" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DMOBIGO2_PGO_PROFILE="$MERGED_PROFILE" \
    -DPKG_CONFIG_EXECUTABLE="$PKG_CONFIG_BIN"
cmake --build "$FINAL_DIR" --config Release --parallel

echo "PGO emulator: $FINAL_DIR/mobigo2_emu"
