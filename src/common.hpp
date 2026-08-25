#pragma once
/*
 * mobigo2_emu - GPL16250 / unSP-based VTech MobiGo 2 emulator.
 *
 * CPU interpreter portions are a clean standalone port of documented unSP
 * ISA 1.2 behavior with reference to MAME's GPL-2.0+ unSP core by
 * Segher Boessenkool, Ryan Holtz, and David Haywood. This file is therefore
 * distributed under GPL-2.0-or-later.
 *
 * Hardware map/register names are from the supplied Generalplus manuals and
 * MAME's BSD-3-Clause GPL16250 notes. Where behavior is not documented, source
 * comments say ASSUMPTION and runtime logs keep the firmware-visible access.
 */

#include <SDL.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mobigo {

inline std::string path_to_utf8(const std::filesystem::path &path);
inline std::filesystem::path path_from_utf8(std::string_view text);

constexpr uint32_t kAddrMask = 0x3fffff;      // unSP 22-bit word address
constexpr uint32_t kPpuAddrHighMask = 0x07ff; // PPU FREE mode exposes a 27-bit word address.
constexpr uint32_t kWordCount = 0x400000;
constexpr uint32_t kCsBase = 0x030000;        // GPAC800/GPL16250 NAND variant external chip-select base.
constexpr uint32_t kSdramWords = 0x400000;    // EM638165TS-6G: 4M x16 SDRAM, 64 Mbit / 8 MiB.
constexpr uint32_t kMmioBase = 0x007000;
constexpr uint32_t kMmioEnd = 0x007fff;
constexpr uint16_t UNSP_N = 0x0200;
constexpr uint16_t UNSP_Z = 0x0100;
constexpr uint16_t UNSP_S = 0x0080;
constexpr uint16_t UNSP_C = 0x0040;

inline std::ofstream g_log;

constexpr uint64_t kDefaultRenderInterval = 50000;
constexpr int kDefaultInstructionBatch = 20000;
// Frame-base PPU rendering is hardware work, not an instantaneous register
// side effect. Give firmware enough elapsed device time to observe the busy
// bit and service intervening video/timer IRQs before completion latches.
constexpr uint64_t kPpuJobCycles = 160000;

struct TouchAdcPoint {
    uint16_t x;
    uint16_t y;
};

constexpr TouchAdcPoint screen_to_touch_adc(int screen_x, int screen_y,
                                             int width = 320, int height = 240) {
    // Retail calibration recovered from the firmware's board-input state.
    // Both panel electrodes are reversed relative to LCD coordinates.
    constexpr uint16_t x_min = 0x0186;
    constexpr uint16_t x_max = 0x0e80;
    constexpr uint16_t y_min = 0x02b6;
    constexpr uint16_t y_max = 0x0d5c;
    const int x = std::clamp(screen_x, 0, width - 1);
    const int y = std::clamp(screen_y, 0, height - 1);
    return {
        uint16_t(x_max - x * (x_max - x_min) / (width - 1)),
        uint16_t(y_max - y * (y_max - y_min) / (height - 1)),
    };
}

inline uint32_t ppu_frame_addr(uint16_t low, uint16_t high) {
    // Verified SDK examples require FBI_ADDR[26:0] to be 16-word aligned;
    // the low four address bits are hard-wired to zero.
    return uint32_t(low & 0xfff0) | ((uint32_t(high) & kPpuAddrHighMask) << 16);
}

[[noreturn]] inline void die(const std::string &msg) {
    throw std::runtime_error(msg);
}

inline std::vector<uint8_t> read_file_bytes(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary);
#ifdef __EMSCRIPTEN__
    // GitHub Pages has a 100 MiB per-file limit.  The stock NAND image is
    // shipped as two preloaded Emscripten files and joined at startup.
    if (!f && path.filename() == "nand.bin") {
        std::vector<uint8_t> out;
        const std::string encoded_path = path_to_utf8(path);
        for (const char *suffix : {".part00", ".part01"}) {
            std::ifstream part(encoded_path + suffix, std::ios::binary);
            if (!part) die("failed to open " + encoded_path + suffix);
            part.seekg(0, std::ios::end);
            const auto size = part.tellg();
            part.seekg(0, std::ios::beg);
            const size_t old_size = out.size();
            out.resize(old_size + static_cast<size_t>(size));
            part.read(reinterpret_cast<char *>(out.data() + old_size),
                      static_cast<std::streamsize>(size));
        }
        return out;
    }
#endif
    if (!f) die("failed to open " + path_to_utf8(path));
    f.seekg(0, std::ios::end);
    const auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> out(static_cast<size_t>(size));
    if (!out.empty()) f.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(out.size()));
    return out;
}

inline void replace_file_atomic(const std::filesystem::path &temporary,
                                const std::filesystem::path &destination,
                                std::string_view description) {
    std::error_code error;
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
    error = std::error_code(static_cast<int>(GetLastError()),
                            std::system_category());
#else
    std::filesystem::rename(temporary, destination, error);
    if (!error) return;
#endif
    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);
    die("failed to replace " + std::string(description) + " " +
        path_to_utf8(destination) + ": " + error.message());
}

inline void write_file_bytes_atomic(const std::filesystem::path &path,
                                    const std::vector<uint8_t> &bytes) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
        if (!f) die("failed to open " + path_to_utf8(temporary));
        if (!bytes.empty())
            f.write(reinterpret_cast<const char *>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!f) die("failed while writing " + path_to_utf8(temporary));
    }
    std::error_code ec;
    const auto permissions = std::filesystem::status(path, ec).permissions();
    if (!ec) std::filesystem::permissions(temporary, permissions, ec);
    replace_file_atomic(temporary, path, "file");
}

inline uint16_t le16(const std::vector<uint8_t> &v, size_t byte) {
    if (byte + 1 >= v.size()) return 0xffff;
    return uint16_t(v[byte] | (v[byte + 1] << 8));
}

inline std::vector<uint16_t> bytes_to_words(const std::vector<uint8_t> &v, bool big_endian) {
    std::vector<uint16_t> out((v.size() + 1) / 2, 0xffff);
    for (size_t i = 0; i < out.size(); ++i) {
        const size_t byte = i * 2;
        if (byte + 1 >= v.size()) out[i] = 0xffff;
        else if (big_endian) out[i] = uint16_t((v[byte] << 8) | v[byte + 1]);
        else out[i] = le16(v, byte);
    }
    return out;
}

struct ScriptedTouch {
    uint64_t at = 0;
    uint64_t duration = 0;
    uint16_t x = 0;
    uint16_t y = 0;
};

struct ScriptedKeyTransition {
    uint64_t at = 0;
    unsigned row = 0;
    unsigned column = 0;
    bool pressed = false;
    std::string name;
};

inline std::string ascii_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

// SDL2main supplies UTF-8 argv and file-drop paths on Windows. Keep persisted
// and displayed paths UTF-8 on every host instead of relying on the current
// narrow-string locale.
inline std::string path_to_utf8(const std::filesystem::path &path) {
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}

inline std::filesystem::path path_from_utf8(std::string_view text) {
    if (text.empty()) return {};
    const auto *first = reinterpret_cast<const char8_t *>(text.data());
    return std::filesystem::path(std::u8string(first, first + text.size()));
}

struct MatrixKey {
    unsigned row = 0;
    unsigned column = 0;

    bool operator==(const MatrixKey &) const = default;
};

struct NamedMatrixKey {
    std::string_view name;
    MatrixKey key;
};

// This is the single source of truth for both scripted and SDL input. Host
// key aliases below resolve to one of these logical MobiGo controls.
inline constexpr std::array<NamedMatrixKey, 49> kNamedMatrixKeys{{
    {"t", {0, 0}}, {"y", {0, 1}}, {"u", {0, 2}}, {"i", {0, 3}},
    {"o", {0, 4}}, {"p", {0, 5}}, {"w", {0, 6}}, {"e", {0, 7}},
    {"r", {0, 8}},
    {"f", {1, 0}}, {"g", {1, 1}}, {"h", {1, 2}}, {"j", {1, 3}},
    {"k", {1, 4}}, {"l", {1, 5}}, {"a", {1, 6}}, {"s", {1, 7}},
    {"d", {1, 8}},
    {"c", {2, 0}}, {"v", {2, 1}}, {"b", {2, 2}}, {"n", {2, 3}},
    {"m", {2, 4}}, {"del", {2, 5}}, {"caps", {2, 6}}, {"z", {2, 7}},
    {"x", {2, 8}},
    {"leftarrow", {3, 0}}, {"space", {3, 1}}, {"off", {3, 2}},
    {"left", {3, 3}}, {"up", {3, 4}}, {"primary", {3, 5}},
    {"q", {3, 6}}, {"num", {3, 7}},
    {"rightarrow", {4, 0}}, {"enter", {4, 1}}, {"exit", {4, 2}},
    {"right", {4, 3}}, {"down", {4, 4}}, {"help", {4, 5}},
    {"brightness", {4, 6}}, {"voldown", {4, 7}}, {"volup", {4, 8}},
    {"question", {5, 5}},
    // Useful spelling aliases. These intentionally share physical switches.
    {"delete", {2, 5}}, {"questionmark", {5, 5}}, {"power", {3, 2}},
}};

inline std::optional<MatrixKey> matrix_key_from_name(std::string_view name) {
    for (const NamedMatrixKey &candidate : kNamedMatrixKeys) {
        if (candidate.name == name) return candidate.key;
    }
    return std::nullopt;
}

// The launcher persists these bindings so a player can choose the physical
// keys that feel natural without changing the emulated MobiGo matrix.
enum class BindableControl : size_t {
    Left,
    Right,
    Up,
    Down,
    Primary,
    Exit,
    Help,
    Power,
    Brightness,
    VolumeDown,
    VolumeUp,
    MotionLeft,
    MotionRight,
    MotionUp,
    MotionDown,
    Count,
};

constexpr size_t kBindableControlCount = size_t(BindableControl::Count);

inline constexpr std::array<std::string_view, kBindableControlCount>
    kBindableControlNames{{
        "D-PAD LEFT", "D-PAD RIGHT", "D-PAD UP", "D-PAD DOWN", "PRIMARY",
        "EXIT", "HELP", "POWER", "BRIGHTNESS", "VOLUME DOWN", "VOLUME UP",
        "MOTION LEFT", "MOTION RIGHT", "MOTION UP", "MOTION DOWN",
    }};

inline constexpr std::array<std::string_view, kBindableControlCount>
    kBindableControlConfigKeys{{
        "bind_left", "bind_right", "bind_up", "bind_down", "bind_primary",
        "bind_exit", "bind_help", "bind_power", "bind_brightness",
        "bind_volume_down", "bind_volume_up", "bind_motion_left",
        "bind_motion_right", "bind_motion_up", "bind_motion_down",
    }};

inline constexpr std::array<SDL_Keycode, kBindableControlCount>
    kDefaultInputBindings{{
        SDLK_LEFT, SDLK_RIGHT, SDLK_UP, SDLK_DOWN, SDLK_LCTRL, SDLK_ESCAPE,
        SDLK_F1, SDLK_F2, SDLK_F6, SDLK_F7, SDLK_F8, SDLK_HOME, SDLK_END,
        SDLK_PAGEUP, SDLK_PAGEDOWN,
    }};

inline constexpr std::array<MatrixKey, 11> kBindableMatrixKeys{{
    {3, 3}, {4, 3}, {3, 4}, {4, 4}, {3, 5}, {4, 2},
    {4, 5}, {3, 2}, {4, 6}, {4, 7}, {4, 8},
}};

struct InputBindings {
    std::array<SDL_Keycode, kBindableControlCount> keys =
        kDefaultInputBindings;
};

inline bool input_binding_contains(const InputBindings &bindings,
                                   SDL_Keycode keycode) {
    return std::find(bindings.keys.begin(), bindings.keys.end(), keycode) !=
           bindings.keys.end();
}

inline std::optional<MatrixKey>
matrix_key_from_sdl(const InputBindings &bindings, SDL_Keycode keycode) {
    for (size_t index = 0; index < kBindableMatrixKeys.size(); ++index) {
        if (bindings.keys[index] == keycode)
            return kBindableMatrixKeys[index];
    }
    return std::nullopt;
}

inline std::optional<unsigned>
motion_direction_from_sdl(const InputBindings &bindings, SDL_Keycode keycode) {
    constexpr size_t kMotionFirst = size_t(BindableControl::MotionLeft);
    for (size_t index = 0; index < 4; ++index) {
        if (bindings.keys[kMotionFirst + index] == keycode)
            return unsigned(index);
    }
    return std::nullopt;
}

inline std::optional<MatrixKey> matrix_key_from_sdl(SDL_Keycode keycode) {
    if (const auto configured = matrix_key_from_sdl(InputBindings{}, keycode))
        return configured;
    const char *name = nullptr;
    switch (keycode) {
    case SDLK_t: name = "t"; break; case SDLK_y: name = "y"; break;
    case SDLK_u: name = "u"; break; case SDLK_i: name = "i"; break;
    case SDLK_o: name = "o"; break; case SDLK_p: name = "p"; break;
    case SDLK_w: name = "w"; break; case SDLK_e: name = "e"; break;
    case SDLK_r: name = "r"; break; case SDLK_f: name = "f"; break;
    case SDLK_g: name = "g"; break; case SDLK_h: name = "h"; break;
    case SDLK_j: name = "j"; break; case SDLK_k: name = "k"; break;
    case SDLK_l: name = "l"; break; case SDLK_a: name = "a"; break;
    case SDLK_s: name = "s"; break; case SDLK_d: name = "d"; break;
    case SDLK_c: name = "c"; break; case SDLK_v: name = "v"; break;
    case SDLK_b: name = "b"; break; case SDLK_n: name = "n"; break;
    case SDLK_m: name = "m"; break; case SDLK_BACKSPACE: name = "del"; break;
    case SDLK_CAPSLOCK: name = "caps"; break; case SDLK_z: name = "z"; break;
    case SDLK_x: name = "x"; break; case SDLK_LEFTBRACKET: name = "leftarrow"; break;
    case SDLK_SPACE: name = "space"; break; case SDLK_RCTRL: name = "primary"; break;
    case SDLK_q: name = "q"; break; case SDLK_NUMLOCKCLEAR: name = "num"; break;
    case SDLK_RIGHTBRACKET: name = "rightarrow"; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: name = "enter"; break;
    case SDLK_SLASH: name = "question"; break;
    default: return std::nullopt;
    }
    return matrix_key_from_name(name);
}

struct Options {
    std::filesystem::path rom = "internalrom.bin";
    std::filesystem::path cart;
    std::filesystem::path spi = "spi.bin";
    std::filesystem::path nand = "nand.bin";
    std::filesystem::path dump_frame;
    std::filesystem::path dump_current_frame;
    std::filesystem::path dump_frame_dir;
    std::filesystem::path dump_memory;
    std::filesystem::path dump_code;
    uint32_t dump_memory_base = 0;
    uint32_t dump_memory_words = 0;
    uint32_t dump_code_base = 0;
    uint32_t dump_code_words = 0;
    uint64_t max_steps = 0;
    uint64_t render_interval = kDefaultRenderInterval;
    uint32_t max_present_hz = 60;
    uint32_t speed_percent = 100;
    uint64_t open_window_at = 0;
    uint64_t start_logging_at = 0;
    uint64_t dump_frame_interval = 0;
    uint64_t trace_limit = 0;
    uint64_t trace_transition_limit = 0;
    uint64_t trace_start_insn = 0;
    std::vector<ScriptedTouch> scripted_touches;
    std::vector<ScriptedKeyTransition> scripted_key_transitions;
    uint32_t trace_lo = 0;
    uint32_t trace_hi = 0;
    uint32_t rom_base = 0x008000;
    uint32_t start_pc = 0;
    std::string rom_endian = "le";
    bool trace = false;
    bool trace_transitions = false;
    bool trace_range = false;
    bool dump_memory_dma = false;
    bool start_pc_set = false;
    bool rom_shadow_low = false;
    bool rom_fetch_mirror64 = false;
    bool allow_invalid_alu_nop = false;
    bool auto_power_wake = false;
    bool log = false;
    bool vsync = false;
    uint16_t efuse0 = 0;
    uint16_t efuse1 = 0;
    uint16_t efuse2 = 0x0300;
    uint16_t gpio_a = 0x7fff;
    uint16_t gpio_b = 0xfffe;
    uint16_t gpio_c = 0xfeff;
    uint16_t gpio_d = 0xffff;
    uint16_t gpio_e = 0x0000;
    uint16_t battery_adc = 0x0500;
    bool window = true;
    bool realtime_cap = true;
    bool realtime_cap_explicit = false;
    bool audio = false;
    bool show_speed = true;
    InputBindings input_bindings;
    // Desktop-frontend presentation choices. These are deliberately not CLI
    // switches, so legacy invocations retain exactly their existing defaults.
    int window_scale = 2;
    bool fullscreen = false;
    bool integer_scaling = false;
    std::filesystem::path log_path = "emulator.log";
};

inline Options parse_args(int argc, char **argv) {
    Options opt;
    if (argc == 1) {
        // Desktop packages are intentionally useful when double-clicked.
        opt.audio = true;
        opt.vsync = true;
    }
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char *name) -> std::string {
            if (i + 1 >= argc) die(std::string("missing value for ") + name);
            return argv[++i];
        };
        auto need_path = [&](const char *name) -> std::filesystem::path {
            return path_from_utf8(need(name));
        };
        if (a == "--rom") opt.rom = need_path("--rom");
        else if (a == "--cart") opt.cart = need_path("--cart");
        else if (a == "--spi") opt.spi = need_path("--spi");
        else if (a == "--nand") opt.nand = need_path("--nand");
        else if (a == "--rom-base") opt.rom_base = uint32_t(std::stoul(need("--rom-base"), nullptr, 0));
        else if (a == "--rom-endian") opt.rom_endian = need("--rom-endian");
        else if (a == "--start-pc") {
            opt.start_pc = uint32_t(std::stoul(need("--start-pc"), nullptr, 0)) & kAddrMask;
            opt.start_pc_set = true;
        }
        else if (a == "--steps") opt.max_steps = std::stoull(need("--steps"));
        else if (a == "--render-interval") opt.render_interval = std::stoull(need("--render-interval"));
        else if (a == "--max-present-hz") opt.max_present_hz = uint32_t(
            std::stoul(need("--max-present-hz")));
        else if (a == "--speed-percent") opt.speed_percent = uint32_t(
            std::stoul(need("--speed-percent")));
        else if (a == "--open-window-at") opt.open_window_at = std::stoull(need("--open-window-at"));
        else if (a == "--start-logging-at") opt.start_logging_at = std::stoull(need("--start-logging-at"));
        else if (a == "--dump-frame") opt.dump_frame = need_path("--dump-frame");
        else if (a == "--dump-current-frame") opt.dump_current_frame = need_path("--dump-current-frame");
        else if (a == "--dump-frame-dir") opt.dump_frame_dir = need_path("--dump-frame-dir");
        else if (a == "--dump-frame-interval") opt.dump_frame_interval = std::stoull(need("--dump-frame-interval"));
        else if (a == "--touch-event") {
            const std::string value = need("--touch-event");
            std::array<std::string, 4> fields;
            size_t start = 0;
            for (size_t field = 0; field < fields.size(); ++field) {
                const size_t comma = value.find(',', start);
                if (field + 1 < fields.size() && comma == std::string::npos)
                    die("--touch-event expects at,duration,x,y");
                if (field + 1 == fields.size() && comma != std::string::npos)
                    die("--touch-event expects exactly four values");
                fields[field] = value.substr(
                    start, comma == std::string::npos ? comma : comma - start);
                start = comma == std::string::npos ? value.size() : comma + 1;
            }
            ScriptedTouch touch;
            touch.at = std::stoull(fields[0]);
            touch.duration = std::stoull(fields[1]);
            touch.x = uint16_t(std::stoul(fields[2]));
            touch.y = uint16_t(std::stoul(fields[3]));
            if (touch.duration == 0 || touch.x >= 320 || touch.y >= 240)
                die("--touch-event requires nonzero duration and LCD coordinates x<320,y<240");
            opt.scripted_touches.push_back(touch);
        }
        else if (a == "--key-event") {
            const std::string value = need("--key-event");
            std::array<std::string, 3> fields;
            size_t start = 0;
            for (size_t field = 0; field < fields.size(); ++field) {
                const size_t comma = value.find(',', start);
                if (field + 1 < fields.size() && comma == std::string::npos)
                    die("--key-event expects at,duration,key");
                if (field + 1 == fields.size() && comma != std::string::npos)
                    die("--key-event expects exactly three values");
                fields[field] = value.substr(
                    start, comma == std::string::npos ? comma : comma - start);
                start = comma == std::string::npos ? value.size() : comma + 1;
            }
            const uint64_t at = std::stoull(fields[0]);
            const uint64_t duration = std::stoull(fields[1]);
            if (duration == 0) die("--key-event requires nonzero duration");
            const std::string key = ascii_lower(fields[2]);
            const std::optional<MatrixKey> matrix_key = matrix_key_from_name(key);
            if (!matrix_key)
                die("--key-event key name is not in the MobiGo 2 matrix map");
            opt.scripted_key_transitions.push_back(
                {at, matrix_key->row, matrix_key->column, true, key});
            opt.scripted_key_transitions.push_back(
                {at + duration, matrix_key->row, matrix_key->column, false, key});
        }
        else if (a == "--dump-memory") opt.dump_memory = need_path("--dump-memory");
        else if (a == "--dump-code") opt.dump_code = need_path("--dump-code");
        else if (a == "--dump-memory-base") {
            opt.dump_memory_base = uint32_t(std::stoul(need("--dump-memory-base"), nullptr, 0));
        }
        else if (a == "--dump-memory-words") {
            opt.dump_memory_words = uint32_t(std::stoul(need("--dump-memory-words"), nullptr, 0));
        }
        else if (a == "--dump-code-base") {
            opt.dump_code_base = uint32_t(std::stoul(need("--dump-code-base"), nullptr, 0));
        }
        else if (a == "--dump-code-words") {
            opt.dump_code_words = uint32_t(std::stoul(need("--dump-code-words"), nullptr, 0));
        }
        else if (a == "--trace-limit") opt.trace_limit = std::stoull(need("--trace-limit"));
        else if (a == "--trace-start-insn") opt.trace_start_insn = std::stoull(need("--trace-start-insn"));
        else if (a == "--trace-pc") {
            opt.trace_lo = uint32_t(std::stoul(need("--trace-pc lo"), nullptr, 0));
            opt.trace_hi = uint32_t(std::stoul(need("--trace-pc hi"), nullptr, 0));
            opt.trace_range = true;
            opt.trace = true;
        }
        else if (a == "--trace") { opt.trace = true; opt.log = true; }
        else if (a == "--trace-transitions") {
            opt.trace_transitions = true;
            opt.log = true;
        }
        else if (a == "--trace-transition-limit") {
            opt.trace_transition_limit = std::stoull(need("--trace-transition-limit"));
        }
        else if (a == "--dump-memory-dma") opt.dump_memory_dma = true;
        else if (a == "--log") opt.log = true;
        else if (a == "--log-file") { opt.log = true; opt.log_path = need_path("--log-file"); }
        else if (a == "--vsync") opt.vsync = true;
        else if (a == "--no-window") opt.window = false;
        else if (a == "--no-cap") {
            opt.realtime_cap = false;
            opt.realtime_cap_explicit = true;
        }
        else if (a == "--cap") {
            opt.realtime_cap = true;
            opt.realtime_cap_explicit = true;
        }
        else if (a == "--audio") opt.audio = true;
        else if (a == "--show-speed") opt.show_speed = true;
        else if (a == "--hide-speed") opt.show_speed = false;
        else if (a == "--rom-fetch-mirror64") opt.rom_fetch_mirror64 = true;
        else if (a == "--rom-shadow-low") opt.rom_shadow_low = true;
        else if (a == "--no-rom-shadow-low") opt.rom_shadow_low = false;
        else if (a == "--no-rom-fetch-mirror64") opt.rom_fetch_mirror64 = false;
        else if (a == "--allow-invalid-alu-nop") opt.allow_invalid_alu_nop = true;
        else if (a == "--auto-power-wake") {
            opt.auto_power_wake = true;
        }
        else if (a == "--no-auto-power-wake") {
            opt.auto_power_wake = false;
        }
        else if (a == "--efuse0") opt.efuse0 = uint16_t(std::stoul(need("--efuse0"), nullptr, 0));
        else if (a == "--efuse1") opt.efuse1 = uint16_t(std::stoul(need("--efuse1"), nullptr, 0));
        else if (a == "--efuse2") opt.efuse2 = uint16_t(std::stoul(need("--efuse2"), nullptr, 0));
        else if (a == "--gpio-a") opt.gpio_a = uint16_t(std::stoul(need("--gpio-a"), nullptr, 0));
        else if (a == "--gpio-b") opt.gpio_b = uint16_t(std::stoul(need("--gpio-b"), nullptr, 0));
        else if (a == "--gpio-c") opt.gpio_c = uint16_t(std::stoul(need("--gpio-c"), nullptr, 0));
        else if (a == "--gpio-d") opt.gpio_d = uint16_t(std::stoul(need("--gpio-d"), nullptr, 0));
        else if (a == "--gpio-e") opt.gpio_e = uint16_t(std::stoul(need("--gpio-e"), nullptr, 0));
        else if (a == "--battery-adc") opt.battery_adc = uint16_t(
            std::stoul(need("--battery-adc"), nullptr, 0) & 0x0fff);
        else if (a == "--help" || a == "-h") {
            std::cout << R"(usage: mobigo2_emu [options]

With no options, open the desktop launcher. Passing any option uses the
stable command-line interface.

Images:
  --rom PATH                 Internal ROM image
  --spi PATH                 SPI flash image
  --nand PATH                NAND image
  --cart PATH                Cartridge image
  --rom-base ADDRESS         Internal ROM word address
  --rom-endian le|be         Internal ROM byte order
  --start-pc ADDRESS         Override reset program counter

Execution and presentation:
  --speed-percent N          Run windowed emulation at 25..400% of hardware speed
  --steps N                  Stop after N instructions
  --render-interval N        Instructions between renderer updates
  --max-present-hz N         Host presentation rate (0 uses automatic 120 Hz)
  --open-window-at N         Defer the window until instruction N
  --no-window                Run headlessly
  --cap / --no-cap           Enable or disable real-time host pacing
  --vsync                    Synchronize window presentation
  --audio                    Enable host audio output
  --show-speed / --hide-speed Show or hide the live speed in the window title

Scripted input and output:
  --touch-event A,D,X,Y      Touch at instruction A for duration D (repeatable)
  --key-event A,D,KEY        Matrix key event (repeatable)
  --dump-frame PATH          Write the final composed framebuffer
  --dump-current-frame PATH  Write the current displayed framebuffer
  --dump-frame-dir PATH      Write a numbered framebuffer sequence
  --dump-frame-interval N    Instructions between sequence frames
  --dump-memory PATH         Write a final memory range
  --dump-memory-base ADDRESS First word address to dump
  --dump-memory-words N      Number of words to dump
  --dump-memory-dma          Read the dump through DMA-visible mapping
  --dump-code PATH           Write a final code range
  --dump-code-base ADDRESS   First code word address to dump
  --dump-code-words N        Number of code words to dump

Diagnostics:
  --log                      Enable the diagnostic log
  --log-file PATH            Enable logging to PATH
  --start-logging-at N       Defer logging until instruction N
  --trace                    Enable instruction tracing and logging
  --trace-pc LO HI           Trace only an inclusive PC range
  --trace-limit N            Limit instruction trace records
  --trace-start-insn N       Defer instruction tracing until N
  --trace-transitions        Log control-flow transitions
  --trace-transition-limit N Limit transition records

Hardware research options:
  --rom-shadow-low / --no-rom-shadow-low
  --rom-fetch-mirror64 / --no-rom-fetch-mirror64
  --allow-invalid-alu-nop
  --auto-power-wake / --no-auto-power-wake
  --efuse0 N  --efuse1 N  --efuse2 N
  --gpio-a N  --gpio-b N  --gpio-c N  --gpio-d N  --gpio-e N
  --battery-adc N

  -h, --help                 Show this help
)";
            std::exit(0);
        } else {
            die("unknown argument: " + a);
        }
    }
    if (opt.speed_percent < 25 || opt.speed_percent > 400)
        die("--speed-percent must be between 25 and 400");
    std::sort(opt.scripted_touches.begin(), opt.scripted_touches.end(),
              [](const ScriptedTouch &a, const ScriptedTouch &b) { return a.at < b.at; });
    for (size_t i = 1; i < opt.scripted_touches.size(); ++i) {
        const ScriptedTouch &previous = opt.scripted_touches[i - 1];
        if (opt.scripted_touches[i].at < previous.at + previous.duration)
            die("--touch-event ranges must not overlap");
    }
    std::stable_sort(
        opt.scripted_key_transitions.begin(), opt.scripted_key_transitions.end(),
        [](const ScriptedKeyTransition &a, const ScriptedKeyTransition &b) {
            if (a.at != b.at) return a.at < b.at;
            return a.pressed && !b.pressed;
        });
    return opt;
}

inline std::filesystem::path executable_directory(const char *argv0) {
    std::error_code error;
    const std::filesystem::path supplied = path_from_utf8(argv0);
    std::filesystem::path candidate = std::filesystem::absolute(supplied, error);
    if (error) candidate = supplied;
    candidate = std::filesystem::weakly_canonical(candidate, error);
    return (error ? supplied : candidate).parent_path();
}

inline void resolve_packaged_firmware(Options &opt, const char *argv0) {
    const std::filesystem::path executable = executable_directory(argv0);
    const std::array<std::filesystem::path, 5> roots{{
        std::filesystem::current_path(),
        executable,
        executable / "firmware",
        executable.parent_path() / "firmware",
        executable.parent_path() / "Resources" / "firmware",
    }};
    auto resolve = [&](std::filesystem::path &path) {
        if (path.is_absolute() || std::filesystem::exists(path)) return;
        for (const auto &root : roots) {
            const auto candidate = root / path.filename();
            if (std::filesystem::is_regular_file(candidate)) {
                path = candidate;
                return;
            }
        }
    };
    resolve(opt.rom);
    resolve(opt.spi);
    resolve(opt.nand);
}

} // namespace mobigo
