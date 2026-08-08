#pragma once

#include "cpu.hpp"

namespace mobigo {

inline void spi_boot(Bus &bus, Cpu &cpu) {
    bus.internal_rom_shadow_low = false;
    bus.internal_rom_fetch_mirror64 = false;
    static constexpr std::array<uint16_t, 5> tag{0x4750, 0x7370, 0x6973, 0x7069, 0x7370};
    for (size_t i = 0; i < tag.size(); ++i) {
        const uint16_t w = le16(bus.spi.bytes, i * 2);
        if (w != tag[i]) die("spi.bin does not start with documented GPspispisp SPI boot tag");
    }

    const uint16_t cs0 = le16(bus.spi.bytes, 10), cs1 = le16(bus.spi.bytes, 12), cs2 = le16(bus.spi.bytes, 14);
    const uint16_t cs3 = le16(bus.spi.bytes, 16), cs4 = le16(bus.spi.bytes, 18);
    const uint32_t dest = le16(bus.spi.bytes, 20) | (uint32_t(le16(bus.spi.bytes, 22) & 0x3f) << 16);
    const uint16_t sectors = le16(bus.spi.bytes, 24);
    const uint32_t words_to_copy = uint32_t(sectors) * 256;

    bus.write(0x7820, cs0); bus.write(0x7821, cs1); bus.write(0x7822, cs2);
    bus.write(0x7823, cs3); bus.write(0x7824, cs4);
    bus.write(0x7825, le16(bus.spi.bytes, 26)); bus.write(0x782d, le16(bus.spi.bytes, 28));
    bus.write(0x782f, le16(bus.spi.bytes, 32)); bus.write(0x783d, le16(bus.spi.bytes, 34));
    bus.write(0x783c, le16(bus.spi.bytes, 36)); bus.write(0x783b, le16(bus.spi.bytes, 38));
    bus.write(0x783e, le16(bus.spi.bytes, 40)); bus.write(0x783a, le16(bus.spi.bytes, 42));

    for (uint32_t i = 0; i < words_to_copy && (i * 2 + 1) < bus.spi.bytes.size(); ++i)
        bus.write(dest + i, le16(bus.spi.bytes, i * 2));

    const uint32_t start = (dest + 0x20) & kAddrMask;
    cpu.reset_core(start);

    if (g_log) {
        g_log << "SPI boot: dest=0x" << std::hex << dest
              << " sectors=0x" << sectors << " start=0x" << start
              << " cs0=" << cs0 << " cs1=" << cs1 << " cs2=" << cs2
              << " cs3=" << cs3 << " cs4=" << cs4 << std::dec << "\n";
    }
}

inline void rom_boot(Bus &bus, Cpu &cpu, uint32_t start_pc, bool start_pc_set) {
    if (bus.internal_rom.empty()) die("ROM boot requested but no internal ROM was loaded");

    const uint32_t start = start_pc_set ? start_pc : uint32_t(bus.read(0x00fff7));
    cpu.reset_core(start);

    if (g_log) {
        g_log << "ROM boot: rom_base=0x" << std::hex << bus.internal_rom_base
              << " words=0x" << bus.internal_rom.size()
              << " reset_vector=0x" << bus.read(0x00fff7)
              << " start=0x" << start
              << " start_override=" << (start_pc_set ? 1 : 0)
              << " shadow_low=" << (bus.internal_rom_shadow_low ? 1 : 0)
              << " fetch_mirror64=" << (bus.internal_rom_fetch_mirror64 ? 1 : 0) << std::dec << "\n";
    }
}

inline void power_key_wake_reset(Bus &bus, Cpu &cpu) {
    // This path is retained for experiments with the ROM's sleep branch.
    // Normal MobiGo 2 boot does not enter it when E-Fuse2 has its observed
    // required value (0x0300).
    bus.system_reset();
    const uint32_t start = bus.read(0x00fff7);
    cpu.reset_core(start);
    if (g_log) {
        g_log << "POWER automatic power-key wake reset_vector=0x" << std::hex << start
              << " reset_count=" << std::dec << bus.power_reset_count << "\n";
    }
}

} // namespace mobigo
