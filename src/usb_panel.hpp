#pragma once

#include "bus.hpp"

namespace mobigo {

struct UsbHostProtocol {
    enum class Operation {
        None, Version, Serial, Disconnect, DisconnectStatus, ReadFlash, WriteFlash,
        WriteFile
    };
    enum class Stage {
        Idle, DiscoverBoot, DiscoverFat, SimpleRead,
        CommandRead, CommandWrite, DataRead, DataWrite,
        VerifyCommandRead, VerifyCommandWrite, VerifyDataRead,
        MailboxRingB, MailboxRequest, MailboxRingA, MailboxReply,
        FileDataRingBCommand, FileDataCommand, FileDataRingBPayload,
        FileDataPayload, FileDataRingA, FileDataReply,
        FileReadRingBCommand, FileReadCommand, FileReadRingAData,
        FileReadData, FileReadRingAAck, FileReadAck
    };
    enum class FilePhase {
        None, PathType, Open, PreSize, PreMeta, Timestamp, Chunk,
        FinalSize, FinalMeta, Close, NotifyOne, NotifyTwo, VerifyType, VerifyStat,
        VerifyOpen, VerifyChunk, VerifyClose
    };
    enum class BotPhase { Idle, SendCbw, SendData, ReceiveData, ReceiveCsw };

    Operation operation = Operation::None;
    Operation pending_operation = Operation::None;
    Stage stage = Stage::Idle;
    BotPhase bot_phase = BotPhase::Idle;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> result;
    std::vector<uint8_t> command_bytes;
    std::vector<uint8_t> bot_data;
    std::vector<uint8_t> file_chunk_data;
    std::array<uint8_t, 31> cbw{};
    size_t bot_position = 0;
    size_t bot_expected_bytes = 0;
    size_t position = 0;
    size_t expected = 0;
    size_t sectors_remaining = 0;
    size_t chunk_bytes = 0;
    size_t chunk_position = 0;
    uint64_t deadline = 0;
    uint32_t address = 0;
    uint32_t reserved_base = 0;
    uint32_t volume_lba = 0;
    uint32_t fat_lba = 0;
    uint32_t fat_scan_index = 0;
    uint32_t current_lba = 0;
    uint32_t bot_tag = 0;
    uint32_t next_tag = 1;
    bool reserved_base_valid = false;
    bool partition_boot_pending = false;
    bool bot_write = false;
    bool connected_seen = false;
    bool discovery_attempted = false;
    FilePhase file_phase = FilePhase::None;
    std::string remote_path;
    uint32_t file_handle = 0;
    bool file_create = false;
    uint32_t file_timestamp = 0;
    size_t file_read_step_sectors = 0;
    std::filesystem::path path;
    std::string status = "DISCONNECTED";

    bool busy() const { return stage != Stage::Idle || bot_phase != BotPhase::Idle; }

    void reset(const std::string &message = "DISCONNECTED") {
        operation = Operation::None;
        pending_operation = Operation::None;
        stage = Stage::Idle;
        bot_phase = BotPhase::Idle;
        payload.clear(); result.clear(); command_bytes.clear(); bot_data.clear(); file_chunk_data.clear();
        bot_position = bot_expected_bytes = position = expected = sectors_remaining = 0;
        chunk_bytes = chunk_position = 0;
        reserved_base_valid = false;
        volume_lba = 0;
        fat_lba = 0;
        fat_scan_index = 0;
        partition_boot_pending = false;
        connected_seen = false;
        discovery_attempted = false;
        file_phase = FilePhase::None;
        remote_path.clear();
        file_handle = 0;
        file_create = false;
        file_timestamp = 0;
        file_read_step_sectors = 0;
        status = message;
    }

    void fail(const std::string &message) {
        status = "ERROR: " + message;
        operation = pending_operation = Operation::None;
        stage = Stage::Idle;
        bot_phase = BotPhase::Idle;
    }

    void refresh_deadline(Bus &bus) { deadline = bus.cycles + 480000000ull; }

    static uint16_t le16(const uint8_t *p) {
        return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
    }

    bool start_scsi(Bus &bus, uint32_t lba, bool write,
                    const std::vector<uint8_t> &data = {}, uint16_t blocks = 1) {
        if (!bus.usb_enumerated || bot_phase != BotPhase::Idle) return false;
        if (blocks == 0) return false;
        bot_write = write;
        current_lba = lba;
        bot_data = data;
        bot_expected_bytes = size_t(blocks) * 512;
        if (write && bot_data.size() != bot_expected_bytes) return false;
        if (!write) bot_data.clear();
        bot_position = 0;
        bot_tag = next_tag++;
        cbw.fill(0);
        cbw[0] = 'U'; cbw[1] = 'S'; cbw[2] = 'B'; cbw[3] = 'C';
        cbw[4] = uint8_t(bot_tag); cbw[5] = uint8_t(bot_tag >> 8);
        cbw[6] = uint8_t(bot_tag >> 16); cbw[7] = uint8_t(bot_tag >> 24);
        const uint32_t transfer_bytes = uint32_t(bot_expected_bytes);
        cbw[8] = uint8_t(transfer_bytes); cbw[9] = uint8_t(transfer_bytes >> 8);
        cbw[10] = uint8_t(transfer_bytes >> 16); cbw[11] = uint8_t(transfer_bytes >> 24);
        cbw[12] = write ? 0x00 : 0x80;
        cbw[14] = 10;
        cbw[15] = write ? 0x2a : 0x28; // SCSI WRITE(10) / READ(10)
        cbw[17] = uint8_t(lba >> 24); cbw[18] = uint8_t(lba >> 16);
        cbw[19] = uint8_t(lba >> 8); cbw[20] = uint8_t(lba);
        cbw[22] = uint8_t(blocks >> 8); cbw[23] = uint8_t(blocks);
        bot_phase = BotPhase::SendCbw;
        refresh_deadline(bus);
        return true;
    }

    // Returns 1 when a command completes, 0 while it is in progress, -1 on error.
    int poll_bot(Bus &bus) {
        if (bot_phase == BotPhase::Idle) return 0;
        if (bus.cycles > deadline) { fail("USB MASS STORAGE TIMEOUT"); return -1; }
        if (bot_phase == BotPhase::SendCbw) {
            if (!bus.usb_host_send_bulk_out(cbw.data(), cbw.size())) return 0;
            bot_phase = bot_write ? BotPhase::SendData : BotPhase::ReceiveData;
            bot_position = 0;
            refresh_deadline(bus);
            return 0;
        }
        if (bot_phase == BotPhase::SendData) {
            const size_t count = std::min<size_t>(64, bot_data.size() - bot_position);
            if (!bus.usb_host_send_bulk_out(bot_data.data() + bot_position, count)) return 0;
            bot_position += count;
            refresh_deadline(bus);
            if (bot_position == bot_data.size()) bot_phase = BotPhase::ReceiveCsw;
            return 0;
        }
        std::vector<uint8_t> packet;
        if (!bus.usb_host_take_bulk_in(packet)) return 0;
        refresh_deadline(bus);
        if (bot_phase == BotPhase::ReceiveData) {
            const size_t count = std::min(bot_expected_bytes - bot_data.size(), packet.size());
            bot_data.insert(bot_data.end(), packet.begin(), packet.begin() + count);
            if (bot_data.size() == bot_expected_bytes) bot_phase = BotPhase::ReceiveCsw;
            return 0;
        }
        if (packet.size() != 13 || packet[0] != 'U' || packet[1] != 'S' ||
            packet[2] != 'B' || packet[3] != 'S') {
            fail("INVALID USB MASS STORAGE STATUS"); return -1;
        }
        const uint32_t tag = uint32_t(packet[4]) | (uint32_t(packet[5]) << 8) |
                             (uint32_t(packet[6]) << 16) | (uint32_t(packet[7]) << 24);
        const uint32_t residue = uint32_t(packet[8]) | (uint32_t(packet[9]) << 8) |
                                 (uint32_t(packet[10]) << 16) | (uint32_t(packet[11]) << 24);
        if (tag != bot_tag || residue != 0 || packet[12] != 0) {
            fail("SCSI COMMAND FAILED"); return -1;
        }
        bot_phase = BotPhase::Idle;
        return 1;
    }

    static std::vector<uint8_t> flash_command(uint32_t flash_address, size_t length) {
        const uint16_t sectors = uint16_t(std::min<size_t>(0xffff, (length + 511) / 512));
        // Exact eight-byte record built by VTech2010USBDllU.dll at
        // 0x100084e7/0x10008607: big-endian address, opcode, reserved byte,
        // then big-endian sector count.
        return {uint8_t(flash_address >> 24), uint8_t(flash_address >> 16),
                uint8_t(flash_address >> 8), uint8_t(flash_address),
                0x06, 0x00, uint8_t(sectors >> 8), uint8_t(sectors)};
    }

    bool request(Bus &bus, Operation op) {
        if (busy() || !bus.usb_host_connected) return false;
        operation = op;
        // File commands use fixed whole-disk mailbox LBAs observed on USB.
        // They do not depend on the separate vendor flash window's FAT-derived
        // base and therefore must not be gated on that discovery scan.
        if (op == Operation::WriteFile) {
            if (!bus.usb_enumerated) { operation = Operation::None; return false; }
            begin_operation(bus);
            return true;
        }
        if (!reserved_base_valid) {
            pending_operation = op;
            stage = Stage::DiscoverBoot;
            discovery_attempted = true;
            status = "READING DEVICE FAT16 GEOMETRY";
            if (bus.usb_enumerated) start_scsi(bus, 0, false);
            return true;
        }
        begin_operation(bus);
        return true;
    }

    bool get_version(Bus &bus) { return request(bus, Operation::Version); }
    bool get_serial(Bus &bus) { return request(bus, Operation::Serial); }
    bool get_disconnect_status(Bus &bus) { return request(bus, Operation::DisconnectStatus); }
    bool request_disconnect(Bus &bus) {
        command_bytes.assign(16, 0);
        command_bytes[0] = 0xf0; command_bytes[1] = 0xd2;
        command_bytes[14] = 0x47; command_bytes[15] = 0x50;
        return request(bus, Operation::Disconnect);
    }

    bool read_flash(Bus &bus, uint32_t flash_address, size_t length,
                    const std::filesystem::path &output) {
        if (length == 0 || length > uint64_t(UINT32_MAX) - flash_address + 1) return false;
        address = flash_address; expected = length; path = output;
        return request(bus, Operation::ReadFlash);
    }

    bool write_flash(Bus &bus, uint32_t flash_address,
                     const std::filesystem::path &input) {
        if (busy()) return false;
        try { payload = read_file_bytes(input); }
        catch (const std::exception &e) { fail(e.what()); return false; }
        if (payload.empty()) { fail("INPUT FILE IS EMPTY"); return false; }
        if (payload.size() > UINT32_MAX) { fail("INPUT FILE EXCEEDS PROTOCOL SIZE"); return false; }
        if (payload.size() > uint64_t(UINT32_MAX) - flash_address + 1) {
            fail("INPUT EXCEEDS 32-BIT FLASH ADDRESS SPACE"); return false;
        }
        address = flash_address; expected = payload.size(); path = input;
        return request(bus, Operation::WriteFlash);
    }

    static void put_le32(std::vector<uint8_t> &out, size_t offset, uint32_t value) {
        out[offset] = uint8_t(value);
        out[offset + 1] = uint8_t(value >> 8);
        out[offset + 2] = uint8_t(value >> 16);
        out[offset + 3] = uint8_t(value >> 24);
    }

    static uint32_t get_le32(const std::vector<uint8_t> &in, size_t offset) {
        if (offset + 4 > in.size()) return 0;
        return uint32_t(in[offset]) | (uint32_t(in[offset + 1]) << 8) |
               (uint32_t(in[offset + 2]) << 16) | (uint32_t(in[offset + 3]) << 24);
    }

    static std::vector<uint8_t> mailbox_command(uint32_t opcode,
                                                 uint32_t arg0 = 0,
                                                 uint32_t arg1 = 0) {
        std::vector<uint8_t> command(512, 0);
        put_le32(command, 0, opcode);
        put_le32(command, 4, arg0);
        put_le32(command, 8, arg1);
        return command;
    }

    static std::vector<uint8_t> mailbox_doorbell(size_t sectors) {
        std::vector<uint8_t> control(512, 0);
        put_le32(control, 0, 0x00002800);
        put_le32(control, 4, 0x00000006 | (uint32_t(sectors) << 24));
        return control;
    }

    bool write_file(Bus &bus, const std::string &device_path,
                    const std::filesystem::path &input) {
        if (busy()) return false;
        std::string normalized = device_path;
        std::replace(normalized.begin(), normalized.end(), '/', '\\');
        if (normalized.empty() || normalized.size() > 41) {
            fail("DEVICE PATH MUST BE 1 TO 41 ASCII CHARACTERS");
            return false;
        }
        if (!std::all_of(normalized.begin(), normalized.end(), [](unsigned char c) {
                return c >= 0x20 && c < 0x7f;
            })) {
            fail("DEVICE PATH MUST BE ASCII");
            return false;
        }
        try { payload = read_file_bytes(input); }
        catch (const std::exception &e) { fail(e.what()); return false; }
        if (payload.empty()) { fail("INPUT FILE IS EMPTY"); return false; }
        if (payload.size() > UINT32_MAX) { fail("INPUT FILE EXCEEDS PROTOCOL SIZE"); return false; }
        remote_path = normalized;
        expected = payload.size();
        path = input;
        file_handle = 0;
        file_create = false;
        file_timestamp = uint32_t(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        return request(bus, Operation::WriteFile);
    }

    void begin_mailbox_command(Bus &bus, std::vector<uint8_t> command) {
        command_bytes = std::move(command);
        result.clear();
        stage = Stage::MailboxRingB;
        start_scsi(bus, 15834, true, mailbox_doorbell(1));
    }

    void begin_path_command(Bus &bus, uint32_t opcode, uint16_t mode = 0) {
        std::vector<uint8_t> command(512, 0);
        put_le32(command, 0, opcode);
        std::copy(remote_path.begin(), remote_path.end(), command.begin() + 4);
        if (opcode == 0x02) {
            command[46] = uint8_t(mode);
            command[47] = uint8_t(mode >> 8);
        }
        begin_mailbox_command(bus, std::move(command));
    }

    void begin_file_phase(Bus &bus, FilePhase next) {
        file_phase = next;
        if (next == FilePhase::PathType) {
            status = "CHECKING DEVICE PATH";
            begin_path_command(bus, 0x10);
        } else if (next == FilePhase::Open) {
            status = "OPENING DEVICE FILE";
            begin_path_command(bus, 0x02, 2);
        } else if (next == FilePhase::PreSize) {
            begin_mailbox_command(bus, mailbox_command(0x0c, 0, file_handle));
        } else if (next == FilePhase::PreMeta || next == FilePhase::FinalMeta) {
            auto command = mailbox_command(0x0d);
            command[4] = uint8_t(file_handle);
            command[5] = uint8_t(file_handle >> 8);
            // The DLL helper only defines the low handle half.  For creation,
            // 0x004f is the stable value used by the recovered desktop flow;
            // replacement leaves the otherwise inherited upper half clear.
            const uint16_t upper = next == FilePhase::PreMeta && file_create ? 0x004f : 0;
            command[6] = uint8_t(upper);
            command[7] = uint8_t(upper >> 8);
            begin_mailbox_command(bus, std::move(command));
        } else if (next == FilePhase::Timestamp) {
            begin_mailbox_command(bus, mailbox_command(0x0e, file_handle, file_timestamp));
        } else if (next == FilePhase::Chunk) {
            begin_file_chunk(bus);
        } else if (next == FilePhase::FinalSize) {
            begin_mailbox_command(bus, mailbox_command(0x0c, uint32_t(expected), file_handle));
        } else if (next == FilePhase::Close) {
            begin_mailbox_command(bus, mailbox_command(0x05, file_handle));
        } else if (next == FilePhase::VerifyType) {
            status = "VERIFYING DEVICE FILE";
            begin_path_command(bus, 0x10);
        } else if (next == FilePhase::VerifyStat) {
            begin_path_command(bus, 0x09);
        } else if (next == FilePhase::VerifyOpen) {
            begin_path_command(bus, 0x02, 1);
        } else if (next == FilePhase::VerifyChunk) {
            begin_file_read_chunk(bus);
        } else if (next == FilePhase::VerifyClose) {
            begin_mailbox_command(bus, mailbox_command(0x05, file_handle));
        } else {
            begin_mailbox_command(bus, mailbox_command(0x11, 0x41));
        }
    }

    void begin_file_chunk(Bus &bus) {
        static constexpr size_t kChunkBytes = 0x80 * 512;
        chunk_bytes = std::min(kChunkBytes, expected - position);
        const size_t wire_bytes = (chunk_bytes + 511) & ~size_t(511);
        command_bytes = mailbox_command(0x04, file_handle, uint32_t(wire_bytes));
        file_chunk_data.assign(wire_bytes, 0);
        std::copy_n(payload.begin() + position, chunk_bytes, file_chunk_data.begin());
        sectors_remaining = wire_bytes / 512;
        stage = Stage::FileDataRingBCommand;
        status = "UPLOADING " + std::to_string(position) + " / " + std::to_string(expected);
        start_scsi(bus, 15834, true, mailbox_doorbell(1));
    }

    void begin_file_read_chunk(Bus &bus) {
        // Captured file reads request 0x28000-byte stream chunks, delivered
        // through 128-, 128-, and 64-sector reply doorbells.
        static constexpr size_t kChunkBytes = 0x28000;
        chunk_bytes = std::min(kChunkBytes, expected - position);
        const size_t wire_bytes = (chunk_bytes + 511) & ~size_t(511);
        command_bytes = mailbox_command(0x03, file_handle, uint32_t(wire_bytes));
        sectors_remaining = wire_bytes / 512;
        chunk_position = 0;
        stage = Stage::FileReadRingBCommand;
        status = "VERIFYING CONTENT " + std::to_string(position) + " / " +
                 std::to_string(expected);
        start_scsi(bus, 15834, true, mailbox_doorbell(1));
    }

    void begin_flash_chunk(Bus &bus) {
        // The DLL restarts the command for each chunk.  Its staging windows
        // are adjacent 0x100-sector regions (read at 0x3b00, write at 0x3c00),
        // so no command may spill into the following private region.
        static constexpr size_t kWindowBytes = 0x100 * 512;
        chunk_bytes = std::min(kWindowBytes, expected - position);
        chunk_position = 0;
        command_bytes = flash_command(address + uint32_t(position), chunk_bytes);
        stage = Stage::CommandRead;
        const uint32_t selector = operation == Operation::ReadFlash ? 0x3d28 : 0x3d2a;
        start_scsi(bus, reserved_base + selector, false);
    }

    void begin_write_verification(Bus &bus) {
        command_bytes = flash_command(address + uint32_t(position), chunk_bytes);
        stage = Stage::VerifyCommandRead;
        status = "VERIFYING FLASH WRITE";
        start_scsi(bus, reserved_base + 0x3d28, false);
    }

    void begin_operation(Bus &bus) {
        pending_operation = Operation::None;
        status = "COMMAND IN PROGRESS";
        if (operation == Operation::Version) {
            stage = Stage::SimpleRead; start_scsi(bus, reserved_base + 0x3020, false);
        } else if (operation == Operation::Serial) {
            stage = Stage::SimpleRead; start_scsi(bus, reserved_base + 0x30d4, false);
        } else if (operation == Operation::DisconnectStatus) {
            stage = Stage::SimpleRead; start_scsi(bus, reserved_base + 0x30d3, false);
        } else if (operation == Operation::ReadFlash || operation == Operation::WriteFlash) {
            position = 0;
            result.clear();
            begin_flash_chunk(bus);
        } else if (operation == Operation::WriteFile) {
            position = 0;
            result.clear();
            begin_file_phase(bus, FilePhase::PathType);
        } else {
            stage = Stage::CommandRead;
            start_scsi(bus, reserved_base + 0x30d2, false);
        }
    }

    bool parse_boot_sector() {
        if (bot_data.size() != 512) return false;
        const bool jump = (bot_data[0] == 0xeb && bot_data[2] == 0x90) || bot_data[0] == 0xe9;
        if (!jump || std::memcmp(bot_data.data() + 3, "MSWIN4.1", 8) != 0 ||
            std::memcmp(bot_data.data() + 0x36, "FAT16   ", 8) != 0 ||
            le16(bot_data.data() + 0x0b) != 512 || le16(bot_data.data() + 0x11) != 512)
            return false;
        const uint32_t reserved = le16(bot_data.data() + 0x0e);
        const uint32_t fats = bot_data[0x10];
        const uint32_t sectors_per_fat = le16(bot_data.data() + 0x16);
        if (fats == 0 || sectors_per_fat < 64) return false;
        // The vendor DLL derives the private window from the first data
        // sector after two FATs and the fixed 512-entry root directory.
        fat_lba = volume_lba + reserved;
        reserved_base = fat_lba + fats * sectors_per_fat + 32;
        return true;
    }

    bool validate_reserved_fat_sector() const {
        if (bot_data.size() != 512) return false;
        size_t offset = 0;
        if (fat_scan_index == 0) {
            if (le16(bot_data.data()) != 0xfff8) return false;
            offset = 2;
        }
        for (; offset < 512; offset += 2) {
            const uint16_t entry = le16(bot_data.data() + offset);
            if (entry != 0xfff7 && entry != 0xffff) return false;
        }
        return true;
    }

    bool find_fat_partition() {
        if (bot_data.size() != 512 || bot_data[510] != 0x55 || bot_data[511] != 0xaa)
            return false;
        for (size_t i = 0; i < 4; ++i) {
            const uint8_t *entry = bot_data.data() + 446 + i * 16;
            const uint8_t type = entry[4];
            if (type != 0x01 && type != 0x04 && type != 0x06 && type != 0x0e) continue;
            const uint32_t first = uint32_t(entry[8]) | (uint32_t(entry[9]) << 8) |
                                   (uint32_t(entry[10]) << 16) | (uint32_t(entry[11]) << 24);
            if (first != 0) { volume_lba = first; return true; }
        }
        return false;
    }

    void finish_operation() {
        const Operation done = operation;
        if (done == Operation::Version || done == Operation::Serial) {
            const size_t limit = done == Operation::Version ? 0x27 : 0x24;
            std::string text;
            for (size_t i = 0; i < std::min(limit, result.size()) && result[i]; ++i)
                text += result[i] >= 0x20 && result[i] < 0x7f ? char(result[i]) : '.';
            status = (done == Operation::Version ? "VERSION: " : "SERIAL: ") + text;
        } else if (done == Operation::DisconnectStatus) {
            status = result.empty() ? "NO STATUS BYTE" :
                std::string("DISCONNECT STATUS: ") + (result[0] ? "REQUESTED" : "CLEAR");
        } else if (done == Operation::Disconnect) {
            status = "DISCONNECT REQUEST SENT";
        } else if (done == Operation::ReadFlash) {
            if (result.size() > expected) result.resize(expected);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) { fail("CANNOT OPEN OUTPUT FILE"); return; }
            out.write(reinterpret_cast<const char *>(result.data()),
                      static_cast<std::streamsize>(result.size()));
            if (!out) { fail("OUTPUT WRITE FAILED"); return; }
            status = "READ COMPLETE: " + std::to_string(result.size()) + " BYTES";
        } else if (done == Operation::WriteFlash) {
            status = "WRITE COMPLETE: " + std::to_string(expected) + " BYTES";
        } else if (done == Operation::WriteFile) {
            status = "FILE UPLOAD COMPLETE: " + std::to_string(expected) + " BYTES";
        }
        operation = Operation::None;
        file_phase = FilePhase::None;
        stage = Stage::Idle;
    }

    void advance_file_command(Bus &bus) {
        if (file_phase == FilePhase::PathType) {
            const uint16_t kind = uint16_t(get_le32(result, 0));
            if (kind != 0 && kind != 1) {
                fail("DEVICE PATH IS NOT A FILE");
                return;
            }
            file_create = kind == 0;
            begin_file_phase(bus, FilePhase::Open);
        } else if (file_phase == FilePhase::Open) {
            file_handle = get_le32(result, 0);
            if (file_handle == 0) {
                fail("DEVICE REFUSED FILE OPEN");
                return;
            }
            begin_file_phase(bus, FilePhase::PreSize);
        } else if (file_phase == FilePhase::PreSize) {
            begin_file_phase(bus, FilePhase::PreMeta);
        } else if (file_phase == FilePhase::PreMeta) {
            begin_file_phase(bus, file_create ? FilePhase::Timestamp : FilePhase::Chunk);
        } else if (file_phase == FilePhase::Timestamp) {
            begin_file_phase(bus, FilePhase::Chunk);
        } else if (file_phase == FilePhase::FinalSize) {
            begin_file_phase(bus, FilePhase::FinalMeta);
        } else if (file_phase == FilePhase::FinalMeta) {
            begin_file_phase(bus, FilePhase::Close);
        } else if (file_phase == FilePhase::Close) {
            if (file_create) begin_file_phase(bus, FilePhase::NotifyOne);
            else begin_file_phase(bus, FilePhase::VerifyType);
        } else if (file_phase == FilePhase::NotifyOne) {
            begin_file_phase(bus, FilePhase::NotifyTwo);
        } else if (file_phase == FilePhase::NotifyTwo) {
            begin_file_phase(bus, FilePhase::VerifyType);
        } else if (file_phase == FilePhase::VerifyType) {
            if (uint16_t(get_le32(result, 0)) != 1) {
                fail("UPLOADED DEVICE PATH IS NOT A FILE");
                return;
            }
            begin_file_phase(bus, FilePhase::VerifyStat);
        } else if (file_phase == FilePhase::VerifyStat) {
            if (get_le32(result, 4) != uint32_t(expected)) {
                fail("UPLOADED FILE SIZE VERIFY FAILED");
                return;
            }
            begin_file_phase(bus, FilePhase::VerifyOpen);
        } else if (file_phase == FilePhase::VerifyOpen) {
            file_handle = get_le32(result, 0);
            // Some logical DEFAULT files report their backing container size
            // from open even though stat and the stream expose logical bytes.
            if (file_handle == 0) {
                fail("UPLOADED FILE COULD NOT BE OPENED FOR VERIFY");
                return;
            }
            position = 0;
            begin_file_phase(bus, FilePhase::VerifyChunk);
        } else if (file_phase == FilePhase::VerifyClose) {
            finish_operation();
        }
    }

    void advance(Bus &bus) {
        if (stage == Stage::MailboxRingB) {
            stage = Stage::MailboxRequest;
            start_scsi(bus, 15536, true, command_bytes);
            return;
        }
        if (stage == Stage::MailboxRequest) {
            stage = Stage::MailboxRingA;
            start_scsi(bus, 15832, true, mailbox_doorbell(1));
            return;
        }
        if (stage == Stage::MailboxRingA) {
            stage = Stage::MailboxReply;
            start_scsi(bus, 15280, false);
            return;
        }
        if (stage == Stage::MailboxReply) {
            result = bot_data;
            advance_file_command(bus);
            return;
        }
        if (stage == Stage::FileDataRingBCommand) {
            stage = Stage::FileDataCommand;
            start_scsi(bus, 15536, true, command_bytes);
            return;
        }
        if (stage == Stage::FileDataCommand) {
            stage = Stage::FileDataRingBPayload;
            start_scsi(bus, 15834, true, mailbox_doorbell(sectors_remaining));
            return;
        }
        if (stage == Stage::FileDataRingBPayload) {
            stage = Stage::FileDataPayload;
            start_scsi(bus, 15536, true, file_chunk_data, uint16_t(sectors_remaining));
            return;
        }
        if (stage == Stage::FileDataPayload) {
            stage = Stage::FileDataRingA;
            start_scsi(bus, 15832, true, mailbox_doorbell(1));
            return;
        }
        if (stage == Stage::FileDataRingA) {
            stage = Stage::FileDataReply;
            start_scsi(bus, 15280, false);
            return;
        }
        if (stage == Stage::FileDataReply) {
            position += chunk_bytes;
            if (position < expected) begin_file_phase(bus, FilePhase::Chunk);
            else begin_file_phase(bus, FilePhase::FinalSize);
            return;
        }
        if (stage == Stage::FileReadRingBCommand) {
            stage = Stage::FileReadCommand;
            start_scsi(bus, 15536, true, command_bytes);
            return;
        }
        if (stage == Stage::FileReadCommand) {
            stage = Stage::FileReadRingAData;
            file_read_step_sectors = std::min<size_t>(sectors_remaining, 0x80);
            start_scsi(bus, 15832, true, mailbox_doorbell(file_read_step_sectors));
            return;
        }
        if (stage == Stage::FileReadRingAData) {
            stage = Stage::FileReadData;
            start_scsi(bus, 15280, false, {}, uint16_t(file_read_step_sectors));
            return;
        }
        if (stage == Stage::FileReadData) {
            size_t mismatch = 0;
            const size_t compare_bytes = std::min(bot_data.size(), chunk_bytes - chunk_position);
            while (mismatch < compare_bytes &&
                   bot_data[mismatch] == payload[position + chunk_position + mismatch]) ++mismatch;
            if (mismatch != compare_bytes) {
                fail("UPLOADED FILE CONTENT VERIFY FAILED AT " +
                     std::to_string(position + chunk_position + mismatch));
                return;
            }
            chunk_position += compare_bytes;
            sectors_remaining -= file_read_step_sectors;
            if (sectors_remaining != 0) {
                stage = Stage::FileReadRingAData;
                file_read_step_sectors = std::min<size_t>(sectors_remaining, 0x80);
                start_scsi(bus, 15832, true, mailbox_doorbell(file_read_step_sectors));
                return;
            }
            if (chunk_position != chunk_bytes) {
                fail("SHORT DEVICE FILE READ DURING VERIFY");
                return;
            }
            stage = Stage::FileReadRingAAck;
            start_scsi(bus, 15832, true, mailbox_doorbell(1));
            return;
        }
        if (stage == Stage::FileReadRingAAck) {
            stage = Stage::FileReadAck;
            start_scsi(bus, 15280, false);
            return;
        }
        if (stage == Stage::FileReadAck) {
            position += chunk_bytes;
            if (position < expected) begin_file_phase(bus, FilePhase::VerifyChunk);
            else begin_file_phase(bus, FilePhase::VerifyClose);
            return;
        }
        if (stage == Stage::DiscoverBoot) {
            if (!parse_boot_sector()) {
                // The DLL opens the mounted Windows volume, while this host
                // speaks SCSI to the whole disk. Reproduce the OS partition
                // translation before applying the DLL's volume-relative LBAs.
                if (!partition_boot_pending && find_fat_partition()) {
                    partition_boot_pending = true;
                    start_scsi(bus, volume_lba, false);
                    return;
                }
                fail("VTECH FAT16 BOOT SECTOR NOT FOUND"); return;
            }
            stage = Stage::DiscoverFat;
            fat_scan_index = 0;
            status = "VALIDATING VTECH PRIVATE FAT AREA";
            start_scsi(bus, fat_lba, false);
            return;
        }
        if (stage == Stage::DiscoverFat) {
            if (!validate_reserved_fat_sector()) {
                fail("VTECH RESERVED FAT AREA NOT FOUND"); return;
            }
            ++fat_scan_index;
            if (fat_scan_index < 64) {
                start_scsi(bus, fat_lba + fat_scan_index, false);
                return;
            }
            reserved_base_valid = true;
            operation = pending_operation;
            status = "DEVICE READY";
            if (operation == Operation::None) stage = Stage::Idle;
            else begin_operation(bus);
            return;
        }
        if (stage == Stage::SimpleRead) {
            result = bot_data;
            finish_operation();
            return;
        }
        if (stage == Stage::CommandRead) {
            std::vector<uint8_t> sector = bot_data;
            std::copy(command_bytes.begin(), command_bytes.end(), sector.begin());
            stage = Stage::CommandWrite;
            start_scsi(bus, current_lba, true, sector);
            return;
        }
        if (stage == Stage::CommandWrite) {
            if (operation == Operation::Disconnect) { finish_operation(); return; }
            sectors_remaining = (chunk_bytes + 511) / 512;
            stage = operation == Operation::ReadFlash ? Stage::DataRead : Stage::DataWrite;
            if (stage == Stage::DataRead) {
                start_scsi(bus, reserved_base + 0x3b00, false, {},
                           uint16_t(sectors_remaining));
            } else {
                std::vector<uint8_t> window(sectors_remaining * 512, 0);
                std::copy_n(payload.begin() + position, chunk_bytes, window.begin());
                start_scsi(bus, reserved_base + 0x3c00, true, window,
                           uint16_t(sectors_remaining));
            }
            return;
        }
        if (stage == Stage::VerifyCommandRead) {
            std::vector<uint8_t> sector = bot_data;
            std::copy(command_bytes.begin(), command_bytes.end(), sector.begin());
            stage = Stage::VerifyCommandWrite;
            start_scsi(bus, current_lba, true, sector);
            return;
        }
        if (stage == Stage::VerifyCommandWrite) {
            stage = Stage::VerifyDataRead;
            start_scsi(bus, reserved_base + 0x3b00, false, {},
                       uint16_t((chunk_bytes + 511) / 512));
            return;
        }
        if (stage == Stage::VerifyDataRead) {
            size_t mismatch = 0;
            while (mismatch < chunk_bytes && mismatch < bot_data.size() &&
                   bot_data[mismatch] == payload[position + mismatch]) ++mismatch;
            if (mismatch != chunk_bytes) {
                std::ostringstream message;
                message << "FLASH WRITE VERIFY FAILED AT 0X" << std::hex
                        << std::uppercase
                        << (address + uint32_t(position + mismatch));
                fail(message.str());
                return;
            }
            position += chunk_bytes;
            if (position == expected) finish_operation();
            else begin_flash_chunk(bus);
            return;
        }
        if (stage == Stage::DataRead) {
            if (bot_data.size() < chunk_bytes) { fail("SHORT FLASH DATA WINDOW"); return; }
            result.insert(result.end(), bot_data.begin(), bot_data.begin() + chunk_bytes);
            position += chunk_bytes;
            if (position == expected) finish_operation();
            else begin_flash_chunk(bus);
            return;
        }
        if (stage == Stage::DataWrite) {
            begin_write_verification(bus);
        }
    }

    void poll(Bus &bus) {
        if (!bus.usb_host_connected) {
            if (connected_seen || status != "DISCONNECTED") reset();
            return;
        }
        connected_seen = true;
        if (!bus.usb_enumerated) { status = "ENUMERATING USB DEVICE"; return; }
        if (stage == Stage::Idle && status == "ENUMERATING USB DEVICE") status = "DEVICE CONNECTED";
        if (stage != Stage::Idle && bot_phase == BotPhase::Idle) {
            if (stage == Stage::DiscoverBoot) start_scsi(bus, 0, false);
        }
        const int bot = poll_bot(bus);
        if (bot > 0) advance(bus);
    }
};

struct UsbPanel {
    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    uint32_t window_id = 0;
    UsbHostProtocol protocol;
    std::string address = "00000000";
    std::string length = "00000200";
    std::string path = "usb_file.bin";
    // Neutral device-root example.  Installers discover any firmware-specific
    // bundle path instead of baking a regional filename into the UI.
    std::string remote_path = "A:\\MyHomebrew.MBA";
    int active_field = 4;
    std::chrono::steady_clock::time_point next_render{};

    struct Button { SDL_Rect rect; const char *label; int action; };
    std::array<Button, 9> buttons{{
        {{20, 20, 180, 34}, "CONNECT / DISCONNECT", 1},
        {{20, 72, 180, 34}, "GET VERSION", 2},
        {{220, 72, 180, 34}, "GET SERIAL", 3},
        {{420, 72, 180, 34}, "DISCONNECT STATUS", 4},
        {{20, 120, 180, 34}, "REQUEST DISCONNECT", 5},
        {{20, 390, 180, 38}, "READ FLASH TO FILE", 6},
        {{220, 390, 180, 38}, "WRITE FILE TO FLASH", 7},
        {{420, 390, 180, 38}, "UPLOAD DEVICE FILE", 9},
        {{20, 442, 180, 38}, "CLEAR STATUS", 8},
    }};

    static const char *glyph(char c) {
        c = char(std::toupper(static_cast<unsigned char>(c)));
        switch (c) {
        case 'A': return "01110100011000111111100011000110001";
        case 'B': return "11110100011000111110100011000111110";
        case 'C': return "01111100001000010000100001000001111";
        case 'D': return "11110100011000110001100011000111110";
        case 'E': return "11111100001000011110100001000011111";
        case 'F': return "11111100001000011110100001000010000";
        case 'G': return "01111100001000010111100011000101111";
        case 'H': return "10001100011000111111100011000110001";
        case 'I': return "11111001000010000100001000010011111";
        case 'J': return "00111000100001000010100101001001100";
        case 'K': return "10001100101010011000101001001010001";
        case 'L': return "10000100001000010000100001000011111";
        case 'M': return "10001110111010110101100011000110001";
        case 'N': return "10001110011010110011100011000110001";
        case 'O': return "01110100011000110001100011000101110";
        case 'P': return "11110100011000111110100001000010000";
        case 'Q': return "01110100011000110001101011001001101";
        case 'R': return "11110100011000111110101001001010001";
        case 'S': return "01111100001000001110000010000111110";
        case 'T': return "11111001000010000100001000010000100";
        case 'U': return "10001100011000110001100011000101110";
        case 'V': return "10001100011000110001100010101000100";
        case 'W': return "10001100011000110101101011101110001";
        case 'X': return "10001100010101000100010101000110001";
        case 'Y': return "10001100010101000100001000010000100";
        case 'Z': return "11111000010001000100010001000011111";
        case '0': return "01110100011001110101110011000101110";
        case '1': return "00100011000010000100001000010001110";
        case '2': return "01110100010000100010001000100011111";
        case '3': return "11110000010000101110000010000111110";
        case '4': return "00010001100101010010111110001000010";
        case '5': return "11111100001000011110000010000111110";
        case '6': return "01110100001000011110100011000101110";
        case '7': return "11111000010001000100010000100001000";
        case '8': return "01110100011000101110100011000101110";
        case '9': return "01110100011000101111000010000101110";
        case ':': return "00000001000010000000001000010000000";
        case '.': return "00000000000000000000000000010000100";
        case '/': return "00001000100001000100010001000010000";
        case '\\': return "10000010000010000010000010000100001";
        case '_': return "00000000000000000000000000000011111";
        case '-': return "00000000000000011111000000000000000";
        default: return "00000000000000000000000000000000000";
        }
    }

    void text(int x, int y, const std::string &s, int scale = 2, SDL_Color color = {225,225,225,255}) {
        SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
        for (char c : s) {
            const char *bits = glyph(c);
            for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col) {
                if (bits[row * 5 + col] == '1') {
                    SDL_Rect pixel{x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(ren, &pixel);
                }
            }
            x += 6 * scale;
        }
    }

    void init() {
        win = SDL_CreateWindow("MobiGo 2 USB Host", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, 640, 540, SDL_WINDOW_SHOWN);
        if (!win) die(SDL_GetError());
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
        if (!ren) die(SDL_GetError());
        window_id = SDL_GetWindowID(win);
        SDL_StartTextInput();
        render(true);
    }

    void shutdown() {
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        ren = nullptr; win = nullptr; window_id = 0;
    }

    static bool inside(const SDL_Rect &r, int x, int y) {
        return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
    }

    uint32_t address_value() const {
        try { return uint32_t(std::stoul(address, nullptr, 16)); } catch (...) { return 0; }
    }
    size_t length_value() const {
        try { return size_t(std::stoull(length, nullptr, 16)); } catch (...) { return 0; }
    }

    void action(Bus &bus, int which) {
        if (which == 1) bus.set_usb_host_connected(!bus.usb_host_connected);
        else if (which == 2 && !protocol.get_version(bus)) protocol.status = "DEVICE NOT READY";
        else if (which == 3 && !protocol.get_serial(bus)) protocol.status = "DEVICE NOT READY";
        else if (which == 4 && !protocol.get_disconnect_status(bus)) protocol.status = "DEVICE NOT READY";
        else if (which == 5 && !protocol.request_disconnect(bus)) protocol.status = "DEVICE NOT READY";
        else if (which == 6 && !protocol.read_flash(bus, address_value(), length_value(), path))
            protocol.status = "READ COMMAND REJECTED";
        else if (which == 7 && !protocol.write_flash(bus, address_value(), path))
            protocol.status = "WRITE COMMAND REJECTED";
        else if (which == 8 && !protocol.busy()) protocol.status.clear();
        else if (which == 9 && !protocol.write_file(bus, remote_path, path) &&
                 protocol.status.rfind("ERROR", 0) != 0)
            protocol.status = "FILE UPLOAD COMMAND REJECTED";
    }

    void event(Bus &bus, const SDL_Event &ev) {
        auto selected_field = [&]() -> std::string * {
            return active_field == 1 ? &address : active_field == 2 ? &length :
                   active_field == 3 ? &remote_path : &path;
        };
        auto append_text = [&](const std::string &text_value) {
            std::string *field = selected_field();
            for (char c : text_value) {
                if ((active_field >= 3 && c >= 0x20) ||
                    (active_field < 3 && std::isxdigit(static_cast<unsigned char>(c)))) {
                    field->push_back(c);
                }
            }
        };
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.windowID == window_id) {
            for (const Button &button : buttons)
                if (inside(button.rect, ev.button.x, ev.button.y)) action(bus, button.action);
            if (ev.button.y >= 168 && ev.button.y < 202) active_field = 1;
            else if (ev.button.y >= 210 && ev.button.y < 244) active_field = 2;
            else if (ev.button.y >= 252 && ev.button.y < 286) active_field = 3;
            else if (ev.button.y >= 294 && ev.button.y < 374) active_field = 4;
        }
        if (ev.type == SDL_KEYDOWN && ev.key.windowID == window_id && ev.key.keysym.sym == SDLK_BACKSPACE) {
            std::string *field = selected_field();
            if (!field->empty()) field->pop_back();
        }
        if (ev.type == SDL_KEYDOWN && ev.key.windowID == window_id &&
            (ev.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) && ev.key.keysym.sym == SDLK_a) {
            selected_field()->clear();
        }
        if (ev.type == SDL_KEYDOWN && ev.key.windowID == window_id &&
            (ev.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) && ev.key.keysym.sym == SDLK_v &&
            SDL_HasClipboardText()) {
            char *clipboard = SDL_GetClipboardText();
            if (clipboard) {
                append_text(clipboard);
                SDL_free(clipboard);
            }
        }
        if (ev.type == SDL_TEXTINPUT && ev.text.windowID == window_id) {
            append_text(ev.text.text);
        }
    }

    static std::string tail(const std::string &value, size_t count) {
        return value.size() <= count ? value : value.substr(value.size() - count);
    }

    void render(bool force = false) {
        if (!ren) return;
        const auto now = std::chrono::steady_clock::now();
        if (!force && now < next_render) return;
        next_render = now + std::chrono::milliseconds(33);
        SDL_SetRenderDrawColor(ren, 22, 25, 31, 255); SDL_RenderClear(ren);
        for (const Button &button : buttons) {
            SDL_SetRenderDrawColor(ren, 54, 75, 99, 255); SDL_RenderFillRect(ren, &button.rect);
            text(button.rect.x + 8, button.rect.y + 10, button.label, 1);
        }
        text(20, 176, "FLASH ADDRESS HEX", 2);
        text(20, 218, "LENGTH HEX", 2);
        text(20, 260, "DEVICE FILE PATH", 2);
        text(20, 302, "HOST FILE PATH", 2);
        std::array<std::pair<SDL_Rect, std::string *>, 4> fields{{
            {SDL_Rect{220, 168, 380, 34}, &address},
            {SDL_Rect{220, 210, 380, 34}, &length},
            {SDL_Rect{220, 252, 380, 34}, &remote_path},
            {SDL_Rect{220, 294, 380, 80}, &path}
        }};
        for (size_t i = 0; i < fields.size(); ++i) {
            SDL_SetRenderDrawColor(ren, active_field == int(i + 1) ? 72 : 43, 47, 58, 255);
            SDL_RenderFillRect(ren, &fields[i].first);
            text(fields[i].first.x + 8, fields[i].first.y + 9, tail(*fields[i].second, 30), 2);
        }
        text(20, 505, protocol.status.substr(0, 48), 2,
             protocol.status.rfind("ERROR", 0) == 0 ? SDL_Color{255,110,110,255} : SDL_Color{155,225,170,255});
        SDL_RenderPresent(ren);
    }
};

} // namespace mobigo
