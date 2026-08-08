#pragma once

#include "common.hpp"

#include <thread>

namespace mobigo {

// Converts emulated GPL16250 clock cycles into a cumulative real-time target.
// Conversion is separate from sleeping so it can be tested without depending
// on host scheduler timing.
struct RealtimeThrottle {
    using Clock = std::chrono::steady_clock;

    bool enabled = false;
    Clock::time_point wall_origin = Clock::now();
    uint64_t emulated_nanoseconds = 0;
    uint64_t fractional_numerator = 0;
    uint64_t fractional_clock_hz = 1;

    explicit RealtimeThrottle(bool should_enable)
        : enabled(should_enable) {}

    // Start a new host-time epoch. Deferred presentation uses this at the
    // exact MBA handoff so the uncapped boot is never counted as time that the
    // foreground application must sleep or catch up against.
    void rebase() {
        wall_origin = Clock::now();
        emulated_nanoseconds = 0;
        fractional_numerator = 0;
        fractional_clock_hz = 1;
    }

    void set_enabled(bool should_enable) {
        enabled = should_enable;
        rebase();
    }

    void advance_cycles(uint64_t cycles, uint64_t clock_hz) {
        if (!enabled || cycles == 0) return;
        clock_hz = std::max<uint64_t>(1, clock_hz);
        if (fractional_clock_hz != clock_hz) {
            fractional_numerator = uint64_t(
                (static_cast<unsigned __int128>(fractional_numerator) * clock_hz) /
                fractional_clock_hz);
            fractional_clock_hz = clock_hz;
        }
        const unsigned __int128 total =
            static_cast<unsigned __int128>(cycles) * 1000000000ull +
            fractional_numerator;
        emulated_nanoseconds += uint64_t(total / clock_hz);
        fractional_numerator = uint64_t(total % clock_hz);
    }

    void wait_until_current() {
        if (!enabled) return;
        const auto elapsed = std::chrono::nanoseconds(emulated_nanoseconds);
        auto target = wall_origin + elapsed;
        const auto now = Clock::now();

        // Avoid a long full-speed catch-up after pausing the process or severe
        // host overload. Small scheduler deficits remain cumulative.
        constexpr auto max_catch_up = std::chrono::milliseconds(250);
        if (now > target + max_catch_up) {
            wall_origin = now - elapsed;
            target = now;
        }
        if (now < target) std::this_thread::sleep_until(target);
    }
};

} // namespace mobigo
