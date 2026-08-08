#pragma once

#include "bus.hpp"

namespace mobigo {

// GPL16250 audio frontend and SPU mixer. The register layout and 281250 Hz
// SPU service clock come from the Generalplus SDK material in Assets. SDL is
// deliberately fed from the emulation thread with SDL_QueueAudio so the audio
// callback never races the CPU over emulated memory or MMIO state.
struct Audio {
    static constexpr uint32_t kRequestedRate = 48000;
    static constexpr size_t kHostQueueFrames = 256;
    static constexpr uint64_t kSpuServiceRate = 281250;
    static constexpr uint64_t kSpuOutputRate = kSpuServiceRate / 4;
    static constexpr uint64_t kPhaseScale = 1u << 19;

    struct ImaState {
        int signal = 0;
        int step = 0;

        void reset() { signal = 0; step = 0; }

        int16_t clock(uint8_t nibble) {
            static constexpr std::array<int, 89> step_table{
                7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
                50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,
                230,253,279,307,337,371,408,449,494,544,598,658,724,796,
                876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,
                2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,
                8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,
                22385,24623,27086,29794,32767
            };
            static constexpr std::array<int, 16> index_table{
                -1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8
            };
            const int delta_step = step_table[size_t(step)];
            int delta = delta_step >> 3;
            if (nibble & 1) delta += delta_step >> 2;
            if (nibble & 2) delta += delta_step >> 1;
            if (nibble & 4) delta += delta_step;
            signal += (nibble & 8) ? -delta : delta;
            signal = std::clamp(signal, -32768, 32767);
            step = std::clamp(step + index_table[nibble & 15], 0, 88);
            return int16_t(signal);
        }
    };

    struct Channel {
        bool playing = false;
        uint32_t wave_addr = 0;
        uint32_t envelope_addr = 0;
        uint64_t rate_accum = 0;
        uint64_t service_accum = 0;
        uint32_t envelope_frames = 4;
        uint32_t rampdown_frames = 0;
        uint8_t sample_shift = 0;
        uint16_t previous = 0x8000;
        uint16_t current = 0x8000;
        ImaState ima;
        uint16_t adpcm36_header = 0;
        uint8_t adpcm36_remaining = 0;
        std::array<int16_t, 2> adpcm36_previous{};
        uint32_t start_sequence = 0;
    };

    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec obtained{};
    std::array<Channel, 32> channels{};
    std::vector<int16_t> output;
    std::vector<int16_t> host_staging;
    uint64_t last_cycles = 0;
    uint64_t frame_phase = 0;
    uint64_t direct_phase_a = 0;
    uint64_t direct_phase_b = 0;
    uint32_t reset_count = 0;
    int32_t logged_output_peak = 0;
    int16_t direct_a = 0;
    int16_t direct_b = 0;
    uint32_t active_channels = 0;
    bool timeline_started = false;

    struct MixCache {
        uint32_t active = 0;
        uint64_t rate_denominator = 0;
        bool interpolate = false;
        unsigned volume_shift = 0;
        int32_t main_volume = 0;
        std::array<uint64_t, 32> rate_step{};
        std::array<int32_t, 32> left_gain{};
        std::array<int32_t, 32> right_gain{};
    };

    void init() {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::cerr << "warning: SDL audio unavailable: " << SDL_GetError() << "\n";
            return;
        }
        SDL_AudioSpec wanted{};
        wanted.freq = int(kRequestedRate);
        wanted.format = AUDIO_S16SYS;
        wanted.channels = 2;
        wanted.samples = 1024;
        device = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained,
                                     SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (!device) {
            std::cerr << "warning: failed to open audio device: " << SDL_GetError() << "\n";
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return;
        }
        host_staging.reserve(kHostQueueFrames * 2);
        SDL_PauseAudioDevice(device, 0);
    }

    void shutdown() {
        if (device) SDL_CloseAudioDevice(device);
        device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    uint32_t rate() const {
        return obtained.freq > 0 ? uint32_t(obtained.freq) : kRequestedRate;
    }

    static uint16_t &spu_ctrl(Bus &bus, unsigned bank, unsigned offset) {
        return bus.mmio[(0x7b80 + bank * 0x20 + offset) - kMmioBase];
    }

    static uint16_t &channel_reg(Bus &bus, unsigned channel, unsigned offset) {
        return bus.sound_ram[channel * 0x10 + offset];
    }

    static uint16_t &phase_reg(Bus &bus, unsigned channel, unsigned offset) {
        return bus.sound_ram[0x200 + channel * 0x10 + offset];
    }

    static uint16_t channel_mask(unsigned channel) {
        return uint16_t(1u << (channel & 15));
    }

    static unsigned channel_bank(unsigned channel) { return channel >> 4; }

    static bool channel_bit(Bus &bus, unsigned channel, unsigned offset) {
        return (spu_ctrl(bus, channel_bank(channel), offset) & channel_mask(channel)) != 0;
    }

    static uint32_t wave_address(Bus &bus, unsigned channel) {
        const uint16_t mode = channel_reg(bus, channel, 1);
        return uint32_t(channel_reg(bus, channel, 0)) |
               (uint32_t(mode & 0x003f) << 16);
    }

    static uint32_t loop_address(Bus &bus, unsigned channel) {
        const uint16_t mode = channel_reg(bus, channel, 1);
        return uint32_t(channel_reg(bus, channel, 2)) |
               (uint32_t((mode >> 6) & 0x003f) << 16);
    }

    static uint32_t pitch(Bus &bus, unsigned channel) {
        return (uint32_t(phase_reg(bus, channel, 0) & 7) << 16) |
               phase_reg(bus, channel, 4);
    }

    static void store_wave_address(Bus &bus, unsigned channel, uint32_t address) {
        uint16_t &mode = channel_reg(bus, channel, 1);
        mode = uint16_t((mode & ~uint16_t(0x003f)) | ((address >> 16) & 0x003f));
        channel_reg(bus, channel, 0) = uint16_t(address);
    }

    void reset_timeline(Bus &bus) {
        for (Channel &channel : channels) channel = Channel{};
        last_cycles = bus.cycles;
        frame_phase = 0;
        direct_phase_a = direct_phase_b = 0;
        direct_a = direct_b = 0;
        active_channels = 0;
        host_staging.clear();
        reset_count = bus.power_reset_count;
        logged_output_peak = 0;
        timeline_started = false;
        if (device) SDL_ClearQueuedAudio(device);
    }

    bool hardware_active(Bus &bus) const {
        const bool output_gate = (bus.mmio[0x78ff - kMmioBase] & 0x0001) != 0;
        const bool dac_a = (bus.mmio[0x78f0 - kMmioBase] & 0x2000) != 0;
        const bool dac_b = (bus.mmio[0x78f8 - kMmioBase] & 0x2000) != 0;
        const bool spu = bus.mmio[0x7b80 - kMmioBase] ||
                         bus.mmio[0x7ba0 - kMmioBase];
        return output_gate &&
               ((dac_a && (spu || !bus.audio_fifo_a.empty())) ||
                (dac_b && !bus.audio_fifo_b.empty()));
    }

    void start_channel(Bus &bus, unsigned index) {
        Channel &state = channels[index];
        state = Channel{};
        state.playing = true;
        state.start_sequence = bus.spu_channel_start_sequence[index];
        state.wave_addr = wave_address(bus, index);
        state.envelope_addr = uint32_t(channel_reg(bus, index, 8)) |
                              (uint32_t(channel_reg(bus, index, 7) & 0x003f) << 16);
        state.previous = channel_reg(bus, index, 9);
        state.current = channel_reg(bus, index, 0x0b);
        active_channels |= uint32_t(1) << index;
        const uint16_t mask = channel_mask(index);
        spu_ctrl(bus, channel_bank(index), 0x0f) |= mask;
        if (g_log) {
            g_log << "SPU start channel=" << index << " wave=0x" << std::hex
                  << state.wave_addr << " mode=0x" << channel_reg(bus, index, 1)
                  << " pitch=0x" << pitch(bus, index)
                  << " panvol=0x" << channel_reg(bus, index, 3)
                  << " env=0x" << channel_reg(bus, index, 5)
                  << " format=0x" << channel_reg(bus, index, 0x0d)
                  << " mainvol=0x" << spu_ctrl(bus, 0, 0x01)
                  << " spuctrl=0x" << spu_ctrl(bus, 0, 0x0d)
                  << " dac=0x" << bus.mmio[0x78f0 - kMmioBase]
                  << " pga=0x" << bus.mmio[0x78fb - kMmioBase]
                  << " dacctrl=0x" << bus.mmio[0x78fd - kMmioBase]
                  << " hpamp=0x" << bus.mmio[0x78fe - kMmioBase]
                  << " audioctrl=0x" << bus.mmio[0x78ff - kMmioBase]
                  << std::dec << "\n";
        }
    }

    void stop_channel(Bus &bus, unsigned index, bool natural_end) {
        Channel &state = channels[index];
        if (g_log && state.playing) {
            g_log << "SPU stop channel=" << index
                  << " natural=" << (natural_end ? 1 : 0)
                  << " wave=0x" << std::hex << state.wave_addr
                  << " cycles=0x" << bus.cycles << std::dec << "\n";
        }
        state.playing = false;
        active_channels &= ~(uint32_t(1) << index);
        const uint16_t mask = channel_mask(index);
        // CH_ENABLE is a programmed enable mask, not the live playback
        // bitmap. A hardware one-shot leaves it set when it reaches its end;
        // CH_STATUS is what falls. Software-requested stops have already
        // cleared CH_ENABLE and retain that state here.
        if (!natural_end)
            spu_ctrl(bus, channel_bank(index), 0x00) &= uint16_t(~mask);
        uint16_t &status = spu_ctrl(bus, channel_bank(index), 0x0f);
        status &= uint16_t(~mask);
        if (natural_end) {
            // A tone/end-marker completion raises the per-channel SPU event.
            // The resident sound manager uses this interrupt to retire its
            // software voice; merely clearing CH_STATUS leaves that voice in
            // an eternal "stopping" state even though the hardware channel is
            // already idle.
            spu_ctrl(bus, channel_bank(index), 0x03) |= mask;
            spu_ctrl(bus, channel_bank(index), 0x0b) |= mask;
        }
    }

    void sync_channels(Bus &bus) {
        for (unsigned index = 0; index < channels.size(); ++index) {
            const bool enabled = channel_bit(bus, index, 0x00);
            const bool stopped = channel_bit(bus, index, 0x0b);
            if (!enabled) {
                if (channels[index].playing) stop_channel(bus, index, false);
            } else if (!stopped &&
                       (!channels[index].playing ||
                        channels[index].start_sequence !=
                            bus.spu_channel_start_sequence[index])) {
                start_channel(bus, index);
            }
        }
    }

    static uint16_t unsigned_sample(int sample) {
        return uint16_t(int16_t(std::clamp(sample, -32768, 32767))) ^ 0x8000;
    }

    uint16_t decode_adpcm36(Channel &state, uint8_t nibble) {
        const int shift = state.adpcm36_header & 0x0f;
        int filter = (state.adpcm36_header >> 4) & 0x3f;
        if (filter & 0x20) filter -= 0x40;
        const int16_t signed_nibble = int16_t(nibble << 12);
        int sample = (signed_nibble >> shift) +
                     ((state.adpcm36_previous[0] * filter + 32) >> 12);
        sample = std::clamp(sample, -32768, 32767);
        state.adpcm36_previous[1] = state.adpcm36_previous[0];
        state.adpcm36_previous[0] = int16_t(sample);
        return unsigned_sample(sample);
    }

    bool handle_end_marker(Bus &bus, unsigned index, uint16_t tone_mode) {
        if (tone_mode == 1) {
            stop_channel(bus, index, true);
            return false;
        }
        const uint32_t target = loop_address(bus, index);
        if (g_log) {
            g_log << "SPU loop channel=" << index << " loop=0x"
                  << std::hex << target << std::dec << "\n";
        }
        channels[index].wave_addr = target;
        channels[index].sample_shift = 0;
        channels[index].adpcm36_remaining = 0;
        store_wave_address(bus, index, channels[index].wave_addr);
        return true;
    }

    bool fetch_sample(Bus &bus, unsigned index) {
        Channel &state = channels[index];
        uint16_t &mode = channel_reg(bus, index, 1);
        const uint16_t tone_mode = (mode >> 12) & 3;
        state.previous = state.current;
        channel_reg(bus, index, 9) = state.previous;

        if (tone_mode == 0) {
            state.current = channel_reg(bus, index, 0x0b);
            return true;
        }

        const bool adpcm36 = (channel_reg(bus, index, 0x0d) & 0x8000) != 0;
        if (adpcm36 && state.adpcm36_remaining == 0) {
            state.adpcm36_header = bus.dma_read(state.wave_addr & kAddrMask);
            ++state.wave_addr;
            state.adpcm36_remaining = 8;
        }

        const uint16_t raw = bus.dma_read(state.wave_addr & kAddrMask);
        if (raw == 0xffff && state.sample_shift == 0) {
            return handle_end_marker(bus, index, tone_mode);
        }

        if ((mode & 0x8000) || adpcm36) {
            const uint8_t nibble = uint8_t((raw >> state.sample_shift) & 0x0f);
            state.current = adpcm36 ? decode_adpcm36(state, nibble)
                                    : unsigned_sample(state.ima.clock(nibble));
            state.sample_shift += 4;
            if (state.sample_shift == 16) {
                state.sample_shift = 0;
                ++state.wave_addr;
                if (adpcm36 && state.adpcm36_remaining) --state.adpcm36_remaining;
            }
        } else if (mode & 0x4000) {
            state.current = raw;
            ++state.wave_addr;
        } else {
            const uint8_t byte = uint8_t(raw >> state.sample_shift);
            state.current = uint16_t(byte) * 0x0101;
            state.sample_shift ^= 8;
            if (state.sample_shift == 0) ++state.wave_addr;
        }

        channel_reg(bus, index, 9) = state.previous;
        channel_reg(bus, index, 0x0b) = state.current;
        store_wave_address(bus, index, state.wave_addr);
        return true;
    }

    static uint32_t envelope_frame_count(Bus &bus, unsigned index) {
        static constexpr std::array<uint32_t, 16> counts{
            4,8,16,32,64,128,256,512,1024,2048,4096,8192,8192,8192,8192,8192
        };
        const unsigned group = (index & 15) / 4;
        const unsigned offset = 0x06 + group;
        const uint16_t selection = spu_ctrl(bus, channel_bank(index), offset);
        return counts[(selection >> ((index & 3) * 4)) & 0x0f];
    }

    static uint32_t rampdown_frame_count(Bus &bus, unsigned index) {
        static constexpr std::array<uint32_t, 8> counts{
            13*4,13*16,13*64,13*256,13*1024,13*4096,13*8192,13*8192
        };
        return counts[phase_reg(bus, index, 3) & 7];
    }

    void envelope_tick(Bus &bus, unsigned index) {
        uint16_t &data = channel_reg(bus, index, 5);
        uint8_t count = uint8_t(data >> 8);
        if (count) --count;
        if (count) {
            data = uint16_t((uint16_t(count) << 8) | (data & 0x007f));
            return;
        }

        const uint16_t env0 = channel_reg(bus, index, 4);
        const uint8_t target = uint8_t((env0 >> 8) & 0x7f);
        const uint8_t increment = uint8_t(env0 & 0x7f);
        uint8_t level = uint8_t(data & 0x7f);
        if (level != target) {
            if (env0 & 0x0080) {
                level = increment > level ? 0 : uint8_t(level - increment);
                if (level < target) level = target;
                if (level == 0) {
                    stop_channel(bus, index, true);
                    return;
                }
            } else {
                level = uint8_t(std::min<unsigned>(target, unsigned(level) + increment));
            }
        }

        uint16_t &env1 = channel_reg(bus, index, 6);
        if (level == target) {
            if (env1 & 0x0100) {
                unsigned repeats = (env1 >> 9) & 0x7f;
                if (repeats) --repeats;
                env1 = uint16_t((env1 & 0x01ff) | (repeats << 9));
                if (repeats == 0) {
                    channel_reg(bus, index, 4) = bus.dma_read(channels[index].envelope_addr++);
                    channel_reg(bus, index, 6) = bus.dma_read(channels[index].envelope_addr++);
                    channel_reg(bus, index, 0x0a) = bus.dma_read(channels[index].envelope_addr++);
                }
            } else {
                channel_reg(bus, index, 4) = bus.dma_read(channels[index].envelope_addr++);
                channel_reg(bus, index, 6) = bus.dma_read(channels[index].envelope_addr++);
            }
            env1 = channel_reg(bus, index, 6);
        }
        count = uint8_t(env1);
        data = uint16_t((uint16_t(count) << 8) | level);
    }

    void service_envelope(Bus &bus, unsigned index) {
        Channel &state = channels[index];
        state.service_accum += kSpuOutputRate;
        while (state.service_accum >= rate()) {
            state.service_accum -= rate();
            if (channel_bit(bus, index, 0x0a)) {
                if (state.rampdown_frames) --state.rampdown_frames;
                if (state.rampdown_frames == 0) {
                    const unsigned decrement = (channel_reg(bus, index, 0x0a) >> 9) & 0x7f;
                    uint16_t &env = channel_reg(bus, index, 5);
                    const unsigned level = env & 0x7f;
                    const unsigned next = decrement >= level ? 0 : level - decrement;
                    env = uint16_t((env & 0xff00) | next);
                    if (next == 0) {
                        stop_channel(bus, index, true);
                        return;
                    }
                    state.rampdown_frames = rampdown_frame_count(bus, index);
                }
            } else if (!channel_bit(bus, index, 0x15)) {
                if (state.envelope_frames) --state.envelope_frames;
                if (state.envelope_frames == 0) {
                    envelope_tick(bus, index);
                    state.envelope_frames = envelope_frame_count(bus, index);
                }
            }
        }
    }

    MixCache prepare_mix(Bus &bus) const {
        MixCache cache;
        cache.active = active_channels;
        cache.rate_denominator = uint64_t(rate()) * kPhaseScale;
        const uint16_t control = spu_ctrl(bus, 0, 0x0d);
        cache.interpolate = (control & 0x0200) == 0;
        static constexpr std::array<unsigned, 4> volume_shifts{5, 3, 2, 0};
        cache.volume_shift = volume_shifts[(control >> 6) & 3];
        cache.main_volume = spu_ctrl(bus, 0, 0x01) & 0x7f;

        uint32_t remaining = cache.active;
        while (remaining) {
            const unsigned index = std::countr_zero(remaining);
            remaining &= remaining - 1;
            cache.rate_step[index] = uint64_t(pitch(bus, index)) * kSpuServiceRate;
            const int32_t volume = channel_reg(bus, index, 3) & 0x7f;
            const int32_t pan = (channel_reg(bus, index, 3) >> 8) & 0x7f;
            cache.left_gain[index] = pan < 0x40 ? 0x7f * volume
                                                : (0x7f - pan) * 2 * volume;
            cache.right_gain[index] = pan < 0x40 ? pan * 2 * volume
                                                 : 0x7f * volume;
        }
        return cache;
    }

    std::pair<int32_t, int32_t> mix_spu(Bus &bus, const MixCache &cache) {
        int32_t left = 0;
        int32_t right = 0;

        uint32_t remaining = cache.active;
        while (remaining) {
            const unsigned index = std::countr_zero(remaining);
            remaining &= remaining - 1;
            Channel &state = channels[index];
            if (!state.playing) continue;
            state.rate_accum += cache.rate_step[index];
            while (state.rate_accum >= cache.rate_denominator && state.playing) {
                state.rate_accum -= cache.rate_denominator;
                if (!fetch_sample(bus, index)) break;
            }
            if (!state.playing) continue;

            int32_t sample = int16_t(state.current ^ 0x8000);
            if (cache.interpolate) {
                const int32_t previous = int16_t(state.previous ^ 0x8000);
                const uint32_t factor = uint32_t(std::min<uint64_t>(
                    256, (state.rate_accum * 256) / cache.rate_denominator));
                sample = (previous * int32_t(256 - factor) + sample * int32_t(factor)) >> 8;
            }
            sample = (sample * int32_t(channel_reg(bus, index, 5) & 0x7f)) >> 7;
            left += (sample * cache.left_gain[index]) >> 14;
            right += (sample * cache.right_gain[index]) >> 14;
            service_envelope(bus, index);
        }

        // Zero is the disabled/reset value for Wave-In. Treating it as an
        // unsigned PCM sample would otherwise inject full-scale negative DC.
        const uint16_t wave_in_left = spu_ctrl(bus, 0, 0x10);
        const uint16_t wave_in_right = spu_ctrl(bus, 0, 0x11);
        if (wave_in_left) left += int16_t(wave_in_left ^ 0x8000);
        if (wave_in_right) right += int16_t(wave_in_right ^ 0x8000);
        left >>= cache.volume_shift;
        right >>= cache.volume_shift;
        left = (left * cache.main_volume) >> 7;
        right = (right * cache.main_volume) >> 7;
        spu_ctrl(bus, 0, 0x12) = uint16_t(int16_t(std::clamp(left, -32768, 32767)));
        spu_ctrl(bus, 0, 0x13) = uint16_t(int16_t(std::clamp(right, -32768, 32767)));
        return {left, right};
    }

    static uint32_t dac_source_rate(Bus &bus) {
        const uint16_t ctrl = bus.mmio[0x78f0 - kMmioBase];
        if (ctrl & 0x0400) {
            static constexpr std::array<uint32_t, 9> rates{
                44100,48000,32000,22050,24000,16000,11250,12000,8000
            };
            const unsigned selection = ctrl & 0x0f;
            return selection < rates.size() ? rates[selection] : 0;
        }
        const uint16_t timer = bus.mmio[0x78e0 - kMmioBase];
        if ((timer & 0x2000) == 0) return 0;
        return uint32_t(bus.timer_effective_hz(timer, bus.system_clock_hz()) / 2);
    }

    void consume_fifo(Bus &bus, bool channel_b) {
        auto &fifo = channel_b ? bus.audio_fifo_b : bus.audio_fifo_a;
        uint8_t &level = channel_b ? bus.audio_fifo_level_b : bus.audio_fifo_level_a;
        int16_t &held = channel_b ? direct_b : direct_a;
        const uint32_t ctrl_addr = channel_b ? 0x78f8 : 0x78f0;
        const uint32_t fifo_addr = channel_b ? 0x78fa : 0x78f2;
        if (!fifo.empty()) {
            const uint16_t sample = fifo.front();
            fifo.pop_front();
            level = uint8_t(fifo.size());
            const bool signed_input = (bus.mmio[0x78f0 - kMmioBase] & 0x0800) != 0;
            held = signed_input ? int16_t(sample) : int16_t(sample ^ 0x8000);
        } else {
            level = 0;
            bus.mmio[fifo_addr - kMmioBase] |= 0x4000;
        }
        const unsigned threshold = (bus.mmio[fifo_addr - kMmioBase] >> 4) & 0x0f;
        if (level < threshold) bus.mmio[ctrl_addr - kMmioBase] |= 0x8000;
    }

    std::pair<int32_t, int32_t> mix_direct_dac(Bus &bus, uint32_t source_rate) {
        if (bus.mmio[0x78f0 - kMmioBase] & 0x2000) {
            direct_phase_a += source_rate;
            while (source_rate && direct_phase_a >= rate()) {
                direct_phase_a -= rate();
                consume_fifo(bus, false);
            }
        } else {
            direct_a = 0;
        }
        if (bus.mmio[0x78f8 - kMmioBase] & 0x2000) {
            direct_phase_b += source_rate;
            while (source_rate && direct_phase_b >= rate()) {
                direct_phase_b -= rate();
                consume_fifo(bus, true);
            }
            return {direct_a, direct_b};
        }
        return {direct_a, direct_a};
    }

    void render(Bus &bus, uint64_t frames) {
        output.clear();
        output.reserve(size_t(frames) * 2);
        sync_channels(bus);
        const MixCache cache = prepare_mix(bus);
        const uint32_t source_rate = dac_source_rate(bus);
        const bool output_gate =
            (bus.mmio[0x78ff - kMmioBase] & 0x0001) != 0;
        for (uint64_t i = 0; i < frames; ++i) {
            auto [left, right] = mix_spu(bus, cache);
            auto [dac_left, dac_right] = mix_direct_dac(bus, source_rate);
            left += dac_left;
            right += dac_right;
            if (!output_gate) left = right = 0;
            output.push_back(int16_t(std::clamp(left, -32768, 32767)));
            output.push_back(int16_t(std::clamp(right, -32768, 32767)));
        }
        if (g_log) {
            int32_t peak = 0;
            for (const int16_t sample : output)
                peak = std::max(peak, std::abs(int32_t(sample)));
            if (peak > logged_output_peak) {
                logged_output_peak = peak;
                g_log << "AUDIO output frames=" << frames << " peak=" << peak
                      << " queued=" << (device ? SDL_GetQueuedAudioSize(device) : 0)
                      << "\n";
            }
        }
    }

    void queue_host_output() {
        if (!device || output.empty()) return;
        host_staging.insert(host_staging.end(), output.begin(), output.end());
        if (host_staging.size() < kHostQueueFrames * 2) return;
        if (SDL_QueueAudio(device, host_staging.data(),
                           Uint32(host_staging.size() * sizeof(host_staging.front()))) != 0) {
            die(std::string("failed to queue audio: ") + SDL_GetError());
        }
        host_staging.clear();
    }

    void pump(Bus &bus) {
        if (reset_count != bus.power_reset_count || bus.cycles < last_cycles) {
            reset_timeline(bus);
        }
        if (!timeline_started) {
            last_cycles = bus.cycles;
            if (!hardware_active(bus)) return;
            timeline_started = true;
            sync_channels(bus);
            return;
        }

        const uint64_t elapsed = bus.cycles - last_cycles;
        last_cycles = bus.cycles;
        const uint64_t clock = bus.system_clock_hz();
        const unsigned __int128 total = static_cast<unsigned __int128>(frame_phase) +
                                        static_cast<unsigned __int128>(elapsed) * rate();
        const uint64_t frames = uint64_t(total / clock);
        frame_phase = uint64_t(total % clock);
        if (frames) {
            render(bus, frames);
            queue_host_output();
        }

        // Keep audible runs close to real time and cap latency. Startup before
        // the first hardware audio enable remains unthrottled.
        const Uint32 bytes_per_second = rate() * 2 * sizeof(int16_t);
        while (device && SDL_GetQueuedAudioSize(device) > bytes_per_second / 8) {
            SDL_Delay(1);
        }
    }
};

} // namespace mobigo
