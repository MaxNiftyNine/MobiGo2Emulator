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
CMAKE_GENERATOR_NAME=${MOBIGO_CMAKE_GENERATOR:-}
NATIVE_OPTIMIZATIONS=${MOBIGO_NATIVE_OPTIMIZATIONS:-OFF}

if [ -z "$CMAKE_GENERATOR_NAME" ] && command -v ninja >/dev/null 2>&1; then
    CMAKE_GENERATOR_NAME=Ninja
fi

run_cmake() {
    if [ -n "$CMAKE_GENERATOR_NAME" ]; then
        cmake -G "$CMAKE_GENERATOR_NAME" "$@"
    else
        cmake "$@"
    fi
}

find_emulator() {
    directory=$1
    for candidate in \
        "$directory/mobigo2_emu" \
        "$directory/mobigo2_emu.exe" \
        "$directory/Release/mobigo2_emu" \
        "$directory/Release/mobigo2_emu.exe"
    do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return
        fi
    done
    echo "CMake did not produce a runnable emulator under $directory" >&2
    return 1
}

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
rm -f "$RAW_PROFILE" "$MERGED_PROFILE"
# PGO results are compiler- and generator-specific. Reconfigure both stages
# from clean generated directories so a previous local toolchain cannot leak
# into a new release build.
cmake -E remove_directory "$GENERATE_DIR"
cmake -E remove_directory "$FINAL_DIR"
run_cmake -S "$SCRIPT_DIR" -B "$GENERATE_DIR" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DMOBIGO2_NATIVE_OPTIMIZATIONS="$NATIVE_OPTIMIZATIONS" \
    -DMOBIGO2_PGO_GENERATE=ON \
    -DMOBIGO2_PGO_PROFILE= \
    -DPKG_CONFIG_EXECUTABLE="$PKG_CONFIG_BIN"
cmake --build "$GENERATE_DIR" --config Release --parallel

TRAINING_EMULATOR=$(find_emulator "$GENERATE_DIR")
LLVM_PROFILE_FILE="$RAW_PROFILE" "$TRAINING_EMULATOR" \
    --no-cap --no-window --steps "$TRAIN_STEPS" \
    --rom "$REPO_DIR/firmware/internalrom.bin" \
    --spi "$REPO_DIR/firmware/spi.bin" \
    --nand "$REPO_DIR/firmware/nand.bin"
[ -s "$RAW_PROFILE" ] || {
    echo "Instrumented run did not produce $RAW_PROFILE" >&2
    exit 1
}
"$LLVM_PROFDATA" merge -output="$MERGED_PROFILE" "$RAW_PROFILE"

run_cmake -S "$SCRIPT_DIR" -B "$FINAL_DIR" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DMOBIGO2_NATIVE_OPTIMIZATIONS="$NATIVE_OPTIMIZATIONS" \
    -DMOBIGO2_PGO_GENERATE=OFF \
    -DMOBIGO2_PGO_PROFILE="$MERGED_PROFILE" \
    -DPKG_CONFIG_EXECUTABLE="$PKG_CONFIG_BIN"
cmake --build "$FINAL_DIR" --config Release --parallel

FINAL_EMULATOR=$(find_emulator "$FINAL_DIR")
echo "PGO emulator: $FINAL_EMULATOR"
