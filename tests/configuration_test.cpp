#include "mba_overlay.hpp"
#include "cpu.hpp"
#include "realtime_throttle.hpp"

#include <iostream>

using namespace mobigo;

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

Options options(std::initializer_list<const char *> arguments) {
    std::vector<std::string> storage{"mobigo2_emu"};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char *> argv;
    argv.reserve(storage.size());
    for (std::string &value : storage) argv.push_back(value.data());
    return parse_args(int(argv.size()), argv.data());
}

std::vector<uint8_t> mba_with_role(std::string_view role) {
    const bool system = role == "MGB_SYS";
    const bool g1 = role == "MGB_G1";
    std::vector<uint8_t> mba(system ? 0x174000 : g1 ? 0x214000 : 0x200, 0);
    static constexpr std::array<uint8_t, 8> magic{
        'b', 'M', '_', 'g', 'b', 'M', 'Q', 'a'
    };
    std::copy(magic.begin(), magic.end(), mba.begin());
    auto put32 = [&](size_t offset, uint32_t value) {
        mba[offset] = uint8_t(value);
        mba[offset + 1] = uint8_t(value >> 8);
        mba[offset + 2] = uint8_t(value >> 16);
        mba[offset + 3] = uint8_t(value >> 24);
    };
    put32(0x08, uint32_t(mba.size() / 2));
    put32(0x0c, system ? 0x5387a : g1 ? 0x3bc0b : 0);
    put32(0x10, system ? 0x0f3e60 : g1 ? 0x0f3e5c : 0);
    put32(0x14, g1 ? 0x0e1a55 : 0x0dfc1d);
    put32(0x18, 0x0c8800);
    std::copy(role.begin(), role.end(), mba.begin() + 0x80);
    return mba;
}

template <typename Function>
void require_throws(Function &&function, const char *message) {
    try {
        function();
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error(message);
}

void test_cli_surface() {
    const Options defaults = options({});
    require(defaults.realtime_cap, "accurate mode unexpectedly disabled pacing");
    require(defaults.speed_percent == 100, "default speed is not real-time");
    require(defaults.show_speed, "live speed display is not enabled by default");
    require(!defaults.auto_power_wake, "default settings automatically wake power-off");
    require(defaults.audio && defaults.vsync,
            "argument-free desktop launch did not enable audio and vsync");

    const Options custom_speed = options({"--speed-percent", "150", "--no-cap", "--hide-speed"});
    require(custom_speed.speed_percent == 150 && !custom_speed.realtime_cap &&
                !custom_speed.show_speed,
            "speed, pacing, or live-speed controls were not parsed");
    const Options wake = options({"--auto-power-wake"});
    require(wake.auto_power_wake, "explicit automatic power wake was ignored");

    for (const char *removed : {"--mode", "--mba", "--mba-target", "--mba-slot",
                                "--boot", "--usb", "--open-window-on-mba"}) {
        require_throws([&] { options({removed}); },
                       "removed launcher option was still accepted");
    }
    require_throws([] { options({"--speed-percent", "24"}); },
                   "unsafe speed percentage was accepted");

    const char *unicode_path = "games/café_日本.bin";
    const Options unicode = options({"--cart", unicode_path,
                                     "--log-file", unicode_path,
                                     "--dump-current-frame", unicode_path});
    require(path_to_utf8(unicode.cart) == unicode_path &&
                path_to_utf8(unicode.log_path) == unicode_path &&
                path_to_utf8(unicode.dump_current_frame) == unicode_path,
            "UTF-8 CLI paths did not round-trip through filesystem::path");
}

void test_automation_cli_contract() {
    // Preserve the regular firmware/cart automation surface while deliberately
    // excluding the retired MBA, USB, boot-path, and execution-mode options.
    const Options run = options({
        "--rom", "firmware/internalrom.bin",
        "--spi", "firmware/spi.bin",
        "--nand", "firmware/nand.us-stitched.bin",
        "--cart", "games/cartridge.bin",
        "--speed-percent", "125",
        "--no-cap",
        "--audio",
    });
    require(run.rom == "firmware/internalrom.bin", "starter --rom path changed");
    require(run.spi == "firmware/spi.bin", "starter --spi path changed");
    require(run.nand == "firmware/nand.us-stitched.bin", "starter --nand path changed");
    require(run.cart == "games/cartridge.bin", "cart path changed");
    require(run.speed_percent == 125 && !run.realtime_cap,
            "automation speed or pacing setting changed");
    require(run.audio && !run.vsync,
            "explicit starter audio changed unrelated CLI defaults");

    const Options regular = options({
        "--rom", "internalrom.bin", "--spi", "spi.bin",
        "--nand", "nand.edited.bin", "--open-window-at", "220000000",
    });
    require(regular.realtime_cap, "regular CLI launch was not paced");
    require(regular.open_window_at == 220000000,
            "deferred-window threshold changed");
    require(!regular.audio && !regular.vsync,
            "ordinary CLI invocation inherited desktop-only defaults");

    const Options uncapped = options({
        "--rom", "internalrom.bin", "--spi", "spi.bin",
        "--nand", "nand.edited.bin", "--open-window-at", "220000000",
        "--no-cap", "--max-present-hz", "30",
    });
    require(!uncapped.realtime_cap && uncapped.max_present_hz == 30,
            "automation pacing options changed");

    // Shared verification scripts use this deterministic headless surface.
    const Options verify = options({
        "--rom", "internalrom.bin", "--spi", "spi.bin", "--nand", "nand.bin",
        "--cart", "game.bin", "--no-cap",
        "--no-window", "--steps", "500000000",
        "--dump-frame", "final.bmp", "--dump-frame-dir", "frames",
        "--dump-frame-interval", "4300000",
        "--dump-memory", "state.bin", "--dump-memory-base", "0x5800",
        "--dump-memory-words", "0x20", "--log", "--log-file", "run.log",
        "--start-logging-at", "200000000",
        "--touch-event", "300000000,10000000,160,180",
        "--key-event", "245000000,5000000,volup",
    });
    require(!verify.window && verify.max_steps == 500000000,
            "starter headless bounded execution changed");
    require(verify.dump_frame == "final.bmp" && verify.dump_frame_dir == "frames" &&
                verify.dump_frame_interval == 4300000,
            "starter framebuffer evidence options changed");
    require(verify.dump_memory == "state.bin" &&
                verify.dump_memory_base == 0x5800 && verify.dump_memory_words == 0x20,
            "starter memory evidence options changed");
    require(verify.log && verify.log_path == "run.log" &&
                verify.start_logging_at == 200000000,
            "starter logging options changed");
    require(verify.scripted_touches.size() == 1 &&
                verify.scripted_touches[0].at == 300000000 &&
                verify.scripted_touches[0].duration == 10000000 &&
                verify.scripted_touches[0].x == 160 &&
                verify.scripted_touches[0].y == 180,
            "starter scripted touch option changed");
    require(verify.scripted_key_transitions.size() == 2 &&
                verify.scripted_key_transitions[0].at == 245000000 &&
                verify.scripted_key_transitions[0].pressed &&
                verify.scripted_key_transitions[1].at == 250000000 &&
                !verify.scripted_key_transitions[1].pressed,
            "starter scripted key option changed");
}

void test_matrix_map() {
    require(matrix_key_from_name("up") == std::optional<MatrixKey>{{3, 4}},
            "Up matrix coordinate is wrong");
    require(matrix_key_from_name("brightness") == std::optional<MatrixKey>{{4, 6}},
            "Brightness matrix coordinate is wrong");
    require(matrix_key_from_name("volup") == std::optional<MatrixKey>{{4, 8}},
            "Volume-up matrix coordinate is wrong");
    require(matrix_key_from_sdl(SDLK_F2) == std::optional<MatrixKey>{{3, 2}},
            "SDL power mapping differs from scripted mapping");
    require(matrix_key_from_sdl(SDLK_ESCAPE) == std::optional<MatrixKey>{{4, 2}},
            "SDL Exit mapping differs from scripted mapping");
    require(!matrix_key_from_sdl(SDLK_HOME),
            "dedicated motion key unexpectedly closes a matrix switch");
}

void test_mba_metadata_and_targets() {
    const MbaMetadata system = inspect_mba_metadata(mba_with_role("MGB_SYS"));
    require(system.detected_target == MbaTarget::System, "MGB_SYS was not detected");
    require(resolve_mba_target(system, MbaTarget::Auto) == MbaTarget::System,
            "automatic system target resolution failed");

    const MbaMetadata g1 = inspect_mba_metadata(mba_with_role("MGB_G1"));
    require(g1.detected_target == MbaTarget::G1, "MGB_G1 was not detected");
    require_throws([&] { resolve_mba_target(g1, MbaTarget::System); },
                   "conflicting generated role and explicit target were accepted");
    std::vector<uint8_t> inconsistent = mba_with_role("MGB_SYS");
    inconsistent[0x14] ^= 1;
    require_throws([&] { inspect_mba_metadata(inconsistent); },
                   "inconsistent generated role/profile metadata was accepted");

    const MbaMetadata unknown = inspect_mba_metadata(mba_with_role("CUSTOM"));
    require(!unknown.detected_target, "unknown MBA title was classified");
    require_throws([&] { resolve_mba_target(unknown, MbaTarget::Auto); },
                   "unknown automatic target silently fell back");
    require(resolve_mba_target(unknown, MbaTarget::Menu) == MbaTarget::Menu,
            "verified explicit target did not accept a nonstandard title");

    require(mba_target_matches_path(MbaTarget::System,
                                    "/BUNDLE/SY/135804SY.MBA"),
            "system suffix/path match failed");
    require(!mba_target_matches_path(MbaTarget::System,
                                     "/BUNDLE/G1/135804SY.MBA"),
            "system match escaped the SY directory");
    require(mba_target_matches_path(MbaTarget::G1,
                                    "/bundle/g1/135804g1.mba"),
            "case-insensitive G1 suffix/path match failed");
    require(mba_target_matches_path(MbaTarget::Menu, "/DEFAULT/MM.MBA"),
            "main-menu suffix match failed");
}

void test_realtime_rebase() {
    RealtimeThrottle throttle(false);
    throttle.advance_cycles(48000000, 48000000);
    require(throttle.emulated_nanoseconds == 0,
            "disabled throttle accounted uncapped boot time");
    throttle.set_enabled(true);
    require(throttle.emulated_nanoseconds == 0,
            "enabling throttle did not start a fresh epoch");
    throttle.advance_cycles(24000000, 48000000);
    require(throttle.emulated_nanoseconds == 500000000,
            "enabled throttle cycle conversion is wrong");
    throttle.set_enabled(false);
    require(throttle.emulated_nanoseconds == 0 &&
            throttle.fractional_numerator == 0,
            "disabling throttle did not clear the old epoch");
}

void test_fast_history_is_guest_equivalent() {
    Bus accurate_bus;
    Bus fast_bus;
    Cpu accurate(accurate_bus);
    Cpu fast(fast_bus);
    constexpr uint32_t start = kCsBase;
    accurate_bus.sdram[0] = 0x9640; // R3 = 0
    fast_bus.sdram[0] = 0x9640;
    accurate.reset_core(start);
    fast.reset_core(start);
    fast.track_recent_history = false;
    accurate.r[Cpu::R3] = fast.r[Cpu::R3] = 0xffff;
    accurate.step();
    fast.step();
    require(accurate.r == fast.r && accurate.get_fr() == fast.get_fr() &&
            accurate_bus.cycles == fast_bus.cycles &&
            accurate_bus.mmio == fast_bus.mmio,
            "disabling diagnostic history changed guest-visible execution");
    require(fast.recent_pos == 0 && accurate.recent_pos == 1,
            "fast diagnostic-history optimization was not exercised");
}

void dma_mba_header(Bus &bus, uint32_t entry) {
    constexpr uint32_t source = kCsBase + 0x100;
    constexpr uint32_t destination = kCsBase + 0x200;
    const std::array<uint16_t, 16> header{
        0x4d62, 0x675f, 0x4d62, 0x6151,
        0, 0, 0, 0, 0, 0,
        uint16_t(entry), uint16_t(entry >> 16),
        0x8800, 0x000c, 0, 0,
    };
    for (size_t i = 0; i < header.size(); ++i)
        bus.dma_write(source + uint32_t(i), header[i]);
    bus.write(0x7a81, uint16_t(source));
    bus.write(0x7a84, uint16_t(source >> 16));
    bus.write(0x7a82, uint16_t(destination));
    bus.write(0x7a85, uint16_t(destination >> 16));
    bus.write(0x7a83, uint16_t(header.size()));
    bus.write(0x7a86, 0);
    bus.write(0x7a80, 1);
}

void test_selected_mba_entry_is_pinned() {
    Bus selected;
    constexpr uint32_t expected = 0x0dfc1d;
    constexpr uint32_t unrelated = 0x0e1a55;
    selected.configure_mba_application_target(expected, true);
    dma_mba_header(selected, unrelated);
    require(selected.mba_application_entry_pinned,
            "selected MBA entry was not pinned");
    require(selected.mba_application_entry == expected,
            "an unrelated DMA-loaded MBA replaced the selected entry");
    selected.maybe_begin_mba_application(unrelated, 0x6ffd);
    require(selected.mba_launch_count == 0,
            "unrelated MBA triggered selected application launch state");
    selected.maybe_begin_mba_application(expected, 0x6ffd);
    require(selected.mba_launch_count == 1,
            "selected MBA entry did not trigger launch state");
    selected.maybe_arm_mba_watchdog_handoff(expected);
    require(selected.watchdog_enabled,
            "selected MBA did not exercise its synthetic handoff watchdog");

    dma_mba_header(selected, unrelated);
    require(!selected.mba_application_entry_pinned,
            "active selected MBA remained pinned across a follow-up launch");
    require(selected.mba_application_entry == unrelated,
            "follow-up MBA entry was not discovered after the selected app started");
    require(selected.mba_application_handoff_pending,
            "follow-up MBA lifecycle handoff was not armed");
    require(!selected.watchdog_enabled &&
                selected.mba_watchdog_entry == UINT32_MAX,
            "selected-overlay watchdog leaked into the follow-up MBA");

    Bus discovered;
    dma_mba_header(discovered, unrelated);
    require(discovered.mba_application_entry == unrelated,
            "normal NAND DMA discovery stopped working without a selected MBA");
}

} // namespace

int main() {
    try {
        test_cli_surface();
        test_automation_cli_contract();
        test_matrix_map();
        test_mba_metadata_and_targets();
        test_realtime_rebase();
        test_fast_history_is_guest_equivalent();
        test_selected_mba_entry_is_pinned();
        std::cout << "configuration tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "configuration test failed: " << error.what() << "\n";
        return 1;
    }
}
