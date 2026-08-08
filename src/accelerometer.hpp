#pragma once

#include "common.hpp"

namespace mobigo {

// MobiGo 2 firmware supports more than one accelerometer. Retail units using
// the newer path expose a Bosch BMA222E at 7-bit I2C address 0x18. The GPL16250
// has no I2C controller, so firmware bit-bangs the bus on IOE6 (SCL) and IOE7
// (SDA). This device model implements the transactions and registers used by
// the resident motion driver.
struct MotionAccelerometer {
    enum class Phase {
        idle,
        receive,
        receive_ack,
        send,
        send_ack,
    };

    static constexpr uint8_t kAddress = 0x18;
    static constexpr int16_t kOneG = 0x0400;

    Phase phase = Phase::idle;
    bool previous_scl = true;
    bool previous_host_sda = true;
    bool drive_sda_low = false;
    bool address_expected = true;
    bool expecting_register = false;
    bool selected = false;
    bool read_transaction = false;
    bool ack_seen = false;
    bool send_ack_released = false;
    bool host_acked = false;
    uint8_t receive_byte = 0;
    uint8_t bit_count = 0;
    uint8_t register_pointer = 0;
    uint8_t transmit_byte = 0;
    std::array<uint8_t, 0x40> registers{};

    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    int16_t x = 0;
    int16_t y = kOneG;
    int16_t z = 0;

    MotionAccelerometer() {
        registers[0x00] = 0xf8; // BMA222E BGW_CHIPID.
        registers[0x0f] = 0x03; // +/-2 g.
        registers[0x10] = 0x0f; // Reset bandwidth value.
    }

    void reset_bus() {
        phase = Phase::idle;
        previous_scl = true;
        previous_host_sda = true;
        drive_sda_low = false;
        address_expected = true;
        expecting_register = false;
        selected = false;
        read_transaction = false;
        ack_seen = false;
        send_ack_released = false;
        host_acked = false;
        receive_byte = 0;
        bit_count = 0;
    }

    void set_direction(unsigned direction, bool pressed) {
        switch (direction) {
        case 0: left = pressed; break;
        case 1: right = pressed; break;
        case 2: up = pressed; break;
        case 3: down = pressed; break;
        default: return;
        }
        x = int16_t((right ? kOneG : 0) - (left ? kOneG : 0));
        z = int16_t((up ? kOneG : 0) - (down ? kOneG : 0));
        y = (x == 0 && z == 0) ? kOneG : 0;
    }

    void clear_directions() {
        left = right = up = down = false;
        x = 0;
        y = kOneG;
        z = 0;
    }

    bool sda_is_low() const { return drive_sda_low; }

    static uint8_t signed_axis_byte(int value) {
        return uint8_t(int8_t(std::clamp(value / 16, -128, 127)));
    }

    uint8_t read_register(uint8_t reg) const {
        switch (reg) {
        case 0x00: return 0xf8;
        case 0x02: return 0x00;
        case 0x03: return signed_axis_byte(-x);
        case 0x04: return 0x00;
        case 0x05: return signed_axis_byte(y);
        case 0x06: return 0x00;
        case 0x07: return signed_axis_byte(-z);
        default: return reg < registers.size() ? registers[reg] : 0;
        }
    }

    void write_register(uint8_t reg, uint8_t value) {
        if (reg < registers.size() && reg != 0x00) registers[reg] = value;
    }

    void begin_start() {
        phase = Phase::receive;
        drive_sda_low = false;
        address_expected = true;
        selected = false;
        read_transaction = false;
        ack_seen = false;
        receive_byte = 0;
        bit_count = 0;
    }

    void begin_stop() {
        phase = Phase::idle;
        drive_sda_low = false;
        selected = false;
    }

    void finish_received_byte() {
        bool acknowledge = selected;
        if (address_expected) {
            const uint8_t address = receive_byte >> 1;
            read_transaction = (receive_byte & 1) != 0;
            selected = address == kAddress;
            acknowledge = selected;
            address_expected = false;
            if (selected && !read_transaction) expecting_register = true;
        } else if (selected && !read_transaction) {
            if (expecting_register) {
                register_pointer = receive_byte;
                expecting_register = false;
            } else {
                write_register(register_pointer++, receive_byte);
            }
        }
        phase = Phase::receive_ack;
        ack_seen = false;
        drive_sda_low = acknowledge;
    }

    void prepare_transmit_byte() {
        transmit_byte = read_register(register_pointer);
        bit_count = 0;
        phase = Phase::send;
        drive_sda_low = (transmit_byte & 0x80) == 0;
    }

    // Observe host-driven I2C line levels. host_sda is the master/open-drain
    // contribution only; the device's low contribution is returned separately
    // through sda_is_low() for GPIO pad reads.
    void observe(bool scl, bool host_sda) {
        const bool start = previous_host_sda && !host_sda && scl;
        const bool stop = !previous_host_sda && host_sda && scl;
        if (start) begin_start();
        else if (stop) begin_stop();

        const bool rising = !previous_scl && scl;
        const bool falling = previous_scl && !scl;

        if (!start && !stop && rising) {
            if (phase == Phase::receive) {
                receive_byte = uint8_t((receive_byte << 1) | (host_sda ? 1 : 0));
                if (++bit_count == 8) finish_received_byte();
            } else if (phase == Phase::receive_ack) {
                ack_seen = true;
            } else if (phase == Phase::send) {
                if (++bit_count == 8) {
                    phase = Phase::send_ack;
                    send_ack_released = false;
                }
            } else if (phase == Phase::send_ack && send_ack_released) {
                host_acked = !host_sda;
            }
        }

        if (!start && !stop && falling) {
            if (phase == Phase::receive_ack && ack_seen) {
                drive_sda_low = false;
                receive_byte = 0;
                bit_count = 0;
                if (!selected) phase = Phase::idle;
                else if (read_transaction) prepare_transmit_byte();
                else phase = Phase::receive;
            } else if (phase == Phase::send) {
                drive_sda_low = (transmit_byte & (0x80 >> bit_count)) == 0;
            } else if (phase == Phase::send_ack) {
                if (!send_ack_released) {
                    drive_sda_low = false;
                    send_ack_released = true;
                    host_acked = false;
                } else if (host_acked) {
                    ++register_pointer;
                    prepare_transmit_byte();
                } else {
                    phase = Phase::idle;
                }
            }
        }

        previous_scl = scl;
        previous_host_sda = host_sda;
    }
};

} // namespace mobigo
