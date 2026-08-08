#pragma once

#include "accelerometer.hpp"
#include "devices.hpp"

namespace mobigo {

struct Bus {
    std::vector<uint16_t> mem = std::vector<uint16_t>(kWordCount, 0);
    // The cartridge connector is wired to the board's CS3 NOR window.  Keep
    // it separate from SDRAM: both are externally addressed devices, but
    // writes used to load runtime modules into SDRAM must never alter a cart.
    std::vector<uint16_t> cart_mem;
    std::vector<uint16_t> sdram = std::vector<uint16_t>(kSdramWords, 0xffff);
    std::array<uint16_t, 0x1000> mmio{};
    std::array<uint16_t, 0x100> rowscroll_ram{};
    std::array<uint16_t, 0x100> rowzoom_ram{};
    std::array<uint16_t, 0x200> tx3_transform_ram{};
    std::array<uint16_t, 0x1000> palette_ram{};
    std::array<uint16_t, 0x800> sprite_ram{};
    std::array<uint16_t, 0x400> sound_ram{};
    std::array<uint32_t, 32> spu_channel_start_sequence{};
    std::vector<uint16_t> internal_rom;
    uint32_t internal_rom_base = 0x008000;
    bool internal_rom_shadow_low = true;
    bool internal_rom_fetch_mirror64 = true;
    SpiNorDevice spi;
    NandDevice nand;
    MotionAccelerometer accelerometer;
    uint64_t cycles = 0;
    uint64_t clock_change_generation = 0;
    uint32_t pc_for_log = 0;
    std::unordered_set<uint32_t> logged_unknown_reads;
    std::unordered_set<uint32_t> logged_unknown_writes;
    uint32_t dma_write_log_count = 0;
    uint32_t cs_oob_log_count = 0;
    uint32_t cart_probe_log_count = 0;
    uint32_t rom_write_log_count = 0;
    uint32_t irq_log_count = 0;
    uint32_t stack_watch_log_count = 0;
    uint32_t handoff_watch_log_count = 0;
    uint32_t app_wait_watch_log_count = 0;
    uint32_t gpio_read_log_count = 0;
    uint32_t gpiob_aux_read_log_count = 0;
    uint32_t timer_status_read_log_count = 0;
    uint32_t late_irq_log_count = 0;
    uint32_t focused_late_irq_log_count = 0;
    uint32_t ppu_write_log_count = 0;
    uint32_t ppu_ram_write_log_count = 0;
    uint32_t video_dma_touch_log_count = 0;
    uint32_t video_status_read_log_count = 0;
    uint32_t tft_status_read_log_count = 0;
    uint32_t late_video_status_log_count = 0;
    uint32_t late_video_edge_log_count = 0;
    uint32_t post_ppu_video_log_count = 0;
    uint32_t post_ppu_irq_log_count = 0;
    uint32_t fb_ppu_go_log_count = 0;
    uint32_t app_wait_read_log_count = 0;
    uint32_t late_app_wait_log_count = 0;
    uint32_t late_app_wait_write_log_count = 0;
    uint32_t event_state_read_log_count = 0;
    uint32_t event_state_write_log_count = 0;
    uint32_t late_event_detail_read_log_count = 0;
    uint32_t late_event_detail_write_log_count = 0;
    uint32_t event_ram_watch_log_count = 0;
    uint32_t int_status1_read_log_count = 0;
    uint32_t dma_irq_log_count = 0;
    uint32_t int_control_log_count = 0;
    uint32_t rtc_log_count = 0;
    uint32_t late_timer_ack_log_count = 0;
    uint32_t late_timer_overflow_log_count = 0;
    uint32_t foreground_local_log_count = 0;
    uint32_t a6fa_bus_log_count = 0;
    uint32_t last_framebuffer_base = 0;
    uint32_t last_ppu_framebuffer_base = 0;
    uint8_t audio_fifo_level_a = 0;
    uint8_t audio_fifo_level_b = 0;
    std::deque<uint16_t> audio_fifo_a;
    std::deque<uint16_t> audio_fifo_b;
    bool audio_shared_fifo_next_b = false;
    bool last_framebuffer_valid = false;
    bool ppu_go_pending = false;
    uint64_t ppu_go_due_cycles = 0;
    bool ppu_framebuffer_valid = false;
    uint32_t last_video_scanline = UINT32_MAX;
    uint64_t next_video_edge_cycles = 0;
    uint64_t last_periodic_update_cycles = UINT64_MAX;
    uint64_t next_periodic_event_cycles = 0;
    uint64_t last_timer_cycles = UINT64_MAX;
    // Fractional source ticks are kept as a numerator whose denominator is
    // periodic_clock_hz. This preserves the non-integral 32.768 kHz-derived
    // periods at the normal 48 MHz system clock.
    std::array<uint64_t, 4> timer_phase_accum{};
    bool timer_any_enabled = false;
    uint64_t last_timebase_cycles = UINT64_MAX;
    std::array<uint64_t, 3> timebase_phase_accum{};
    uint64_t last_scheduler_cycles = UINT64_MAX;
    uint64_t scheduler_phase_accum = 0;
    uint64_t last_rtc_cycles = UINT64_MAX;
    uint64_t rtc_phase_accum = 0;
    uint64_t last_spu_beat_cycles = UINT64_MAX;
    uint64_t spu_beat_phase_accum = 0;
    uint64_t spu_beat_elapsed_ticks = 0;
    uint64_t periodic_clock_hz = 0;
    uint64_t usb_device_enabled_cycle = UINT64_MAX;
    bool usb_suspend_latched = false;
    bool usb_host_available = false;
    bool usb_host_connected = false;
    bool usb_bus_reset_pending = false;
    bool usb_enumerated = false;
    std::deque<uint8_t> usb_ep0_fifo;
    std::deque<uint8_t> usb_bulk_out_fifo;
    std::vector<uint8_t> usb_bulk_in_fifo;
    std::vector<uint8_t> usb_interrupt_in_fifo;
    uint32_t usb_dma_programmed_bytes = 0;
    uint32_t usb_dma_transferred_bytes = 0;
    bool sleep_requested = false;
    // The MobiGo 2 board keeps its external power latch asserted through
    // GPIO-D4. Resident request_poweroff() leaves D4 configured as a normal
    // output and drops its buffer bit from high to low.
    bool power_latch_seen_high = false;
    bool poweroff_requested = false;
    bool system_reset_requested = false;
    bool system_reset_preserve_memory = false;
    bool watchdog_enabled = false;
    bool watchdog_reset_cpu_only = false;
    uint64_t watchdog_expire_cycles = 0;
    uint32_t mba_watchdog_entry = UINT32_MAX;
    bool mba_watchdog_handoff_pending = false;
    uint32_t mba_application_entry = UINT32_MAX;
    bool mba_application_entry_pinned = false;
    bool mba_application_handoff_pending = false;
    bool mba_application_active = false;
    bool mba_entry_stack_valid = false;
    uint32_t mba_entry_stack_address = 0;
    uint32_t mba_launch_count = 0;
    uint32_t mba_return_count = 0;
    uint32_t power_reset_count = 0;
    uint16_t gpio_a_input = 0x7fff;
    uint16_t gpio_b_input = 0xfffe;
    uint16_t gpio_c_input = 0xfeff;
    uint16_t gpio_d_input = 0xffff;
    uint16_t gpio_e_input = 0x0000;
    // Physical MobiGo keyboard/button matrix. Firmware selects one of six
    // output lines (IOC[10,9,7,6,5], then IOE[2]) and samples nine
    // pull-low column lines on IOA[13:11]/IOB[15:10]. A closed key connects
    // the selected high row to its column.
    std::array<uint16_t, 6> matrix_pressed{};
    // MobiGo board four-wire resistive touch panel. Firmware drives the panel
    // through IOE[8,10,14,15], samples contact on IOE8, and converts the two
    // coordinates through manual ADC channels 3 and 2 respectively.
    bool touch_pressed = false;
    uint16_t touch_adc_x = 0;
    uint16_t touch_adc_y = 0;
    // Board ADC channel 0 is the battery/power monitor. The retail driver
    // treats 0x3ff..0x450 as its normal operating band and powers off after
    // repeated samples <=0x3ae.
    uint16_t battery_adc = 0x0500;
    bool adc_manual_pending = false;
    uint64_t adc_manual_due_cycles = 0;
    uint8_t adc_manual_channel = 0;
    const uint64_t *cpu_insns = nullptr;

    void set_usb_host_available(bool available) {
        usb_host_available = available;
        if (!available) set_usb_host_connected(false);
    }

    void set_usb_host_connected(bool connected) {
        if (!usb_host_available && connected) return;
        usb_host_connected = connected;
        // Verified MobiGo board sensing: USB cable present pulls IOC11 low.
        if (connected) gpio_c_input &= uint16_t(~0x0800);
        else gpio_c_input |= 0x0800;
        usb_bus_reset_pending = connected;
        usb_enumerated = false;
        if (!connected) {
            usb_ep0_fifo.clear();
            usb_bulk_out_fifo.clear();
            usb_bulk_in_fifo.clear();
            usb_interrupt_in_fifo.clear();
            usb_dma_programmed_bytes = 0;
            usb_dma_transferred_bytes = 0;
        }
        if (g_log) g_log << "USB host " << (connected ? "connected" : "disconnected") << "\n";
    }

    bool usb_transceiver_enabled() const {
        return (mmio[0x7a30 - kMmioBase] & 0x000a) == 0x000a;
    }

    void usb_set_interrupt_flag(uint16_t flag) {
        if (mmio[0x7a39 - kMmioBase] & flag) mmio[0x7a3a - kMmioBase] |= flag;
    }

    void usb_bus_reset() {
        usb_ep0_fifo.clear();
        usb_bulk_out_fifo.clear();
        usb_bulk_in_fifo.clear();
        usb_interrupt_in_fifo.clear();
        usb_dma_programmed_bytes = 0;
        usb_dma_transferred_bytes = 0;
        mmio[0x7a31 - kMmioBase] &= uint16_t(~0x01ff); // address/configuration
        mmio[0x7a32 - kMmioBase] = 0x0002;              // reset released, not suspended
        mmio[0x7a37 - kMmioBase] = 0;
        usb_set_interrupt_flag(0x8000);
        usb_bus_reset_pending = false;
        usb_enumerated = false;
        if (g_log) g_log << "USB host bus reset\n";
    }

    // Standard requests other than GET/SET_DESCRIPTOR are handled by the
    // controller itself on this Generalplus USB block.
    void usb_host_enumerate() {
        if (!usb_host_connected || !usb_transceiver_enabled()) return;
        if (usb_bus_reset_pending) usb_bus_reset();
        mmio[0x7a31 - kMmioBase] = uint16_t((mmio[0x7a31 - kMmioBase] & 0xfe00) |
                                            (1u << 7) | 1u);
        if (mmio[0x7a3b - kMmioBase] & 0x0010) mmio[0x7a3c - kMmioBase] |= 0x0010;
        if (mmio[0x7a3b - kMmioBase] & 0x0004) mmio[0x7a3c - kMmioBase] |= 0x0004;
        usb_enumerated = true;
        if (g_log) g_log << "USB host set address=1 configuration=1\n";
    }

    bool usb_host_send_setup(uint8_t request_type, uint8_t request,
                             uint16_t value, uint16_t index, uint16_t length) {
        if (!usb_enumerated) return false;
        mmio[0x7a46 - kMmioBase] = request_type;
        mmio[0x7a47 - kMmioBase] = request;
        mmio[0x7a48 - kMmioBase] = value;
        mmio[0x7a49 - kMmioBase] = index;
        mmio[0x7a4a - kMmioBase] = length;
        mmio[0x7a3e - kMmioBase] &= uint16_t(~0x0001);
        mmio[0x7a37 - kMmioBase] |= 0x0001;
        usb_set_interrupt_flag(0x0001);
        if (g_log) {
            g_log << "USB SETUP type=0x" << std::hex << unsigned(request_type)
                  << " request=0x" << unsigned(request) << " value=0x" << value
                  << " index=0x" << index << " length=0x" << length << std::dec << "\n";
        }
        return true;
    }

    bool usb_host_send_bulk_out(const uint8_t *data, size_t length) {
        if (!usb_enumerated || length > 64) return false;
        // In DMA Bulk-OUT mode the endpoint FIFO is connected to an external-
        // request DMA channel.  A received USB packet supplies the demand
        // requests; it must not be consumed when the channel is merely armed.
        if (mmio[0x7a31 - kMmioBase] & 0x0400)
            return usb_dma_bulk_out_packet(data, length);
        if ((mmio[0x7a37 - kMmioBase] & 0x0800) == 0) {
            mmio[0x7a37 - kMmioBase] |= 0x2000;
            usb_set_interrupt_flag(0x0400);
            return false;
        }
        usb_bulk_out_fifo.assign(data, data + length);
        mmio[0x7a37 - kMmioBase] &= uint16_t(~0x0800);
        mmio[0x7a37 - kMmioBase] |= 0x1000;
        usb_set_interrupt_flag(0x0800); // INTFLAG.BOPS for EPEvent.BOPR
        return true;
    }

    bool usb_host_send_ep0_out(const uint8_t *data, size_t length) {
        if (!usb_enumerated || length > 8 ||
            (mmio[0x7a37 - kMmioBase] & 0x0002) == 0) return false;
        usb_ep0_fifo.assign(data, data + length);
        mmio[0x7a37 - kMmioBase] &= uint16_t(~0x0002);
        mmio[0x7a37 - kMmioBase] |= 0x0004;
        usb_set_interrupt_flag(0x0002);
        return true;
    }

    bool usb_host_take_ep0_in(std::vector<uint8_t> &packet) {
        if (!usb_enumerated || (mmio[0x7a37 - kMmioBase] & 0x0010) == 0) return false;
        packet.assign(usb_ep0_fifo.begin(), usb_ep0_fifo.end());
        usb_ep0_fifo.clear();
        mmio[0x7a37 - kMmioBase] &= uint16_t(~0x0010);
        usb_set_interrupt_flag(0x0010); // E0INPC
        return true;
    }

    void usb_host_complete_status() {
        if ((mmio[0x7a37 - kMmioBase] & 0x0040) == 0) return;
        mmio[0x7a37 - kMmioBase] &= uint16_t(~0x0040);
        usb_set_interrupt_flag(0x0040); // E0SC
    }

    bool usb_host_take_bulk_in(std::vector<uint8_t> &packet) {
        if ((mmio[0x7a31 - kMmioBase] & 0x0200) && usb_bulk_in_fifo.empty())
            usb_dma_fill_bulk_in_packet();
        if (!usb_enumerated) return false;
        if ((mmio[0x7a37 - kMmioBase] & 0x0100) == 0) {
            // A real host issues IN tokens while polling a bulk endpoint. If
            // no packet is armed, the device responds NAK and latches BINA;
            // the MobiGo firmware enables that interrupt to queue its next
            // packet (including the BOT command-status wrapper).
            mmio[0x7a37 - kMmioBase] |= 0x0400;
            usb_set_interrupt_flag(0x0100);
            return false;
        }
        packet = usb_bulk_in_fifo;
        usb_bulk_in_fifo.clear();
        mmio[0x7a37 - kMmioBase] &= uint16_t(~0x0100);
        mmio[0x7a37 - kMmioBase] |= 0x0200;
        usb_set_interrupt_flag(0x0200); // BIPC
        return true;
    }

    int usb_external_dma_channel(uint32_t peripheral_addr, bool peripheral_is_source) const {
        const uint16_t source_select = mmio[0x7abe - kMmioBase];
        for (uint32_t ch = 0; ch < 4; ++ch) {
            const uint32_t base = 0x7a80 + ch * 8;
            const uint16_t ctrl = mmio[base - kMmioBase];
            const uint32_t src = uint32_t(mmio[base + 1 - kMmioBase]) |
                                 (uint32_t(mmio[base + 4 - kMmioBase]) << 16);
            const uint32_t dst = uint32_t(mmio[base + 2 - kMmioBase]) |
                                 (uint32_t(mmio[base + 5 - kMmioBase]) << 16);
            const uint32_t count = uint32_t(mmio[base + 3 - kMmioBase]) |
                                   (uint32_t(mmio[base + 6 - kMmioBase]) << 16);
            if ((ctrl & 0x0005) != 0x0005 || count == 0) continue;
            if (((source_select >> (ch * 4)) & 0x0f) != 0) continue;
            if (peripheral_is_source ? ((src & 0xffff) == peripheral_addr)
                                     : ((dst & 0xffff) == peripheral_addr))
                return int(ch);
        }
        return -1;
    }

    void usb_dma_update_ack(uint32_t packets_remaining) {
        mmio[0x7a52 - kMmioBase] = uint16_t(packets_remaining);
        mmio[0x7a53 - kMmioBase] = uint16_t((packets_remaining >> 16) & 0x0007);
    }

    void complete_external_dma(unsigned ch, uint32_t cur_src, uint32_t cur_dst) {
        const uint32_t base = 0x7a80 + ch * 8;
        mmio[base + 1 - kMmioBase] = uint16_t(cur_src);
        mmio[base + 4 - kMmioBase] = uint16_t(cur_src >> 16);
        mmio[base + 2 - kMmioBase] = uint16_t(cur_dst);
        mmio[base + 5 - kMmioBase] = uint16_t(cur_dst >> 16);
        mmio[base + 3 - kMmioBase] = 0;
        mmio[base + 6 - kMmioBase] = 0;
        mmio[base - kMmioBase] &= uint16_t(~0x0003); // CE/BS clear
        mmio[0x7abf - kMmioBase] |= uint16_t(1u << ch);
        // P_USBD_DMAINT.DMAINTF is independent of the normal endpoint flags.
        mmio[0x7a59 - kMmioBase] |= 0x0001;
        usb_dma_update_ack(0);
    }

    bool usb_dma_bulk_out_packet(const uint8_t *data, size_t length) {
        if (!usb_enumerated || length == 0 || length > 64) return false;
        const int found = usb_external_dma_channel(0x7a35, true);
        if (found < 0) return false;
        const unsigned ch = unsigned(found);
        const uint32_t base = 0x7a80 + ch * 8;
        uint32_t dst = uint32_t(mmio[base + 2 - kMmioBase]) |
                       (uint32_t(mmio[base + 5 - kMmioBase]) << 16);
        uint32_t remaining = uint32_t(mmio[base + 3 - kMmioBase]) |
                             (uint32_t(mmio[base + 6 - kMmioBase]) << 16);
        // This firmware programs a 16-bit USB DMA path: one DMA request moves
        // one little-endian word from the Bulk-OUT FIFO into target memory.
        const uint32_t words = std::min<uint32_t>(remaining, uint32_t((length + 1) / 2));
        for (uint32_t i = 0; i < words; ++i) {
            const size_t byte = size_t(i) * 2;
            uint16_t value = data[byte];
            if (byte + 1 < length) value |= uint16_t(data[byte + 1]) << 8;
            dma_write(dst++, value);
        }
        remaining -= words;
        usb_dma_transferred_bytes += uint32_t(length);
        const uint32_t total = usb_dma_programmed_bytes
            ? usb_dma_programmed_bytes : usb_dma_transferred_bytes + remaining * 2;
        const uint32_t bytes_left = total > usb_dma_transferred_bytes
            ? total - usb_dma_transferred_bytes : 0;
        usb_dma_update_ack((bytes_left + 63) / 64);
        mmio[base + 2 - kMmioBase] = uint16_t(dst);
        mmio[base + 5 - kMmioBase] = uint16_t(dst >> 16);
        mmio[base + 3 - kMmioBase] = uint16_t(remaining);
        mmio[base + 6 - kMmioBase] = uint16_t(remaining >> 16);
        if (remaining == 0) {
            const uint32_t src = uint32_t(mmio[base + 1 - kMmioBase]) |
                                 (uint32_t(mmio[base + 4 - kMmioBase]) << 16);
            complete_external_dma(ch, src, dst);
        }
        return words != 0;
    }

    bool usb_dma_fill_bulk_in_packet() {
        const int found = usb_external_dma_channel(0x7a34, false);
        if (found < 0) return false;
        const unsigned ch = unsigned(found);
        const uint32_t base = 0x7a80 + ch * 8;
        uint32_t src = uint32_t(mmio[base + 1 - kMmioBase]) |
                       (uint32_t(mmio[base + 4 - kMmioBase]) << 16);
        uint32_t remaining = uint32_t(mmio[base + 3 - kMmioBase]) |
                             (uint32_t(mmio[base + 6 - kMmioBase]) << 16);
        const uint32_t words = std::min<uint32_t>(remaining, 32);
        usb_bulk_in_fifo.clear();
        usb_bulk_in_fifo.reserve(words * 2);
        for (uint32_t i = 0; i < words; ++i) {
            const uint16_t value = dma_read(src++);
            usb_bulk_in_fifo.push_back(uint8_t(value));
            usb_bulk_in_fifo.push_back(uint8_t(value >> 8));
        }
        remaining -= words;
        usb_dma_transferred_bytes += words * 2;
        const uint32_t total = usb_dma_programmed_bytes
            ? usb_dma_programmed_bytes : usb_dma_transferred_bytes + remaining * 2;
        const uint32_t bytes_left = total > usb_dma_transferred_bytes
            ? total - usb_dma_transferred_bytes : 0;
        usb_dma_update_ack((bytes_left + 63) / 64);
        mmio[base + 1 - kMmioBase] = uint16_t(src);
        mmio[base + 4 - kMmioBase] = uint16_t(src >> 16);
        mmio[base + 3 - kMmioBase] = uint16_t(remaining);
        mmio[base + 6 - kMmioBase] = uint16_t(remaining >> 16);
        if (!usb_bulk_in_fifo.empty()) mmio[0x7a37 - kMmioBase] |= 0x0100;
        if (remaining == 0) {
            const uint32_t dst = uint32_t(mmio[base + 2 - kMmioBase]) |
                                 (uint32_t(mmio[base + 5 - kMmioBase]) << 16);
            complete_external_dma(ch, src, dst);
        }
        return !usb_bulk_in_fifo.empty();
    }

    bool usb_irq3_asserted_no_update() const {
        const bool pending = (mmio[0x7a3a - kMmioBase] & mmio[0x7a39 - kMmioBase]) ||
                             (mmio[0x7a3c - kMmioBase] & mmio[0x7a3b - kMmioBase]) ||
                             ((mmio[0x7a59 - kMmioBase] & 0x0003) == 0x0003);
        return pending && ((mmio[0x78a4 - kMmioBase] & 0x0008) == 0);
    }

    void set_matrix_key(unsigned row, unsigned column, bool pressed) {
        const uint16_t bit = uint16_t(1u << (column));
        if (pressed) matrix_pressed[row] |= bit;
        else matrix_pressed[row] &= uint16_t(~bit);
    }

    void set_motion_direction(unsigned direction, bool pressed) {
        accelerometer.set_direction(direction, pressed);
    }

    void set_touch(bool pressed, uint16_t adc_x, uint16_t adc_y) {
        touch_adc_x = adc_x & 0x0fff;
        touch_adc_y = adc_y & 0x0fff;
        touch_pressed = pressed;
    }

    uint16_t touch_adjusted_gpio_e(uint16_t external_input) const {
        // FUN_039938 configures IOE10 as a high output, delays, then samples
        // IOE8. A pressed panel electrically joins the driven and sensed
        // layers. Unpressed IOE8 remains at the board's low input level.
        const uint16_t driven_high = mmio[0x7881 - kMmioBase] &
                                     mmio[0x7882 - kMmioBase] &
                                     mmio[0x7883 - kMmioBase] & 0x0400;
        if (touch_pressed && driven_high) return external_input | 0x0100;
        return external_input & uint16_t(~0x0100);
    }

    uint16_t motion_adjusted_gpio_e(uint16_t external_input) const {
        // IOE6/7 have board pull-ups and form the accelerometer's I2C clock
        // and data lines. The slave can only pull SDA low.
        uint16_t input = external_input | 0x00c0;
        if (accelerometer.sda_is_low()) input &= uint16_t(~0x0080);
        return input;
    }

    bool gpio_e_host_line_high(uint16_t bit) const {
        // The retail bit-banged driver changes IOE_Buffer directly. IOE_Data
        // is the pad sample register and is not rewritten for each edge.
        const uint16_t buffer = mmio[0x7881 - kMmioBase];
        return (buffer & bit) != 0;
    }

    void update_accelerometer_i2c() {
        const bool scl = gpio_e_host_line_high(0x0040);
        const bool sda = gpio_e_host_line_high(0x0080);
        accelerometer.observe(scl, sda);
    }

    static uint32_t adc_conversion_cycles(uint16_t setup) {
        static constexpr std::array<uint16_t, 8> clocks{
            512, 256, 128, 64, 1024, 2048, 512, 512
        };
        return clocks[(setup >> 8) & 7];
    }

    int active_matrix_row() const {
        static constexpr std::array<uint16_t, 5> ioc_row_pins{
            0x0080, 0x0040, 0x0400, 0x0200, 0x0020
        };
        const uint16_t ioc_active = mmio[0x7871 - kMmioBase] &
                                    mmio[0x7872 - kMmioBase] &
                                    mmio[0x7873 - kMmioBase];
        for (unsigned row = 0; row < ioc_row_pins.size(); ++row) {
            if (ioc_active & ioc_row_pins[row]) return int(row);
        }
        const uint16_t ioe_active = mmio[0x7881 - kMmioBase] &
                                    mmio[0x7882 - kMmioBase] &
                                    mmio[0x7883 - kMmioBase];
        if (ioe_active & 0x0004) return 5;
        return -1;
    }

    uint16_t matrix_adjusted_input(uint32_t addr, uint16_t external_input) const {
        const int row = active_matrix_row();
        if (row < 0) return external_input;
        const uint16_t pressed = matrix_pressed[unsigned(row)];
        if (addr == 0x7868) {
            return uint16_t((external_input & 0x03ff) | ((pressed & 0x003f) << 10));
        }
        if (addr == 0x7860) {
            return uint16_t((external_input & 0xc7ff) | ((pressed & 0x01c0) << 5));
        }
        return external_input;
    }
    uint16_t adc_manual_data = 0x0000;
    uint16_t adc_auto_data = 0x0000;
    uint32_t system_ctrl_read_log_count = 0;
    uint32_t frame_local_watch_log_count = 0;
    uint32_t frame_local_read_log_count = 0;
    uint32_t frame_any_read_log_count = 0;
    uint32_t frame_a_read_log_count = 0;

    Bus() {
        apply_reset_defaults();
    }

    void apply_reset_defaults() {
        mmio.fill(0);
        mmio[0x703c - kMmioBase] = 0x0020; // MAME renderer reset default: neutral TV saturation.
        mmio[0x7042 - kMmioBase] = 0x0001; // Sprite engine enabled by default in MAME's GPL renderer.
        mmio[0x707f - kMmioBase] = 0x0001; // PPU enabled reset value used by MAME's GPL162xx video.
        mmio[0x7800 - kMmioBase] = 0x8688; // P_BodyID documented value.
        mmio[0x7804 - kMmioBase] = 0xffff; // Peripheral clock control reset defaults.
        mmio[0x7805 - kMmioBase] = 0xffff;
        mmio[0x780f - kMmioBase] = 0x0001; // P_State reset default: 12 MHz/slow clock state.
        mmio[0x7817 - kMmioBase] = 0x0010; // P_PLLN documented reset value.
        mmio[0x781f - kMmioBase] = 0x0101; // P_AD_Driving documented reset value.
        mmio[0x7810 - kMmioBase] = 0x0001; // MAME GPL16250 reset default for banked CS window.
        mmio[0x7820 - kMmioBase] = 0x003f; // P_MCS0_Ctrl documented reset value.
        mmio[0x7821 - kMmioBase] = 0x003f; // P_MCS1_Ctrl documented reset value.
        mmio[0x7822 - kMmioBase] = 0x003f; // P_MCS2_Ctrl documented reset value.
        mmio[0x7823 - kMmioBase] = 0x003f; // P_MCS3_Ctrl documented reset value.
        mmio[0x7824 - kMmioBase] = 0x003f; // P_MCS4_Ctrl documented reset value.
        mmio[0x7825 - kMmioBase] = 0x012f; // P_EMUCS_Ctrl documented reset value.
        mmio[0x782b - kMmioBase] = 0x0300; // P_MCS3_TimingCtrl documented reset value.
        mmio[0x782c - kMmioBase] = 0x0300; // P_MCS4_TimingCtrl documented reset value.
        mmio[0x7840 - kMmioBase] = 0x030f; // P_Mem_Ctrl documented reset value.
        mmio[0x7841 - kMmioBase] = 0x007f; // P_Addr_Ctrl documented reset value.
        mmio[0x78a5 - kMmioBase] = 0x0008; // P_INT_Priority2 documented reset value.
        mmio[0x78f0 - kMmioBase] = 0x8000; // P_CHA_Ctrl reset: FIFO empty flag set.
        mmio[0x78f2 - kMmioBase] = 0x0100; // P_CHA_FIFO reset: FIFO reset bit defaults high.
        mmio[0x78f8 - kMmioBase] = 0x8000; // P_CHB_Ctrl reset: FIFO empty flag set.
        mmio[0x78fa - kMmioBase] = 0x0100; // P_CHB_FIFO reset: FIFO reset bit defaults high.
        mmio[0x78fd - kMmioBase] = 0x000c; // P_DAC_Ctrl reset: both DAC channels powered down.
        mmio[0x78fe - kMmioBase] = 0x0013; // P_HPAMP_Ctrl reset: headphone driver powered down.
        mmio[0x7961 - kMmioBase] = 0x0080; // P_MADC_Ctrl reset: manual ADC ready bit defaults high.
        mmio[0x7970 - kMmioBase] = 0x083e; // P_HQADC_Ctrl documented reset value.
        // GPL16250 register lists identify 0x7ae2 as E-Fuse2. The internal
        // ROM requires bits 8 and 9 set before proceeding into boot-device
        // probing; zero takes its power-down path. The exact fuse meanings
        // remain undocumented, but 0x0300 is directly required by this ROM.
        mmio[0x7ae2 - kMmioBase] = 0x0300;
        mmio[0x7869 - kMmioBase] = 0x0010; // GPIO-B SPI NOR CS idle high.
    }

    void system_reset(bool preserve_memory = false) {
        (void)preserve_memory;
        // A system reset returns P_Reset_Flag to its reset value. The verified
        // programming guide distinguishes this from CPU-only reset, where
        // peripherals (including the reset-cause latch) retain their state.
        rowscroll_ram.fill(0);
        rowzoom_ram.fill(0);
        tx3_transform_ram.fill(0);
        sprite_ram.fill(0);
        sound_ram.fill(0);
        spu_channel_start_sequence.fill(0);
        apply_reset_defaults();

        nand.command = 0;
        nand.addr_low = 0;
        nand.addr_high = 0;
        nand.ctrl = 0;
        nand.type = 0;
        nand.cursor = 0;
        nand.effective = 0;
        nand.status = 0x40;
        spi.reset_transaction();
        spi.chip_selected = false;
        audio_fifo_level_a = 0;
        audio_fifo_level_b = 0;
        audio_fifo_a.clear();
        audio_fifo_b.clear();
        audio_shared_fifo_next_b = false;

        cycles = 0;
        last_video_scanline = UINT32_MAX;
        next_video_edge_cycles = 0;
        last_periodic_update_cycles = UINT64_MAX;
        next_periodic_event_cycles = 0;
        last_timer_cycles = UINT64_MAX;
        timer_phase_accum.fill(0);
        timer_any_enabled = false;
        last_timebase_cycles = UINT64_MAX;
        timebase_phase_accum.fill(0);
        last_scheduler_cycles = UINT64_MAX;
        scheduler_phase_accum = 0;
        last_rtc_cycles = UINT64_MAX;
        rtc_phase_accum = 0;
        last_spu_beat_cycles = UINT64_MAX;
        spu_beat_phase_accum = 0;
        spu_beat_elapsed_ticks = 0;
        periodic_clock_hz = 0;
        usb_device_enabled_cycle = UINT64_MAX;
        usb_suspend_latched = false;
        usb_dma_programmed_bytes = 0;
        usb_dma_transferred_bytes = 0;
        sleep_requested = false;
        power_latch_seen_high = false;
        poweroff_requested = false;
        system_reset_requested = false;
        system_reset_preserve_memory = false;
        watchdog_enabled = false;
        watchdog_reset_cpu_only = false;
        watchdog_expire_cycles = 0;
        mba_watchdog_handoff_pending = mba_watchdog_entry != UINT32_MAX;
        mba_application_handoff_pending = mba_application_entry != UINT32_MAX;
        mba_application_active = false;
        mba_entry_stack_valid = false;
        mba_entry_stack_address = 0;
        adc_manual_pending = false;
        adc_manual_due_cycles = 0;
        adc_manual_channel = 0;
        adc_manual_data = 0;
        accelerometer.reset_bus();
        logged_unknown_reads.clear();
        logged_unknown_writes.clear();
        dma_write_log_count = 0;
        cs_oob_log_count = 0;
        cart_probe_log_count = 0;
        rom_write_log_count = 0;
        irq_log_count = 0;
        stack_watch_log_count = 0;
        handoff_watch_log_count = 0;
        app_wait_watch_log_count = 0;
        gpio_read_log_count = 0;
        gpiob_aux_read_log_count = 0;
        timer_status_read_log_count = 0;
        late_irq_log_count = 0;
        focused_late_irq_log_count = 0;
        ppu_write_log_count = 0;
        ppu_ram_write_log_count = 0;
        video_dma_touch_log_count = 0;
        video_status_read_log_count = 0;
        late_video_status_log_count = 0;
        late_video_edge_log_count = 0;
        post_ppu_video_log_count = 0;
        post_ppu_irq_log_count = 0;
        fb_ppu_go_log_count = 0;
        app_wait_read_log_count = 0;
        late_app_wait_log_count = 0;
        late_app_wait_write_log_count = 0;
        event_state_read_log_count = 0;
        event_state_write_log_count = 0;
        late_event_detail_read_log_count = 0;
        late_event_detail_write_log_count = 0;
        event_ram_watch_log_count = 0;
        late_timer_ack_log_count = 0;
        late_timer_overflow_log_count = 0;
        foreground_local_log_count = 0;
        a6fa_bus_log_count = 0;
        system_ctrl_read_log_count = 0;
        frame_local_watch_log_count = 0;
        frame_local_read_log_count = 0;
        frame_any_read_log_count = 0;
        frame_a_read_log_count = 0;
        last_framebuffer_base = 0;
        last_ppu_framebuffer_base = 0;
        last_framebuffer_valid = false;
        ppu_go_pending = false;
        ppu_go_due_cycles = 0;
        ppu_framebuffer_valid = false;
        ++power_reset_count;
    }

    bool internal_rom_base_contains(uint32_t addr) const {
        if (internal_rom.empty()) return false;
        if (addr < internal_rom_base) return false;
        return (addr - internal_rom_base) < internal_rom.size();
    }

    bool internal_rom_shadow_contains(uint32_t addr) const {
        if (!internal_rom_shadow_low || internal_rom.empty()) return false;
        if (addr >= kMmioBase && addr <= kMmioEnd) return false;
        return addr < internal_rom.size();
    }

    uint16_t gpio_pad_read(uint32_t data_addr, uint16_t external_input) const {
        const uint16_t data = mmio[data_addr - kMmioBase];
        const uint16_t dir = mmio[data_addr + 2 - kMmioBase];
        const uint16_t attrib = mmio[data_addr + 3 - kMmioBase];
        const uint16_t output_pad = uint16_t((data & attrib) | (~data & ~attrib));
        return uint16_t((external_input & ~dir) | (output_pad & dir));
    }

    void log_gpio_read(uint32_t addr, uint16_t value, uint16_t external_input) {
        if (pc_for_log >= 0x030000 && g_log) {
            g_log << "GPIO READ pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr
                  << " value=0x" << value
                  << " input=0x" << external_input
                  << " data=0x" << mmio[addr - kMmioBase]
                  << " dir=0x" << mmio[addr + 2 - kMmioBase]
                  << " attr=0x" << mmio[addr + 3 - kMmioBase]
                  << std::dec << "\n";
            for (unsigned bit = 0; bit < 16; ++bit) {
                g_log << "GPIO BIT READ pc=0x" << std::hex << pc_for_log
                      << " port=0x" << addr << " bit=" << std::dec << bit
                      << " value=" << ((value >> bit) & 1) << "\n";
            }
        }
    }

    bool is_gpio_data_register(uint32_t addr) const {
        return addr == 0x7860 || addr == 0x7868 || addr == 0x7870 ||
               addr == 0x7878 || addr == 0x7880;
    }

    void update_power_latch() {
        static constexpr uint16_t kPowerLatch = 0x0010; // GPIO-D4
        const bool output = (mmio[0x787a - kMmioBase] & kPowerLatch) != 0;
        const bool normal_polarity =
            (mmio[0x787b - kMmioBase] & kPowerLatch) != 0;
        const bool high = (mmio[0x7879 - kMmioBase] & kPowerLatch) != 0;

        // Ignore a cold/unconfigured low level. The physical shutdown edge is
        // only meaningful after firmware has established D4 as the active-high
        // power-hold output.
        if (output && normal_polarity && high) {
            power_latch_seen_high = true;
            return;
        }
        if (!power_latch_seen_high || !output || !normal_polarity || high ||
            poweroff_requested) {
            return;
        }

        poweroff_requested = true;
        if (g_log) {
            g_log << "POWER GPIO-D4 latch released pc=0x" << std::hex
                  << pc_for_log << " buffer=0x"
                  << mmio[0x7879 - kMmioBase] << " dir=0x"
                  << mmio[0x787a - kMmioBase] << " attr=0x"
                  << mmio[0x787b - kMmioBase] << " cycles=0x" << cycles
                  << std::dec << "\n";
        }
    }

    bool trace_a6fa_bus() const {
        return pc_for_log >= 0x03a6fa && pc_for_log < 0x03a750;
    }

    void log_a6fa_bus(const char *op, uint32_t addr, uint16_t value) {
        if (g_log && cycles >= 0x30d00000ull && trace_a6fa_bus() && a6fa_bus_log_count++ < 32768) {
            g_log << "A6FA BUS " << op
                  << " pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr
                  << " value=0x" << value
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
    }

    bool trace_foreground_local(uint32_t addr) const {
        return cycles >= 0x39d00000ull &&
               pc_for_log >= 0x030400 && pc_for_log <= 0x030460 &&
               ((addr >= 0x6aa0 && addr <= 0x6ae0) ||
                (addr >= 0x6d00 && addr <= 0x6d30));
    }

    bool trace_event_ram(uint32_t addr) const {
        if (cycles < 0x2f000000ull) return false;
        if (addr >= 0x09b0 && addr <= 0x09ef) return true;
        const uint16_t event_base = mem[0x09c8];
        if (event_base != 0 && addr >= event_base && addr < uint32_t(event_base + 0x80)) return true;
        return addr >= 0xda80 && addr <= 0xdb10;
    }

    void log_event_ram_read(uint32_t addr, uint16_t value) {
        if (!g_log || !trace_event_ram(addr) || event_ram_watch_log_count++ >= 65536) return;
        g_log << "EVENT RAM read pc=0x" << std::hex << pc_for_log
              << " addr=0x" << addr
              << " data=0x" << value
              << " b7=0x" << mem[0x09b7]
              << " b8=0x" << mem[0x09b8]
              << " b9=0x" << mem[0x09b9]
              << " ba=0x" << mem[0x09ba]
              << " c3=0x" << mem[0x09c3]
              << " c5=0x" << mem[0x09c5]
              << " c6=0x" << mem[0x09c6]
              << " c7=0x" << mem[0x09c7]
              << " c8=0x" << mem[0x09c8]
              << " cycles=0x" << cycles << std::dec << "\n";
    }

    void log_event_ram_write(uint32_t addr, uint16_t old_value, uint16_t value) {
        if (!g_log || !trace_event_ram(addr) || event_ram_watch_log_count++ >= 65536) return;
        g_log << "EVENT RAM write pc=0x" << std::hex << pc_for_log
              << " addr=0x" << addr
              << " old=0x" << old_value
              << " data=0x" << value
              << " b7=0x" << mem[0x09b7]
              << " b8=0x" << mem[0x09b8]
              << " b9=0x" << mem[0x09b9]
              << " ba=0x" << mem[0x09ba]
              << " c3=0x" << mem[0x09c3]
              << " c5=0x" << mem[0x09c5]
              << " c6=0x" << mem[0x09c6]
              << " c7=0x" << mem[0x09c7]
              << " c8=0x" << mem[0x09c8]
              << " cycles=0x" << cycles << std::dec << "\n";
    }

    uint16_t internal_rom_base_read(uint32_t addr) const {
        return internal_rom[addr - internal_rom_base];
    }

    uint16_t internal_rom_shadow_read(uint32_t addr) const {
        return internal_rom[addr];
    }

    uint16_t read_code(uint32_t addr) {
        addr &= kAddrMask;
        // Program fetches can execute internal ROM bytes through the data-space
        // MMIO window. The internal ROM uses CLRB PC,15 to branch from 0x00f2e3
        // to code at 0x0072e4; data reads in this range still resolve to MMIO.
        if (addr >= kMmioBase && addr <= kMmioEnd &&
            !internal_rom.empty() && addr < internal_rom.size()) {
            return internal_rom[addr];
        }
        if (addr >= kMmioBase && addr <= kMmioEnd) return read_mmio(addr);
        if (internal_rom_fetch_mirror64 && !internal_rom.empty() && addr >= 0x010000) {
            const uint32_t mirrored = addr & 0x00ffff;
            if (mirrored < internal_rom.size()) return internal_rom[mirrored];
        }
        if (internal_rom_base_contains(addr)) return internal_rom_base_read(addr);
        if (internal_rom_shadow_contains(addr)) return internal_rom_shadow_read(addr);
        return read(addr);
    }

    uint16_t read(uint32_t addr) {
        addr &= kAddrMask;
        if (addr >= 0x7c00 && addr <= 0x7fff) {
            const uint16_t value = sound_ram[addr - 0x7c00];
            if (g_log) log_a6fa_bus("READ", addr, value);
            return value;
        }
        if (addr >= kMmioBase && addr <= kMmioEnd) {
            const uint16_t value = read_mmio(addr);
            if (g_log) log_a6fa_bus("READ", addr, value);
            return value;
        }
        if (internal_rom_base_contains(addr)) {
            const uint16_t value = internal_rom_base_read(addr);
            if (g_log) log_a6fa_bus("READ", addr, value);
            return value;
        }
        if (addr >= kCsBase && addr < 0x200000) {
            const uint16_t value = cs_read(addr - kCsBase);
            if (g_log) log_a6fa_bus("READ", addr, value);
            return value;
        }
        if (addr >= 0x200000) {
            const uint32_t bank = mmio[0x7810 - kMmioBase] & 0x3f;
            const int64_t real = int64_t(addr - 0x200000) + int64_t(bank) * 0x200000 - kCsBase;
            const uint16_t value = real >= 0 ? cs_read(uint32_t(real)) : 0;
            if (g_log) log_a6fa_bus("READ", addr, value);
            return value;
        }
        if (g_log && addr >= 0x09b7 && addr <= 0x09c7 &&
            pc_for_log >= 0x063500 && pc_for_log <= 0x0635d0 &&
            app_wait_read_log_count++ < 256) {
            g_log << "APP WAIT WATCH read pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " data=0x" << mem[addr]
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        if (g_log && (addr == 0x09b7 || addr == 0x09b8 || addr == 0x09c6 || addr == 0x09c7) &&
            pc_for_log >= 0x063500 && pc_for_log <= 0x0635d0 &&
            event_state_read_log_count++ < 512) {
            g_log << "EVENT STATE read pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " data=0x" << mem[addr]
                  << " b7=0x" << mem[0x09b7]
                  << " b8=0x" << mem[0x09b8]
                  << " c6=0x" << mem[0x09c6]
                  << " c7=0x" << mem[0x09c7]
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        if (g_log && cycles >= 0x2f000000ull && addr >= 0x09b7 && addr <= 0x09c7 &&
            pc_for_log >= 0x063500 && pc_for_log <= 0x0635d0 &&
            late_app_wait_log_count++ < 64) {
            g_log << "LATE APP WAIT read pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " data=0x" << mem[addr]
                  << " b7=0x" << mem[0x09b7]
                  << " b8=0x" << mem[0x09b8]
                  << " c6=0x" << mem[0x09c6]
                  << " c7=0x" << mem[0x09c7]
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        if (g_log && cycles >= 0x39d00000ull && addr >= 0x09c3 && addr <= 0x09c8 &&
            pc_for_log >= 0x063500 && pc_for_log <= 0x0635d0 &&
            late_event_detail_read_log_count++ < 4096) {
            g_log << "LATE EVENT DETAIL read pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " data=0x" << mem[addr]
                  << " c3=0x" << mem[0x09c3]
                  << " c5=0x" << mem[0x09c5]
                  << " c6=0x" << mem[0x09c6]
                  << " c7=0x" << mem[0x09c7]
                  << " c8=0x" << mem[0x09c8]
                  << " b7=0x" << mem[0x09b7]
                  << " b8=0x" << mem[0x09b8]
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        const uint16_t value = mem[addr];
        if (g_log && cycles >= 0x39d00000ull && pc_for_log >= 0x054a00 && pc_for_log <= 0x054a90 &&
            addr < 0x7000 && frame_a_read_log_count++ < 50000) {
            g_log << "FRAME A read pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " data=0x" << value
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        if (g_log && cycles >= 0x39d00000ull && pc_for_log >= 0x053400 && pc_for_log <= 0x053700 &&
            addr < 0x7000 && frame_any_read_log_count++ < 120000) {
            g_log << "FRAME ANY read pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " data=0x" << value
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        if (g_log && cycles >= 0x39d00000ull && pc_for_log >= 0x053400 && pc_for_log <= 0x053700 &&
            addr >= 0x6800 && addr <= 0x68ff && frame_local_read_log_count++ < 30000) {
            g_log << "FRAME LOCAL read pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " data=0x" << value
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        log_event_ram_read(addr, value);
        if (g_log && trace_foreground_local(addr) && foreground_local_log_count++ < 8192) {
            g_log << "FOREGROUND LOCAL read pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr
                  << " data=0x" << value
                  << " sp0=0x" << mem[0x6abb]
                  << " local_6abf=0x" << mem[0x6abf]
                  << " local_6adb=0x" << mem[0x6adb]
                  << " data_6d10=0x" << mem[0x6d10]
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        if (g_log) log_a6fa_bus("READ", addr, value);
        return value;
    }

    void write(uint32_t addr, uint16_t value) {
        addr &= kAddrMask;
        if (addr >= 0x7c00 && addr <= 0x7fff) {
            const uint32_t sound_offset = addr - 0x7c00;
            sound_ram[sound_offset] = value;
            if (sound_offset < 0x200 && (sound_offset & 0x0f) <= 1) {
                ++spu_channel_start_sequence[sound_offset >> 4];
            }
            if (g_log) log_a6fa_bus("WRITE", addr, value);
            return;
        }
        if (addr >= kMmioBase && addr <= kMmioEnd) {
            write_mmio(addr, value);
            if (g_log) log_a6fa_bus("WRITE", addr, value);
            return;
        }
        if (internal_rom_base_contains(addr)) {
            if (g_log && rom_write_log_count++ < 32) {
                g_log << "ROM WRITE IGNORED pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr << " data=0x" << value << std::dec << "\n";
            }
            return;
        }
        if (addr >= kCsBase && addr < 0x200000) {
            cs_write(addr - kCsBase, value);
            if (g_log) log_a6fa_bus("WRITE", addr, value);
            return;
        }
        if (addr >= 0x200000) {
            const uint32_t bank = mmio[0x7810 - kMmioBase] & 0x3f;
            const int64_t real = int64_t(addr - 0x200000) + int64_t(bank) * 0x200000 - kCsBase;
            if (real >= 0) cs_write(uint32_t(real), value);
            if (g_log) log_a6fa_bus("WRITE", addr, value);
            return;
        }
        if (g_log && pc_for_log >= 0x030000 && addr >= 0x6bf0 && addr <= 0x6c10 &&
            stack_watch_log_count++ < 256) {
            g_log << "STACK WATCH write pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " data=0x" << value << std::dec << "\n";
        }
        if (g_log && addr >= 0x2200 && addr <= 0x2230 &&
            handoff_watch_log_count++ < 256) {
            g_log << "HANDOFF WATCH write pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " data=0x" << value << std::dec << "\n";
        }
        if (g_log && addr >= 0x09a0 && addr <= 0x09d0 &&
            app_wait_watch_log_count++ < 4096) {
            g_log << "APP WAIT WATCH write pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " data=0x" << value
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        if (g_log && cycles >= 0x2f000000ull && addr >= 0x09b7 && addr <= 0x09c7 &&
            late_app_wait_write_log_count++ < 4096) {
            g_log << "LATE APP WAIT write pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " data=0x" << value
                  << " old=0x" << mem[addr]
                  << " b7=0x" << mem[0x09b7]
                  << " b8=0x" << mem[0x09b8]
                  << " c6=0x" << mem[0x09c6]
                  << " c7=0x" << mem[0x09c7]
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        if (g_log && (addr == 0x09b7 || addr == 0x09b8 || addr == 0x09c6 || addr == 0x09c7) &&
            event_state_write_log_count++ < 16384) {
            g_log << "EVENT STATE write pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " old=0x" << mem[addr]
                  << " data=0x" << value
                  << " b7=0x" << mem[0x09b7]
                  << " b8=0x" << mem[0x09b8]
                  << " c6=0x" << mem[0x09c6]
                  << " c7=0x" << mem[0x09c7]
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        if (g_log && cycles >= 0x39d00000ull && addr >= 0x09c3 && addr <= 0x09c8 &&
            late_event_detail_write_log_count++ < 4096) {
            g_log << "LATE EVENT DETAIL write pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " old=0x" << mem[addr]
                  << " data=0x" << value
                  << " c3=0x" << mem[0x09c3]
                  << " c5=0x" << mem[0x09c5]
                  << " c6=0x" << mem[0x09c6]
                  << " c7=0x" << mem[0x09c7]
                  << " c8=0x" << mem[0x09c8]
                  << " b7=0x" << mem[0x09b7]
                  << " b8=0x" << mem[0x09b8]
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        if (g_log && trace_foreground_local(addr) && foreground_local_log_count++ < 8192) {
            g_log << "FOREGROUND LOCAL write pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr
                  << " old=0x" << mem[addr]
                  << " data=0x" << value
                  << " sp0=0x" << mem[0x6abb]
                  << " local_6abf=0x" << mem[0x6abf]
                  << " local_6adb=0x" << mem[0x6adb]
                  << " data_6d10=0x" << mem[0x6d10]
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        const uint16_t old_value = mem[addr];
        if (g_log && cycles >= 0x39d00000ull && addr >= 0x6830 && addr <= 0x68ff &&
            frame_local_watch_log_count++ < 20000) {
            g_log << "FRAME LOCAL write pc=0x" << std::hex << pc_for_log
                  << " addr=0x" << addr << " old=0x" << old_value
                  << " data=0x" << value << " cycles=0x" << cycles << std::dec << "\n";
        }
        log_event_ram_write(addr, old_value, value);
        mem[addr] = value;
        if (g_log) log_a6fa_bus("WRITE", addr, value);
    }

    uint32_t mcs_start(unsigned index) const {
        // GPL16250 static-memory space begins at physical word 0x20000.  The
        // start of each later chip select is implicit in the programmed sizes
        // of the preceding MCS control registers (64K words per page).
        uint32_t start = 0x020000;
        for (unsigned i = 0; i < index; ++i) {
            const uint16_t ctrl = mmio[0x7820 - kMmioBase + i];
            start += uint32_t(((ctrl >> 8) & 0xff) + 1) << 16;
        }
        return start;
    }

    bool cart_address(uint32_t offset, uint32_t &cart_offset) const {
        if (cart_mem.empty()) return false;
        const uint32_t physical = offset + kCsBase;
        const uint32_t cs3_start = mcs_start(3);
        const uint32_t cs3_words =
            uint32_t(((mmio[0x7823 - kMmioBase] >> 8) & 0xff) + 1) << 16;
        if (physical < cs3_start || physical - cs3_start >= cs3_words) return false;
        // The dump is an 8M-word NOR while CS3 is configured for a 16M-word
        // aperture.  The unconnected top address line mirrors the fitted ROM.
        cart_offset = (physical - cs3_start) % uint32_t(cart_mem.size());
        return true;
    }

    uint16_t cs_read(uint32_t offset) {
        uint32_t cart_offset = 0;
        if (cart_address(offset, cart_offset)) {
            const uint16_t value = cart_mem[cart_offset];
            if (g_log && cart_offset < 4 && cart_probe_log_count++ < 32) {
                g_log << "CART READ pc=0x" << std::hex << pc_for_log
                      << " physical=0x" << (offset + kCsBase)
                      << " offset=0x" << cart_offset << " value=0x" << value
                      << " bank=0x" << (mmio[0x7810 - kMmioBase] & 0x3f)
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            return value;
        }
        // The physical EM638165TS-6G is a 4M x16 SDRAM, so addresses beyond
        // its fitted capacity wrap over the chip's connected address lines.
        return sdram[offset & (kSdramWords - 1)];
    }

    void cs_write(uint32_t offset, uint16_t value) {
        if (g_log && offset == 0x00000e) {
            g_log << "MODULE STATE write pc=0x" << std::hex << pc_for_log
                  << " addr=0x3000e old=0x" << sdram[offset]
                  << " data=0x" << value
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        uint32_t cart_offset = 0;
        if (cart_address(offset, cart_offset)) {
            // Cartridge storage is NOR/ROM.  Plain stores do not mutate it;
            // command-sequence emulation can be added if software needs it.
            return;
        }
        sdram[offset & (kSdramWords - 1)] = value;
    }

    uint16_t dma_read(uint32_t offset) {
        if (offset < kCsBase) return read(offset);
        return cs_read(offset - kCsBase);
    }

    void dma_write(uint32_t offset, uint16_t value) {
        if (offset < kCsBase) write(offset, value);
        else cs_write(offset - kCsBase, value);
    }

    uint16_t read_audio_ctrl(uint32_t addr) const {
        return mmio[addr - kMmioBase];
    }

    uint16_t read_audio_fifo(uint32_t addr, uint8_t level) const {
        const uint16_t cfg = mmio[addr - kMmioBase] & 0x40f0;
        uint16_t status = cfg | uint16_t(level & 0x0f);
        if (level >= 16) status |= 0x8000;
        return status;
    }

    void write_audio_ctrl(uint32_t addr, uint16_t value) {
        const uint16_t old = mmio[addr - kMmioBase];
        uint16_t stored = value & 0x7fff;
        // SRC reset completes immediately in this timing model.
        stored &= uint16_t(~0x0200);
        stored |= old & 0x8000;
        if (value & 0x8000) stored &= uint16_t(~0x8000);
        mmio[addr - kMmioBase] = stored;
    }

    void write_audio_fifo(uint32_t addr, uint16_t value, uint8_t &level,
                          std::deque<uint16_t> &fifo, uint32_t ctrl_addr) {
        if (value & 0x0100) {
            level = 0;
            fifo.clear();
            mmio[ctrl_addr - kMmioBase] |= 0x8000;
        }
        const uint16_t underrun = (value & 0x0100) ? 0 :
            (mmio[addr - kMmioBase] & 0x4000);
        mmio[addr - kMmioBase] = uint16_t(underrun | (value & 0x00f0));
    }

    void write_audio_data(uint32_t addr, uint16_t value, uint8_t &level,
                          std::deque<uint16_t> &fifo, uint32_t ctrl_addr) {
        mmio[addr - kMmioBase] = value;
        if (level < 16) {
            fifo.push_back(value);
            ++level;
        }
        if (level != 0) mmio[ctrl_addr - kMmioBase] &= uint16_t(~0x8000);
    }

    void write_spu_control(uint32_t addr, uint16_t value) {
        const uint32_t offset = addr & 0x1f;
        if (offset == 0x04 || offset == 0x05) update_periodic_events();
        uint16_t &reg = mmio[addr - kMmioBase];
        // Per-channel FIQ status, stop status, and envelope IRQ status are
        // hardware latches acknowledged by writing one. Channel status is
        // read-only. The low/high channel banks share the same layout.
        if (offset == 0x00) {
            const uint16_t rising = uint16_t(value & ~reg);
            const unsigned bank = (addr >= 0x7ba0) ? 1u : 0u;
            reg = value;
            for (unsigned bit = 0; bit < 16; ++bit) {
                if (rising & uint16_t(1u << bit)) {
                    ++spu_channel_start_sequence[bank * 16 + bit];
                }
            }
        } else if (offset == 0x0b) {
            const unsigned bank = (addr >= 0x7ba0) ? 1u : 0u;
            const uint32_t enable_addr = bank ? 0x7ba0 : 0x7b80;
            const uint32_t status_addr = bank ? 0x7baf : 0x7b8f;
            // P_SPU_CH_STOP_STATUS is both the stop command and a W1C latch.
            // The resident resets all voices by writing ffff to both banks.
            reg &= uint16_t(~value);
            mmio[enable_addr - kMmioBase] &= uint16_t(~value);
            mmio[status_addr - kMmioBase] &= uint16_t(~value);
        } else if (offset == 0x03 || offset == 0x17) {
            reg &= uint16_t(~value);
        } else if (offset == 0x0f) {
            return;
        } else if (offset == 0x04) {
            reg = value & 0x07ff;
            last_spu_beat_cycles = cycles;
            spu_beat_phase_accum = 0;
            spu_beat_elapsed_ticks = 0;
        } else if (offset == 0x05) {
            const uint16_t status = reg & 0x4000;
            reg = uint16_t((value & ~uint16_t(0x4000)) |
                           (status & ~value));
            last_spu_beat_cycles = cycles;
            spu_beat_phase_accum = 0;
            spu_beat_elapsed_ticks = 0;
        } else {
            reg = value;
        }
    }

    uint32_t tft_cycles_per_line() const {
        // The verified GPF16001A SDK encodes the TFT clock divisor in
        // P_TFT_Ctrl[3:1] as SYSCLK / 1 through SYSCLK / 8.  The MobiGo
        // programs H_Width=0x400; H_Start=0x32 and H_End=0x3f2 describe the
        // 960 serial clocks occupied by 320 RGB pixels, confirming that this
        // generation treats H_Width as the complete line-clock count.
        const uint32_t horizontal_clocks = mmio[0x7055 - kMmioBase]
            ? mmio[0x7055 - kMmioBase] : 1024u;
        const uint32_t clock_divisor = ((mmio[0x7050 - kMmioBase] & 0x000e) >> 1) + 1;
        return horizontal_clocks * clock_divisor;
    }

    uint32_t tft_total_lines() const {
        // V_Width is the final zero-based line number.  The stock value
        // 0x10f therefore describes 272 lines; V_Start=31 and V_End=271
        // delimit the expected 240-line active region.
        const uint32_t final_line = mmio[0x7051 - kMmioBase];
        return final_line ? final_line + 1 : 262u;
    }

    uint32_t tft_scanline() const {
        return uint32_t((cycles / tft_cycles_per_line()) % tft_total_lines());
    }

    void schedule_next_video_edge() {
        const uint64_t cycles_per_line = tft_cycles_per_line();
        const uint64_t total_lines = tft_total_lines();
        const uint64_t frame_cycles = cycles_per_line * total_lines;
        const uint64_t frame_position = cycles % frame_cycles;
        const uint64_t compare_position =
            uint64_t(mmio[0x7054 - kMmioBase] % total_lines) * cycles_per_line;
        const uint64_t to_wrap = frame_cycles - frame_position;
        uint64_t to_compare = compare_position > frame_position
            ? compare_position - frame_position
            : frame_cycles - frame_position + compare_position;
        if (to_compare == 0) to_compare = frame_cycles;
        next_video_edge_cycles = cycles + std::min(to_wrap, to_compare);
    }

    void update_video_edges() {
        if (next_video_edge_cycles != 0 && cycles < next_video_edge_cycles) return;

        const uint64_t cycles_per_line = tft_cycles_per_line();
        const uint64_t total_lines = tft_total_lines();
        const uint64_t frame_cycles = cycles_per_line * total_lines;
        const uint64_t frame_position = cycles % frame_cycles;
        const uint32_t compare_line = mmio[0x7054 - kMmioBase] % total_lines;
        const uint64_t compare_position = uint64_t(compare_line) * cycles_per_line;
        const bool compare_is_latest = compare_line != 0 && frame_position >= compare_position;

        last_video_scanline = uint32_t(frame_position / cycles_per_line);
        if (compare_is_latest) {
            // The target observes vertical-blank and frame-end together at
            // the programmed comparison line. Both are sticky until the next
            // frame wrap or a W1C acknowledgement.
            mmio[0x7063 - kMmioBase] |= 0x0801;
            mmio[0x705a - kMmioBase] |= 0x0002;
        } else {
            mmio[0x7063 - kMmioBase] &= ~uint16_t(0x0801);
        }
        if (g_log && cycles >= 0x30000000ull && late_video_edge_log_count++ < 256) {
            g_log << "VIDEO STATUS EDGE "
                  << (compare_is_latest ? "vblank-frame-end" : "frame-wrap")
                  << " pc=0x" << std::hex << pc_for_log
                  << " enable=0x" << mmio[0x7062 - kMmioBase]
                  << " status=0x" << mmio[0x7063 - kMmioBase]
                  << " scanline=0x" << last_video_scanline
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        schedule_next_video_edge();
    }

    uint16_t read_mmio(uint32_t addr) {
        update_periodic_events();
        if (addr >= 0x7100 && addr <= 0x71ff) {
            if (mmio[0x707e - kMmioBase] & 1) return tx3_transform_ram[addr - 0x7100];
            return rowscroll_ram[addr - 0x7100];
        }
        if (addr >= 0x7200 && addr <= 0x72ff) {
            if (mmio[0x707e - kMmioBase] & 1) return tx3_transform_ram[0x100 + addr - 0x7200];
            return rowzoom_ram[addr - 0x7200];
        }
        if (addr >= 0x7300 && addr <= 0x73ff) {
            const uint32_t bank = (mmio[0x703a - kMmioBase] & 0x000c) << 6;
            return palette_ram[(addr - 0x7300) | bank];
        }
        if (addr >= 0x7400 && addr <= 0x77ff) {
            const uint32_t bank = (mmio[0x707e - kMmioBase] & 1) ? 0x400 : 0;
            return sprite_ram[(addr - 0x7400) | bank];
        }
        switch (addr) {
        case 0x7038: return uint16_t(tft_scanline());
        case 0x703a:
        case 0x703c:
        case 0x7042:
        case 0x707e:
        case 0x707f:
            return mmio[addr - kMmioBase];
        case 0x7050:
            // 0x7050 is TFT_Ctrl; 0x705a is the TFT status register.
            {
                const uint16_t raw = mmio[addr - kMmioBase];
                if (g_log && (tft_status_read_log_count++ < 256 ||
                              (pc_for_log >= 0x069400 && pc_for_log < 0x069500))) {
                    g_log << "TFT CTRL READ pc=0x" << std::hex << pc_for_log
                          << " addr=0x" << addr
                          << " raw=0x" << raw
                          << " value=0x" << raw
                          << " cycles=0x" << cycles << std::dec << "\n";
                }
                return raw;
            }
        case 0x7051: return 0x03ff; // MAME GPL162xx returns this for TFT/STN clip/status probing
        case 0x7052:
        case 0x7056:
        case 0x705b:
        case 0x705c:
            // These display-status fields are polled in the frame-base path.
            // The verified boot snapshots treat them as ready-like bits.
            return mmio[addr - kMmioBase] | 0x0001;
        case 0x705a:
            // GPL16250V headers define P_TFT_Status bit 1 as the frame
            // interrupt flag and bit 0 as frame interrupt enable/status.
            return mmio[addr - kMmioBase];
        case 0x7062:
        case 0x7063:
            // MAME/reference behavior: this is the latched video IRQ status.
            // Status generation is gated by 0x7062 enable bits; writes to this
            // register acknowledge bits with write-one-to-clear semantics.
            log_post_ppu_video("READ", addr, mmio[addr - kMmioBase], mmio[addr - kMmioBase]);
            if (g_log && (video_status_read_log_count++ < 64 ||
                          (should_log_late_video_status() && late_video_status_log_count++ < 256))) {
                g_log << "VIDEO STATUS READ pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr
                      << " value=0x" << mmio[addr - kMmioBase]
                      << " enable=0x" << mmio[0x7062 - kMmioBase]
                      << " status=0x" << mmio[0x7063 - kMmioBase]
                      << " scanline=0x" << tft_scanline()
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            return mmio[addr - kMmioBase];
        case 0x7072: return 0x0000; // video DMA not busy
        case 0x707c:
            // Bit 15 is the firmware-polled ready bit. A write to bit 0 starts
            // the frame-base PPU render job, which the main loop services at
            // the next instruction boundary.
            {
                const uint16_t value = uint16_t(mmio[addr - kMmioBase] |
                                                (ppu_go_pending ? 0x0000 : 0x8000));
                log_post_ppu_video("READ", addr, value, mmio[addr - kMmioBase]);
                if (g_log && fb_ppu_go_log_count++ < 2048) {
                    g_log << "PPU_GO READ pc=0x" << std::hex << pc_for_log
                          << " value=0x" << value
                          << " raw=0x" << mmio[addr - kMmioBase]
                          << " pending=" << (ppu_go_pending ? 1 : 0)
                          << " due=0x" << ppu_go_due_cycles
                          << " wait_b7=0x" << mem[0x09b7]
                          << " wait_b8=0x" << mem[0x09b8]
                          << " wait_c6=0x" << mem[0x09c6]
                          << " wait_c7=0x" << mem[0x09c7]
                          << " cycles=0x" << cycles << std::dec << "\n";
                }
                return value;
            }
        case 0x70e0: return uint16_t((cycles * 1103515245u + 12345u) & 0x7fff); // deterministic PRNG/status
        case 0x7850: return mmio[addr - kMmioBase] | 0x8000; // NAND ready
        case 0x7854: return nand.read_data();
        case 0x7820: case 0x7821: case 0x7822: case 0x7823: case 0x7824:
        case 0x7825: case 0x7826: case 0x7827: case 0x7828: case 0x7829:
        case 0x782a: case 0x782b: case 0x782c: case 0x782d: case 0x782e:
        case 0x782f:
            return mmio[addr - kMmioBase];
        case 0x7830: case 0x7831: case 0x7832: case 0x7833:
            return mmio[addr - kMmioBase];
        case 0x7834: case 0x7835: case 0x7836: case 0x7837: case 0x7838:
        case 0x7839: case 0x783a: case 0x783b: case 0x783c: case 0x783d:
        case 0x783e: case 0x783f:
        case 0x7840: case 0x7841:
            return mmio[addr - kMmioBase];
        case 0x7858:
            return mmio[addr - kMmioBase];
        case 0x784e:
        case 0x784f:
        case 0x785e:
        case 0x785f:
            // Documented no-error encoding: 2ERR=0, 1ERR=0,
            // FAILBIT=3, FAILLINE=0xff.
            return 0x03ff;
        case 0x7870:
            // ASSUMPTION: GPIO-C bit 8 gates the ROM's complete SPI flash
            // identification path. The MobiGo 2 has the flash populated, so
            // represent the active-low presence/boot strap as asserted.
            {
                const uint16_t value = gpio_pad_read(addr, gpio_c_input);
                log_gpio_read(addr, value, gpio_c_input);
                return value;
            }
        case 0x7880:
            {
                const uint16_t input = motion_adjusted_gpio_e(
                    touch_adjusted_gpio_e(gpio_e_input));
                const uint16_t value = gpio_pad_read(addr, input);
                log_gpio_read(addr, value, input);
                return value;
            }
        case 0x7860:
            // ASSUMPTION: MobiGo 2 cold boot begins while the active-low
            // power key on GPIO-A bit 15 is held. Firmware masks exactly
            // 0x8000 at 0x0368bc immediately before handing off to the
            // SPI-loaded module. Input timing is not modeled yet, so retain
            // the asserted level through startup.
            {
                const uint16_t input = matrix_adjusted_input(addr, gpio_a_input);
                const uint16_t value = gpio_pad_read(addr, input);
                log_gpio_read(addr, value, input);
                return value;
            }
        case 0x7868:
            // GPL16250 datasheet boot pins: BM0/IOB0 must be pulled low,
            // BM1/IOB1 high selects internal-ROM boot, and BM2/IOB2 high
            // selects the internal PLL used by handheld applications.
            {
                const uint16_t input = matrix_adjusted_input(addr, gpio_b_input);
                const uint16_t value = gpio_pad_read(addr, input);
                log_gpio_read(addr, value, input);
                return value;
            }
        case 0x7878:
            {
                const uint16_t value = gpio_pad_read(addr, gpio_d_input);
                log_gpio_read(addr, value, gpio_d_input);
                return value;
            }
        case 0x7861: case 0x7862: case 0x7863: case 0x7864:
        case 0x7869: case 0x786a: case 0x786b: case 0x786c: case 0x786d:
            if ((addr == 0x786a || addr == 0x786b) &&
                pc_for_log >= 0x069000 && gpiob_aux_read_log_count++ < 1024 && g_log) {
                g_log << "GPIO-B AUX READ pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr
                      << " value=0x" << mmio[addr - kMmioBase]
                      << " input=0x" << gpio_b_input << std::dec << "\n";
            }
            return mmio[addr - kMmioBase];
        case 0x7871: case 0x7872: case 0x7873: case 0x7874: case 0x7875: case 0x7876: case 0x7877:
        case 0x7879: case 0x787a: case 0x787b: case 0x787c: case 0x787d: case 0x787e: case 0x787f:
        case 0x7881: case 0x7882: case 0x7883: case 0x7884:
        case 0x7888: case 0x7889: case 0x788a: case 0x788b: case 0x788c: case 0x788d: case 0x788e: case 0x788f:
            return mmio[addr - kMmioBase];
        case 0x7a32:
            return uint16_t((mmio[addr - kMmioBase] & uint16_t(~0x0003)) |
                            (usb_host_connected ? 0x0002 :
                             (usb_transceiver_enabled() ? 0x0003 : 0x0000)));
        case 0x7a33: {
            if (usb_ep0_fifo.empty()) return 0;
            const uint8_t byte = usb_ep0_fifo.front();
            usb_ep0_fifo.pop_front();
            return byte;
        }
        case 0x7a35: {
            if (usb_bulk_out_fifo.empty()) return 0;
            const uint8_t byte = usb_bulk_out_fifo.front();
            usb_bulk_out_fifo.pop_front();
            if (usb_bulk_out_fifo.empty() && (mmio[0x7a3d - kMmioBase] & 0x0008)) {
                mmio[0x7a37 - kMmioBase] &= uint16_t(~0x1000);
                mmio[0x7a37 - kMmioBase] |= 0x0800;
            }
            return byte;
        }
        case 0x7a37:
            return mmio[addr - kMmioBase];
        case 0x7a38: {
            const uint16_t flags = mmio[0x7a3a - kMmioBase];
            uint16_t global = 0;
            if (flags & 0x007f) global |= 0x0001;
            if (flags & 0x0180) global |= 0x0002;
            if (flags & 0x0600) global |= 0x0004;
            if (flags & 0x1800) global |= 0x0008;
            if (flags & 0xe000) global |= 0x0010;
            if (mmio[0x7a3c - kMmioBase]) global |= 0x0020;
            return global;
        }
        case 0x7a3a: case 0x7a3c:
            return mmio[addr - kMmioBase];
        case 0x7a41:
            return uint16_t(usb_ep0_fifo.size() & 0x000f);
        case 0x7a42:
            return uint16_t(usb_bulk_out_fifo.size() & 0x007f);
        case 0x7a43:
            return uint16_t((usb_ep0_fifo.size() & 7) << 3);
        case 0x7a44:
            return uint16_t((usb_bulk_in_fifo.size() & 0xff) << 8);
        case 0x7a45:
            return uint16_t((usb_bulk_out_fifo.size() & 0xff) << 8);
        case 0x7a30: case 0x7a31: case 0x7a34: case 0x7a36:
        case 0x7a39: case 0x7a3b: case 0x7a3d: case 0x7a3e: case 0x7a3f:
        case 0x7a40: case 0x7a46: case 0x7a47: case 0x7a48: case 0x7a49:
        case 0x7a4a: case 0x7a50: case 0x7a51: case 0x7a52: case 0x7a53:
        case 0x7a54: case 0x7a57: case 0x7a58: case 0x7a59:
            return mmio[addr - kMmioBase];
        case 0x78a0:
            if (g_log && (pc_for_log >= 0x069000 || cycles >= 0x30000000ull) &&
                int_status1_read_log_count++ < 2048) {
                g_log << "INT_STATUS1 READ pc=0x" << std::hex << pc_for_log
                      << " value=0x" << int_status1_value()
                      << " raw=0x" << mmio[addr - kMmioBase]
                      << " viden=0x" << mmio[0x7062 - kMmioBase]
                      << " vidst=0x" << mmio[0x7063 - kMmioBase]
                      << " tft=0x" << mmio[0x7050 - kMmioBase]
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            return int_status1_value();
        case 0x78a1:
            if (pc_for_log >= 0x069000 && timer_status_read_log_count++ < 1024 && g_log) {
                g_log << "INT_STATUS2 READ pc=0x" << std::hex << pc_for_log
                      << " value=0x" << int_status2_value()
                      << " raw=0x" << mmio[addr - kMmioBase]
                      << " tba=0x" << mmio[0x78b0 - kMmioBase]
                      << " tbb=0x" << mmio[0x78b1 - kMmioBase]
                      << " tbc=0x" << mmio[0x78b2 - kMmioBase]
                      << std::dec << "\n";
            }
            return int_status2_value();
        case 0x78a2:
            return mmio[addr - kMmioBase];
        case 0x78a3:
            return int_status3_value();
        case 0x78a4:
        case 0x78a5:
        case 0x78a6:
            return mmio[addr - kMmioBase];
        case 0x780f:
            // The documented active states follow P_Clock_Ctrl: C32K selects
            // the 32768 Hz state regardless of FAST; otherwise FAST selects
            // the fast PLL or the reset/12 MHz slow state. Clock changes are
            // short compared with an interpreted instruction, so reads expose
            // the resulting stable state.
            if (mmio[0x7807 - kMmioBase] & 0x4000) return 0x0003;
            return (mmio[0x7807 - kMmioBase] & 0x8000) ? 0x0002 : 0x0001;
        case 0x7800:
        case 0x7803:
        case 0x7804:
        case 0x7805:
        case 0x7808:
        case 0x780a:
        case 0x780b:
        case 0x780c:
        case 0x780d:
        case 0x780e:
            return mmio[addr - kMmioBase];
        case 0x7806:
            if (g_log && system_ctrl_read_log_count++ < 256) {
                g_log << "SYSTEM CTRL READ pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr
                      << " value=0x" << mmio[addr - kMmioBase]
                      << " reset_count=" << std::dec << power_reset_count << "\n";
            }
            return mmio[addr - kMmioBase];
        case 0x7807:
            // P_Clock_Ctrl[2:0] is the documented SYSCLK divider. Clock-change
            // state is reported by P_Power_State at 0x780f, not synthesized in
            // this register.
            {
                const uint16_t raw = mmio[addr - kMmioBase];
                if (g_log && system_ctrl_read_log_count++ < 256) {
                    g_log << "SYSTEM CTRL READ pc=0x" << std::hex << pc_for_log
                          << " addr=0x" << addr
                          << " raw=0x" << raw
                          << " value=0x" << raw
                          << " reset_count=" << std::dec << power_reset_count << "\n";
                }
                return raw;
            }
        case 0x7810:
            if (g_log && system_ctrl_read_log_count++ < 256) {
                g_log << "SYSTEM CTRL READ pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr
                      << " value=0x" << mmio[addr - kMmioBase]
                      << " reset_count=" << std::dec << power_reset_count << "\n";
            }
            return mmio[addr - kMmioBase];
        case 0x7817:
            return mmio[addr - kMmioBase];
        case 0x7818:
        case 0x781f:
            return mmio[addr - kMmioBase];
        case 0x7819:
            // Firmware writes cache setup 0x001c, then command 0x0002 and
            // polls until bit 1 clears. The emulator does not cache CPU
            // accesses, so invalidate/maintenance is complete immediately.
            // The command-bit meaning is inferred from this firmware loop;
            // the GPL16250 register list only identifies Cache_Ctrl by name.
            {
                const uint16_t value = mmio[addr - kMmioBase] & uint16_t(~0x0002);
                if (g_log && system_ctrl_read_log_count++ < 256) {
                    g_log << "SYSTEM CTRL READ pc=0x" << std::hex << pc_for_log
                          << " addr=0x" << addr
                          << " raw=0x" << mmio[addr - kMmioBase]
                          << " value=0x" << value
                          << " reset_count=" << std::dec << power_reset_count << "\n";
                }
                return value;
            }
        case 0x78c0:
        case 0x78c1:
        case 0x78c2:
        case 0x78c3:
        case 0x78c4:
        case 0x78c8:
        case 0x78c9:
        case 0x78ca:
        case 0x78cb:
        case 0x78cc:
        case 0x78d1:
        case 0x78d2:
        case 0x78d3:
        case 0x78d4:
        case 0x78d9:
        case 0x78da:
        case 0x78db:
        case 0x78dc:
        case 0x78e0:
        case 0x78e1:
        case 0x78e2:
        case 0x78e3:
        case 0x78e4:
        case 0x78e8:
        case 0x78e9:
        case 0x78ea:
        case 0x78eb:
        case 0x78ec: return mmio[addr - kMmioBase];
        case 0x78b0:
        case 0x78b1:
        case 0x78b2:
        case 0x78b8:
        case 0x78d0:
        case 0x78d8:
            if (pc_for_log >= 0x069000 && timer_status_read_log_count++ < 1024 && g_log) {
                g_log << "TIMER READ pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr
                      << " value=0x" << mmio[addr - kMmioBase]
                      << " int2=0x" << int_status2_value()
                      << std::dec << "\n";
            }
            return mmio[addr - kMmioBase];
        case 0x78f0:
        case 0x78f8:
            return read_audio_ctrl(addr);
        case 0x78f1:
        case 0x78f9:
            return mmio[addr - kMmioBase];
        case 0x78f2:
            return read_audio_fifo(addr, audio_fifo_level_a);
        case 0x78fa:
            return read_audio_fifo(addr, audio_fifo_level_b);
        case 0x78fd:
        case 0x78fe:
        case 0x78ff:
            return mmio[addr - kMmioBase];
        case 0x7920:
        case 0x7921:
        case 0x7922:
        case 0x7924:
        case 0x7925:
        case 0x7926:
        case 0x7934:
        case 0x7935:
        case 0x7936:
            if (g_log && (pc_for_log >= 0x069000 || cycles >= 0x30000000ull) &&
                rtc_log_count++ < 1024) {
                g_log << "RTC READ pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr
                      << " value=0x" << mmio[addr - kMmioBase]
                      << " int2=0x" << int_status2_value()
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            return mmio[addr - kMmioBase];
        case 0x7940: return mmio[addr - kMmioBase];
        case 0x7941: return 0x0007; // ASSUMPTION: SPI TX FIFO idle/ready.
        case 0x7943: return 0x0007; // MAME GP_SPISPI reference has this status value.
        case 0x7944: return spi.read_rx(pc_for_log);
        case 0x7945: return 0x0000; // ASSUMPTION: SPI misc flags idle.
        case 0x7960:
            return mmio[addr - kMmioBase];
        case 0x7961:
            return mmio[addr - kMmioBase];
        case 0x7962:
            return adc_manual_data;
        case 0x7963:
            // P_ASADC_Ctrl: no HQADC samples are generated yet, so FIFO level,
            // full, overflow, and interrupt flags remain clear.
            return mmio[addr - kMmioBase] & uint16_t(~0xb01f);
        case 0x7964:
            return adc_auto_data;
        case 0x7965:
            return mmio[addr - kMmioBase] & uint16_t(~0x9000);
        case 0x7970:
        case 0x7971:
        case 0x7972:
        case 0x7973:
            return mmio[addr - kMmioBase];
        case 0x79e0: case 0x79e1: case 0x79e2: case 0x79e3: case 0x79e4:
        case 0x79e5: case 0x79e6: case 0x79e8: case 0x79e9: case 0x79ea:
            return mmio[addr - kMmioBase]; // SD2 controller registers.
        case 0x79e7:
            return mmio[addr - kMmioBase]; // SD2_Status.
        case 0x7abe:
            return mmio[addr - kMmioBase]; // DMA source memtype select
        case 0x7abf:
            return mmio[addr - kMmioBase]; // DMA complete/status flags
        case 0x7ae2: return mmio[addr - kMmioBase]; // E-Fuse2 boot configuration, decoded by internal ROM
        default:
            if (addr >= 0x7a80 && addr <= 0x7abf) return mmio[addr - kMmioBase];
            if (addr >= 0x7b80 && addr <= 0x7bbf) return mmio[addr - kMmioBase];
            if (g_log && logged_unknown_reads.insert(addr).second) {
                g_log << "UNKNOWN MMIO READ pc=" << std::hex << pc_for_log
                      << " addr=" << addr << " -> last=" << mmio[addr - kMmioBase] << std::dec << "\n";
            }
            return mmio[addr - kMmioBase];
        }
    }

    uint64_t system_clock_hz() const {
        const uint16_t clock_ctrl = mmio[0x7807 - kMmioBase];
        uint64_t source_hz = 12000000ull;
        if (clock_ctrl & 0x4000) {
            source_hz = 32768ull;
        } else if (clock_ctrl & 0x8000) {
            // The verified GPF16001A clock table defines P_PLLChange=N as
            // N*3 MHz when C_FastPLL is selected.
            source_hz = uint64_t(mmio[0x7817 - kMmioBase] & 0x007f) * 3000000ull;
        }
        source_hz >>= (clock_ctrl & 0x0007);
        return std::max<uint64_t>(1, source_hz);
    }

    static uint64_t accumulate_source_ticks(uint64_t &phase, uint64_t elapsed,
                                            uint64_t source_hz, uint64_t clock_hz) {
        if (source_hz == 0 || elapsed == 0) return 0;
        const unsigned __int128 total = static_cast<unsigned __int128>(phase) +
            static_cast<unsigned __int128>(elapsed) * source_hz;
        const uint64_t ticks = uint64_t(total / clock_hz);
        phase = uint64_t(total % clock_hz);
        return ticks;
    }

    static uint64_t cycles_until_source_ticks(uint64_t ticks, uint64_t phase,
                                               uint64_t source_hz,
                                               uint64_t clock_hz) {
        if (ticks == 0 || source_hz == 0) return UINT64_MAX;
        const unsigned __int128 target =
            static_cast<unsigned __int128>(ticks) * clock_hz;
        if (target <= phase) return 1;
        const unsigned __int128 remaining = target - phase;
        return uint64_t((remaining + source_hz - 1) / source_hz);
    }

    uint64_t watchdog_period_cycles(uint16_t ctrl) const {
        const uint64_t clock_hz = system_clock_hz();
        switch (ctrl & 0x0007) {
        case 0: return clock_hz * 2;
        case 1: return clock_hz;
        case 2: return clock_hz / 2;
        case 3: return clock_hz / 4;
        case 4:
        case 6: return clock_hz / 8;
        case 5:
        case 7: return uint64_t((static_cast<unsigned __int128>(clock_hz) * 125) / 2);
        default: return clock_hz;
        }
    }

    void configure_mba_watchdog_handoff(uint32_t entry_address) {
        configure_mba_application_target(entry_address, true);
    }

    void configure_mba_application_target(uint32_t entry_address,
                                          bool arm_watchdog) {
        mba_application_entry_pinned = true;
        observe_mba_application_entry(entry_address);
        if (!arm_watchdog) {
            mba_watchdog_entry = UINT32_MAX;
            mba_watchdog_handoff_pending = false;
            return;
        }
        mba_watchdog_entry = entry_address & kAddrMask;
        mba_watchdog_handoff_pending = true;
    }

    void observe_mba_application_entry(uint32_t entry_address) {
        mba_application_entry = entry_address & kAddrMask;
        mba_application_handoff_pending = true;
        mba_application_active = false;
        mba_entry_stack_valid = false;
    }

    void maybe_begin_mba_application(uint32_t pc, uint32_t stack_address) {
        if (!mba_application_handoff_pending ||
            (pc & kAddrMask) != mba_application_entry)
            return;
        mba_application_handoff_pending = false;
        mba_application_active = true;
        mba_entry_stack_valid = true;
        mba_entry_stack_address = stack_address & kAddrMask;
        ++mba_launch_count;
        if (g_log) {
            g_log << "MBA APPLICATION ENTRY entry=0x" << std::hex
                  << mba_application_entry << " stack=0x"
                  << mba_entry_stack_address << " cycles=0x" << cycles
                  << std::dec << "\n";
        }
    }

    void maybe_arm_mba_watchdog_handoff(uint32_t pc, uint32_t = 0) {
        if (!mba_watchdog_handoff_pending || (pc & kAddrMask) != mba_watchdog_entry)
            return;
        // An explicitly substituted MBA starts with the firmware's short
        // system watchdog still active. It must feed or disable it at startup.
        mba_watchdog_handoff_pending = false;
        write(0x780a, 0x8000);
        if (g_log) {
            g_log << "MBA WATCHDOG HANDOFF entry=0x" << std::hex << mba_watchdog_entry
                  << " stack=0x" << mba_entry_stack_address
                  << " ctrl=0x8000 cycles=0x" << cycles << std::dec << "\n";
        }
    }

    bool mba_entry_return_stack(uint32_t stack_address) const {
        return mba_application_active && mba_entry_stack_valid &&
               (stack_address & kAddrMask) == mba_entry_stack_address;
    }

    void note_mba_application_return(uint32_t return_address) {
        if (!mba_application_active) return;
        mba_application_active = false;
        mba_entry_stack_valid = false;
        ++mba_return_count;
        // Returning from a G1 MBA entry is an application exit back into LD.
        // The loader may launch the same replacement again, so re-arm entry
        // recognition without changing the already-running watchdog.
        mba_application_handoff_pending = mba_application_entry != UINT32_MAX;
        mba_watchdog_handoff_pending =
            mba_watchdog_entry != UINT32_MAX &&
            mba_watchdog_entry == mba_application_entry;
        if (g_log) {
            g_log << "MBA APPLICATION RETURN target=0x" << std::hex
                  << (return_address & kAddrMask)
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
    }

    uint64_t timer_source_a_hz(uint16_t ctrl, uint64_t clock_hz) const {
        const uint16_t src = ctrl & 0x000f;
        switch (src) {
        case 0x0: return clock_hz / 2;   // SYSCLK/2
        case 0x1: return clock_hz / 256; // SYSCLK/256
        case 0x2: return 32768;
        case 0x3: return 8192;
        case 0x4: return 4096;
        case 0x5:
            return 0;         // Static logic high, used to gate source B through.
        case 0x6:
            return 0;         // Timer(X+1) overflow cascade is event-driven.
        case 0x7:
            return 0;         // EXTA prescaler source is event-driven.
        case 0x8:
            return 0;         // Logic low.
        default:
            return 0;         // Reserved.
        }
    }

    uint64_t timer_source_b_hz(uint16_t ctrl) const {
        const uint16_t src = (ctrl >> 4) & 0x0007;
        switch (src) {
        case 0x0: return 2048;
        case 0x1: return 1024;
        case 0x2: return 256;
        case 0x3:
        {
            const uint16_t tbb = mmio[0x78b1 - kMmioBase];
            if ((tbb & 0x2000) == 0) return 0;
            static constexpr std::array<uint32_t, 4> hz{8, 16, 32, 64};
            return hz[tbb & 3];
        }
        case 0x4:
        {
            const uint16_t tba = mmio[0x78b0 - kMmioBase];
            if ((tba & 0x2000) == 0) return 0;
            // Selector zero is reserved on TimeBase A.
            static constexpr std::array<uint32_t, 4> hz{0, 1, 2, 4};
            return hz[tba & 3];
        }
        case 0x5:
            return 0;         // Logic low.
        case 0x6:
            return 0;         // Static logic high, used to gate source A through.
        case 0x7:
            return 0;         // EXTB prescaler source is event-driven.
        default:
            return 0;
        }
    }

    uint64_t timer_effective_hz(uint16_t ctrl, uint64_t clock_hz) const {
        const uint16_t src_a = ctrl & 0x000f;
        const uint16_t src_b = (ctrl >> 4) & 0x0007;
        if (src_a == 0x8 || src_b == 0x5) return 0; // Either source forced low blocks the timer clock.
        if (src_a == 0x5) return timer_source_b_hz(ctrl);
        if (src_b == 0x6) return timer_source_a_hz(ctrl, clock_hz);
        return timer_source_a_hz(ctrl, clock_hz);
    }

    void refresh_timer_any_enabled() {
        timer_any_enabled =
            (mmio[0x78c0 - kMmioBase] & 0x2000) ||
            (mmio[0x78c8 - kMmioBase] & 0x2000) ||
            (mmio[0x78d0 - kMmioBase] & 0x2000) ||
            (mmio[0x78d8 - kMmioBase] & 0x2000);
    }

    void schedule_next_periodic_event(uint64_t clock_hz) {
        uint64_t next = next_video_edge_cycles;
        const auto consider_delta = [&](uint64_t delta) {
            if (delta == UINT64_MAX) return;
            const uint64_t due = cycles + std::max<uint64_t>(1, delta);
            next = next == 0 ? due : std::min(next, due);
        };
        const auto consider_absolute = [&](uint64_t due) {
            if (due == 0 || due <= cycles) return;
            next = next == 0 ? due : std::min(next, due);
        };

        if (adc_manual_pending) consider_absolute(adc_manual_due_cycles);
        if (watchdog_enabled) consider_absolute(watchdog_expire_cycles);
        if (usb_device_enabled_cycle != UINT64_MAX &&
            !usb_host_connected && !usb_suspend_latched)
            consider_absolute(usb_device_enabled_cycle + 4096);

        const uint16_t spu_counter = mmio[0x7b85 - kMmioBase];
        const uint64_t spu_base = mmio[0x7b84 - kMmioBase] & 0x07ff;
        if ((spu_counter & 0xc000) == 0x8000 && spu_base != 0) {
            const uint64_t count = (spu_counter & 0x3fff) ?
                (spu_counter & 0x3fff) : 1;
            const uint64_t required = spu_base * count * 4;
            if (spu_beat_elapsed_ticks < required) {
                consider_delta(cycles_until_source_ticks(
                    required - spu_beat_elapsed_ticks, spu_beat_phase_accum,
                    281250, clock_hz));
            }
        }

        static constexpr std::array<uint32_t, 4> ctrl_addrs{
            0x78c0, 0x78c8, 0x78d0, 0x78d8};
        static constexpr std::array<uint32_t, 4> upcount_addrs{
            0x78c4, 0x78cc, 0x78d4, 0x78dc};
        for (uint32_t i = 0; i < ctrl_addrs.size(); ++i) {
            const uint16_t ctrl = mmio[ctrl_addrs[i] - kMmioBase];
            if ((ctrl & 0xa000) != 0x2000) continue;
            const uint64_t source_hz = timer_effective_hz(ctrl, clock_hz);
            const uint64_t remaining =
                0x10000ull - mmio[upcount_addrs[i] - kMmioBase];
            consider_delta(cycles_until_source_ticks(
                remaining, timer_phase_accum[i], source_hz, clock_hz));
        }

        static constexpr std::array<std::array<uint16_t, 4>, 3> timebase_hz{{
            {{0, 1, 2, 4}}, {{8, 16, 32, 64}}, {{128, 256, 512, 1024}}
        }};
        for (uint32_t i = 0; i < timebase_phase_accum.size(); ++i) {
            const uint16_t ctrl = mmio[0x78b0 + i - kMmioBase];
            if ((ctrl & 0xa000) != 0x2000) continue;
            consider_delta(cycles_until_source_ticks(
                1, timebase_phase_accum[i], timebase_hz[i][ctrl & 3], clock_hz));
        }

        const uint16_t rtc_ctrl = mmio[0x7934 - kMmioBase];
        if (rtc_ctrl & 0x8000) {
            consider_delta(cycles_until_source_ticks(
                1, rtc_phase_accum, 1, clock_hz));
        }
        if ((rtc_ctrl & 0x0100) && !(mmio[0x7935 - kMmioBase] & 0x0100)) {
            static constexpr std::array<uint32_t, 8> scheduler_hz{
                16, 32, 64, 128, 256, 512, 1024, 2048};
            consider_delta(cycles_until_source_ticks(
                1, scheduler_phase_accum, scheduler_hz[rtc_ctrl & 7], clock_hz));
        }
        next_periodic_event_cycles = next;
    }

    void update_periodic_events(bool force = true) {
        if (!force && next_periodic_event_cycles != 0 &&
            cycles < next_periodic_event_cycles) return;
        if (last_periodic_update_cycles == cycles) return;
        last_periodic_update_cycles = cycles;
        if (adc_manual_pending && cycles >= adc_manual_due_cycles) {
            adc_manual_pending = false;
            uint16_t sample = 0;
            if (adc_manual_channel == 0) sample = battery_adc;
            else if (touch_pressed && adc_manual_channel == 3) sample = touch_adc_x;
            else if (touch_pressed && adc_manual_channel == 2) sample = touch_adc_y;
            adc_manual_data = uint16_t(sample << 4);
            uint16_t &ctrl = mmio[0x7961 - kMmioBase];
            // Conversion completion always raises ADCRIF; ADCRIEN gates CPU
            // interrupt delivery, not the hardware status flag itself.
            ctrl |= 0x8080;
        }
        const uint64_t clock_hz = system_clock_hz();
        if (periodic_clock_hz == 0) {
            periodic_clock_hz = clock_hz;
        } else if (periodic_clock_hz != clock_hz) {
            // Preserve the fractional position of clocks derived independently
            // of SYSCLK when firmware changes PLL/divider settings.
            const uint64_t old_clock_hz = periodic_clock_hz;
            const auto rescale_phase = [old_clock_hz, clock_hz](uint64_t &phase) {
                phase = uint64_t((static_cast<unsigned __int128>(phase) * clock_hz) /
                                 old_clock_hz);
            };
            for (uint64_t &phase : timer_phase_accum) rescale_phase(phase);
            for (uint64_t &phase : timebase_phase_accum) rescale_phase(phase);
            rescale_phase(scheduler_phase_accum);
            rescale_phase(spu_beat_phase_accum);
            periodic_clock_hz = clock_hz;
        }
        if (last_spu_beat_cycles == UINT64_MAX) {
            last_spu_beat_cycles = cycles;
        }
        const uint64_t spu_beat_elapsed = cycles - last_spu_beat_cycles;
        last_spu_beat_cycles = cycles;
        uint16_t &spu_beat_counter = mmio[0x7b85 - kMmioBase];
        const uint16_t spu_beat_base = mmio[0x7b84 - kMmioBase] & 0x07ff;
        const uint16_t spu_beat_count = spu_beat_counter & 0x3fff;
        if ((spu_beat_counter & 0x8000) == 0 ||
            (spu_beat_counter & 0x4000) != 0 ||
            spu_beat_base == 0) {
            spu_beat_phase_accum = 0;
            spu_beat_elapsed_ticks = 0;
        } else {
            // The Generalplus SPU examples define one beat-base unit as four
            // 281.25 kHz service frames. Beat status latches until firmware
            // acknowledges bit 14 or programs the next count.
            spu_beat_elapsed_ticks += accumulate_source_ticks(
                spu_beat_phase_accum,
                spu_beat_elapsed,
                281250,
                clock_hz);
            // A zero count is the shortest interval, not a disabled timer.
            // The resident intentionally leaves 0x8000 programmed while idle
            // so a later music object is noticed by the next heartbeat.
            const uint64_t effective_count = spu_beat_count ? spu_beat_count : 1;
            const uint64_t required_ticks =
                uint64_t(spu_beat_base) * effective_count * 4;
            if (spu_beat_elapsed_ticks >= required_ticks) {
                spu_beat_counter |= 0x4000;
                spu_beat_phase_accum = 0;
                spu_beat_elapsed_ticks = 0;
            }
        }
        if (timer_any_enabled) {
            if (last_timer_cycles == UINT64_MAX) last_timer_cycles = cycles;
            const uint64_t elapsed = cycles - last_timer_cycles;
            if (elapsed != 0) {
                last_timer_cycles = cycles;
                static constexpr std::array<uint32_t, 4> ctrl_addrs{0x78c0, 0x78c8, 0x78d0, 0x78d8};
                static constexpr std::array<uint32_t, 4> preload_addrs{0x78c2, 0x78ca, 0x78d2, 0x78da};
                static constexpr std::array<uint32_t, 4> upcount_addrs{0x78c4, 0x78cc, 0x78d4, 0x78dc};
                for (uint32_t i = 0; i < ctrl_addrs.size(); ++i) {
                    uint16_t &ctrl = mmio[ctrl_addrs[i] - kMmioBase];
                    if ((ctrl & 0x2000) == 0) continue;
                    const uint64_t source_hz = timer_effective_hz(ctrl, clock_hz);
                    const uint64_t increments = accumulate_source_ticks(
                        timer_phase_accum[i], elapsed, source_hz, clock_hz);
                    if (increments == 0) continue;
                    uint64_t count = uint64_t(mmio[upcount_addrs[i] - kMmioBase]) + increments;
                    if (count >= 0x10000) {
                        const uint64_t preload = mmio[preload_addrs[i] - kMmioBase];
                        const uint64_t period = std::max<uint64_t>(1, 0x10000ull - preload);
                        count = preload + ((count - 0x10000ull) % period);
                        ctrl |= 0x8000;
                        if (g_log && cycles >= 0x39d00000ull && late_timer_overflow_log_count++ < 2048) {
                            g_log << "LATE TIMER OVERFLOW index=" << std::dec << i
                                  << " pc=0x" << std::hex << pc_for_log
                                  << " ctrl=0x" << ctrl
                                  << " preload=0x" << preload
                                  << " next=0x" << count
                                  << " inc=0x" << increments
                                  << " int2=0x" << int_status2_value()
                                  << " pri2=0x" << mmio[0x78a5 - kMmioBase]
                                  << " viden=0x" << mmio[0x7062 - kMmioBase]
                                  << " vidst=0x" << mmio[0x7063 - kMmioBase]
                                  << " cycles=0x" << cycles << std::dec << "\n";
                        }
                    }
                    mmio[upcount_addrs[i] - kMmioBase] = uint16_t(count);
                }
            }
        } else {
            last_timer_cycles = cycles;
        }
        if (last_timebase_cycles == UINT64_MAX) last_timebase_cycles = cycles;
        const uint64_t timebase_elapsed = cycles - last_timebase_cycles;
        last_timebase_cycles = cycles;
        static constexpr std::array<std::array<uint16_t, 4>, 3> timebase_hz{{
            {{0, 1, 2, 4}},
            {{8, 16, 32, 64}},
            {{128, 256, 512, 1024}}
        }};
        for (uint32_t i = 0; i < timebase_phase_accum.size(); ++i) {
            uint16_t &ctrl = mmio[0x78b0 + i - kMmioBase];
            if ((ctrl & 0x2000) == 0) continue;
            const uint64_t ticks = accumulate_source_ticks(
                timebase_phase_accum[i], timebase_elapsed,
                timebase_hz[i][ctrl & 0x0003], clock_hz);
            if (ticks != 0) ctrl |= 0x8000;
        }
        if (last_scheduler_cycles == UINT64_MAX) last_scheduler_cycles = cycles;
        const uint64_t scheduler_elapsed = cycles - last_scheduler_cycles;
        last_scheduler_cycles = cycles;
        const uint16_t rtc_ctrl = mmio[0x7934 - kMmioBase];
        if (last_rtc_cycles == UINT64_MAX) last_rtc_cycles = cycles;
        const uint64_t rtc_elapsed = cycles - last_rtc_cycles;
        last_rtc_cycles = cycles;
        if (rtc_ctrl & 0x8000) {
            const uint64_t seconds = accumulate_source_ticks(
                rtc_phase_accum, rtc_elapsed, 1, clock_hz);
            if (seconds != 0) {
                uint64_t total = uint64_t(mmio[0x7920 - kMmioBase] % 60) + seconds;
                const uint64_t minute_carry = total / 60;
                mmio[0x7920 - kMmioBase] = uint16_t(total % 60);
                mmio[0x7935 - kMmioBase] |= 0x0002;
                if (minute_carry != 0) {
                    total = uint64_t(mmio[0x7921 - kMmioBase] % 60) + minute_carry;
                    const uint64_t hour_carry = total / 60;
                    mmio[0x7921 - kMmioBase] = uint16_t(total % 60);
                    mmio[0x7935 - kMmioBase] |= 0x0004;
                    if (hour_carry != 0) {
                        mmio[0x7922 - kMmioBase] =
                            uint16_t((uint64_t(mmio[0x7922 - kMmioBase] % 24) +
                                      hour_carry) % 24);
                        mmio[0x7935 - kMmioBase] |= 0x0008;
                    }
                }
                if ((rtc_ctrl & 0x0400) &&
                    mmio[0x7920 - kMmioBase] == mmio[0x7924 - kMmioBase] &&
                    mmio[0x7921 - kMmioBase] == mmio[0x7925 - kMmioBase] &&
                    mmio[0x7922 - kMmioBase] == mmio[0x7926 - kMmioBase]) {
                    mmio[0x7935 - kMmioBase] |= 0x0400;
                }
            }
        } else {
            rtc_phase_accum = 0;
        }
        if (rtc_ctrl & 0x0100) {
            static constexpr std::array<uint32_t, 8> scheduler_hz{
                16, 32, 64, 128, 256, 512, 1024, 2048
            };
            const uint64_t ticks = accumulate_source_ticks(
                scheduler_phase_accum, scheduler_elapsed,
                scheduler_hz[rtc_ctrl & 0x0007], clock_hz);
            if (ticks != 0) {
                // RTC_INT_Status bit 8 is SCHIF/C. If SCHIEN is set in
                // RTC_INT_Ctrl, this flag is also visible as P_INT_Status2 bit 2
                // and asserts unSP IRQ6.
                mmio[0x7935 - kMmioBase] |= 0x0100;
            }
        } else {
            scheduler_phase_accum = 0;
        }
        // Scanline reads remain cycle-exact, while status work is performed
        // only at the two hardware-visible edges per frame. This removes a
        // division/modulo pair from the interpreter's per-instruction path.
        update_video_edges();
        // ASSUMPTION: With the USB device enabled but no host attached, the
        // controller reports bus suspend after a short interval. The ROM
        // explicitly handles USBD_INT bit 0x20 and otherwise waits forever.
        // Model it as a one-shot event; a future USB host model must replace
        // this with actual line-state timing.
        if (usb_host_connected && usb_transceiver_enabled() && !usb_enumerated) {
            usb_host_enumerate();
        }
        if (usb_device_enabled_cycle != UINT64_MAX &&
            !usb_host_connected && !usb_suspend_latched &&
            cycles - usb_device_enabled_cycle >= 4096) {
            mmio[0x7a3a - kMmioBase] |= 0x0020;
            usb_suspend_latched = true;
            if (g_log) g_log << "USB device suspend event latched (no host)\n";
        }
        if (watchdog_enabled && watchdog_expire_cycles != 0 && cycles >= watchdog_expire_cycles) {
            watchdog_enabled = false;
            watchdog_expire_cycles = 0;
            mmio[0x7806 - kMmioBase] |= 0x0010;
            system_reset_requested = true;
            system_reset_preserve_memory = watchdog_reset_cpu_only;
            if (g_log) {
                g_log << "WATCHDOG RESET EXPIRED pc=0x" << std::hex << pc_for_log
                      << " ctrl=0x" << mmio[0x780a - kMmioBase]
                      << " target=" << (watchdog_reset_cpu_only ? "cpu" : "system")
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
        }
        schedule_next_periodic_event(clock_hz);
    }

    uint16_t int_status2_value() const {
        uint16_t value = 0;
        if (mmio[0x78c0 - kMmioBase] & 0x8000) value |= 0x1000;
        if (mmio[0x78c8 - kMmioBase] & 0x8000) value |= 0x2000;
        if (mmio[0x78d0 - kMmioBase] & 0x8000) value |= 0x4000;
        if (mmio[0x78d8 - kMmioBase] & 0x8000) value |= 0x8000;
        if (mmio[0x78b0 - kMmioBase] & 0x8000) value |= 0x0100;
        if (mmio[0x78b1 - kMmioBase] & 0x8000) value |= 0x0200;
        if (mmio[0x78b2 - kMmioBase] & 0x8000) value |= 0x0400;
        if (mmio[0x7935 - kMmioBase] & 0x050f) value |= 0x0004;
        return value;
    }

    uint16_t int_status3_value() const {
        uint16_t value = 0;
        const uint16_t channel_low = mmio[0x7b83 - kMmioBase] &
                                     mmio[0x7b82 - kMmioBase];
        const uint16_t channel_high = mmio[0x7ba3 - kMmioBase] &
                                      mmio[0x7ba2 - kMmioBase];
        if (channel_low || channel_high) value |= 0x0001;
        if (mmio[0x7b97 - kMmioBase] || mmio[0x7bb7 - kMmioBase]) value |= 0x0002;
        if (mmio[0x7b85 - kMmioBase] & 0x4000) value |= 0x0004;
        return value;
    }

    uint16_t int_status1_value() const {
        uint16_t value = mmio[0x78a0 - kMmioBase];
        // P_INT_Status1 bit 2 is the aggregate DMA interrupt source. The
        // per-channel completion flags live in P_DMA_INT.
        if (dma_irq_pending_no_update()) value |= 0x0004;
        if (usb_irq3_asserted_no_update()) value |= 0x0008;
        // The channel's FIFO-empty latch is visible in P_CHA/B_Ctrl at reset,
        // but the aggregate interrupt source must remain inactive until both
        // the latch and that channel's FIFO interrupt enable are set.
        if ((mmio[0x78f0 - kMmioBase] & 0xc000) == 0xc000) value |= 0x0010;
        if ((mmio[0x78f8 - kMmioBase] & 0xc000) == 0xc000) value |= 0x0020;
        if (mmio[0x7961 - kMmioBase] & 0x8000) value |= 0x4000;
        return value;
    }

    bool audio_irq0_asserted_no_update() const {
        const uint16_t priority = mmio[0x78a4 - kMmioBase];
        const bool channel_a = (mmio[0x78f0 - kMmioBase] & 0xc000) == 0xc000 &&
                               (priority & 0x0010) == 0;
        const bool channel_b = (mmio[0x78f8 - kMmioBase] & 0xc000) == 0xc000 &&
                               (priority & 0x0020) == 0;
        return channel_a || channel_b;
    }

    bool audio_fiq_asserted_no_update() const {
        const uint16_t priority = mmio[0x78a4 - kMmioBase];
        const uint16_t spu_status = int_status3_value();
        const uint16_t spu_priority = mmio[0x78a6 - kMmioBase];
        const bool channel_a = (mmio[0x78f0 - kMmioBase] & 0xc000) == 0xc000 &&
                               (priority & 0x0010) != 0;
        const bool channel_b = (mmio[0x78f8 - kMmioBase] & 0xc000) == 0xc000 &&
                               (priority & 0x0020) != 0;
        const bool spu = (spu_status & spu_priority & 0x0007) != 0;
        return channel_a || channel_b || spu;
    }

    bool adc_irq1_asserted_no_update() const {
        const uint16_t priority = mmio[0x78a4 - kMmioBase];
        const uint16_t madc = mmio[0x7961 - kMmioBase];
        return (madc & 0xc000) == 0xc000 && !(priority & 0x4000);
    }

    bool dma_irq_pending_no_update() const {
        uint16_t enabled_flags = 0;
        for (uint32_t ch = 0; ch < 4; ++ch) {
            const uint32_t base = 0x7a80 + ch * 8;
            if (mmio[base - kMmioBase] & 0x0100) enabled_flags |= uint16_t(1u << ch);
        }
        return (mmio[0x7abf - kMmioBase] & enabled_flags & 0x000f) != 0;
    }

    bool dma_irq3_asserted_no_update() const {
        // The GPF16001A interrupt table routes DMA Transfer to IRQ3 unless
        // C_INT_DMA is selected as FIQ in P_INT_Priority1.
        return dma_irq_pending_no_update() && ((mmio[0x78a4 - kMmioBase] & 0x0004) == 0);
    }

    bool video_irq_asserted_no_update() {
        const uint16_t control = mmio[0x7062 - kMmioBase];
        const uint16_t pending = control & mmio[0x7063 - kMmioBase];
        if (g_log && cycles >= 0x39d00000ull && post_ppu_irq_log_count++ < 32) {
            g_log << "POST PPU IRQCHECK pc=0x" << std::hex << pc_for_log
                  << " pending=0x" << pending
                  << " enable=0x" << mmio[0x7062 - kMmioBase]
                  << " status=0x" << mmio[0x7063 - kMmioBase]
                  << " tft_status=0x" << mmio[0x705a - kMmioBase]
                  << " ppu_go=0x" << mmio[0x707c - kMmioBase]
                  << " scanline=0x" << tft_scanline()
                  << " b7=0x" << mem[0x09b7]
                  << " b8=0x" << mem[0x09b8]
                  << " b9=0x" << mem[0x09b9]
                  << " ba=0x" << mem[0x09ba]
                  << " c6=0x" << mem[0x09c6]
                  << " c7=0x" << mem[0x09c7]
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        return pending != 0;
    }

    bool video_irq_asserted() {
        update_periodic_events();
        return video_irq_asserted_no_update();
    }

    bool should_log_late_video_status() const {
        if (cycles < 0x30000000ull) return false;
        if (pc_for_log >= 0x69440 && pc_for_log <= 0x69470) return true;
        if (pc_for_log >= 0x63500 && pc_for_log <= 0x63700) return true;
        if (pc_for_log >= 0x30000 && pc_for_log <= 0x30060) return true;
        return false;
    }

    void log_post_ppu_video(const char *op, uint32_t addr, uint16_t value, uint16_t raw) {
        if (!g_log || cycles < 0x39d00000ull || post_ppu_video_log_count++ >= 4096) return;
        g_log << "POST PPU VIDEO " << op
              << " pc=0x" << std::hex << pc_for_log
              << " addr=0x" << addr
              << " value=0x" << value
              << " raw=0x" << raw
              << " enable=0x" << mmio[0x7062 - kMmioBase]
              << " status=0x" << mmio[0x7063 - kMmioBase]
              << " ppu_go=0x" << mmio[0x707c - kMmioBase]
              << " pending=" << (ppu_go_pending ? 1 : 0)
              << " scanline=0x" << tft_scanline()
              << " b7=0x" << mem[0x09b7]
              << " b8=0x" << mem[0x09b8]
              << " b9=0x" << mem[0x09b9]
              << " ba=0x" << mem[0x09ba]
              << " c6=0x" << mem[0x09c6]
              << " c7=0x" << mem[0x09c7]
              << " cycles=0x" << cycles << std::dec << "\n";
    }

    bool timer_irq4_asserted_no_update() const {
        const uint16_t spu_status = int_status3_value() & 0x0007;
        const uint16_t spu_priority = mmio[0x78a6 - kMmioBase];
        if ((spu_status & uint16_t(~spu_priority)) != 0) {
            return true;
        }
        static constexpr std::array<uint32_t, 4> ctrl_addrs{0x78c0, 0x78c8, 0x78d0, 0x78d8};
        const uint16_t priority2 = mmio[0x78a5 - kMmioBase];
        for (uint32_t i = 0; i < ctrl_addrs.size(); ++i) {
            const uint16_t ctrl = mmio[ctrl_addrs[i] - kMmioBase];
            const uint16_t source_bit = uint16_t(0x1000u << i);
            if ((ctrl & 0xc000) == 0xc000 && (priority2 & source_bit) == 0) return true;
        }
        return false;
    }

    bool timer_irq4_asserted() {
        update_periodic_events();
        return timer_irq4_asserted_no_update();
    }

    bool irq6_asserted_no_update() const {
        const uint16_t status2 = int_status2_value();
        const uint16_t tbc = mmio[0x78b2 - kMmioBase];
        const bool tbc_irq_enabled = (tbc & 0x6000) == 0x6000;
        if ((status2 & 0x0400) && tbc_irq_enabled) return true;
        if (mmio[0x7935 - kMmioBase] & mmio[0x7936 - kMmioBase] & 0x050f) return true;
        return false;
    }

    bool irq6_asserted() {
        update_periodic_events();
        return irq6_asserted_no_update();
    }

    void write_mmio(uint32_t addr, uint16_t value) {
        // Any peripheral write can change a divisor, enable, status latch, or
        // deadline. The invalidation must happen after the pre-write state is
        // synchronized, including on the many early-return register paths.
        struct DeadlineInvalidator {
            uint64_t &deadline;
            ~DeadlineInvalidator() { deadline = 0; }
        } deadline_invalidator{next_periodic_event_cycles};
        const bool key_video_write =
            (addr == 0x7062 && value != mmio[addr - kMmioBase]) ||
            (addr >= 0x7078 && addr <= 0x707f) ||
            (addr >= 0x7000 && addr <= 0x702f && (value != mmio[addr - kMmioBase]));
        if (g_log && ((addr >= 0x7000 && addr <= 0x7088 && ppu_write_log_count++ < 2048) ||
                      key_video_write)) {
            g_log << "PPU WRITE cycles=0x" << std::hex << cycles
                  << " pc=0x" << pc_for_log << " addr=0x" << addr
                  << " data=0x" << value << std::dec << "\n";
        }
        if (g_log && addr >= 0x78a0 && addr <= 0x78a5) {
            g_log << "INTC WRITE cycles=0x" << std::hex << cycles
                  << " pc=0x" << pc_for_log << " addr=0x" << addr
                  << " old=0x" << mmio[addr - kMmioBase]
                  << " data=0x" << value << std::dec << "\n";
        }
        if (addr >= 0x7a30 && addr <= 0x7a59 && g_log) {
            g_log << "USB MMIO WRITE cycles=0x" << std::hex << cycles
                  << " pc=0x" << pc_for_log << " addr=0x" << addr
                  << " old=0x" << mmio[addr - kMmioBase]
                  << " data=0x" << value << std::dec << "\n";
        }
        if (addr >= 0x7100 && addr <= 0x71ff) {
            const bool transform_bank = (mmio[0x707e - kMmioBase] & 1) != 0;
            if (g_log && ppu_ram_write_log_count++ < 2048) {
                g_log << "PPU RAM WRITE cycles=0x" << std::hex << cycles
                      << " pc=0x" << pc_for_log
                      << " addr=0x" << addr
                      << " bank=0x" << mmio[0x707e - kMmioBase]
                      << " target=" << (transform_bank ? "tx3-cos-sin-lo" : "tx-hvoffset")
                      << " data=0x" << value << std::dec << "\n";
            }
            if (transform_bank) tx3_transform_ram[addr - 0x7100] = value;
            else rowscroll_ram[addr - 0x7100] = value;
            return;
        }
        if (addr >= 0x7200 && addr <= 0x72ff) {
            const bool transform_bank = (mmio[0x707e - kMmioBase] & 1) != 0;
            if (g_log && ppu_ram_write_log_count++ < 2048) {
                g_log << "PPU RAM WRITE cycles=0x" << std::hex << cycles
                      << " pc=0x" << pc_for_log
                      << " addr=0x" << addr
                      << " bank=0x" << mmio[0x707e - kMmioBase]
                      << " target=" << (transform_bank ? "tx3-cos-sin-hi" : "hcm-value")
                      << " data=0x" << value << std::dec << "\n";
            }
            if (transform_bank) tx3_transform_ram[0x100 + addr - 0x7200] = value;
            else rowzoom_ram[addr - 0x7200] = value;
            return;
        }
        if (addr >= 0x7300 && addr <= 0x73ff) {
            const uint32_t bank = (mmio[0x703a - kMmioBase] & 0x000c) << 6;
            palette_ram[(addr - 0x7300) | bank] = value;
            return;
        }
        if (addr >= 0x7400 && addr <= 0x77ff) {
            const uint32_t bank = (mmio[0x707e - kMmioBase] & 1) ? 0x400 : 0;
            sprite_ram[(addr - 0x7400) | bank] = value;
            return;
        }
        if (addr == 0x7a31) {
            if (value & 0x0800) {
                usb_ep0_fifo.clear();
                usb_bulk_out_fifo.clear();
                usb_bulk_in_fifo.clear();
                usb_interrupt_in_fifo.clear();
                mmio[0x7a37 - kMmioBase] = 0;
            }
            // SRST is a write control; address/configuration are read-only.
            mmio[addr - kMmioBase] = uint16_t((mmio[addr - kMmioBase] & 0x01ff) |
                                              (value & 0x0600));
            if (value & 0x0600) {
                usb_dma_programmed_bytes =
                    (uint32_t(mmio[0x7a51 - kMmioBase] & 0x00ff) << 16) |
                    mmio[0x7a50 - kMmioBase];
                // The controller count is expressed in 16-bit DMA transfers
                // on this bus even though the endpoint traffic is byte-based.
                usb_dma_programmed_bytes *= 2;
                usb_dma_transferred_bytes = 0;
                usb_dma_update_ack((usb_dma_programmed_bytes + 63) / 64);
            }
            return;
        }
        if (addr == 0x7a52) {
            // Documented: a write resets DMAWC and DMAACK.
            mmio[0x7a50 - kMmioBase] = 0;
            mmio[0x7a51 - kMmioBase] = 0;
            usb_dma_programmed_bytes = 0;
            usb_dma_transferred_bytes = 0;
            usb_dma_update_ack(0);
            return;
        }
        if (addr == 0x7a59) {
            uint16_t &reg = mmio[addr - kMmioBase];
            if (value & 0x0001) reg &= uint16_t(~0x0001); // W1C DMAINTF
            if (value & 0x0002) reg |= 0x0002;            // enable
            if (value & 0x0004) reg &= uint16_t(~0x0002); // disable
            return;
        }
        if (addr == 0x7a33) {
            if (usb_ep0_fifo.size() < 8) usb_ep0_fifo.push_back(uint8_t(value));
            if (usb_ep0_fifo.size() == 8 && (mmio[0x7a3d - kMmioBase] & 0x0002))
                mmio[0x7a37 - kMmioBase] |= 0x0010;
            return;
        }
        if (addr == 0x7a34) {
            if (usb_bulk_in_fifo.size() < 64) usb_bulk_in_fifo.push_back(uint8_t(value));
            if (usb_bulk_in_fifo.size() == 64 && (mmio[0x7a3d - kMmioBase] & 0x0004))
                mmio[0x7a37 - kMmioBase] |= 0x0100;
            return;
        }
        if (addr == 0x7a36) {
            if (usb_interrupt_in_fifo.size() < 8) usb_interrupt_in_fifo.push_back(uint8_t(value));
            if (usb_interrupt_in_fifo.size() == 8 && (mmio[0x7a3d - kMmioBase] & 0x0010))
                mmio[0x7a37 - kMmioBase] |= 0x4000;
            return;
        }
        if (addr == 0x7a37) {
            uint16_t &events = mmio[addr - kMmioBase];
            static constexpr uint16_t w1c = 0xb6ad;
            static constexpr uint16_t settable = 0x4952;
            events &= uint16_t(~(value & w1c));
            events |= value & settable;
            return;
        }
        if (addr == 0x7a3c) {
            mmio[addr - kMmioBase] &= uint16_t(~value);
            return;
        }
        if (addr == 0x7a3f) {
            if (value & 0x0001) usb_ep0_fifo.clear();
            if (value & 0x0002) usb_bulk_in_fifo.clear();
            if (value & 0x0004) usb_bulk_out_fifo.clear();
            if (value & 0x0008) usb_interrupt_in_fifo.clear();
            return;
        }
        if (addr == 0x7a40) {
            uint16_t &events = mmio[0x7a37 - kMmioBase];
            if (value & 0x0001) events &= uint16_t(~0x0002);
            if (value & 0x0002) events &= uint16_t(~0x0010);
            if (value & 0x0004) events &= uint16_t(~0x0040);
            if (value & 0x0008) events &= uint16_t(~0x0200);
            if (value & 0x0010) events &= uint16_t(~0x0800);
            if (value & 0x0020) events &= uint16_t(~0x4000);
            return;
        }
        if ((addr >= 0x7a41 && addr <= 0x7a4a) || addr == 0x7a54) {
            // FIFO counts/pointers and setup fields are hardware-owned.
            return;
        }
        if (addr == 0x7063 || addr == 0x79e7 || addr == 0x7a3a ||
            addr == 0x7abf) {
            // SD2_Status is cleared this way by the internal ROM before each
            // command. This W1C behavior is inferred from that access pattern;
            // the available GPL16250 register list names the register but does
            // not document its individual bits.
            if (addr == 0x7063 && g_log &&
                (video_status_read_log_count++ < 2048 ||
                 (should_log_late_video_status() && late_video_status_log_count++ < 4096))) {
                g_log << "VIDEO STATUS W1C pc=0x" << std::hex << pc_for_log
                      << " value=0x" << value
                      << " before=0x" << mmio[addr - kMmioBase]
                      << " enable=0x" << mmio[0x7062 - kMmioBase]
                      << " scanline=0x" << tft_scanline()
                      << " wait_b7=0x" << mem[0x09b7]
                      << " wait_b8=0x" << mem[0x09b8]
                      << " wait_b9=0x" << mem[0x09b9]
                      << " wait_ba=0x" << mem[0x09ba]
                      << " wait_c6=0x" << mem[0x09c6]
                      << " wait_c7=0x" << mem[0x09c7]
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            if (addr == 0x7063) log_post_ppu_video("W1C", addr, value, mmio[addr - kMmioBase]);
            if (addr == 0x7063) {
                // P_TFT_INT_CLR bit 0 acknowledges the TFT frame interrupt
                // latch (also reported by P_TFT_Status bit 1). PPU status
                // bits written in their native positions retain normal W1C
                // behavior; bit 11 also drops naturally at frame wrap.
                if (value & 0x0001) {
                    mmio[addr - kMmioBase] &= ~uint16_t(0x0001);
                    mmio[0x705a - kMmioBase] &= ~uint16_t(0x0002);
                }
                mmio[addr - kMmioBase] &= ~uint16_t(value & ~uint16_t(0x0001));
            } else {
                mmio[addr - kMmioBase] &= ~value;
            }
            return;
        }
        if (addr == 0x7963) {
            // P_ASADC_Ctrl: ASIF is W1C; FIFO status/level bits are hardware
            // read-only in this minimal no-sample model.
            mmio[addr - kMmioBase] = value & uint16_t(~0xb01f);
            return;
        }
        if (addr == 0x7965) {
            // P_TP_Ctrl: TPIF is W1C and TPST is read-only. Preserve enable,
            // mode, debounce, and debounce timing configuration.
            mmio[addr - kMmioBase] = value & uint16_t(~0x9000);
            return;
        }
        if (addr == 0x780a) {
            // P_Watchdog_Ctrl. The verified Generalplus programming guide
            // documents bit 15 as watchdog enable and bit 14 as reset target:
            // 0 = system reset, 1 = CPU reset.
            mmio[addr - kMmioBase] = value;
            if (value & 0x8000) {
                watchdog_enabled = true;
                watchdog_reset_cpu_only = (value & 0x4000) != 0;
                watchdog_expire_cycles = cycles + watchdog_period_cycles(value);
                if (g_log) {
                    g_log << "WATCHDOG CONFIG pc=0x" << std::hex << pc_for_log
                          << " value=0x" << value
                          << " target=" << (watchdog_reset_cpu_only ? "cpu" : "system")
                          << " expire=0x" << watchdog_expire_cycles
                          << std::dec << "\n";
                }
            } else {
                watchdog_enabled = false;
                watchdog_reset_cpu_only = false;
                watchdog_expire_cycles = 0;
                if (g_log) {
                    g_log << "WATCHDOG DISABLED pc=0x" << std::hex << pc_for_log
                          << " cycles=0x" << cycles << std::dec << "\n";
                }
            }
            return;
        }
        if (addr == 0x7806) {
            // P_Reset_Flag event bits are write-one-to-clear. Hardware latches
            // watchdog/LVR/protect reset causes across the reset event so boot
            // code can distinguish reset paths.
            mmio[addr - kMmioBase] &= uint16_t(~value);
            return;
        }
        if (addr == 0x780b) {
            mmio[addr - kMmioBase] = value;
            if (value == 0xa005) {
                watchdog_expire_cycles = 0;
                if (watchdog_enabled) {
                    watchdog_expire_cycles = cycles +
                        watchdog_period_cycles(mmio[0x780a - kMmioBase]);
                }
                if (g_log) {
                    g_log << "WATCHDOG CLEAR pc=0x" << std::hex << pc_for_log
                          << " cycles=0x" << cycles << std::dec << "\n";
                }
            } else {
                system_reset_requested = true;
                system_reset_preserve_memory = true;
                if (g_log) {
                    g_log << "WATCHDOG CLEAR CPU RESET pc=0x" << std::hex << pc_for_log
                          << " value=0x" << value << std::dec << "\n";
                }
            }
            return;
        }
        if (addr == 0x707c) {
            // P_FB_PPU_GO: bit 0 starts a PPU render into the frame-base
            // output buffer. MAME's GPL162xx comments identify bit 15 as the
            // completion/ready bit read by firmware.
            if (g_log && fb_ppu_go_log_count++ < 2048) {
                g_log << "PPU_GO WRITE pc=0x" << std::hex << pc_for_log
                      << " value=0x" << value
                      << " before=0x" << mmio[addr - kMmioBase]
                      << " due=0x" << ppu_go_due_cycles
                      << " wait_b7=0x" << mem[0x09b7]
                      << " wait_b8=0x" << mem[0x09b8]
                      << " wait_c6=0x" << mem[0x09c6]
                      << " wait_c7=0x" << mem[0x09c7]
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            log_post_ppu_video("WRITE", addr, value, mmio[addr - kMmioBase]);
            mmio[addr - kMmioBase] = value & uint16_t(~0x8000);
            if (value & 0x0001) {
                ppu_go_pending = true;
                ppu_go_due_cycles = cycles + kPpuJobCycles;
            }
            return;
        }
        if (addr == 0x7961) {
            // P_MADC_Ctrl: bit 15 is W1C conversion-ready interrupt flag, bit
            // 6 starts a manual conversion, and bit 7 reports completion.
            uint16_t stored = value & uint16_t(~0x80c0);
            if (!(value & 0x8000)) stored |= mmio[addr - kMmioBase] & 0x8000;
            if (value & 0x0040) {
                adc_manual_pending = true;
                adc_manual_channel = uint8_t(value & 7);
                adc_manual_due_cycles = cycles +
                    adc_conversion_cycles(mmio[0x7960 - kMmioBase]);
            } else {
                stored |= mmio[addr - kMmioBase] & 0x0080;
            }
            mmio[addr - kMmioBase] = stored;
            return;
        }
        if (addr == 0x78a0 || addr == 0x78a1 || addr == 0x78a3) {
            if (addr == 0x78a1 && cycles >= 0x2f000000ull &&
                late_timer_ack_log_count++ < 2048 && g_log) {
                g_log << "LATE INT_STATUS2 W1C pc=0x" << std::hex << pc_for_log
                      << " value=0x" << value
                      << " before=0x" << int_status2_value()
                      << " ta=0x" << mmio[0x78c0 - kMmioBase]
                      << " tb=0x" << mmio[0x78c8 - kMmioBase]
                      << " tc=0x" << mmio[0x78d0 - kMmioBase]
                      << " td=0x" << mmio[0x78d8 - kMmioBase]
                      << " b7=0x" << mem[0x09b7]
                      << " b8=0x" << mem[0x09b8]
                      << " c6=0x" << mem[0x09c6]
                      << " c7=0x" << mem[0x09c7]
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            mmio[addr - kMmioBase] &= ~value;
            if (addr == 0x78a0) {
                if (value & 0x0004) mmio[0x7abf - kMmioBase] &= ~uint16_t(0x000f);
            }
            if (addr == 0x78a1) {
                // P_INT_Status2 reports TimerD/C/B/A in bits 15/14/13/12.
                // The status register is derived, but firmware still
                // acknowledges sources through it with write-one-to-clear.
                if (value & 0x1000) mmio[0x78c0 - kMmioBase] &= ~uint16_t(0x8000);
                if (value & 0x2000) mmio[0x78c8 - kMmioBase] &= ~uint16_t(0x8000);
                if (value & 0x4000) mmio[0x78d0 - kMmioBase] &= ~uint16_t(0x8000);
                if (value & 0x8000) mmio[0x78d8 - kMmioBase] &= ~uint16_t(0x8000);
                if (value & 0x0400) mmio[0x78b2 - kMmioBase] &= ~uint16_t(0x8000);
                if (value & 0x0004) mmio[0x7935 - kMmioBase] &= ~uint16_t(0x050f);
            }
            if (addr == 0x78a3 && (value & 0x0004)) {
                mmio[0x7b85 - kMmioBase] &= ~uint16_t(0x4000);
                last_spu_beat_cycles = cycles;
                spu_beat_phase_accum = 0;
                spu_beat_elapsed_ticks = 0;
            }
            return;
        }
        if (addr >= 0x78a2 && addr <= 0x78a8) {
            mmio[addr - kMmioBase] = value;
            if (g_log && int_control_log_count++ < 2048) {
                g_log << "INT CONTROL WRITE pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr
                      << " value=0x" << value
                      << " pri1=0x" << mmio[0x78a4 - kMmioBase]
                      << " pri2=0x" << mmio[0x78a5 - kMmioBase]
                      << " int1=0x" << int_status1_value()
                      << " int2=0x" << int_status2_value()
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            return;
        }
        if (addr == 0x78b0 || addr == 0x78b1 || addr == 0x78b2) {
            update_periodic_events();
            const uint16_t old_ctrl = mmio[addr - kMmioBase];
            if (value & 0x8000) {
                static constexpr std::array<uint16_t, 3> bits{0x0100, 0x0200, 0x0400};
                mmio[0x78a1 - kMmioBase] &= ~bits[addr - 0x78b0];
            }
            uint16_t stored = uint16_t((value & 0x7fff) | (old_ctrl & 0x8000));
            if (value & 0x8000) stored &= ~uint16_t(0x8000);
            mmio[addr - kMmioBase] = stored;
            if ((old_ctrl ^ stored) & 0x3fff) {
                timebase_phase_accum[addr - 0x78b0] = 0;
            }
            if (g_log) {
                g_log << "TIMEBASE write pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr << " data=0x" << value
                      << " stored=0x" << mmio[addr - kMmioBase] << std::dec << "\n";
            }
            return;
        }
        if (addr == 0x78b8) {
            if (value == 0x5555) {
                last_timebase_cycles = cycles;
                timebase_phase_accum.fill(0);
            }
            mmio[addr - kMmioBase] = value;
            return;
        }
        if (addr == 0x7935) {
            mmio[addr - kMmioBase] &= ~value;
            if ((mmio[addr - kMmioBase] & 0x050f) == 0)
                mmio[0x78a1 - kMmioBase] &= ~uint16_t(0x0004);
            if (g_log && rtc_log_count++ < 1024) {
                g_log << "RTC W1C pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr
                      << " value=0x" << value
                      << " stored=0x" << mmio[addr - kMmioBase]
                      << " int2=0x" << int_status2_value()
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            return;
        }
        if (addr == 0x7934 || addr == 0x7936) {
            if (addr == 0x7934) {
                update_periodic_events();
                scheduler_phase_accum = 0;
            }
            mmio[addr - kMmioBase] = value;
            if (g_log && rtc_log_count++ < 1024) {
                g_log << "RTC WRITE pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr
                      << " value=0x" << value
                      << " ctrl=0x" << mmio[0x7934 - kMmioBase]
                      << " int_status=0x" << mmio[0x7935 - kMmioBase]
                      << " int_ctrl=0x" << mmio[0x7936 - kMmioBase]
                      << " int2=0x" << int_status2_value()
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            return;
        }
        if ((addr >= 0x7920 && addr <= 0x7922) ||
            (addr >= 0x7924 && addr <= 0x7926)) {
            update_periodic_events();
            if (addr == 0x7920 || addr == 0x7924) value %= 60;
            if (addr == 0x7921 || addr == 0x7925) value %= 60;
            if (addr == 0x7922 || addr == 0x7926) value %= 24;
            mmio[addr - kMmioBase] = value;
            if (addr <= 0x7922) rtc_phase_accum = 0;
            return;
        }
        if (addr == 0x78c0 || addr == 0x78c8 || addr == 0x78d0 ||
            addr == 0x78d8 || addr == 0x78e0 || addr == 0x78e8) {
            update_periodic_events();
            const uint16_t old_ctrl = mmio[addr - kMmioBase];
            // Generalplus timer documentation: bit 15 is the overflow/event
            // flag and writing one clears it. The remaining control fields are
            // ordinary R/W configuration bits.
            if (value & 0x8000) {
                if (addr >= 0x78c0 && addr <= 0x78d8) {
                    const uint32_t index = (addr - 0x78c0) / 8;
                    if (index < 4) mmio[0x78a1 - kMmioBase] &= ~uint16_t(0x1000u << index);
                }
            }
            uint16_t stored = uint16_t((value & 0x7fff) | (old_ctrl & 0x8000));
            if (value & 0x8000) stored &= ~uint16_t(0x8000);
            mmio[addr - kMmioBase] = stored;
            if (addr >= 0x78c0 && addr <= 0x78d8) {
                const uint32_t index = (addr - 0x78c0) / 8;
                const uint16_t new_ctrl = mmio[addr - kMmioBase];
                if ((old_ctrl ^ new_ctrl) & 0x3fff) timer_phase_accum[index] = 0;
                if ((old_ctrl & 0x2000) == 0 && (new_ctrl & 0x2000) != 0) {
                    // The documented first up-count starts at the preload
                    // value; subsequent overflows synchronously reload it.
                    mmio[addr + 4 - kMmioBase] = mmio[addr + 2 - kMmioBase];
                }
                refresh_timer_any_enabled();
            }
            if (g_log && int_control_log_count++ < 2048) {
                g_log << "TIMER WRITE pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr
                      << " value=0x" << value
                      << " stored=0x" << mmio[addr - kMmioBase]
                      << " int2=0x" << int_status2_value()
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            return;
        }
        if (addr == 0x78f0 || addr == 0x78f8) {
            // DAC channel control: bit 15 is the documented FIFO-empty W1C
            // flag. A write of zero must not clear the reset/empty flag.
            if (addr == 0x78f8 &&
                ((mmio[addr - kMmioBase] ^ value) & 0x1c00) != 0) {
                audio_shared_fifo_next_b = false;
            }
            write_audio_ctrl(addr, value);
            if (g_log) {
                g_log << "AUDIO CTRL WRITE pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr << " value=0x" << value
                      << " stored=0x" << mmio[addr - kMmioBase]
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            return;
        }
        if (addr == 0x78f1) {
            // With CHB SSF+CHACFG enabled, the manuals define CHA_Data as a
            // shared feed: stereo writes alternate A/B, while mono duplicates
            // each sample to both FIFOs.
            mmio[addr - kMmioBase] = value;
            const uint16_t chb_ctrl = mmio[0x78f8 - kMmioBase];
            if ((chb_ctrl & 0x1800) == 0x1800) {
                if (chb_ctrl & 0x0400) {
                    write_audio_data(0x78f1, value, audio_fifo_level_a,
                                     audio_fifo_a, 0x78f0);
                    write_audio_data(0x78f9, value, audio_fifo_level_b,
                                     audio_fifo_b, 0x78f8);
                } else if (audio_shared_fifo_next_b) {
                    write_audio_data(0x78f9, value, audio_fifo_level_b,
                                     audio_fifo_b, 0x78f8);
                    audio_shared_fifo_next_b = false;
                } else {
                    write_audio_data(0x78f1, value, audio_fifo_level_a,
                                     audio_fifo_a, 0x78f0);
                    audio_shared_fifo_next_b = true;
                }
            } else {
                audio_shared_fifo_next_b = false;
                write_audio_data(addr, value, audio_fifo_level_a,
                                 audio_fifo_a, 0x78f0);
            }
            return;
        }
        if (addr == 0x78f9) {
            write_audio_data(addr, value, audio_fifo_level_b, audio_fifo_b, 0x78f8);
            return;
        }
        if (addr == 0x78f2) {
            if (value & 0x0100) audio_shared_fifo_next_b = false;
            write_audio_fifo(addr, value, audio_fifo_level_a, audio_fifo_a, 0x78f0);
            return;
        }
        if (addr == 0x78fa) {
            write_audio_fifo(addr, value, audio_fifo_level_b, audio_fifo_b, 0x78f8);
            return;
        }
        if (addr == 0x78fb || addr == 0x78fc ||
            addr == 0x78fd || addr == 0x78fe || addr == 0x78ff) {
            mmio[addr - kMmioBase] = value;
            if (g_log) {
                g_log << "AUDIO ANALOG WRITE pc=0x" << std::hex << pc_for_log
                      << " addr=0x" << addr << " value=0x" << value
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
            return;
        }
        if (addr >= 0x7b80 && addr <= 0x7bbf) {
            write_spu_control(addr, value);
            return;
        }
        if (addr == 0x7807 || addr == 0x7817) {
            // Account for elapsed peripheral time using the old clock before
            // changing either the SYSCLK selector/divider or PLL multiplier.
            update_periodic_events();
            mmio[addr - kMmioBase] = value;
            ++clock_change_generation;
            return;
        }
        if ((addr == 0x7a80 || addr == 0x7a88 ||
             addr == 0x7a90 || addr == 0x7a98) && (value & 0x0200)) {
            // Documented DMA RS bit: writing one resets this channel's
            // control register. It is not a transfer-start bit.
            mmio[addr - kMmioBase] = 0;
            log_dma_param_write(addr, value);
            return;
        }
        if (addr == 0x7038) {
            // P_Line_Counter is read-only. Retail initialization writes zero
            // defensively, but the hardware-visible value remains scanline
            // timing derived from the TFT counters.
            return;
        }
        mmio[addr - kMmioBase] = value;
        if (addr == 0x7050 || addr == 0x7051 ||
            addr == 0x7054 || addr == 0x7055) {
            next_video_edge_cycles = 0;
        }
        if (is_gpio_data_register(addr)) {
            // The I/O Buffer register is the DATA latch; DATA reads return the
            // external pad state, while BUFFER reads back the last written data.
            mmio[addr + 1 - kMmioBase] = value;
        }
        if (addr >= 0x7878 && addr <= 0x787b) update_power_latch();
        if (addr >= 0x7880 && addr <= 0x7883) update_accelerometer_i2c();
        switch (addr) {
        case 0x7000: case 0x7001: case 0x7002: case 0x7003:
        case 0x7004: case 0x7005: case 0x7006: case 0x7007:
        case 0x7008: case 0x7009: case 0x700a: case 0x700b:
        case 0x700c: case 0x700d: case 0x700e: case 0x700f:
        case 0x7010: case 0x7011: case 0x7012: case 0x7013:
        case 0x7014: case 0x7015: case 0x7016: case 0x7017:
        case 0x7018: case 0x7019: case 0x701a: case 0x701b:
        case 0x701c: case 0x701d: case 0x701e:
        case 0x7020: case 0x7021: case 0x7022: case 0x7023:
        case 0x7024: case 0x7028: case 0x7029: case 0x702a:
        case 0x702b: case 0x702c:
        case 0x702d: case 0x702e: case 0x702f: case 0x7030:
        case 0x7036: case 0x7037: case 0x703a: case 0x703c:
        case 0x7042:
        case 0x7050: case 0x7051: case 0x7052: case 0x7053:
        case 0x7054: case 0x7055: case 0x7056: case 0x7057:
        case 0x7058: case 0x7059: case 0x705a: case 0x705b:
        case 0x705c:
        case 0x7062:
        case 0x706c: case 0x706d: case 0x706e: case 0x706f:
        case 0x7073: case 0x7074: case 0x7078: case 0x7079:
        case 0x707a: case 0x707b: case 0x707c: case 0x707d:
        case 0x707e: case 0x707f:
        case 0x7080: case 0x7081: case 0x7082: case 0x7083:
        case 0x7084: case 0x7085: case 0x7086: case 0x7087:
        case 0x7088:
            break;
        case 0x7063:
            break;
        case 0x7070:
        case 0x7071:
            break;
        case 0x7072:
            run_video_dma();
            break;
        case 0x7850:
            nand.ctrl = value;
            if (g_log) g_log << "NAND Ctrl=0x" << std::hex << value << std::dec << "\n";
            nand.recalc();
            break;
        case 0x7851:
            nand.command = value;
            nand.cursor = 0;
            if (g_log) g_log << "NAND CMD=0x" << std::hex << value << std::dec << "\n";
            nand.recalc();
            if (value == 0xd0) nand.erase_selected_block();
            else if (value == 0x10) nand.status = 0x40;
            else if (value == 0x80) nand.status = 0x40;
            break;
        case 0x7852:
            nand.addr_low = value;
            nand.cursor = 0;
            if (g_log) g_log << "NAND AddrL=0x" << std::hex << value << std::dec << "\n";
            nand.recalc();
            break;
        case 0x7853:
            nand.addr_high = value;
            nand.cursor = 0;
            if (g_log) g_log << "NAND AddrH=0x" << std::hex << value << std::dec << "\n";
            nand.recalc();
            break;
        case 0x7854:
            nand.write_data(value);
            break;
        case 0x7855:
            nand.dma_int_ctrl = value;
            if (g_log) {
                g_log << "NAND DMA_INT=0x" << std::hex << value
                      << " adr2=" << ((value & 0x0200) ? 1 : 0)
                      << " adr3=" << ((value & 0x0400) ? 1 : 0)
                      << " adr4=" << ((value & 0x0800) ? 1 : 0)
                      << " dma=" << ((value & 0x4000) ? 1 : 0)
                      << " int=" << ((value & 0x2000) ? 1 : 0)
                      << std::dec << "\n";
            }
            nand.recalc();
            break;
        case 0x7856:
            nand.type = value;
            if (g_log) g_log << "NAND Type=0x" << std::hex << value << std::dec << "\n";
            nand.recalc();
            break;
        case 0x7857:
        case 0x785b:
        case 0x785c:
        case 0x785d: break; // ECC/control registers, stored for now
        case 0x7820: case 0x7821: case 0x7822: case 0x7823: case 0x7824:
        case 0x7825: case 0x782d: case 0x782f:
        case 0x7837: case 0x7838: case 0x783a: case 0x783b:
        case 0x783c: case 0x783d: case 0x783e:
        case 0x7840: case 0x7841:
            break;
        case 0x7803: case 0x7804: case 0x7805: case 0x7806:
        case 0x7807: case 0x7808:
        case 0x780a: case 0x780b: case 0x780c: case 0x780d:
        case 0x7810:
            // Only the low six bits select the 2-Mword external-memory bank.
            mmio[addr - kMmioBase] &= 0x003f;
            break;
        case 0x7818: case 0x7819: case 0x781f:
            break;
        case 0x78a3: case 0x78a4: case 0x78a5:
        case 0x7934:
            last_scheduler_cycles = cycles;
            break;
        case 0x7936:
            break;
        case 0x78c0: case 0x78c1: case 0x78c2: case 0x78c3:
        case 0x78c8: case 0x78c9: case 0x78ca: case 0x78cb:
        case 0x78d0: case 0x78d1: case 0x78d2: case 0x78d3:
        case 0x78d8: case 0x78da:
        case 0x78e0: case 0x78e2:
        case 0x78e8: case 0x78ea:
            break;
        case 0x7940:
        case 0x7941:
        case 0x7943:
        case 0x7945:
            break;
        case 0x7942:
            spi.write_tx(value, pc_for_log);
            break;
        case 0x7960:
        case 0x7962:
        case 0x7963:
        case 0x7964:
        case 0x7965:
        case 0x7970:
        case 0x7971:
        case 0x7972:
        case 0x7973:
            break;
        case 0x7902: // UART control; retained until a serial endpoint is modeled.
            break;
        case 0x79e0: case 0x79e1: case 0x79e2: case 0x79e3: case 0x79e4:
        case 0x79e5: case 0x79e6: case 0x79e8: case 0x79e9: case 0x79ea:
            // SD2 transfer behavior is implemented incrementally from the
            // internal ROM's observable command sequence. Register storage is
            // accurate enough to expose the next phase without fabricating a
            // successful data transfer.
            break;
        case 0x79e7:
            break;
        case 0x7860: case 0x7861: case 0x7862: case 0x7863: case 0x7864:
        case 0x7868: case 0x786a: case 0x786b: case 0x786c: case 0x786d:
        case 0x7870: case 0x7871: case 0x7872: case 0x7873: case 0x7874: case 0x7875: case 0x7876: case 0x7877:
        case 0x7878: case 0x7879: case 0x787a: case 0x787b: case 0x787c: case 0x787d: case 0x787e: case 0x787f:
        case 0x7880: case 0x7881: case 0x7882: case 0x7883: case 0x7884:
        case 0x7888: case 0x7889: case 0x788a: case 0x788b: case 0x788c: case 0x788d: case 0x788e: case 0x788f:
            break;
        case 0x7869:
            spi.set_chip_select((value & 0x0010) == 0, pc_for_log);
            break;
        case 0x7a80: case 0x7a88: case 0x7a90: case 0x7a98:
        case 0x7a81: case 0x7a82: case 0x7a83: case 0x7a84:
        case 0x7a85: case 0x7a86: case 0x7a87:
        case 0x7a89: case 0x7a8a: case 0x7a8b: case 0x7a8c:
        case 0x7a8d: case 0x7a8e: case 0x7a8f:
        case 0x7a91: case 0x7a92: case 0x7a93: case 0x7a94:
        case 0x7a95: case 0x7a96: case 0x7a97:
        case 0x7a99: case 0x7a9a: case 0x7a9b: case 0x7a9c:
        case 0x7a9d: case 0x7a9e: case 0x7a9f:
            log_dma_param_write(addr, value);
            maybe_run_dma(addr);
            break;
        // The verified GPL16250-family SDK maps 0x7b80.. to the Speech
        // Processing Unit, not to a second DMA controller. Retain the SPU's
        // firmware-visible registers without interpreting channel-enable,
        // volume, envelope, or status writes as memory transfers.
        case 0x7b80: case 0x7b81: case 0x7b82: case 0x7b83: case 0x7b84:
        case 0x7b85: case 0x7b86: case 0x7b87:
        case 0x7b88: case 0x7b89: case 0x7b8a: case 0x7b8b: case 0x7b8c:
        case 0x7b8d: case 0x7b8e: case 0x7b8f:
        case 0x7b90: case 0x7b91: case 0x7b92: case 0x7b93: case 0x7b94:
        case 0x7b95: case 0x7b96: case 0x7b97:
        case 0x7b98: case 0x7b99: case 0x7b9a: case 0x7b9b: case 0x7b9c:
        case 0x7b9d: case 0x7b9e: case 0x7b9f:
            break;
        case 0x7abe:
            break;
        case 0x7abf:
            break;
        case 0x7ba0: case 0x7ba1: case 0x7ba2: case 0x7ba3:
        case 0x7ba4: case 0x7ba5: case 0x7ba6: case 0x7ba7:
        case 0x7ba8: case 0x7ba9: case 0x7baa: case 0x7bab:
        case 0x7bac: case 0x7bad: case 0x7bae: case 0x7baf:
        case 0x7bb0: case 0x7bb1: case 0x7bb2: case 0x7bb3:
        case 0x7bb4: case 0x7bb5: case 0x7bb6: case 0x7bb7:
        case 0x7bb8: case 0x7bb9: case 0x7bba: case 0x7bbb:
        case 0x7bbc: case 0x7bbd: case 0x7bbe:
            break;
        case 0x7a30:
            if (value && usb_device_enabled_cycle == UINT64_MAX) {
                usb_device_enabled_cycle = cycles;
                usb_suspend_latched = false;
                if (g_log) {
                    g_log << "USB device enabled pc=0x" << std::hex << pc_for_log
                          << " config=0x" << value << std::dec << "\n";
                }
            } else if (!value) {
                usb_device_enabled_cycle = UINT64_MAX;
                usb_suspend_latched = false;
                usb_bus_reset_pending = usb_host_connected;
                usb_enumerated = false;
                usb_ep0_fifo.clear();
                usb_bulk_out_fifo.clear();
                usb_bulk_in_fifo.clear();
                usb_interrupt_in_fifo.clear();
                mmio[0x7a3a - kMmioBase] = 0;
            }
            break;
        case 0x780e:
            if (value == 0xa00a) {
                // Related Generalplus documentation identifies 0x780e as the
                // sleep-entry register. Wake from sleep causes a system reset.
                sleep_requested = true;
                if (g_log) {
                    g_log << "POWER sleep requested pc=0x" << std::hex << pc_for_log
                          << " key=0x" << value << std::dec << "\n";
                }
            }
            break;
        case 0x7ae2:
            break;
        default:
            if (g_log && logged_unknown_writes.insert(addr).second) {
                g_log << "UNKNOWN MMIO WRITE pc=" << std::hex << pc_for_log
                      << " addr=" << addr << " data=" << value << std::dec << "\n";
            }
            break;
        }
    }

    void log_dma_param_write(uint32_t addr, uint16_t value) {
        if (!g_log) return;
        const bool handoff_dma = pc_for_log >= 0x200000;
        if (!handoff_dma && dma_write_log_count >= 5000) return;
        if (!handoff_dma) ++dma_write_log_count;
        const uint32_t controller_base = 0x7a80;
        const uint32_t ch = (addr - controller_base) / 8;
        const uint32_t base = controller_base + ch * 8;
        const uint32_t src = uint32_t(mmio[base + 1 - kMmioBase]) |
                             (uint32_t(mmio[base + 4 - kMmioBase]) << 16);
        const uint32_t dst = uint32_t(mmio[base + 2 - kMmioBase]) |
                             (uint32_t(mmio[base + 5 - kMmioBase]) << 16);
        const uint32_t count = uint32_t(mmio[base + 3 - kMmioBase]) |
                               (uint32_t(mmio[base + 6 - kMmioBase]) << 16);
        g_log << "DMAREG base=0x" << std::hex << controller_base
              << " ch" << std::dec << ch << " off=0x" << std::hex << (addr - base)
              << " data=0x" << value
              << " ctrl=0x" << mmio[base - kMmioBase]
              << " src=0x" << src << " dst=0x" << dst
              << " count=0x" << count << " pc=0x" << pc_for_log << std::dec << "\n";
    }

    void run_video_dma() {
        const uint32_t src = mmio[0x7070 - kMmioBase];
        const uint32_t dst = mmio[0x7071 - kMmioBase] & 0x03ff;
        const uint32_t size = mmio[0x7072 - kMmioBase] & 0x03ff;
        // ASSUMPTION/MAME evidence: GPL162xx video DMA copies inclusive
        // count words from CPU memory to sprite/video RAM starting at 0x7400.
        for (uint32_t i = 0; i <= size && (dst + i) < 0x400; ++i) {
            write(0x7400 + dst + i, read(src + i));
        }
        mmio[0x7072 - kMmioBase] = 0;
        if (mmio[0x7062 - kMmioBase] & 0x0004) mmio[0x7063 - kMmioBase] |= 0x0004;
        if (g_log) {
            g_log << "VIDEO DMA source=0x" << std::hex << src
                  << " dest=0x" << dst << " size=0x" << size
                  << " bank=0x" << mmio[0x707e - kMmioBase]
                  << " ppu=0x" << mmio[0x707f - kMmioBase]
                  << " pc=0x" << pc_for_log << std::dec << "\n";
        }
    }

    void maybe_run_dma(uint32_t written_addr) {
        uint32_t controller_base = 0;
        uint32_t status_addr = 0;
        if (written_addr >= 0x7a80 && written_addr <= 0x7a9f) {
            controller_base = 0x7a80;
            status_addr = 0x7abf;
        } else {
            return;
        }
        const uint32_t ch = (written_addr - controller_base) / 8;
        if (ch >= 4) return;
        const uint32_t base = controller_base + ch * 8;
        const uint16_t ctrl = mmio[base - kMmioBase];
        const uint32_t src = uint32_t(mmio[base + 1 - kMmioBase]) |
                             (uint32_t(mmio[base + 4 - kMmioBase]) << 16);
        const uint32_t dst = uint32_t(mmio[base + 2 - kMmioBase]) |
                             (uint32_t(mmio[base + 5 - kMmioBase]) << 16);
        uint32_t count = uint32_t(mmio[base + 3 - kMmioBase]) |
                         (uint32_t(mmio[base + 6 - kMmioBase]) << 16);
        if (!(ctrl & 0x0001) || count == 0) return;
        // The USB endpoint is genuinely asynchronous: MODE=1 waits for host
        // packets, which supply requests in usb_dma_bulk_* above. Other
        // peripheral-demand paths retain their existing synchronous model
        // until those devices are likewise made request-driven.
        const unsigned selected = (mmio[0x7abe - kMmioBase] >> (ch * 4)) & 0x0f;
        if ((ctrl & 0x0004) && selected == 0 &&
            ((src & 0xffff) == 0x7a35 || (dst & 0xffff) == 0x7a34)) return;
        int32_t src_delta = 0;
        int32_t dst_delta = 0;
        if ((ctrl & 0x00a0) == 0x0000) src_delta = 1;
        else if ((ctrl & 0x00a0) == 0x0020) src_delta = -1;
        if ((ctrl & 0x0050) == 0x0000) dst_delta = 1;
        else if ((ctrl & 0x0050) == 0x0010) dst_delta = -1;
        const bool src_byte = (ctrl & 0x1000) != 0;
        const bool dst_byte = (ctrl & 0x2000) != 0;

        uint32_t cur_src = src;
        uint32_t cur_dst = dst;
        bool src_high_byte = false;
        bool dst_high_byte = false;
        for (uint32_t i = 0; i < count; ++i) {
            const uint16_t source_word = dma_read(cur_src);
            uint16_t value = source_word;
            if (src_byte) {
                // Byte-mode MMIO sources expose their data in the low byte.
                // For memory sources, successive byte requests consume low
                // then high byte before advancing the word address.
                const bool source_is_io = cur_src >= kMmioBase && cur_src <= kMmioEnd;
                value = source_is_io ? uint16_t(source_word & 0x00ff)
                                     : uint16_t((source_word >> (src_high_byte ? 8 : 0)) & 0x00ff);
            } else if (dst_byte) {
                // Word-to-byte DMA emits the low byte and then high byte of
                // each source word. NAND page programming uses this path.
                value = uint16_t((source_word >> (dst_high_byte ? 8 : 0)) & 0x00ff);
            }
            const bool wrote_high_byte = dst_high_byte;

            if (dst_byte) {
                const bool target_is_io = cur_dst >= kMmioBase && cur_dst <= kMmioEnd;
                if (target_is_io) {
                    dma_write(cur_dst, value & 0x00ff);
                } else {
                    const uint16_t old = dma_read(cur_dst);
                    const uint16_t merged = dst_high_byte
                        ? uint16_t((old & 0x00ff) | ((value & 0x00ff) << 8))
                        : uint16_t((old & 0xff00) | (value & 0x00ff));
                    dma_write(cur_dst, merged);
                }
            } else if (src_byte) {
                // Documented byte-to-word behavior: two peripheral byte
                // requests fill one 16-bit destination word, low byte first.
                const uint16_t old = dma_read(cur_dst);
                const uint16_t merged = dst_high_byte
                    ? uint16_t((old & 0x00ff) | ((value & 0x00ff) << 8))
                    : uint16_t((old & 0xff00) | (value & 0x00ff));
                dma_write(cur_dst, merged);
            } else {
                dma_write(cur_dst, value);
            }

            if (!src_byte && dst_byte) {
                if (wrote_high_byte) cur_src = uint32_t(cur_src + src_delta);
            } else if (src_byte && cur_src == 0x7854) {
                // NAND DMA pulls successive bytes from the data FIFO. The
                // programmed source address names the FIFO register; it is not
                // a linear MMIO register range.
            } else if (src_byte && !(cur_src >= kMmioBase && cur_src <= kMmioEnd)) {
                src_high_byte = !src_high_byte;
                if (!src_high_byte) cur_src = uint32_t(cur_src + src_delta);
            } else {
                cur_src = uint32_t(cur_src + src_delta);
            }

            if (src_byte != dst_byte) {
                dst_high_byte = !dst_high_byte;
                if (!dst_high_byte) cur_dst = uint32_t(cur_dst + dst_delta);
            } else {
                cur_dst = uint32_t(cur_dst + dst_delta);
            }
        }
        mmio[status_addr - kMmioBase] |= uint16_t(1u << ch);
        if ((ctrl & 0x0100) && g_log && dma_irq_log_count++ < 2048) {
            g_log << "DMA IRQ LATCH base=0x" << std::hex << controller_base
                  << " ch" << std::dec << ch
                  << " int=0x" << std::hex << mmio[status_addr - kMmioBase]
                  << " int1=0x" << int_status1_value()
                  << " pri1=0x" << mmio[0x78a4 - kMmioBase]
                  << " pc=0x" << pc_for_log
                  << " cycles=0x" << cycles << std::dec << "\n";
        }
        mmio[base + 1 - kMmioBase] = uint16_t(cur_src);
        mmio[base + 4 - kMmioBase] = uint16_t(cur_src >> 16);
        mmio[base + 2 - kMmioBase] = uint16_t(cur_dst);
        mmio[base + 5 - kMmioBase] = uint16_t(cur_dst >> 16);
        mmio[base + 3 - kMmioBase] = 0;
        mmio[base + 6 - kMmioBase] = 0;
        mmio[base - kMmioBase] = ctrl & uint16_t(~0x0003);
        if (g_log) {
            g_log << "DMA base=0x" << std::hex << controller_base
                  << " ch" << std::dec << ch << " copy src=0x" << std::hex << src
                  << " dst=0x" << dst << " count=0x" << count
                  << " ctrl=0x" << ctrl << " src_delta=" << std::dec << src_delta
                  << " dst_delta=" << dst_delta << " src_byte=" << src_byte
                  << " dst_byte=" << dst_byte << " pc=0x" << std::hex << pc_for_log << std::dec << "\n";
        }
        auto transfer_touches = [](uint32_t start, uint32_t words, uint32_t lo, uint32_t hi) {
            if (words == 0) return false;
            const uint64_t end = uint64_t(start) + words - 1;
            return uint64_t(start) <= hi && end >= lo;
        };
        const uint32_t touched_words = std::max<uint32_t>(1, count);
        const uint32_t fbi = ppu_frame_addr(mmio[0x7078 - kMmioBase], mmio[0x7079 - kMmioBase]);
        const uint32_t fbo = ppu_frame_addr(mmio[0x707a - kMmioBase], mmio[0x707b - kMmioBase]);
        const bool touches_video_window =
            transfer_touches(dst, touched_words, 0x2000, 0x2fff) ||
            transfer_touches(dst, touched_words, 0x7100, 0x77ff) ||
            transfer_touches(src, touched_words, 0x2000, 0x2fff) ||
            transfer_touches(src, touched_words, 0x7100, 0x77ff);
        static constexpr uint32_t kFrameWords = 320u * 240u;
        const bool touches_framebuffer =
            (fbi != 0 && transfer_touches(dst, touched_words, fbi, fbi + kFrameWords - 1)) ||
            (fbo != 0 && transfer_touches(dst, touched_words, fbo, fbo + kFrameWords - 1)) ||
            (last_framebuffer_valid && transfer_touches(dst, touched_words,
                                                        last_framebuffer_base,
                                                        last_framebuffer_base + kFrameWords - 1));
        if (g_log && (touches_video_window || touches_framebuffer) &&
            video_dma_touch_log_count++ < 4096) {
            g_log << "VIDEO DMA TOUCH cycles=0x" << std::hex << cycles
                  << " pc=0x" << pc_for_log
                  << " src=0x" << src
                  << " dst=0x" << dst
                  << " count=0x" << count
                  << " ctrl=0x" << ctrl
                  << " fbi=0x" << fbi
                  << " fbo=0x" << fbo
                  << " latch=0x" << last_framebuffer_base
                  << " video_window=" << (touches_video_window ? 1 : 0)
                  << " framebuffer=" << (touches_framebuffer ? 1 : 0)
                  << std::dec << "\n";
        }
        if (!dst_byte && count == 320u * 240u && dst >= kCsBase) {
            last_framebuffer_base = dst;
            last_framebuffer_valid = true;
            if (g_log) {
                g_log << "FRAMEBUFFER DMA base=0x" << std::hex << last_framebuffer_base
                      << " src=0x" << src << " pc=0x" << pc_for_log
                      << " cycles=0x" << cycles << std::dec << "\n";
            }
        }
        if (!dst_byte && count >= 0x10) {
            auto read32 = [&](uint32_t addr) -> uint32_t {
                return uint32_t(dma_read(addr)) | (uint32_t(dma_read(addr + 1)) << 16);
            };
            const uint32_t magic0 = read32(dst);
            const uint32_t magic1 = read32(dst + 2);
            // MBA headers begin "bM_gbMQa"; entry and load base are 32-bit
            // word addresses at byte offsets 0x14 and 0x18. Discovering the
            // header in the normal DMA load path lets strict lifecycle/video
            // behavior apply to MBAs already present in a supplied NAND, not
            // only files injected with --mba.
            if (magic0 == 0x675f4d62 && magic1 == 0x61514d62) {
                const uint32_t entry = read32(dst + 0x0a) & kAddrMask;
                const uint32_t base_addr = read32(dst + 0x0c) & kAddrMask;
                if (entry != 0 && entry != mba_application_entry) {
                    if (!mba_application_entry_pinned) {
                        observe_mba_application_entry(entry);
                    } else if (mba_application_active) {
                        // Pinning protects the explicitly selected overlay
                        // while retail boot code loads unrelated MBA headers.
                        // Once that selected application is actually active,
                        // a different header is a legitimate follow-up launch
                        // (for example Homebrew Launcher starting a game).
                        // The short watchdog armed solely for the substituted
                        // MBA must not leak into the normally loaded title.
                        mba_application_entry_pinned = false;
                        mba_watchdog_entry = UINT32_MAX;
                        mba_watchdog_handoff_pending = false;
                        write(0x780a, 0x0000);
                        observe_mba_application_entry(entry);
                        if (g_log) {
                            g_log << "MBA selected target released for follow-up entry=0x"
                                  << std::hex << entry << std::dec << "\n";
                        }
                    } else if (g_log) {
                        g_log << "MBA DMA header ignored for selected target entry=0x"
                              << std::hex << entry << " expected=0x"
                              << mba_application_entry << std::dec << "\n";
                    }
                }
                if (g_log) {
                    g_log << "MBA DMA header magic0=0x" << std::hex << magic0
                          << " magic1=0x" << magic1 << " entry=0x" << entry
                          << " base=0x" << base_addr
                          << " pc=0x" << pc_for_log << std::dec << "\n";
                }
            }
        }
    }
};

} // namespace mobigo
