#include "realtime_throttle.hpp"
#include "video.hpp"

#include <iostream>
#include <stdexcept>

using namespace mobigo;

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static void test_realtime_throttle_uses_live_hardware_clock() {
    RealtimeThrottle throttle(true);
    throttle.advance_cycles(6000000, 12000000);
    require(throttle.emulated_nanoseconds == 500000000,
            "12 MHz real-time conversion is incorrect");
    throttle.advance_cycles(24000000, 48000000);
    require(throttle.emulated_nanoseconds == 1000000000,
            "48 MHz PLL real-time conversion is incorrect");
    throttle.advance_cycles(32768, 32768);
    require(throttle.emulated_nanoseconds == 2000000000,
            "32.768 kHz real-time conversion is incorrect");

    RealtimeThrottle fractional(true);
    fractional.advance_cycles(1, 3);
    fractional.advance_cycles(1, 3);
    fractional.advance_cycles(1, 3);
    require(fractional.emulated_nanoseconds == 1000000000,
            "fractional cycle time accumulated incorrectly");
}

static void test_touch_frontend_calibration_orientation() {
    const TouchAdcPoint top_left = screen_to_touch_adc(0, 0);
    const TouchAdcPoint bottom_right = screen_to_touch_adc(319, 239);
    const TouchAdcPoint upper = screen_to_touch_adc(160, 60);
    const TouchAdcPoint lower = screen_to_touch_adc(160, 180);

    require(top_left.x == 0x0e80 && top_left.y == 0x0d5c,
            "top-left touch calibration is incorrect");
    require(bottom_right.x == 0x0186 && bottom_right.y == 0x02b6,
            "bottom-right touch calibration is incorrect");
    require(upper.y > lower.y,
            "touchscreen Y electrode orientation is incorrect");
}

struct MotionI2cHost {
    MotionAccelerometer device;
    bool scl = true;
    bool sda = true;

    void lines(bool new_scl, bool new_sda) {
        scl = new_scl;
        sda = new_sda;
        device.observe(scl, sda);
    }

    void start() {
        lines(true, true);
        lines(true, false);
        lines(false, false);
    }

    void stop() {
        lines(false, false);
        lines(true, false);
        lines(true, true);
    }

    bool write_byte(uint8_t value) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool level = (value & (0x80 >> bit)) != 0;
            lines(false, level);
            lines(true, level);
            lines(false, level);
        }
        lines(false, true);
        lines(true, true);
        const bool acknowledged = device.sda_is_low();
        lines(false, true);
        return acknowledged;
    }

    uint8_t read_byte(bool acknowledge) {
        uint8_t value = 0;
        lines(false, true);
        for (unsigned bit = 0; bit < 8; ++bit) {
            lines(true, true);
            value = uint8_t((value << 1) | (device.sda_is_low() ? 0 : 1));
            lines(false, true);
        }
        lines(false, !acknowledge);
        lines(true, !acknowledge);
        lines(false, true);
        return value;
    }

    uint8_t read_register(uint8_t reg) {
        start();
        require(write_byte(uint8_t(MotionAccelerometer::kAddress << 1)),
                "accelerometer did not acknowledge its write address");
        require(write_byte(reg), "accelerometer did not acknowledge its register pointer");
        start();
        require(write_byte(uint8_t((MotionAccelerometer::kAddress << 1) | 1)),
                "accelerometer did not acknowledge its read address");
        const uint8_t value = read_byte(false);
        stop();
        return value;
    }

    template <size_t N>
    std::array<uint8_t, N> read_registers(uint8_t reg) {
        start();
        require(write_byte(uint8_t(MotionAccelerometer::kAddress << 1)),
                "accelerometer did not acknowledge its write address");
        require(write_byte(reg), "accelerometer did not acknowledge its register pointer");
        start();
        require(write_byte(uint8_t((MotionAccelerometer::kAddress << 1) | 1)),
                "accelerometer did not acknowledge its read address");
        std::array<uint8_t, N> values{};
        for (size_t i = 0; i < N; ++i) values[i] = read_byte(i + 1 < N);
        stop();
        return values;
    }
};

static void test_mobigo2_accelerometer_i2c_and_motion_axes() {
    MotionI2cHost host;
    require(host.read_register(0x00) == 0xf8,
            "BMA222E chip identification register is incorrect");
    require(host.read_registers<6>(0x02) ==
                std::array<uint8_t, 6>{0x00, 0x00, 0x00, 0x40, 0x00, 0x00},
            "neutral accelerometer gravity vector is incorrect");

    host.device.set_direction(1, true);
    require(host.read_register(0x03) == 0xc0 && host.read_register(0x05) == 0x00,
            "right-motion accelerometer vector is incorrect");
    host.device.set_direction(1, false);
    host.device.set_direction(0, true);
    require(host.read_register(0x03) == 0x40,
            "left-motion accelerometer vector is incorrect");
    host.device.set_direction(0, false);
    host.device.set_direction(2, true);
    require(host.read_register(0x07) == 0xc0,
            "up-motion accelerometer vector is incorrect");
    host.device.set_direction(2, false);
    host.device.set_direction(3, true);
    require(host.read_register(0x07) == 0x40,
            "down-motion accelerometer vector is incorrect");
}

static void test_gpio_d4_power_latch_falling_edge() {
    Bus bus;

    // A cold or partially configured low output is not a shutdown request.
    bus.write(0x7879, 0x0000);
    bus.write(0x787a, 0x0010);
    bus.write(0x787b, 0x0010);
    require(!bus.poweroff_requested,
            "cold GPIO-D4 low level was mistaken for a shutdown edge");

    // Resident firmware first holds board power high, then request_poweroff()
    // releases the same active-high output through the Buffer register.
    bus.write(0x7879, 0x0010);
    require(bus.power_latch_seen_high,
            "GPIO-D4 high power-hold state was not armed");
    bus.write(0x7879, 0x0000);
    require(bus.poweroff_requested,
            "GPIO-D4 power-hold falling edge did not request power-off");

    bus.system_reset();
    require(!bus.power_latch_seen_high && !bus.poweroff_requested,
            "system reset retained the GPIO power-off latch state");

    // Retail cartridges initialize GPIO-D as a complete port. Their ordinary
    // all-pins Buffer clear must not be confused with the resident's dedicated
    // D4 terminal sequence, even if firmware held D4 high beforehand.
    bus.write(0x787b, 0x0010);
    bus.write(0x787a, 0x0010);
    bus.write(0x7879, 0x0010);
    bus.write(0x787b, 0xffff);
    bus.write(0x787a, 0xffff);
    bus.write(0x7879, 0x0000);
    bus.write(0x7878, 0x0000);
    require(!bus.poweroff_requested,
            "whole-port GPIO-D initialization was mistaken for power-off");
}

static void test_bus_backing_stops_at_external_chip_select() {
    Bus bus;
    require(bus.mem.size() == kCsBase,
            "on-chip RAM backing still allocates the full external address space");

    bus.write(kCsBase - 1, 0x1234);
    bus.write(kCsBase, 0xabcd);
    require(bus.read(kCsBase - 1) == 0x1234,
            "last on-chip RAM word did not round-trip");
    require(bus.read(kCsBase) == 0xabcd && bus.sdram[0] == 0xabcd,
            "first external word did not route to SDRAM");
}

static void test_unlogged_bus_router_matches_diagnostic_routing() {
    const auto exercise_router = []() {
        Bus bus;
        bus.internal_rom_base = 0x008000;
        bus.internal_rom = {0xabcd};
        bus.cart_mem = {0x6000, 0x6001, 0x6002, 0x6003};

        bus.write(0x001234, 0x1111);
        bus.write(0x007c00, 0x2222);
        bus.write(0x008000, 0xffff); // Internal ROM stores are ignored.
        bus.write(0x030100, 0x3333);
        const uint16_t cart_before = bus.read(0x050000);
        bus.write(0x050000, 0xffff); // Cartridge stores are ignored.
        bus.write(0x007824, 0x4444);
        bus.write(0x200100, 0x5555);

        return std::array<uint32_t, 9>{
            bus.read(0x001234),
            bus.read(0x007c00),
            bus.spu_channel_start_sequence[0],
            bus.read(0x008000),
            bus.read(0x030100),
            cart_before,
            bus.read(0x050000),
            bus.read(0x007824),
            bus.read(0x200100),
        };
    };

    if (g_log.is_open()) g_log.close();
    g_log.clear();
    const auto unlogged = exercise_router();

    const auto log_path = std::filesystem::temp_directory_path() /
        ("mobigo2-bus-router-" +
         std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch().count()) +
         ".log");
    g_log.open(log_path, std::ios::out | std::ios::trunc);
    require(g_log.is_open(), "failed to create temporary bus diagnostic log");
    const auto logged = exercise_router();
    g_log.close();
    g_log.clear();
    std::error_code remove_error;
    std::filesystem::remove(log_path, remove_error);

    require(unlogged == logged,
            "unlogged bus fast path diverged from diagnostic routing");
}

static void test_cart_aperture_cache_tracks_mcs_and_rom_size() {
    Bus bus;
    bus.cart_mem = {0x1000, 0x1001, 0x1002, 0x1003};

    // With the reset one-page MCS0..2 sizes, CS3 begins at word 0x50000.
    require(bus.read(0x050000) == 0x1000 && bus.read(0x050006) == 0x1002,
            "power-of-two cartridge mirroring is incorrect");

    // Enlarging MCS0 moves CS3 by one 64K-word page. Guest MMIO writes must
    // invalidate the cached aperture immediately.
    bus.write(0x7820, 0x0100);
    bus.write(0x050000, 0x55aa);
    require(bus.read(0x050000) == 0x55aa,
            "old cartridge aperture remained active after MCS reprogramming");
    require(bus.read(0x060000) == 0x1000,
            "cartridge aperture did not follow MCS reprogramming");

    // Cartridge loaders/tests may replace the backing vector directly. Size
    // changes refresh the mask and retain modulo mirroring for odd-sized NOR.
    bus.cart_mem = {0x2000, 0x2001, 0x2002};
    require(bus.read(0x060004) == 0x2001,
            "non-power-of-two cartridge mirroring is incorrect");
}

static void test_mba_entry_return_is_an_application_exit() {
    Bus bus;
    Cpu cpu(bus);
    constexpr uint32_t entry = 0x0e0100;
    bus.sdram[entry - kCsBase] = 0x9a90; // RETF: pop SR, PC from SP

    cpu.reset_core(0x020000);
    cpu.push(0x1234, cpu.r[Cpu::SP]);
    cpu.push(0x0002, cpu.r[Cpu::SP]);
    cpu.r[Cpu::SR] = uint16_t(entry >> 16);
    cpu.r[Cpu::PC] = uint16_t(entry);
    bus.configure_mba_watchdog_handoff(entry);

    cpu.step();

    require(bus.mba_launch_count == 1, "MBA entry was not recorded as an application launch");
    require(bus.mba_return_count == 1, "top-level MBA RETF was not recorded as an application exit");
    require(!bus.mba_application_active, "returned MBA remained marked active");
    require(cpu.lpc() == 0x021234, "MBA RETF did not return to its LD caller");
}

static void test_mba_scanout_requires_inherited_interrupt_service() {
    Bus bus;
    Cpu cpu(bus);
    Video video;
    constexpr uint32_t entry = 0x0e0100;
    constexpr uint32_t framebuffer = 0x3fd400;

    video.pixels.assign(Video::W * Video::H, 0xffffffffu);
    bus.mmio[0x7078 - kMmioBase] = uint16_t(framebuffer);
    bus.mmio[0x7079 - kMmioBase] = uint16_t(framebuffer >> 16);
    bus.mmio[0x707f - kMmioBase] = 0x0088;
    for (uint32_t i = 0; i < Video::W * Video::H; ++i)
        bus.dma_write(framebuffer + i, 0xf800);

    bus.configure_mba_watchdog_handoff(entry);
    bus.maybe_begin_mba_application(entry, 0x6ffd);
    bus.maybe_arm_mba_watchdog_handoff(entry, 0x6ffd);
    cpu.enable_irq = 0;
    cpu.enable_fiq = 0;
    video.compose(bus, cpu, false);
    require(video.pixels[0] == 0xffffffffu,
            "interrupt-disabled MBA exposed an unlatched framebuffer");

    cpu.enable_irq = 1;
    video.compose(bus, cpu, false);
    require(video.pixels[0] == Video::rgb565_to_argb(0xf800),
            "interrupt-serviced MBA did not latch its framebuffer");

    // The same inherited-display guard applies when an MBA programs direct
    // page/sprite mode. Rendering before checking the guard would silently
    // replace the latched resident frame even though compose() returned early.
    video.pixels.assign(Video::W * Video::H, 0xff123456u);
    bus.mmio[0x707f - kMmioBase] = 0x0000;
    bus.mmio[0x7042 - kMmioBase] = 0x0003;
    bus.sprite_ram[0] = 1;
    bus.palette_ram[0] = 0x7fff;
    cpu.enable_irq = 0;
    video.compose(bus, cpu, false);
    require(video.pixels[0] == 0xff123456u,
            "interrupt-disabled MBA exposed direct PPU output");

    cpu.enable_irq = 1;
    video.compose(bus, cpu, false);
    require(video.pixels[0] == Video::rgb555_to_argb(0x7fff),
            "interrupt-serviced MBA did not expose direct PPU output");
}

static void test_ppu_bit_zero_is_not_a_global_enable() {
    Bus bus;
    Video video;
    bus.mmio[0x707f - kMmioBase] = 0x0000;
    bus.mmio[0x7042 - kMmioBase] = 0x0001;
    require(video.render_ppu(bus),
            "P_PPU_Enable bit zero incorrectly disabled the GPL16250VA PPU");
}

static void test_direct_ppu_ignores_stale_framebuffer_pointers() {
    Bus bus;
    Cpu cpu(bus);
    Video video;
    constexpr uint32_t inherited_framebuffer = 0x3fd400;
    constexpr uint32_t stale_output = 0x41d400;

    // The resident leaves its frame pointers and last completed frame behind
    // when a cartridge switches P_PPU_Enable into direct (non-frame-base)
    // mode. Those stale pointers must not suppress the live page/sprite PPU.
    bus.mmio[0x7078 - kMmioBase] = uint16_t(inherited_framebuffer);
    bus.mmio[0x7079 - kMmioBase] = uint16_t(inherited_framebuffer >> 16);
    bus.mmio[0x707a - kMmioBase] = uint16_t(stale_output);
    bus.mmio[0x707b - kMmioBase] = uint16_t(stale_output >> 16);
    bus.mmio[0x707f - kMmioBase] = 0x0000; // bit 7 clear: direct PPU mode
    bus.last_framebuffer_base = inherited_framebuffer;
    bus.last_framebuffer_valid = true;
    bus.dma_write(inherited_framebuffer, 0xf800);

    bus.mmio[0x7042 - kMmioBase] = 0x0003; // sprites enabled, screen coordinates
    bus.sprite_ram[0] = 1;
    bus.sprite_ram[1] = 0;
    bus.sprite_ram[2] = 0;
    bus.sprite_ram[3] = 0;
    bus.palette_ram[0] = 0x7fff;

    video.compose(bus, cpu);
    require(video.pixels[0] == Video::rgb555_to_argb(0x7fff),
            "stale frame pointers suppressed direct PPU scanout");
}

static void test_rtc_hms_counters_advance() {
    Bus bus;
    bus.cycles = 1;
    bus.write(0x7920, 59);
    bus.write(0x7921, 59);
    bus.write(0x7922, 23);
    bus.write(0x7934, 0x8000);
    bus.update_periodic_events();

    bus.cycles += bus.system_clock_hz();
    bus.update_periodic_events();

    require(bus.read(0x7920) == 0, "RTC second did not wrap at 60");
    require(bus.read(0x7921) == 0, "RTC minute did not wrap at 60");
    require(bus.read(0x7922) == 0, "RTC hour did not wrap at 24");
    require((bus.read(0x7935) & 0x000e) == 0x000e,
            "RTC second/minute/hour status flags were not latched");
}

static void test_video_edges_are_cycle_exact() {
    Bus bus;
    bus.mmio[0x7050 - kMmioBase] = 0;
    bus.mmio[0x7051 - kMmioBase] = 3; // Four total lines.
    bus.mmio[0x7054 - kMmioBase] = 2; // Event at line two.
    bus.mmio[0x7055 - kMmioBase] = 16;
    bus.next_video_edge_cycles = 0;
    bus.cycles = 0;
    bus.update_periodic_events();

    bus.cycles = 31;
    bus.update_periodic_events();
    require((bus.mmio[0x7063 - kMmioBase] & 0x0801) == 0,
            "video edge fired before the programmed line");
    bus.cycles = 32;
    bus.update_periodic_events();
    require((bus.mmio[0x7063 - kMmioBase] & 0x0801) == 0x0801,
            "video edge did not fire on the programmed line");

    bus.write(0x7063, 0x0801);
    bus.cycles = 33;
    bus.update_periodic_events();
    require((bus.mmio[0x7063 - kMmioBase] & 0x0801) == 0,
            "acknowledged video edge relatched within the same frame");
    bus.cycles = 64;
    bus.update_periodic_events();
    require((bus.mmio[0x7063 - kMmioBase] & 0x0801) == 0,
            "frame wrap did not leave the video pulse clear");
}

static void test_timer_deadline_and_lazy_counter_sync() {
    Bus bus;
    bus.cycles = 0;
    bus.write(0x78c2, 0xfffe); // Timer A overflows after two source ticks.
    bus.write(0x78c0, 0x2000); // Enable, SYSCLK/2 source.

    require(bus.next_periodic_event_cycles == 0,
            "timer write did not invalidate the event horizon");
    bus.cycles = 1;
    bus.update_periodic_events(false);
    require(bus.next_periodic_event_cycles == 4,
            "timer overflow deadline was not scheduled exactly");
    require(bus.mmio[0x78c4 - kMmioBase] == 0xfffe,
            "timer advanced before its first source tick");

    bus.cycles = 3;
    bus.update_periodic_events(false);
    require(bus.mmio[0x78c4 - kMmioBase] == 0xfffe,
            "fast scheduler path eagerly synchronized a non-event timer");
    require(bus.read(0x78c4) == 0xffff,
            "timer MMIO read did not lazily synchronize the visible counter");
    require((bus.read(0x78c0) & 0x8000) == 0,
            "timer overflowed before its exact deadline");

    bus.cycles = 4;
    bus.update_periodic_events(false);
    require((bus.read(0x78c0) & 0x8000) != 0,
            "timer did not overflow on its exact deadline");
    require(bus.read(0x78c4) == 0xfffe,
            "timer did not reload its programmed preload on overflow");
}

static void test_cpu_dispatches_periodic_events_only_at_the_deadline() {
    Bus bus;
    Cpu cpu(bus);
    bus.mem[0] = 0xf165; // NOP
    bus.mem[1] = 0xf165;
    cpu.reset_core(0);

    bus.next_periodic_event_cycles = 100;
    cpu.step();
    require(bus.last_periodic_update_cycles == UINT64_MAX,
            "CPU synchronized periodic devices before their event deadline");

    bus.adc_manual_pending = true;
    bus.adc_manual_channel = 0;
    bus.adc_manual_due_cycles = bus.cycles + 1;
    bus.next_periodic_event_cycles = bus.adc_manual_due_cycles;
    cpu.step();
    require(!bus.adc_manual_pending,
            "CPU did not dispatch a periodic device at its exact deadline");
    require(bus.last_periodic_update_cycles == bus.cycles,
            "CPU periodic dispatch used the wrong instruction boundary");
    require((bus.mmio[0x7961 - kMmioBase] & 0x8080) == 0x8080,
            "CPU deadline dispatch did not latch ADC completion");
}

static void test_usb_suspend_deadline_from_cycle_zero() {
    Bus bus;
    bus.cycles = 0;
    bus.write(0x7a30, 1);
    bus.cycles = 1;
    bus.update_periodic_events(false);
    require(bus.next_periodic_event_cycles == 4096,
            "USB suspend deadline was not measured from cycle zero");
    bus.cycles = 4095;
    bus.update_periodic_events(false);
    require((bus.mmio[0x7a3a - kMmioBase] & 0x0020) == 0,
            "USB suspend latched before its exact deadline");
    bus.cycles = 4096;
    bus.update_periodic_events(false);
    require((bus.mmio[0x7a3a - kMmioBase] & 0x0020) != 0,
            "USB suspend did not latch at its exact deadline");
}

static void test_official_fixed_source_dma_completion_path() {
    Bus bus;
    constexpr uint32_t source = 0x002000;
    constexpr uint32_t target = 0x002100;
    bus.write(source, 0x07e0);
    bus.write(0x7a81, uint16_t(source));
    bus.write(0x7a84, uint16_t(source >> 16));
    bus.write(0x7a82, uint16_t(target));
    bus.write(0x7a85, uint16_t(target >> 16));
    bus.write(0x7a83, 4);
    bus.write(0x7a86, 0);
    bus.write(0x7a80, 0x0089); // fixed source + normal completion + enable

    for (uint32_t i = 0; i < 4; ++i)
        require(bus.read(target + i) == 0x07e0, "fixed-source DMA fill produced bad data");
    require((bus.read(0x7abf) & 0x0001) != 0, "DMA completion flag was not set");
    require((bus.read(0x7a80) & 0x0003) == 0, "DMA enable/busy bits did not clear");
    bus.write(0x7abf, 0x0001);
    require((bus.read(0x7abf) & 0x0001) == 0, "DMA completion W1C failed");
}

static void test_dma_loaded_mba_header_registers_entry() {
    Bus bus;
    constexpr uint32_t source = 0x002400;
    constexpr uint32_t target = 0x0c8000;
    constexpr uint32_t entry = 0x0e1b60;
    bus.write(source + 0, 0x4d62);
    bus.write(source + 1, 0x675f);
    bus.write(source + 2, 0x4d62);
    bus.write(source + 3, 0x6151);
    bus.write(source + 0x0a, uint16_t(entry));
    bus.write(source + 0x0b, uint16_t(entry >> 16));
    bus.write(source + 0x0c, 0x1800);
    bus.write(source + 0x0d, 0x000e);
    bus.write(0x7a81, uint16_t(source));
    bus.write(0x7a84, uint16_t(source >> 16));
    bus.write(0x7a82, uint16_t(target));
    bus.write(0x7a85, uint16_t(target >> 16));
    bus.write(0x7a83, 0x10);
    bus.write(0x7a86, 0);
    bus.write(0x7a80, 0x0009);

    require(bus.mba_application_entry == entry,
            "DMA-loaded MBA header did not register its application entry");
    require(bus.mba_application_handoff_pending,
            "DMA-loaded MBA entry was not armed for lifecycle tracking");
    require(!bus.mba_watchdog_handoff_pending,
            "ordinary retail MBA incorrectly received replacement watchdog injection");
}

static void test_ppu_blend_levels_and_direct_color_transparency() {
    Bus bus;
    Video video;
    const uint16_t black = 0x0000;
    const uint16_t white = 0x7fff;
    require(Video::blend_rgb555(black, white, 8) == 0x1ce7,
            "25 percent PPU blend level is wrong");
    require(Video::blend_rgb555(black, white, 16) == 0x3def,
            "50 percent PPU blend level is wrong");
    require(Video::blend_rgb555(black, white, 24) == 0x5ef7,
            "75 percent PPU blend level is wrong");
    require(Video::blend_rgb555(black, white, 32) == white,
            "100 percent PPU blend level is wrong");

    std::array<uint16_t, Video::W> line{};
    line.fill(0x1234);
    bus.mem[0x1000] = 0x8000; // RGB1555 transparency bit.
    video.draw_direct_bitmap_strip(bus, line, 0x1000, 0, 0, 1, 1, false, false);
    require(line[0] == 0x1234, "transparent direct-color pixel overwrote the lower layer");
    bus.mem[0x1000] = 0x7c00;
    video.draw_direct_bitmap_strip(bus, line, 0x1000, 0, 0, 1, 1, false, false);
    require(line[0] == 0x7c00, "opaque direct-color pixel was not rendered");
}

static void test_ppu_fade_lookup() {
    Bus bus;
    Video video;
    bus.mmio[0x703c - kMmioBase] = 0x0020;
    bus.mmio[0x7030 - kMmioBase] = 0;
    video.update_effect_lut(bus);
    require(video.rgb555_effect_lut[0x7fff] == 0xffffffff,
            "neutral PPU color lookup changed white");
    bus.mmio[0x7030 - kMmioBase] = 1;
    video.update_effect_lut(bus);
    require(video.rgb555_effect_lut[0x7fff] == 0xfffefefe,
            "PPU fade register did not subtract one RGB level");
}

static void test_centered_sprite_coordinates_clip_without_wrapping() {
    Bus bus;
    Video video;

    bus.mmio[0x7042 - kMmioBase] = 0x0015; // enabled, centered, direct tile addressing
    bus.sprite_ram[0] = 1;
    bus.sprite_ram[1] = 0;
    bus.sprite_ram[3] = 0; // 8x8, 2-bpp, priority zero
    bus.palette_ram[0] = 0x7fff;

    // Centered Y=128 places an 8-pixel sprite at screen Y=-4. Real hardware
    // clips its upper half and keeps the lower half visible at the top edge.
    bus.sprite_ram[2] = 128;
    require(video.render_ppu(bus), "centered sprite PPU pass did not run");
    require(video.pixels[0 * Video::W + 156] == Video::rgb555_to_argb(0x7fff),
            "centered sprite did not clip against the top edge");
    require(video.pixels[4 * Video::W + 156] == Video::rgb555_to_argb(0x0000),
            "top-clipped centered sprite extended below its real bounds");

    // The coordinate fields are signed ten-bit values. SY encodes negative
    // centered coordinates this way for ordinary lower-screen objects.
    bus.sprite_ram[2] = 0x039a; // -102 -> screen Y=226 for an 8x8 sprite
    require(video.render_ppu(bus), "signed centered sprite PPU pass did not run");
    require(video.pixels[226 * Video::W + 156] == Video::rgb555_to_argb(0x7fff),
            "ten-bit signed centered Y coordinate was decoded incorrectly");

    // SY moves Family-B objects upward by increasing the centered Y value.
    // Once far above the LCD, the signed result is clipped. Treating it as a
    // 9-bit ring makes the object incorrectly reappear at the bottom.
    bus.sprite_ram[2] = 400;
    require(video.render_ppu(bus), "off-screen centered sprite PPU pass did not run");
    require(video.pixels[239 * Video::W + 156] == Video::rgb555_to_argb(0x0000),
            "off-screen centered sprite wrapped into the LCD bottom row");
}

int main() {
    test_realtime_throttle_uses_live_hardware_clock();
    test_touch_frontend_calibration_orientation();
    test_mobigo2_accelerometer_i2c_and_motion_axes();
    test_gpio_d4_power_latch_falling_edge();
    test_bus_backing_stops_at_external_chip_select();
    test_unlogged_bus_router_matches_diagnostic_routing();
    test_cart_aperture_cache_tracks_mcs_and_rom_size();
    test_mba_entry_return_is_an_application_exit();
    test_mba_scanout_requires_inherited_interrupt_service();
    test_ppu_bit_zero_is_not_a_global_enable();
    test_direct_ppu_ignores_stale_framebuffer_pointers();
    test_rtc_hms_counters_advance();
    test_video_edges_are_cycle_exact();
    test_timer_deadline_and_lazy_counter_sync();
    test_cpu_dispatches_periodic_events_only_at_the_deadline();
    test_usb_suspend_deadline_from_cycle_zero();
    test_official_fixed_source_dma_completion_path();
    test_dma_loaded_mba_header_registers_entry();
    test_ppu_blend_levels_and_direct_color_transparency();
    test_ppu_fade_lookup();
    test_centered_sprite_coordinates_clip_without_wrapping();
    std::cout << "hardware accuracy tests passed\n";
    return 0;
}
