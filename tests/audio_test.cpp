#include "audio.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>

using namespace mobigo;

static void test_dac_interrupt_enable_gating() {
    Bus bus;
    assert(bus.read(0x78f0) & 0x8000); // FIFO-empty latch resets set.
    assert((bus.int_status1_value() & 0x0030) == 0); // Disabled sources stay quiet.
    bus.write(0x78f0, 0x4000); // Enable channel-A FIFO interrupt.
    assert(bus.int_status1_value() & 0x0010);
}

static void test_dac_fifo_drain() {
    Bus bus;
    Audio audio;

    bus.write(0x78f2, 0x0180); // Reset FIFO, empty threshold 8.
    bus.write(0x78f0, 0x6405); // CHA, FIFO IRQ, SRC at 16 kHz.
    bus.write(0x78ff, 0x0001); // MobiGo board audio-output gate.
    bus.mmio[0x78a4 - kMmioBase] |= 0x0010; // Route channel A to FIQ.
    for (unsigned i = 0; i < 16; ++i) bus.write(0x78f1, uint16_t(0x7000 + i * 0x100));
    assert(bus.audio_fifo_level_a == 16);
    assert(bus.read(0x78f2) & 0x8000);

    audio.pump(bus);
    bus.cycles += bus.system_clock_hz() / 100; // 10 ms, 160 source samples.
    audio.pump(bus);

    assert(bus.audio_fifo_level_a == 0);
    assert(bus.audio_fifo_a.empty());
    assert(bus.read(0x78f2) & 0x4000); // Underrun keeps the last output.
    assert(bus.read(0x78f0) & 0x8000); // Empty interrupt flag.
    assert(bus.audio_fiq_asserted_no_update());
    assert(std::any_of(audio.output.begin(), audio.output.end(),
                       [](int16_t sample) { return sample != 0; }));
}

static void test_shared_dac_fifo_routing() {
    Bus bus;
    bus.write(0x78f8, 0x1800); // SSF+CHACFG: alternate CHA_Data into A/B.
    bus.write(0x78f1, 0x1111);
    bus.write(0x78f1, 0x2222);
    bus.write(0x78f1, 0x3333);
    bus.write(0x78f1, 0x4444);
    assert((bus.audio_fifo_a == std::deque<uint16_t>{0x1111, 0x3333}));
    assert((bus.audio_fifo_b == std::deque<uint16_t>{0x2222, 0x4444}));

    Bus mono;
    mono.write(0x78f8, 0x1c00); // SSF+CHACFG+MONO: duplicate to A/B.
    mono.write(0x78f1, 0x5678);
    assert((mono.audio_fifo_a == std::deque<uint16_t>{0x5678}));
    assert((mono.audio_fifo_b == std::deque<uint16_t>{0x5678}));
}

static void test_spu_pcm16_oneshot() {
    Bus bus;
    Audio audio;
    constexpr uint32_t wave = 0x001000;
    bus.mem[wave + 0] = 0x8000;
    bus.mem[wave + 1] = 0xc000;
    bus.mem[wave + 2] = 0x4000;
    bus.mem[wave + 3] = 0xffff;

    bus.sound_ram[0x000] = uint16_t(wave);
    bus.sound_ram[0x001] = 0x5000; // PCM16, hardware one-shot.
    bus.sound_ram[0x003] = 0x4040; // Center pan, volume 64.
    bus.sound_ram[0x005] = 0x007f; // Manual envelope at full scale.
    bus.sound_ram[0x204] = 0x7482; // 16 kHz at the 281250 Hz SPU clock.
    bus.mmio[0x7b81 - kMmioBase] = 0x007f;
    bus.mmio[0x7b82 - kMmioBase] = 0x0001; // Channel-end interrupt enable.
    bus.mmio[0x7b8d - kMmioBase] = 0x00c0;
    bus.mmio[0x7b95 - kMmioBase] = 0x0001;
    bus.mmio[0x78f0 - kMmioBase] = 0x2000;
    bus.mmio[0x78ff - kMmioBase] = 0x0001;
    bus.mmio[0x7b80 - kMmioBase] = 0x0001;

    audio.pump(bus);
    bus.cycles += bus.system_clock_hz() / 100; // Far longer than this sample.
    audio.pump(bus);

    assert(!audio.channels[0].playing);
    assert(bus.mmio[0x7b80 - kMmioBase] & 0x0001);
    assert(bus.mmio[0x7b83 - kMmioBase] & 0x0001);
    assert(bus.mmio[0x7b8b - kMmioBase] & 0x0001);
    assert((bus.mmio[0x7b8f - kMmioBase] & 0x0001) == 0);
    assert(bus.timer_irq4_asserted_no_update());
    assert(!bus.audio_fiq_asserted_no_update());

    bus.write(0x78a6, 0x0001); // Route the channel-end source to FIQ.
    assert(!bus.timer_irq4_asserted_no_update());
    assert(bus.audio_fiq_asserted_no_update());
    bus.write(0x7b83, 0x0001); // Per-channel event is W1C.
    assert((bus.mmio[0x7b83 - kMmioBase] & 0x0001) == 0);
    assert(!bus.audio_fiq_asserted_no_update());
    assert(std::any_of(audio.output.begin(), audio.output.end(),
                       [](int16_t sample) { return sample != 0; }));
}

static void test_spu_restart_between_audio_pumps() {
    Bus bus;
    Audio audio;
    constexpr uint32_t first_wave = 0x001000;
    constexpr uint32_t second_wave = 0x001100;
    bus.mem[first_wave] = 0x8080;
    bus.mem[first_wave + 1] = 0xffff;
    bus.mem[second_wave] = 0xc0c0;
    bus.mem[second_wave + 1] = 0xffff;

    bus.write(0x7c00, uint16_t(first_wave));
    bus.write(0x7c01, 0x1000);
    bus.sound_ram[0x003] = 0x4040;
    bus.sound_ram[0x005] = 0x007f;
    bus.sound_ram[0x204] = 0x1000;
    bus.mmio[0x7b81 - kMmioBase] = 0x007f;
    bus.mmio[0x7b8d - kMmioBase] = 0x0088;
    bus.mmio[0x78f0 - kMmioBase] = 0x2000;
    bus.mmio[0x78ff - kMmioBase] = 0x0001;
    bus.write(0x7b80, 0x0001);
    audio.pump(bus);
    assert(audio.channels[0].wave_addr == first_wave);

    // The resident music tick writes a complete enable bitmap, so a selected
    // voice can remain enabled while its wave/mode descriptor is replaced.
    bus.write(0x7c00, uint16_t(second_wave));
    bus.write(0x7c01, 0x1000);
    bus.write(0x7b80, 0x0001);
    const uint64_t same_cycle = bus.cycles;
    const size_t output_before = audio.output.size();
    audio.pump(bus);
    assert(bus.cycles == same_cycle);
    assert(audio.output.size() == output_before);
    assert(audio.channels[0].playing);
    assert(audio.channels[0].wave_addr == second_wave);

    // Disabling a voice is equally immediate and clears the live status even
    // when the emulated clock has not advanced to another host sample.
    bus.write(0x7b80, 0x0000);
    audio.pump(bus);
    assert(!audio.channels[0].playing);
    assert((audio.active_channels & 1) == 0);
    assert((bus.mmio[0x7b8f - kMmioBase] & 1) == 0);
}

static void test_mobigo_output_gate() {
    Bus bus;
    Audio audio;
    constexpr uint32_t wave = 0x001000;
    bus.mem[wave + 0] = 0x2000;
    bus.mem[wave + 1] = 0xe000;
    bus.mem[wave + 2] = 0xffff;

    bus.sound_ram[0x000] = uint16_t(wave);
    bus.sound_ram[0x001] = 0x6000; // PCM16, hardware auto-repeat.
    bus.sound_ram[0x002] = uint16_t(wave);
    bus.sound_ram[0x003] = 0x4040;
    bus.sound_ram[0x005] = 0x007f;
    bus.sound_ram[0x204] = 0x7482;
    bus.mmio[0x7b81 - kMmioBase] = 0x007f;
    bus.mmio[0x7b8d - kMmioBase] = 0x00c0;
    bus.mmio[0x7b95 - kMmioBase] = 0x0001;
    bus.mmio[0x78f0 - kMmioBase] = 0x2000;
    bus.mmio[0x78ff - kMmioBase] = 0x0001;
    bus.mmio[0x7b80 - kMmioBase] = 0x0001;

    audio.pump(bus);
    bus.cycles += bus.system_clock_hz() / 1000;
    audio.pump(bus);
    assert(std::any_of(audio.output.begin(), audio.output.end(),
                       [](int16_t sample) { return sample != 0; }));

    bus.mmio[0x78ff - kMmioBase] = 0;
    bus.cycles += bus.system_clock_hz() / 1000;
    audio.pump(bus);
    assert(std::all_of(audio.output.begin(), audio.output.end(),
                       [](int16_t sample) { return sample == 0; }));

    bus.mmio[0x78ff - kMmioBase] = 1;
    bus.cycles += bus.system_clock_hz() / 1000;
    audio.pump(bus);
    assert(std::any_of(audio.output.begin(), audio.output.end(),
                       [](int16_t sample) { return sample != 0; }));
}

static uint64_t beat_period_cycles(const Bus &bus, uint16_t base, uint16_t count) {
    const unsigned __int128 numerator =
        static_cast<unsigned __int128>(bus.system_clock_hz()) * base * count * 4;
    return uint64_t((numerator + 281250 - 1) / 281250);
}

static void test_spu_beat_irq_and_fiq_routing() {
    Bus bus;
    constexpr uint16_t base = 10;
    constexpr uint16_t count = 2;
    const uint64_t period = beat_period_cycles(bus, base, count);

    bus.write(0x7b84, base);
    bus.write(0x7b85, uint16_t(0xc000 | count));
    assert(bus.read(0x7b84) == base);
    assert(bus.read(0x7b85) == uint16_t(0x8000 | count));

    bus.cycles += period - 1;
    bus.update_periodic_events();
    assert((bus.read(0x7b85) & 0x4000) == 0);
    assert((bus.read(0x78a3) & 0x0004) == 0);

    bus.cycles += 1;
    bus.update_periodic_events();
    assert(bus.read(0x7b85) & 0x4000);
    assert(bus.read(0x78a3) & 0x0004);
    assert(bus.timer_irq4_asserted_no_update());
    assert(!bus.audio_fiq_asserted_no_update());

    bus.write(0x7b85, 0x4000); // Clear beat latch and disable the counter.
    assert((bus.read(0x7b85) & 0xc000) == 0);
    assert((bus.read(0x78a3) & 0x0004) == 0);

    bus.write(0x78a6, 0x0004); // Route the beat source to FIQ.
    bus.write(0x7b85, uint16_t(0xc000 | count));
    bus.cycles += period;
    bus.update_periodic_events();
    assert(bus.read(0x78a3) & 0x0004);
    assert(!bus.timer_irq4_asserted_no_update());
    assert(bus.audio_fiq_asserted_no_update());

    bus.write(0x78a3, 0x0004); // Status3 W1C also clears the source latch.
    assert((bus.read(0x78a3) & 0x0004) == 0);
    assert(!bus.audio_fiq_asserted_no_update());

    // Resident idle state: enabled count zero means one base interval.
    bus.write(0x78a6, 0x0000);
    bus.write(0x7b85, 0x8000);
    const uint64_t idle_period = beat_period_cycles(bus, base, 1);
    bus.cycles += idle_period;
    bus.update_periodic_events();
    assert(bus.read(0x78a3) & 0x0004);
    assert(bus.timer_irq4_asserted_no_update());
}

int main() {
    test_dac_interrupt_enable_gating();
    test_dac_fifo_drain();
    test_shared_dac_fifo_routing();
    test_spu_pcm16_oneshot();
    test_spu_restart_between_audio_pumps();
    test_mobigo_output_gate();
    test_spu_beat_irq_and_fiq_routing();
    std::cout << "audio tests passed\n";
    return 0;
}
