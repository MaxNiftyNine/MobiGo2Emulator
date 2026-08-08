#include "cpu.hpp"

#include <iostream>
#include <stdexcept>

using namespace mobigo;

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static void test_mba_handoff_arms_watchdog_at_entry() {
    Bus bus;
    constexpr uint32_t entry = 0x0e1a55;
    bus.cycles = 1000;
    bus.configure_mba_watchdog_handoff(entry);

    bus.maybe_arm_mba_watchdog_handoff(entry - 1);
    require(!bus.watchdog_enabled, "watchdog armed before MBA entry");
    bus.maybe_arm_mba_watchdog_handoff(entry);
    require(bus.watchdog_enabled, "watchdog was not armed at MBA entry");
    require(bus.mmio[0x780a - kMmioBase] == 0x8000,
            "MBA handoff used the wrong watchdog profile");
    require(bus.watchdog_expire_cycles == 1000 + bus.system_clock_hz() * 2,
            "MBA watchdog deadline is wrong");
    require(!bus.mba_watchdog_handoff_pending, "MBA watchdog armed more than once");
}

static void test_program_can_feed_then_disable_handoff_watchdog() {
    Bus bus;
    bus.configure_mba_watchdog_handoff(0x0e1a55);
    bus.maybe_arm_mba_watchdog_handoff(0x0e1a55);
    const uint64_t first_deadline = bus.watchdog_expire_cycles;

    bus.cycles += 100;
    bus.write(0x780b, 0xa005);
    require(bus.watchdog_expire_cycles > first_deadline, "watchdog feed did not extend deadline");
    bus.write(0x780a, 0x0000);
    require(!bus.watchdog_enabled, "zero control value did not disable watchdog");
    require(bus.watchdog_expire_cycles == 0, "disabled watchdog retained a deadline");

    bus.cycles += bus.system_clock_hz();
    bus.update_periodic_events();
    require(!bus.system_reset_requested, "disabled watchdog reset the system");
}

static void test_fixed_mba_entry_stub_disables_inherited_watchdog() {
    Bus bus;
    Cpu cpu(bus);
    constexpr uint32_t entry = 0x0e1a55;
    const uint16_t stub[] = {
        0x670b, 0x5ffb,       // R3 = 0xa005
        0x990c, 0x780b,       // R4 = P_Watchdog_Clear
        0xd6c4,               // [R4] = R3
        0x9640,               // R3 = 0
        0x990c, 0x780a,       // R4 = P_Watchdog_Ctrl
        0xd6c4                // [R4] = R3
    };
    for (size_t i = 0; i < std::size(stub); ++i)
        bus.sdram[(entry - kCsBase + uint32_t(i)) & (kSdramWords - 1)] = stub[i];

    bus.configure_mba_watchdog_handoff(entry);
    cpu.reset_core(entry);
    // 0x670b and the two 0x990c loads consume their following immediates.
    for (size_t i = 0; i < 6; ++i) cpu.step();

    require(bus.mmio[0x780b - kMmioBase] == 0xa005,
            "fixed MBA entry stub did not service watchdog");
    require(bus.mmio[0x780a - kMmioBase] == 0,
            "fixed MBA entry stub did not clear watchdog control");
    require(!bus.watchdog_enabled,
            "fixed MBA entry stub left inherited watchdog enabled");
}

static void test_unserviced_handoff_watchdog_resets_system() {
    Bus bus;
    bus.configure_mba_watchdog_handoff(0x0e1a55);
    bus.maybe_arm_mba_watchdog_handoff(0x0e1a55);
    bus.cycles = bus.watchdog_expire_cycles;
    bus.update_periodic_events();
    require(bus.system_reset_requested, "expired watchdog did not request reset");
    require(!bus.system_reset_preserve_memory, "MBA watchdog requested a CPU-only reset");
    require((bus.mmio[0x7806 - kMmioBase] & 0x0010) != 0,
            "watchdog reset cause was not latched");
    require(!bus.poweroff_requested,
            "ordinary watchdog expiry was mistaken for a power-off request");
}

int main() {
    test_mba_handoff_arms_watchdog_at_entry();
    test_program_can_feed_then_disable_handoff_watchdog();
    test_fixed_mba_entry_stub_disables_inherited_watchdog();
    test_unserviced_handoff_watchdog_resets_system();
    std::cout << "watchdog tests passed\n";
    return 0;
}
