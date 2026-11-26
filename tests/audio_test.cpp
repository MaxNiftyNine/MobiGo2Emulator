#include "audio.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include "adpcm36_reference.hpp"

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

static void test_dac_refills_during_cpu_batch() {
    Bus bus;
    Audio audio;
    bus.write(0x78f2, 0x0110); // Refill interrupt when the FIFO is empty.
    bus.write(0x78f0, 0x6401); // DAC A at 48 kHz with its FIFO interrupt enabled.
    bus.write(0x78a4, 0x0010); // Route the refill interrupt to FIQ.
    bus.write(0x78ff, 1);

    uint16_t next_sample = 0x8001;
    const auto refill = [&] {
        for (unsigned i = 0; i < 16; ++i) bus.write(0x78f1, next_sample++);
    };
    refill();
    audio.pump_if_due(bus);

    // A regular CPU batch spans several complete FIFO refills. Model a guest
    // that services the interrupt as it becomes visible between instructions.
    unsigned refills = 0;
    for (int i = 0; i < kDefaultInstructionBatch; ++i) {
        bus.cycles += 2;
        audio.pump_if_due(bus);
        if (bus.audio_fiq_asserted_no_update()) {
            refill();
            ++refills;
        }
    }
    assert(refills > 1);
    assert((bus.read(0x78f2) & 0x4000) == 0);
    assert(audio.direct_a == int16_t(bus.cycles * audio.rate() / bus.system_clock_hz()));
}

static void test_audio_phase_across_clock_change() {
    Bus bus;
    Audio audio;
    bus.write(0x7807, 0); // 12 MHz.
    bus.write(0x78f0, 0x2401);
    bus.write(0x78ff, 1);
    bus.write(0x78f1, 0x9000);
    audio.pump_if_due(bus);

    const uint64_t old_clock = bus.system_clock_hz();
    bus.cycles = 125; // Half a frame at 12 MHz.
    bus.write(0x7807, 1); // Divide SYSCLK by two.
    audio.pump(bus, old_clock);
    bus.cycles += 62; // Just short of the other half at 6 MHz.
    audio.pump_if_due(bus);
    assert(bus.audio_fifo_level_a == 1);
    ++bus.cycles;
    audio.pump_if_due(bus);
    assert(bus.audio_fifo_level_a == 0);
    assert(audio.direct_a == 0x1000);
}

static void test_host_audio_prebuffer() {
    assert(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    Audio audio;
    audio.init();
    assert(audio.device != 0);
    assert(SDL_GetAudioDeviceStatus(audio.device) == SDL_AUDIO_PAUSED);

    const auto queue_frames = [&](size_t frames) {
        audio.host_staging.insert(audio.host_staging.end(), 2 * frames, 123);
        audio.flush_host_output();
    };
    const size_t block = Audio::kHostQueueFrames;
    const size_t prebuffer = audio.host_prebuffer_bytes() / Audio::kHostFrameBytes;
    queue_frames(block);
    assert(SDL_GetAudioDeviceStatus(audio.device) == SDL_AUDIO_PAUSED);
    assert(SDL_GetQueuedAudioSize(audio.device) == block * Audio::kHostFrameBytes);
    queue_frames(prebuffer - block);
    assert(SDL_GetAudioDeviceStatus(audio.device) == SDL_AUDIO_PLAYING);

    // Simulate a drained queue without relying on real-time sleeps.
    SDL_ClearQueuedAudio(audio.device);
    queue_frames(block);
    assert(SDL_GetAudioDeviceStatus(audio.device) == SDL_AUDIO_PAUSED);
    queue_frames(prebuffer - block);
    assert(SDL_GetAudioDeviceStatus(audio.device) == SDL_AUDIO_PLAYING);

    queue_frames(1); // A partial block sitting outside SDL.
    audio.set_host_suspended(true);
    assert(audio.host_staging.empty());
    assert(SDL_GetQueuedAudioSize(audio.device) == 0);
    audio.set_host_suspended(false);
    queue_frames(block);
    assert(SDL_GetAudioDeviceStatus(audio.device) == SDL_AUDIO_PAUSED);
    assert(SDL_GetQueuedAudioSize(audio.device) == block * Audio::kHostFrameBytes);
    audio.shutdown();
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

static void test_spu_loop_has_no_extra_sample() {
    Bus bus;
    Audio audio;
    constexpr uint32_t wave = 0x1000;
    bus.mem[wave] = 0x9000;
    bus.mem[wave + 1] = 0xa000;
    bus.mem[wave + 2] = 0xffff;
    Audio::channel_reg(bus, 0, 0) = wave;
    Audio::channel_reg(bus, 0, 1) = 0x6000;
    Audio::channel_reg(bus, 0, 2) = wave;
    audio.start_channel(bus, 0);
    for (unsigned i = 0; i < 128; ++i) {
        assert(audio.fetch_sample(bus, 0));
        assert(audio.channels[0].current == (i % 2 ? 0xa000 : 0x9000));
    }
    // An empty loop must return promptly and keep its address bounded.
    bus.mem[wave] = 0xffff;
    audio.channels[0].wave_addr = wave;
    assert(!audio.fetch_sample(bus, 0));
    assert(audio.channels[0].wave_addr == wave);
}

static void test_spu_pcm8_byte_terminators() {
    for (const uint16_t word : {0xff80, 0x80ff}) {
        Bus bus;
        Audio audio;
        bus.mem[0x1000] = word;
        Audio::channel_reg(bus, 0, 0) = 0x1000;
        Audio::channel_reg(bus, 0, 1) = 0x1000;
        audio.start_channel(bus, 0);
        if (word == 0xff80) {
            assert(audio.fetch_sample(bus, 0));
            assert(audio.channels[0].current == 0x8080);
        }
        assert(!audio.fetch_sample(bus, 0));
        assert(!audio.channels[0].playing);
    }
}

static void test_spu_compressed_attack_enters_pcm_sustain() {
    for (const uint16_t format : {0x0000, 0xbe00}) {
        Bus bus;
        Audio audio;
        bus.mem[0x1000] = 0xffff;
        bus.mem[0x1100] = 0x9000;
        bus.mem[0x1101] = 0xa000;
        bus.mem[0x1102] = 0xffff;
        Audio::channel_reg(bus, 0, 0) = 0x1000;
        Audio::channel_reg(bus, 0, 1) = 0xe000;
        Audio::channel_reg(bus, 0, 2) = 0x1100;
        Audio::channel_reg(bus, 0, 13) = format;
        audio.start_channel(bus, 0);
        assert(audio.fetch_sample(bus, 0));
        assert(audio.channels[0].current == 0x9000);
        assert(audio.channels[0].wave_addr == 0x1101);
        assert(audio.channels[0].sample_shift == 0);
        assert(Audio::channel_reg(bus, 0, 1) == 0x6000);
        assert(Audio::channel_reg(bus, 0, 13) == format);
        assert(audio.fetch_sample(bus, 0));
        assert(audio.channels[0].current == 0xa000);
        assert(audio.fetch_sample(bus, 0));
        assert(audio.channels[0].current == 0x9000);
    }
}

static void test_spu_adpcm36_variable_blocks_and_termination() {
    Bus bus;
    Audio audio;
    const uint16_t words[]{
        0x8404, 0xffff, // Three residuals of -1, fourth nibble is padding.
        0xc804, 0x4321, 0xfff5, // Five residuals, including partial last word.
        0xffff, 0xbe00, 0x7777 // End at the header, without decoding beyond it.
    };
    std::copy(std::begin(words), std::end(words), bus.mem.begin() + 0x1000);
    Audio::channel_reg(bus, 0, 0) = 0x1000;
    Audio::channel_reg(bus, 0, 1) = 0xd000;
    Audio::channel_reg(bus, 0, 13) = 0xbe00;
    audio.start_channel(bus, 0);
    for (const int sample : {-256,-256,-256,256,512,768,1024,1280}) {
        assert(audio.fetch_sample(bus, 0));
        assert(audio.channels[0].current == Audio::unsigned_sample(sample));
    }
    assert(audio.channels[0].wave_addr == 0x1005);
    assert(!audio.fetch_sample(bus, 0));
    assert(!audio.channels[0].playing);
    assert(Audio::spu_ctrl(bus, 0, 0x0b) & 1);
}

static void test_spu_adpcm36_prediction_against_vendor_decoder() {
    Audio audio;
    constexpr std::array<uint8_t, 8> residuals{1,9,2,14,7,8,0,15};
    for (unsigned filter = 0; filter < kAdpcm36Reference.size(); ++filter) {
        Audio::Channel channel;
        channel.adpcm36_previous = {1001, -2003};
        channel.adpcm36_header = uint16_t(0x8e03 | (filter << 4));
        for (unsigned n = 0; n < residuals.size(); ++n) {
            assert(audio.decode_adpcm36(channel, residuals[n]) ==
                   Audio::unsigned_sample(kAdpcm36Reference[filter][n]));
        }
    }
    for (const int sign : {-1, 1}) {
        Audio::Channel channel;
        channel.adpcm36_header = 0xbe50;
        const uint8_t residual = sign < 0 ? 8 : 7;
        assert(audio.decode_adpcm36(channel, residual) ==
               Audio::unsigned_sample(sign < 0 ? -32768 : 28672));
        for (unsigned i = 1; i < 32; ++i)
            assert(audio.decode_adpcm36(channel, residual) ==
                   Audio::unsigned_sample(sign < 0 ? -32768 : 32767));
    }
}

static void configure_cartridge_audio_bus(Bus &bus) {
    // Place CS3 at physical word 0xc00000, with a mirrored synthetic ROM.
    bus.write(0x7820, 0x0044);
    bus.write(0x7821, 0x7c47);
    bus.write(0x7822, 0x3fc4);
    bus.write(0x7823, 0xff87);
    assert(bus.mcs_start(3) == 0xc00000);
    bus.cart_mem.assign(0x1000, 0xffff);
    bus.write(0x7b9f, 4);
    bus.write(0x7bbf, 7); // The upper voice group has no separate wave bank.
    bus.write(0x7810, 2); // CPU banking must not affect the SPU.
}

static void configure_banked_voice(Bus &bus, Audio &audio, unsigned channel,
                                    uint16_t wave, uint16_t mode,
                                    uint16_t loop, uint16_t format = 0) {
    Audio::channel_reg(bus, channel, 0) = wave;
    Audio::channel_reg(bus, channel, 1) = uint16_t(mode | 0x0f3c);
    Audio::channel_reg(bus, channel, 2) = loop;
    Audio::channel_reg(bus, channel, 13) = format;
    audio.start_channel(bus, channel);
}

static void test_spu_cartridge_bank_waveforms_and_sustain() {
    for (const unsigned channel : {0u, 17u}) {
        Bus bus;
        Audio audio;
        configure_cartridge_audio_bus(bus);
        // Poison the corresponding unbanked RAM so missing bank bits cannot
        // accidentally produce the expected samples or terminators.
        for (unsigned i = 0x100; i < 0x410; ++i)
            bus.dma_write(0x3c0000 + i, 0xffff);

        bus.cart_mem[0x100] = 0x9000;
        bus.cart_mem[0x101] = 0xa000;
        configure_banked_voice(bus, audio, channel, 0x100, 0x7000, 0x100);
        for (const uint16_t sample : {0x9000, 0xa000, 0x9000}) {
            assert(audio.fetch_sample(bus, channel));
            assert(audio.channels[channel].current == sample);
        }
        bus.write(0x7810, 9);
        assert(audio.fetch_sample(bus, channel));
        assert(audio.channels[channel].current == 0xa000);

        bus.cart_mem[0x200] = 0x9080;
        bus.cart_mem[0x201] = 0xffa0;
        configure_banked_voice(bus, audio, channel, 0x200, 0x3000, 0x200);
        for (const uint16_t sample : {0x8080, 0x9090, 0xa0a0, 0x8080}) {
            assert(audio.fetch_sample(bus, channel));
            assert(audio.channels[channel].current == sample);
        }

        bus.cart_mem[0x300] = 0x8404; // Three ADPCM36 residuals.
        bus.cart_mem[0x301] = 0xf321;
        bus.cart_mem[0x304] = 0x9200;
        bus.cart_mem[0x305] = 0x9300;
        configure_banked_voice(bus, audio, channel, 0x300, 0xf000, 0x304, 0xbe00);
        for (const uint16_t sample : {0x8100,0x8200,0x8300,0x9200,0x9300,0x9200}) {
            assert(audio.fetch_sample(bus, channel));
            assert(audio.channels[channel].current == sample);
        }
        assert((Audio::channel_reg(bus, channel, 1) & 0x8000) == 0);

        bus.cart_mem[0x400] = 0x1111;
        bus.cart_mem[0x402] = 0x9800;
        configure_banked_voice(bus, audio, channel, 0x400, 0xf000, 0x402);
        Audio::ImaState reference;
        for (unsigned n = 0; n < 4; ++n) {
            assert(audio.fetch_sample(bus, channel));
            assert(audio.channels[channel].current ==
                   Audio::unsigned_sample(reference.clock(1)));
        }
        assert(audio.fetch_sample(bus, channel));
        assert(audio.channels[channel].current == 0x9800);

        // Changing the shared bank affects subsequent reads immediately.
        bus.write(0x7b9f, 0);
        bus.dma_write(0x3c0100, 0xb000);
        configure_banked_voice(bus, audio, channel, 0x100, 0x7000, 0x100);
        assert(audio.fetch_sample(bus, channel));
        assert(audio.channels[channel].current == 0xb000);
    }
}

static void test_spu_cartridge_bank_envelope_tables() {
    for (const unsigned channel : {0u, 17u}) {
        Bus bus;
        Audio audio;
        configure_cartridge_audio_bus(bus);
        constexpr uint32_t base = 0x3c0500;
        bus.dma_write(base, 0xdead);
        bus.dma_write(base + 1, 0xbeef);
        bus.cart_mem[0x500] = 0x2010;
        bus.cart_mem[0x501] = 0x0002;
        Audio::channel_reg(bus, channel, 4) = 0x1001;
        Audio::channel_reg(bus, channel, 5) = 15;
        Audio::channel_reg(bus, channel, 7) = 0x3c;
        Audio::channel_reg(bus, channel, 8) = 0x500;
        audio.start_channel(bus, channel);
        audio.envelope_tick(bus, channel);
        assert(Audio::channel_reg(bus, channel, 4) == 0x2010);
        assert(Audio::channel_reg(bus, channel, 6) == 2);
        assert(Audio::channel_reg(bus, channel, 5) == 0x0210);
        assert(audio.channels[channel].envelope_addr == base + 2);
    }
}

static void configure_constant_voice(Bus &bus, Audio &audio, unsigned channel,
                                     int16_t sample) {
    Audio::channel_reg(bus, channel, 3) = 127; // Full-left pan, volume 127.
    Audio::channel_reg(bus, channel, 5) = 127;
    Audio::channel_reg(bus, channel, 9) = uint16_t(sample) ^ 0x8000;
    Audio::channel_reg(bus, channel, 11) = uint16_t(sample) ^ 0x8000;
    Audio::spu_ctrl(bus, channel / 16, 0x15) |= Audio::channel_mask(channel);
    audio.start_channel(bus, channel);
}

static void test_spu_hardware_gain_steps() {
    Bus bus;
    Audio audio;
    configure_constant_voice(bus, audio, 0, 8192);
    Audio::spu_ctrl(bus, 0, 1) = 127;
    std::array<int32_t, 4> level{};
    for (unsigned setting = 0; setting < 4; ++setting) {
        Audio::spu_ctrl(bus, 0, 0x0d) = uint16_t(setting << 6);
        level[setting] = audio.mix_spu(bus, audio.prepare_mix(bus)).first;
    }
    // Related-chip VOLSEL documentation and the captured x4/x2 upper steps.
    assert(std::abs(level[1] - 4 * level[0]) <= 4);
    assert(std::abs(level[2] - 4 * level[1]) <= 4);
    assert(std::abs(level[3] - 2 * level[2]) <= 2);
    assert(level[2] > 7900 && level[2] < 8192);
}

static void test_spu_saturation_control() {
    for (const int16_t sample : {20000, -20000}) {
        Bus bus;
        Audio audio;
        configure_constant_voice(bus, audio, 0, sample);
        configure_constant_voice(bus, audio, 1, sample);
        Audio::spu_ctrl(bus, 0, 1) = 127;
        Audio::spu_ctrl(bus, 0, 0x0d) = 0x0080; // Unity, saturation disabled.
        const auto wrapped = audio.mix_spu(bus, audio.prepare_mix(bus)).first;
        assert(sample > 0 ? wrapped < 0 : wrapped > 0);
        assert(int16_t(Audio::spu_ctrl(bus, 0, 0x12)) == wrapped);
        Audio::spu_ctrl(bus, 0, 0x0d) |= 0x8000;
        const auto clipped = audio.mix_spu(bus, audio.prepare_mix(bus)).first;
        assert(clipped == (sample > 0 ? 32767 : -32768));
        assert(int16_t(Audio::spu_ctrl(bus, 0, 0x12)) == clipped);
    }
}

static void test_spu_envelope_preserves_half_tick() {
    Bus bus;
    Audio audio;
    Audio::spu_ctrl(bus, 0, 0x15) = 1; // Manual envelope isolates its clock.
    for (unsigned i = 0; i < Audio::kRequestedRate; ++i)
        audio.service_envelope(bus, 0);
    // The 281250/4 Hz envelope clock advances by 70312.5 ticks per second.
    assert(audio.channels[0].service_accum == Audio::kRequestedRate * 2);
    for (unsigned i = 0; i < Audio::kRequestedRate; ++i)
        audio.service_envelope(bus, 0);
    assert(audio.channels[0].service_accum == 0);
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
    SDL_SetMainReady();
    test_dac_interrupt_enable_gating();
    test_dac_fifo_drain();
    test_dac_refills_during_cpu_batch();
    test_audio_phase_across_clock_change();
    test_host_audio_prebuffer();
    test_shared_dac_fifo_routing();
    test_spu_pcm16_oneshot();
    test_spu_restart_between_audio_pumps();
    test_spu_loop_has_no_extra_sample();
    test_spu_pcm8_byte_terminators();
    test_spu_compressed_attack_enters_pcm_sustain();
    test_spu_adpcm36_variable_blocks_and_termination();
    test_spu_adpcm36_prediction_against_vendor_decoder();
    test_spu_cartridge_bank_waveforms_and_sustain();
    test_spu_cartridge_bank_envelope_tables();
    test_spu_hardware_gain_steps();
    test_spu_saturation_control();
    test_spu_envelope_preserves_half_tick();
    test_mobigo_output_gate();
    test_spu_beat_irq_and_fiq_routing();
    std::cout << "audio tests passed\n";
    return 0;
}
