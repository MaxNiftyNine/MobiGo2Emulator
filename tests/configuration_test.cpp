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

void test_modes_and_cli() {
    const Options defaults = options({});
    require(defaults.mode == EmulatorMode::Accurate, "default mode is not accurate");
    require(defaults.realtime_cap, "accurate mode unexpectedly disabled pacing");
    require(!defaults.auto_power_wake, "accurate mode automatically wakes power-off");

    const Options fast = options({"--mode", "fast"});
    require(fast.mode == EmulatorMode::Fast, "fast mode was not parsed");
    require(!fast.realtime_cap, "fast mode did not disable host pacing");
    const Options capped_fast = options({"--mode", "fast", "--cap"});
    require(capped_fast.realtime_cap, "explicit --cap did not override fast mode");
    const Options wake = options({"--auto-power-wake"});
    require(wake.auto_power_wake, "explicit automatic power wake was ignored");

    require(options({"--mba", "test.MBA", "--mba-target", "SY"}).mba_target ==
                MbaTarget::System,
            "SY target alias failed");
    require(options({"--mba", "test.MBA", "--mba-slot", "G1"}).mba_target ==
                MbaTarget::G1,
            "G1 slot alias failed");
    require(options({"--mba", "test.MBA", "--mba-target", "MM"}).mba_target ==
                MbaTarget::Menu,
            "MM target alias failed");
    require_throws([] { options({"--open-window-on-mba"}); },
                   "MBA-triggered window did not require an MBA");
    require_throws([] {
        options({"--mba", "test.MBA", "--open-window-on-mba",
                 "--open-window-at", "1"});
    }, "conflicting deferred-window triggers were accepted");
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
        test_modes_and_cli();
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
