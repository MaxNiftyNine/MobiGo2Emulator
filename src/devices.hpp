#pragma once

#include "common.hpp"

namespace mobigo {

struct NandDevice {
    static constexpr uint32_t kDataBytesPerPage = 2048;
    static constexpr uint32_t kSpareBytesPerPage = 64;
    static constexpr uint32_t kTotalBytesPerPage = kDataBytesPerPage + kSpareBytesPerPage;

    std::vector<uint8_t> bytes;
    uint16_t command = 0;
    uint16_t addr_low = 0;
    uint16_t addr_high = 0;
    uint16_t ctrl = 0;
    uint16_t type = 0;
    uint16_t dma_int_ctrl = 0x1600;
    uint32_t cursor = 0;
    uint32_t effective = 0;
    bool column_valid = true;
    uint8_t status = 0x40;
    uint32_t log_count = 0;
    bool dirty = false;

    uint32_t selected_page() const {
        // The GPL16250 software uses two distinct controller conventions.
        // The ROM's NAND1 routine manually supplies a byte column in AddrL
        // and a page in AddrH. The later physical-page driver programs
        // P_NF_Type=0x27 for reads and 0x37 for programming, transfers all
        // 0x840 raw bytes at once, and supplies the page number across
        // AddrL/AddrH (observed 0x400, 0x401, 0x440, 0x441 at block
        // boundaries). The latter is controller-level packed addressing, not
        // a literal column above the end of a 0x840-byte page.
        if (packed_large_page_mode()) return uint32_t(addr_low) | (uint32_t(addr_high) << 16);
        return addr_high;
    }

    bool packed_large_page_mode() const {
        return (type & 0x0f) == 0x07;
    }

    void recalc() {
        uint32_t page = selected_page();
        if (packed_large_page_mode() && page >= 0x1000) {
            auto page_has_programmed_data = [&](uint32_t p) -> bool {
                const uint64_t first = uint64_t(p) * kTotalBytesPerPage;
                if (first >= bytes.size()) return false;
                const uint64_t end = std::min<uint64_t>(first + kTotalBytesPerPage, bytes.size());
                for (uint64_t i = first; i < end; ++i) {
                    if (bytes[size_t(i)] != 0xff) return true;
                }
                return false;
            };
            const uint32_t shifted_page = page >> 4;
            if (!page_has_programmed_data(page) && page_has_programmed_data(shifted_page)) {
                if (g_log) {
                    g_log << "NAND packed page remap raw=0x" << std::hex << page
                          << " shifted=0x" << shifted_page << std::dec << "\n";
                }
                page = shifted_page;
            }
        }
        uint32_t column = packed_large_page_mode() ? 0 : addr_low;

        // Firmware evidence for this exact board is stronger than the related
        // chip's sector-oriented examples: its NAND loader selects physical
        // pages with AddrH=0,1,2,... and reads AddrL 0x000/0x200/0x400/0x600
        // for the four 512-byte data chunks, followed by
        // 0x800/0x810/0x820/0x830 for the four 16-byte spare chunks.
        // The dump also repeats its block header every 0x21000 bytes, exactly
        // 64 * (2048 + 64). Therefore AddrH is modeled as the physical page
        // and AddrL as a large-page column with spare data based at 0x800.
        //
        // ASSUMPTION: ctrl bit 14 makes the byte column word-addressed. The
        // board's x8 NAND boot path currently leaves it clear.
        if (ctrl & 0x4000) column *= 2;

        const uint32_t page_base = page * kTotalBytesPerPage;
        if (column < kDataBytesPerPage) {
            effective = page_base + column;
            column_valid = true;
        } else if (column < kDataBytesPerPage + kSpareBytesPerPage) {
            effective = page_base + kDataBytesPerPage +
                        (column - kDataBytesPerPage);
            column_valid = true;
        } else {
            // AddrL is the raw first/second NAND address-cycle pair. A
            // 2048+64-byte page has no columns at or above 0x840. Do not let
            // an invalid column alias the following physical page; firmware
            // deliberately probes columns such as 0xac0 while identifying
            // geometry. The external NAND data bus remains erased/high for
            // these out-of-range reads.
            effective = page_base;
            column_valid = false;
        }
        if (g_log) g_log << "NAND effective addr cmd=" << std::hex << command
                         << " page=" << page << " column=" << column
                         << " eff=" << effective << " valid=" << column_valid
                         << std::dec << "\n";
    }

    void erase_selected_block() {
        const uint32_t first_page = selected_page() & ~uint32_t(63);
        const uint64_t first = uint64_t(first_page) * kTotalBytesPerPage;
        const uint64_t end = first + uint64_t(64) * kTotalBytesPerPage;
        if (first >= bytes.size()) {
            status = 0x41;
            return;
        }
        const uint64_t bounded_end = std::min<uint64_t>(end, bytes.size());
        if (std::any_of(bytes.begin() + first, bytes.begin() + bounded_end,
                        [](uint8_t value) { return value != 0xff; })) dirty = true;
        std::fill(bytes.begin() + first, bytes.begin() + bounded_end, 0xff);
        status = 0x40;
        if (g_log) {
            g_log << "NAND erase block first_page=0x" << std::hex << first_page
                  << " raw_offset=0x" << first << std::dec << "\n";
        }
    }

    void write_data(uint16_t value) {
        if (command != 0x80 && command != 0x85) return;
        const uint32_t p = effective + cursor++;
        if (column_valid && p < bytes.size()) {
            // NAND programming can only change one bits to zero bits until
            // the containing block is erased.
            const uint8_t programmed = bytes[p] & uint8_t(value);
            if (programmed != bytes[p]) dirty = true;
            bytes[p] = programmed;
            status = 0x40;
        } else {
            status = 0x41;
        }
    }

    uint16_t read_data() {
        uint16_t result = 0xffff;
        if (command == 0x90) {
            // Board observation says Toshiba NAND. The related Generalplus
            // boot reference lists Toshiba 0x98,0xf1 as NAND1: 128 MiB,
            // 2048+64 bytes/page, 64 pages/block. The extended ID bytes are
            // ASSUMPTION until the exact Toshiba datasheet/marking is matched.
            static constexpr std::array<uint8_t, 5> id{0x98, 0xf1, 0x80, 0x15, 0x40};
            result = id[std::min<size_t>(cursor++, id.size() - 1)];
        } else if (command == 0x70) {
            // Standard large-page NAND status: bit 6 ready, bit 0 pass/fail.
            result = status;
        } else if (command == 0x00 || command == 0x01 || command == 0x30 || command == 0x50) {
            const uint32_t p = effective + cursor++;
            const bool remains_in_page = column_valid &&
                cursor <= kTotalBytesPerPage - (effective % kTotalBytesPerPage);
            result = remains_in_page && p < bytes.size() ? bytes[p] : 0xff;
        } else {
            if (g_log) g_log << "NAND read with unknown command " << std::hex << command << std::dec << "\n";
        }
        if (g_log && log_count++ < 160) {
            g_log << "NAND data cmd=0x" << std::hex << command
                  << " cursor=0x" << (cursor ? cursor - 1 : 0)
                  << " -> 0x" << result << std::dec << "\n";
        }
        return result;
    }
};

struct SpiNorDevice {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> rx_fifo;
    uint8_t command = 0;
    uint32_t address = 0;
    uint8_t address_bytes_left = 0;
    uint8_t dummy_bytes_left = 0;
    uint8_t id_index = 0;
    uint32_t log_count = 0;
    uint32_t read_command_log_count = 0;
    bool chip_selected = false;

    void reset_transaction() {
        command = 0;
        address = 0;
        address_bytes_left = 0;
        dummy_bytes_left = 0;
        id_index = 0;
        rx_fifo.clear();
    }

    void set_chip_select(bool selected, uint32_t pc_for_log) {
        if (selected == chip_selected) return;
        chip_selected = selected;
        // Firmware drives GPIO-B bit 4 as the active-low SPI NOR chip select.
        // Each assertion starts a fresh flash command and each deassertion
        // terminates it, including unread FIFO response bytes.
        reset_transaction();
        if (g_log && log_count++ < 64) {
            g_log << "SPI CS " << (selected ? "assert" : "deassert")
                  << " pc=0x" << std::hex << pc_for_log << std::dec << "\n";
        }
    }

    uint8_t read_flash() {
        if (bytes.empty()) return 0xff;
        const uint8_t v = bytes[address % bytes.size()];
        address = (address + 1) & 0x00ffffff;
        return v;
    }

    uint8_t next_id_byte() {
        // Board SPI chip is MX25L1606E-compatible: Macronix manufacturer
        // 0xc2, memory type 0x20, 16-Mbit capacity 0x15.
        static constexpr std::array<uint8_t, 3> id{0xc2, 0x20, 0x15};
        return id[std::min<size_t>(id_index++, id.size() - 1)];
    }

    void write_tx(uint16_t value, uint32_t pc_for_log) {
        const uint8_t data = uint8_t(value);
        if (command == 0) {
            command = data;
            address = 0;
            address_bytes_left = 0;
            dummy_bytes_left = 0;
            id_index = 0;
            // SPI is full duplex. Most commands return an undefined/high byte
            // while the opcode is shifted out. The GPL16250 ROM, however,
            // reads the MX25 status from the receive register immediately
            // after its 0x05 transfer and does not transmit a separate dummy
            // byte. Model that controller-visible timing specifically rather
            // than moving all command responses one byte early.
            rx_fifo.push_back(command == 0x05 ? 0x00 : 0xff);
            if (command == 0x03 || command == 0x0b) {
                address_bytes_left = 3;
            } else if (command == 0x9f) {
                // JEDEC ID bytes are produced by later dummy transfers.
            } else if (command == 0xab) {
                // Electronic signature is produced on a later transfer.
            } else if (command == 0x05) {
                // Status is produced on later transfers.
            } else if (command == 0xff || command == 0x00) {
                command = 0;
            } else if (command == 0x04) {
                // MX25L1606E WRDI. Writes are not implemented yet, but the
                // boot ROM issues this before read/status transactions.
            } else if (g_log && log_count++ < 64) {
                g_log << "SPI unknown command pc=0x" << std::hex << pc_for_log
                      << " cmd=0x" << int(command) << std::dec << "\n";
            }
            return;
        }

        if (command == 0x9f) {
            rx_fifo.push_back(next_id_byte());
            return;
        }

        if (command == 0x05) {
            rx_fifo.push_back(0x00);
            return;
        }

        if (command == 0xab) {
            rx_fifo.push_back(0x15); // ASSUMPTION: release/read electronic signature.
            return;
        }

        if (command == 0x03 || command == 0x0b) {
            if (address_bytes_left) {
                address = ((address << 8) | data) & 0x00ffffff;
                --address_bytes_left;
                if (address_bytes_left == 0 && command == 0x0b) dummy_bytes_left = 1;
                rx_fifo.push_back(0xff);
                if (address_bytes_left == 0 && g_log && read_command_log_count++ < 1024) {
                    g_log << "SPI read command pc=0x" << std::hex << pc_for_log
                          << " cmd=0x" << int(command) << " addr=0x" << address << std::dec << "\n";
                }
                return;
            }
            if (dummy_bytes_left) {
                --dummy_bytes_left;
                rx_fifo.push_back(0xff);
                return;
            }
            rx_fifo.push_back(read_flash());
            return;
        }

        rx_fifo.push_back(0xff);
    }

    uint16_t read_rx(uint32_t pc_for_log) {
        if (!rx_fifo.empty()) {
            const uint8_t v = rx_fifo.front();
            rx_fifo.erase(rx_fifo.begin());
            return v;
        }
        // The ROM uses system DMA from SPI_RXData after issuing 0x03 and its
        // address. In that controller mode the SPI block automatically clocks
        // further bytes; DMA reads therefore advance the flash stream without
        // explicit CPU writes of dummy bytes.
        if ((command == 0x03 || command == 0x0b) &&
            address_bytes_left == 0 && dummy_bytes_left == 0) {
            return read_flash();
        }
        if (g_log && log_count++ < 64) {
            g_log << "SPI RX underflow pc=0x" << std::hex << pc_for_log
                  << " cmd=0x" << int(command) << " addr=0x" << address
                  << " addr_left=" << std::dec << int(address_bytes_left) << "\n";
        }
        return 0xff;
    }
};

} // namespace mobigo
