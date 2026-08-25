#pragma once

#include "bus.hpp"

namespace mobigo {

struct Cpu {
    enum Reg : uint8_t { SP = 0, R1, R2, R3, R4, BP, SR, PC };
    Bus &bus;
    std::array<uint32_t, 16> r{};
    std::array<uint32_t, 4> sec{};
    uint32_t ss = 0;
    uint32_t mds = 0;
    uint32_t enable_irq = 0, enable_fiq = 0, fir_move = 1;
    uint32_t sb = 0, aq = 0, fra = 0, bnk = 0, ine = 0, pri = 8;
    bool halted = false;
    bool in_irq = false;
    bool in_fiq = false;
    bool suppress_interrupt_check = false;
    bool trace = false;
    bool trace_range = false;
    bool allow_invalid_alu_nop = false;
    uint32_t trace_lo = 0;
    uint32_t trace_hi = 0;
    uint64_t trace_limit = 0;
    uint64_t trace_lines = 0;
    bool trace_transitions = false;
    bool track_recent_history = true;
    uint64_t trace_transition_limit = 0;
    uint64_t trace_start_insn = 0;
    uint64_t trace_transition_lines = 0;
    uint64_t insns = 0;
    std::array<uint32_t, 512> recent_pc{};
    uint32_t recent_pos = 0;
    struct PcTransition { uint32_t from = 0, to = 0; uint16_t op = 0; };
    std::array<PcTransition, 128> recent_transitions{};
    uint32_t recent_transition_pos = 0;
    uint32_t previous_pc = UINT32_MAX;
    uint16_t previous_op = 0;
    uint16_t current_op = 0;
    bool watchdog_entry_logged = false;
    static constexpr uint64_t kProgressPrintInterval = 10000000;
    uint64_t next_progress_print = kProgressPrintInterval;

    explicit Cpu(Bus &b) : bus(b) {
        bus.cpu_insns = &insns;
    }

    void reset_progress_print_threshold() {
        next_progress_print = ((insns / kProgressPrintInterval) + 1) * kProgressPrintInterval;
    }

    void reset_core(uint32_t start) {
        r.fill(0);
        sec.fill(0);
        ss = 0;
        mds = 0;
        enable_irq = 0;
        enable_fiq = 0;
        fir_move = 1;
        sb = 0;
        aq = 0;
        fra = 0;
        bnk = 0;
        ine = 0;
        pri = 8;
        halted = false;
        in_irq = false;
        in_fiq = false;
        suppress_interrupt_check = false;
        recent_pc.fill(0);
        recent_pos = 0;
        recent_transitions.fill({});
        recent_transition_pos = 0;
        previous_pc = UINT32_MAX;
        previous_op = 0;
        current_op = 0;
        watchdog_entry_logged = false;
        reset_progress_print_threshold();
        r[SP] = 0x6fff;
        r[SR] = (start >> 16) & 0x3f;
        r[PC] = uint16_t(start);
    }

    uint32_t lpc() const { return ((r[SR] & 0x3f) << 16) | (r[PC] & 0xffff); }
    uint32_t lreg_i(uint8_t reg) const { return ((r[SR] << 6) & 0x3f0000) | (r[reg] & 0xffff); }
    uint16_t read16(uint32_t addr) { return bus.read(addr); }
    uint16_t read_code16(uint32_t addr) { return bus.read_code(addr); }
    void write16(uint32_t addr, uint16_t v) { bus.write(addr, v); }
    uint8_t read8(uint32_t byte_addr) {
        const uint16_t word = read16((byte_addr >> 1) & kAddrMask);
        return uint8_t((byte_addr & 1) ? (word >> 8) : word);
    }
    void write8(uint32_t byte_addr, uint8_t v) {
        const uint32_t word_addr = (byte_addr >> 1) & kAddrMask;
        const uint16_t old = read16(word_addr);
        const uint16_t next = (byte_addr & 1)
            ? uint16_t((old & 0x00ff) | (uint16_t(v) << 8))
            : uint16_t((old & 0xff00) | v);
        write16(word_addr, next);
    }

    void add_lpc(int32_t off) {
        const uint32_t next = (lpc() + off) & kAddrMask;
        r[PC] = uint16_t(next);
        r[SR] = (r[SR] & 0xffc0) | ((next >> 16) & 0x3f);
    }

    uint16_t fetch() {
        const uint16_t op = read_code16(lpc());
        add_lpc(1);
        return op;
    }

    void print_progress_state(uint32_t pc0, uint16_t op) const {
        std::ostringstream out;
        out << "EMU STATE insns=" << std::dec << insns
           << " cycles=" << bus.cycles
           << " pc=0x" << std::hex << pc0
           << " next_pc=0x" << lpc()
           << " op=0x" << op
           << " sp=0x" << r[SP]
           << " sr=0x" << r[SR]
           << " fr=0x" << get_fr()
           << " r1=0x" << r[R1]
           << " r2=0x" << r[R2]
           << " r3=0x" << r[R3]
           << " r4=0x" << r[R4]
           << " bp=0x" << r[BP]
           << " ss=0x" << ss
           << " mds=0x" << mds
           << std::dec
           << " irq=" << (in_irq ? 1 : 0)
           << " fiq=" << (in_fiq ? 1 : 0)
           << " ine=" << ine
           << " pri=" << pri
           << " ppu_pending=" << (bus.ppu_go_pending ? 1 : 0)
           << " reset_pending=" << (bus.system_reset_requested ? 1 : 0)
           << "\n";
        std::cout << out.str() << std::flush;
    }

    void update_nz(uint32_t v) {
        r[SR] &= ~(UNSP_N | UNSP_Z);
        if (v & 0x8000) r[SR] |= UNSP_N;
        if (uint16_t(v) == 0) r[SR] |= UNSP_Z;
    }

    void update_nzsc(uint32_t v, uint16_t a, uint16_t b) {
        r[SR] &= ~(UNSP_N | UNSP_Z | UNSP_S | UNSP_C);
        if (((v >> 16) & 1) != (((a ^ b) >> 15) & 1)) r[SR] |= UNSP_S;
        if (v & 0x8000) r[SR] |= UNSP_N;
        if (uint16_t(v) == 0) r[SR] |= UNSP_Z;
        if (v & 0x10000) r[SR] |= UNSP_C;
    }

    uint32_t stack_address(uint32_t reg) const {
        return ((ss & 0x3f) << 16) | (reg & 0xffff);
    }

    void set_stack_address(uint32_t addr, uint32_t &reg) {
        addr &= kAddrMask;
        ss = (addr >> 16) & 0x3f;
        reg = uint16_t(addr);
    }

    void push(uint32_t v, uint32_t &reg) {
        const uint32_t addr = stack_address(reg);
        write16(addr, uint16_t(v));
        set_stack_address(addr - 1, reg);
    }

    uint16_t pop(uint32_t &reg) {
        const uint32_t addr = (stack_address(reg) + 1) & kAddrMask;
        set_stack_address(addr, reg);
        return read16(addr);
    }

    uint16_t get_ds() const { return (r[SR] >> 10) & 0x3f; }
    void set_ds(uint16_t ds) { r[SR] = (r[SR] & 0x03ff) | ((ds & 0x3f) << 10); }

    uint16_t get_fr() const {
        return uint16_t((aq << 14) | (bnk << 13) | (fra << 12) | (fir_move << 11) |
                        ((sb & 0xf) << 7) | (enable_fiq << 6) | (enable_irq << 5) |
                        (ine << 4) | (pri & 0xf));
    }

    void set_fr(uint16_t fr) {
        aq = (fr >> 14) & 1;
        const uint32_t old_bnk = bnk;
        bnk = (fr >> 13) & 1;
        if (bnk != old_bnk) {
            std::swap(r[R1], sec[0]);
            std::swap(r[R2], sec[1]);
            std::swap(r[R3], sec[2]);
            std::swap(r[R4], sec[3]);
        }
        fra = (fr >> 12) & 1;
        fir_move = (fr >> 11) & 1;
        sb = (fr >> 7) & 0xf;
        enable_fiq = (fr >> 6) & 1;
        enable_irq = (fr >> 5) & 1;
        ine = (fr >> 4) & 1;
        pri = fr & 0xf;
    }

    [[noreturn]] void unknown(uint16_t op, uint16_t x1 = 0, uint16_t x2 = 0, int n = 0) {
        std::ostringstream ss;
        ss << "UNKNOWN OPCODE pc=" << std::hex << ((lpc() - 1) & kAddrMask)
           << " op=" << op;
        if (n > 0) ss << " x1=" << x1;
        if (n > 1) ss << " x2=" << x2;
        ss << " regs sp=" << r[SP] << " r1=" << r[R1] << " r2=" << r[R2]
           << " r3=" << r[R3] << " r4=" << r[R4] << " bp=" << r[BP]
           << " sr=" << r[SR] << " pc=" << r[PC];
        if (g_log) g_log << ss.str() << "\n";
        if (g_log) {
            g_log << "Recent PCs:";
            for (uint32_t i = 0; i < recent_pc.size(); ++i) {
                const uint32_t idx = (recent_pos + i) % uint32_t(recent_pc.size());
                if (recent_pc[idx]) g_log << " " << std::hex << recent_pc[idx];
            }
            g_log << std::dec << "\n";

            g_log << "Recent nonsequential PCs:";
            for (uint32_t i = 0; i < recent_transitions.size(); ++i) {
                const uint32_t idx = (recent_transition_pos + i) % uint32_t(recent_transitions.size());
                const PcTransition &t = recent_transitions[idx];
                if (t.from || t.to) {
                    g_log << " [" << std::hex << t.from << ":" << t.op << "->" << t.to << "]";
                }
            }
            g_log << std::dec << "\n";

            const uint32_t fault = (lpc() - 1) & kAddrMask;
            g_log << "Fault code:";
            for (int i = -8; i <= 8; ++i) {
                g_log << " [" << std::hex << ((fault + i) & kAddrMask)
                      << "]=" << read_code16((fault + i) & kAddrMask);
            }
            g_log << std::dec << "\n";

            g_log << "Stack memory:";
            for (uint32_t i = 0; i < 12; ++i) {
                const uint32_t a = (r[SP] + 1 + i) & 0xffff;
                g_log << " [" << std::hex << a << "]=" << read16(a);
            }
            g_log << std::dec << "\n";

            g_log << "Low-RAM handoff:";
            for (uint32_t a = 0x2200; a <= 0x2230; ++a) {
                g_log << " [" << std::hex << a << "]=" << read16(a);
            }
            g_log << std::dec << "\n";

            g_log << "Handoff target code:";
            for (uint32_t a = 0x052200; a <= 0x052230; ++a) {
                g_log << " [" << std::hex << a << "]=" << read_code16(a);
            }
            g_log << std::dec << "\n";

            g_log << "DMA clear source:";
            for (uint32_t a = 0x6af7; a <= 0x6bf6; ++a) {
                g_log << " [" << std::hex << a << "]=" << read16(a);
            }
            g_log << std::dec << "\n";

            g_log << "Adjacent SDRAM blocks:";
            for (uint32_t a : {0x052120u, 0x052220u, 0x052320u, 0x052420u}) {
                g_log << " [" << std::hex << a << "]=" << read_code16(a);
            }
            g_log << std::dec << "\n";
            g_log << std::dec << "\n";
        }
        die(ss.str());
    }

    bool alu(uint8_t op, uint32_t &res, uint16_t a, uint16_t b, uint32_t store, bool flags) {
        switch (op) {
        case 0x0: res = a + b; if (flags) update_nzsc(res, a, b); return true;
        case 0x1: res = a + b + ((r[SR] & UNSP_C) ? 1 : 0); if (flags) update_nzsc(res, a, b); return true;
        case 0x2: res = a + uint16_t(~b) + 1u; if (flags) update_nzsc(res, a, uint16_t(~b)); return true;
        case 0x3: res = a + uint16_t(~b) + ((r[SR] & UNSP_C) ? 1 : 0); if (flags) update_nzsc(res, a, uint16_t(~b)); return true;
        case 0x4: res = a + uint16_t(~b) + 1u; if (flags) update_nzsc(res, a, uint16_t(~b)); return false;
        case 0x6: res = uint16_t(-int16_t(b)); if (flags) update_nz(res); return true;
        case 0x8: res = a ^ b; if (flags) update_nz(res); return true;
        case 0x9: res = b; if (flags) update_nz(res); return true;
        case 0xa: res = a | b; if (flags) update_nz(res); return true;
        case 0xb: res = a & b; if (flags) update_nz(res); return true;
        case 0xc: res = a & b; if (flags) update_nz(res); return false;
        case 0xd: write16(store, a); return false;
        default:
            if (allow_invalid_alu_nop) {
                if (g_log) {
                    g_log << "TEMPORARY WORKAROUND: treating documented-invalid ALU op "
                          << std::hex << int(op) << " at " << ((lpc() - 1) & kAddrMask)
                          << " as no-op because --allow-invalid-alu-nop was set\n" << std::dec;
                }
                return false;
            }
            unknown(current_op);
        }
    }

    bool jump_condition(uint16_t op) {
        const uint8_t op0 = (op >> 12) & 0xf;
        const bool c = r[SR] & UNSP_C, z = r[SR] & UNSP_Z, s = r[SR] & UNSP_S, n = r[SR] & UNSP_N;
        switch (op0) {
        case 0: return !c; case 1: return c;
        case 2: return !s; case 3: return s;
        case 4: return !z; case 5: return z;
        case 6: return !n; case 7: return n;
        case 8: return ((r[SR] & (UNSP_Z | UNSP_C)) != UNSP_C);
        case 9: return ((r[SR] & (UNSP_Z | UNSP_C)) == UNSP_C);
        case 10: return z || s; case 11: return !z && !s;
        case 12: return (n == s); case 13: return (n != s);
        case 14: return true;
        default: unknown(op);
        }
    }

    bool exec_jump(uint16_t op) {
        const uint8_t dir = (op >> 6) & 0x7;
        const uint32_t imm = op & 0x3f;
        const bool take = jump_condition(op);
        if (take) add_lpc((dir == 0) ? int32_t(imm) : -int32_t(imm));
        return take;
    }

    void bitop_reg(uint16_t op) {
        const uint8_t bitop = (op >> 4) & 3;
        const uint8_t rd = (op >> 9) & 7;
        const uint8_t offset = (op & 0x40) ? (op & 0xf) : (r[op & 7] & 0xf);
        r[SR] = (r[SR] & ~UNSP_Z) | ((r[rd] & (1u << offset)) ? 0 : UNSP_Z);
        if (bitop == 1) r[rd] |= 1u << offset;
        else if (bitop == 2) r[rd] &= ~(1u << offset);
        else if (bitop == 3) r[rd] ^= 1u << offset;
    }

    void bitop_mem(uint16_t op, bool use_ds, bool offset_is_imm) {
        const uint8_t bitop = (op >> 4) & 3;
        const uint8_t rd = (op >> 9) & 7;
        const uint8_t offset = offset_is_imm ? (op & 0xf) : (r[op & 7] & 0xf);
        const uint32_t addr = (r[rd] & 0xffff) | (use_ds ? (uint32_t(get_ds()) << 16) : 0);
        const uint16_t orig = read16(addr);
        r[SR] = (r[SR] & ~UNSP_Z) | ((orig & (1u << offset)) ? 0 : UNSP_Z);
        if (bitop == 1) write16(addr, orig | (1u << offset));
        else if (bitop == 2) write16(addr, orig & ~(1u << offset));
        else if (bitop == 3) write16(addr, orig ^ (1u << offset));
    }

    void exec_exxx(uint16_t op) {
        if ((op & 0xf1c8) == 0xe000 || (op & 0xf1c0) == 0xe040) return bitop_reg(op);
        if ((op & 0xf1c0) == 0xe180) return bitop_mem(op, false, true);
        if ((op & 0xf1c0) == 0xe1c0) return bitop_mem(op, true, true);
        if ((op & 0xf1c8) == 0xe100) return bitop_mem(op, false, false);
        if ((op & 0xf1c8) == 0xe140) return bitop_mem(op, true, false);
        if ((op & 0xf1f8) == 0xe008) {
            const uint8_t a = (op >> 9) & 7, b = op & 7;
            const uint32_t v = uint16_t(r[a]) * uint16_t(r[b]);
            r[R4] = uint16_t(v >> 16); r[R3] = uint16_t(v); return;
        }
        if ((op & 0xf180) == 0xe080) return exec_muls(op);
        if ((op & 0xf188) == 0xe108) {
            const uint8_t rd = (op >> 9) & 7, rs = op & 7, sh = (op >> 4) & 7;
            const uint8_t count = r[rs] & 0x1f;
            switch (sh) {
            case 0: r[rd] = uint16_t(int16_t(r[rd]) >> count); return;
            case 2: r[rd] = uint16_t(uint16_t(r[rd]) << count); return;
            case 4: r[rd] = uint16_t(uint16_t(r[rd]) >> count); return;
            case 1: { uint32_t v = uint32_t(int32_t(int16_t(r[rd])) << 16) >> count; r[R3] |= uint16_t(v); r[R4] = uint16_t(v >> 16); return; }
            case 3: { uint32_t v = uint32_t(uint16_t(r[rd])) << count; r[R3] = uint16_t(v); r[R4] |= uint16_t(v >> 16); return; }
            case 5: { uint32_t v = (uint32_t(uint16_t(r[rd])) << 16) >> count; r[R3] |= uint16_t(v); r[R4] = uint16_t(v >> 16); return; }
            default: unknown(op);
            }
        }
        unknown(op);
    }

    void exec_muls(uint16_t op) {
        const uint8_t rd = (op >> 9) & 7;
        const uint8_t rs = op & 7;
        const bool rd_signed = (op >> 8) & 1;
        const bool rs_signed = (op >> 12) & 1;
        uint8_t count = (op >> 3) & 0xf;
        if (count == 0) count = 16;

        int64_t acc = 0;
        const uint32_t rd_base = uint16_t(r[rd]);
        const uint32_t rs_base = uint16_t(r[rs]);
        for (uint8_t i = 0; i < count; ++i) {
            const uint32_t rd_addr = uint16_t(rd_base + i);
            const uint32_t rs_addr = uint16_t(rs_base + i);
            const uint16_t raw_a = read16(rd_addr);
            const uint16_t raw_b = read16(rs_addr);
            const int64_t a = rd_signed ? int64_t(int16_t(raw_a)) : int64_t(raw_a);
            const int64_t b = rs_signed ? int64_t(int16_t(raw_b)) : int64_t(raw_b);
            int64_t prod = a * b;
            if (fra) prod <<= 1;
            acc += prod;
            if (fir_move) write16(rd_addr, read16(uint16_t(rd_addr + 1)));
        }

        r[rd] = uint16_t(rd_base + count);
        r[rs] = uint16_t(rs_base + count);
        r[R3] = uint16_t(acc);
        r[R4] = uint16_t(uint64_t(acc) >> 16);
        sb = 0;
        r[SR] &= ~UNSP_S;
        if (acc < INT32_MIN || acc > INT32_MAX) r[SR] |= UNSP_S;
    }

    void exec_extended(uint16_t op) {
        const uint16_t x = fetch();
        const uint8_t group = (x >> 4) & 0x1f;
        auto extreg = [&](uint16_t bits) -> uint8_t { return uint8_t(((bits >> 9) & 7) | ((bits >> 5) & 8)); };
        if (group == 0x00 || group == 0x10) {
            const uint8_t aluop = x >> 12, rb = x & 0xf, ra = extreg(x);
            uint32_t res = 0, store = r[ra];
            if (alu(aluop, res, r[ra], r[rb], store, ra != PC)) r[ra] = uint16_t(res);
            return;
        }
        if (group == 0x02) {
            uint8_t rb = x & 0xf, size = (x >> 12) & 7, rx = (x >> 9) & 7;
            if (size == 0) size = 8;
            if (x & 0x8000) while (size--) push(r[((rx--) & 7) + 8], r[rb]);
            else while (size--) r[((++rx) & 7) + 8] = pop(r[rb]);
            return;
        }
        if (group == 0x04 || group == 0x14 || group == 0x06 || group == 0x16 || group == 0x07 || group == 0x17) {
            const uint16_t imm = fetch();
            const uint8_t aluop = x >> 12, rb = x & 0xf, ra = extreg(x);
            uint32_t res = 0, store = imm;
            uint16_t b = r[rb], c = imm;
            if (group == 0x06 || group == 0x16) c = read16(imm);
            if (group == 0x07 || group == 0x17) b = r[ra], c = r[rb];
            if (alu(aluop, res, b, c, store, ra != PC)) {
                if (group == 0x07 || group == 0x17) write16(imm, uint16_t(res));
                else r[ra] = uint16_t(res);
            }
            return;
        }
        if (group >= 0x08 && group <= 0x0b) {
            const uint8_t aluop = x >> 12, ry = (x & 7) + 8, rx = ((x >> 9) & 7) + 8;
            const bool use_ds = (x >> 5) & 1;
            const uint8_t form = (x >> 3) & 3;
            uint32_t addr = 0;
            if (form == 3) { r[ry] = uint16_t(r[ry] + 1); if (r[ry] == 0 && use_ds) r[SR] += 0x0400; }
            addr = use_ds ? lreg_i(ry) : (r[ry] & 0xffff);
            uint16_t rhs = (aluop == 0x0d) ? 0 : read16(addr);
            if (form == 1) { r[ry] = uint16_t(r[ry] - 1); if (r[ry] == 0xffff && use_ds) r[SR] -= 0x0400; }
            else if (form == 2) { r[ry] = uint16_t(r[ry] + 1); if (r[ry] == 0 && use_ds) r[SR] += 0x0400; }
            uint32_t res = 0;
            if (alu(aluop, res, r[rx], rhs, addr, true)) r[rx] = uint16_t(res);
            return;
        }
        if (group >= 0x18 && group <= 0x1b) {
            const uint8_t aluop = x >> 12, rx = ((x >> 9) & 7) + 8;
            uint32_t res = 0, store = 0;
            if (alu(aluop, res, r[rx], x & 0x3f, store, true)) r[rx] = uint16_t(res);
            return;
        }
        if (group >= 0x0c && group <= 0x0f) {
            const uint8_t aluop = x >> 12, rx = ((x >> 9) & 7) + 8;
            const uint32_t addr = (stack_address(r[BP]) + (x & 0x3f)) & kAddrMask;
            uint32_t res = 0, store = addr;
            if (alu(aluop, res, r[rx], read16(addr), store, true)) r[rx] = uint16_t(res);
            return;
        }
        if (group >= 0x1c && group <= 0x1f) {
            const uint8_t aluop = x >> 12, rx = ((x >> 9) & 7) + 8;
            const uint32_t addr = x & 0x3f;
            uint32_t res = 0, store = addr;
            if (alu(aluop, res, r[rx], read16(addr), store, true)) r[rx] = uint16_t(res);
            return;
        }
        unknown(op, x, 0, 1);
    }

    void exec_fxxx(uint16_t op) {
        if ((op & 0xffc0) == 0xfe00) { set_ds(op & 0x3f); return; }
        // ISA 1.3 adds MDS and SS access in the two special-register slots
        // immediately preceding the documented DS (F020) and FR (F030)
        // slots. The supplied Generalplus PDF renders the fixed opcode bits
        // as placeholders, so F000/F008 and F010/F018 are inferred from that
        // contiguous encoding table. Log writes so a bad inference remains
        // visible while running real firmware.
        if ((op & 0xfff8) == 0xf000) { r[R3] = mds; return; }
        if ((op & 0xfff8) == 0xf008) {
            mds = r[R3];
            if (g_log) g_log << "MDS <- 0x" << std::hex << mds
                             << " pc=0x" << ((lpc() - 1) & kAddrMask) << std::dec << "\n";
            return;
        }
        if ((op & 0xfff8) == 0xf010) { r[op & 7] = ss; return; }
        if ((op & 0xfff8) == 0xf018) {
            ss = r[op & 7] & 0x3f;
            if (g_log) g_log << "SS <- 0x" << std::hex << ss
                             << " from R" << unsigned(op & 7)
                             << " pc=0x" << ((lpc() - 1) & kAddrMask) << std::dec << "\n";
            return;
        }
        if ((op & 0xf1f8) == 0xf020) { r[op & 7] = get_ds(); return; }
        if ((op & 0xf1f8) == 0xf028) { set_ds(r[op & 7]); return; }
        if ((op & 0xf1f8) == 0xf030) { r[op & 7] = get_fr(); return; }
        if ((op & 0xf1f8) == 0xf038) { set_fr(r[op & 7]); return; }
        if ((op & 0xf3c0) == 0xf240) {
            // unSP 2.0 direct-memory bit operation:
            // BITOP {DS:}[imm16], bit. The GPL16250 ROM uses this to set
            // SPI_Misc.SMART at 0x7945 before starting the bulk transfer.
            const uint8_t bitop = (op >> 4) & 3;
            const uint8_t offset = op & 0x0f;
            const bool use_ds = (op & 0x0400) != 0;
            const uint16_t imm = fetch();
            const uint32_t addr = uint32_t(imm) | (use_ds ? (uint32_t(get_ds()) << 16) : 0);
            const uint16_t orig = read16(addr);
            r[SR] = (r[SR] & ~UNSP_Z) | ((orig & (1u << offset)) ? 0 : UNSP_Z);
            if (bitop == 1) write16(addr, orig | uint16_t(1u << offset));
            else if (bitop == 2) write16(addr, orig & uint16_t(~(1u << offset)));
            else if (bitop == 3) write16(addr, orig ^ uint16_t(1u << offset));
            return;
        }
        if ((op & 0xf3c0) == 0xf040) {
            const uint16_t dst = fetch();
            const uint32_t call_target = uint32_t(dst) | (uint32_t(op & 0x3f) << 16);
            if (g_log && ((lpc() - 2) & kAddrMask) == 0x54a6e)
                g_log << "ANIM DMA CALL target=0x" << std::hex << call_target
                      << " cycles=0x" << bus.cycles << std::dec << "\n";
            if (g_log && (call_target == 0x54a00 || call_target == 0x534fa || call_target == 0x54a6e)) {
                g_log << "ANIM CALL pc=0x" << std::hex << ((lpc() - 2) & kAddrMask)
                      << " target=0x" << call_target
                      << " r1=0x" << r[R1] << " r2=0x" << r[R2]
                      << " r3=0x" << r[R3] << " r4=0x" << r[R4]
                      << " sp=0x" << r[SP] << " bp=0x" << r[BP]
                      << " cycles=0x" << bus.cycles << std::dec << "\n";
            }
            push(r[PC], r[SP]); push(r[SR], r[SP]);
            r[PC] = dst; r[SR] = (r[SR] & 0xffc0) | (op & 0x3f); return;
        }
        if ((op & 0xffc0) == 0xfe80) {
            const uint16_t dst = fetch();
            r[PC] = dst; r[SR] = (r[SR] & 0xffc0) | (op & 0x3f); return;
        }
        if ((op & 0xffc0) == 0xfec0) {
            r[PC] = r[R3]; r[SR] = (r[SR] & 0xffc0) | (r[R4] & 0x3f); return;
        }
        if (op == 0xff80) return exec_extended(op);

        const uint8_t group = (op >> 6) & 7;
        if (group == 0 || group == 4) {
            const uint8_t a = (op >> 9) & 7, b = op & 7;
            uint32_t v = uint16_t(r[a]) * uint16_t(r[b]);
            if (group == 0 && (r[b] & 0x8000)) v -= uint16_t(r[a]) << 16;
            if (group == 4) {
                if (r[b] & 0x8000) v -= uint16_t(r[a]) << 16;
                if (r[a] & 0x8000) v -= uint16_t(r[b]) << 16;
            }
            r[R4] = uint16_t(v >> 16); r[R3] = uint16_t(v); return;
        }
        if (group == 5) {
            switch (op & 0xf1ff) {
            case 0xf140: enable_irq = enable_fiq = 0; return;
            case 0xf141: enable_irq = 1; enable_fiq = 0; return;
            case 0xf142: enable_irq = 0; enable_fiq = 1; return;
            case 0xf143: enable_irq = enable_fiq = 1; return;
            case 0xf144: fir_move = 1; return; case 0xf145: fir_move = 0; return;
            case 0xf146: fra = 0; return; case 0xf147: fra = 1; return;
            case 0xf148: enable_irq = 0; return; case 0xf149: enable_irq = 1; return;
            case 0xf14c: enable_fiq = 0; return; case 0xf14e: enable_fiq = 1; return;
            }
            switch (op & 0xf1ff) {
            case 0xf14a: if (bnk) { std::swap(r[R1], sec[0]); std::swap(r[R2], sec[1]); std::swap(r[R3], sec[2]); std::swap(r[R4], sec[3]); } bnk = 0; return;
            case 0xf14b: if (!bnk) { std::swap(r[R1], sec[0]); std::swap(r[R2], sec[1]); std::swap(r[R3], sec[2]); std::swap(r[R4], sec[3]); } bnk = 1; return;
            case 0xf14d: ine = 0; return; case 0xf14f: ine = 1; return;
            }
            if ((op & 0xf167) == 0xf161) {
                const uint32_t addr = (r[R3] & 0xffff) | ((r[R4] & 0x3f) << 16);
                if (g_log && (addr >= 0x030000 || lpc() >= 0x030000)) {
                    g_log << "INDIRECT CALL pc=0x" << std::hex << ((lpc() - 1) & kAddrMask)
                          << " target=0x" << addr
                          << " sp=0x" << r[SP]
                          << " r1=0x" << r[R1]
                          << " r2=0x" << r[R2]
                          << " r3=0x" << r[R3]
                          << " r4=0x" << r[R4]
                          << " sr=0x" << r[SR]
                          << std::dec << "\n";
                }
                push(r[PC], r[SP]); push(r[SR], r[SP]);
                r[PC] = uint16_t(addr); r[SR] = (r[SR] & 0xffc0) | ((addr >> 16) & 0x3f); return;
            }
            if ((op & 0xf167) == 0xf162) return exec_divs();
            if ((op & 0xf167) == 0xf163) return exec_divq();
            if ((op & 0xf167) == 0xf164) {
                const uint16_t v = r[R4];
                r[R2] = (v & 0x8000) ? (std::countl_one(v) - 1) : (std::countl_zero(v) - 1);
                return;
            }
            if ((op & 0xf167) == 0xf165) return; // NOP
            if ((op & 0xf167) == 0xf160) return software_break();
        }
        unknown(op);
    }

    void exec_divs() {
        // The verified unSP ISA manual specifies DIVS as the first step of a
        // signed divide: AQ receives the quotient sign and MR is shifted left
        // once with that sign entering bit 0. Fifteen DIVQ operations follow.
        aq = ((r[R4] ^ r[R2]) >> 15) & 1;
        const uint32_t mr = ((r[R4] << 16) | r[R3]) << 1 | aq;
        r[R4] = uint16_t(mr >> 16);
        r[R3] = uint16_t(mr);
    }

    void exec_divq() {
        // DIVQ is a single, externally visible non-restoring divide step; it
        // has no hidden 16-iteration state. AQ selects add/subtract. The
        // partial remainder is changed before MR is shifted, then the inverse
        // of the newly generated AQ bit enters R3 bit 0. This ordering is
        // required by the manual's integer example, which first shifts the
        // dividend left and then executes sixteen DIVQ instructions.
        const uint16_t divisor = uint16_t(r[R2]);
        const uint16_t partial = aq
            ? uint16_t(r[R4] + divisor)
            : uint16_t(r[R4] - divisor);
        aq = ((partial ^ divisor) >> 15) & 1;
        const uint32_t mr = ((uint32_t(partial) << 16) | r[R3]) << 1 | (aq ^ 1);
        r[R4] = uint16_t(mr >> 16);
        r[R3] = uint16_t(mr);
    }

    void software_break() {
        push(r[PC], r[SP]); push(r[SR], r[SP]);
        r[PC] = read16(0x00fff5);
        r[SR] = 0;
    }

    int pending_irq_line() {
        // Verified GPF16001A headers/ISRs route DMA to IRQ3, timers to IRQ4,
        // PPU/TFT video sources to IRQ5, and scheduler/timebase to IRQ6. The
        // GPL16200x guide routes manual-ADC events to IRQ1 unless its
        // P_INT_Priority1 bit selects FIQ.
        if (bus.audio_irq0_asserted_no_update()) return 0;
        if (bus.adc_irq1_asserted_no_update()) return 1;
        if (bus.dma_irq3_asserted_no_update() || bus.usb_irq3_asserted_no_update()) return 3;
        if (bus.timer_irq4_asserted_no_update()) return 4;
        if (bus.video_irq_asserted_no_update()) return 5;
        if (bus.irq6_asserted_no_update()) return 6;
        return -1;
    }

    bool irq_priority_allows(int line) const {
        if (!enable_irq) return false;
        if (in_irq && !ine) return false;
        if (pri == 0) return false;
        return pri == 8 || uint32_t(line) < pri;
    }

    void service_irq(int line) {
        const uint32_t vector_addr = 0x00fff8 + uint32_t(line);
        const uint16_t vector = read16(vector_addr);
        const uint32_t from_pc = r[PC];
        push(r[PC], r[SP]);
        push(r[SR], r[SP]);
        if (ine) push(get_fr(), r[SP]);
        in_irq = true;
        if (ine) pri = uint32_t(line);
        r[PC] = vector;
        r[SR] = 0;
        suppress_interrupt_check = true;
        if (g_log && bus.irq_log_count++ < 512) {
            g_log << "IRQ" << line << " vector_addr=0x" << std::hex << vector_addr
                  << " vector=0x" << vector << " sp=0x" << r[SP]
                  << " fr=0x" << get_fr() << std::dec << "\n";
        }
        if (g_log && bus.cycles >= 0x30000000ull && bus.late_irq_log_count++ < 512) {
            g_log << "LATE IRQ" << line << " vector_addr=0x" << std::hex << vector_addr
                  << " vector=0x" << vector << " from_pc=0x" << from_pc
                  << " sp=0x" << r[SP] << " fr=0x" << get_fr()
                  << " int2=0x" << bus.int_status2_value()
                  << " intctrl=0x" << bus.mmio[0x78a3 - kMmioBase]
                  << " pri1=0x" << bus.mmio[0x78a4 - kMmioBase]
                  << " pri2=0x" << bus.mmio[0x78a5 - kMmioBase]
                  << " ta=0x" << bus.mmio[0x78c0 - kMmioBase]
                  << " tb=0x" << bus.mmio[0x78c8 - kMmioBase]
                  << " tc=0x" << bus.mmio[0x78d0 - kMmioBase]
                  << " td=0x" << bus.mmio[0x78d8 - kMmioBase]
                  << " tbc=0x" << bus.mmio[0x78b2 - kMmioBase]
                  << " viden=0x" << bus.mmio[0x7062 - kMmioBase]
                  << " vidst=0x" << bus.mmio[0x7063 - kMmioBase]
                  << " cycles=0x" << bus.cycles << std::dec << "\n";
        }
        if (g_log && bus.cycles >= 0x39d00000ull && bus.focused_late_irq_log_count++ < 4096) {
            g_log << "FOCUSED LATE IRQ" << line << " vector_addr=0x" << std::hex << vector_addr
                  << " vector=0x" << vector << " from_pc=0x" << from_pc
                  << " sp=0x" << r[SP] << " fr=0x" << get_fr()
                  << " int1=0x" << bus.int_status1_value()
                  << " int2=0x" << bus.int_status2_value()
                  << " intctrl=0x" << bus.mmio[0x78a3 - kMmioBase]
                  << " pri1=0x" << bus.mmio[0x78a4 - kMmioBase]
                  << " pri2=0x" << bus.mmio[0x78a5 - kMmioBase]
                  << " ta=0x" << bus.mmio[0x78c0 - kMmioBase]
                  << " ta_pre=0x" << bus.mmio[0x78c2 - kMmioBase]
                  << " ta_up=0x" << bus.mmio[0x78c4 - kMmioBase]
                  << " tb=0x" << bus.mmio[0x78c8 - kMmioBase]
                  << " tc=0x" << bus.mmio[0x78d0 - kMmioBase]
                  << " td=0x" << bus.mmio[0x78d8 - kMmioBase]
                  << " rtcst=0x" << bus.mmio[0x7935 - kMmioBase]
                  << " rtcctl=0x" << bus.mmio[0x7936 - kMmioBase]
                  << " viden=0x" << bus.mmio[0x7062 - kMmioBase]
                  << " vidst=0x" << bus.mmio[0x7063 - kMmioBase]
                  << " ppu_go=0x" << bus.mmio[0x707c - kMmioBase]
                  << " c6=0x" << bus.mem[0x09c6]
                  << " c7=0x" << bus.mem[0x09c7]
                  << " cycles=0x" << bus.cycles << std::dec << "\n";
        }
    }

    void service_fiq() {
        const uint16_t vector = read16(0x00fff6);
        push(r[PC], r[SP]);
        push(r[SR], r[SP]);
        if (ine) push(get_fr(), r[SP]);
        in_fiq = true;
        r[PC] = vector;
        r[SR] = 0;
        suppress_interrupt_check = true;
        if (g_log && bus.irq_log_count++ < 512) {
            g_log << "FIQ vector_addr=0xfff6 vector=0x" << std::hex << vector
                  << " sp=0x" << r[SP] << " fr=0x" << get_fr() << std::dec << "\n";
        }
    }

    void maybe_service_interrupts() {
        if (suppress_interrupt_check) {
            suppress_interrupt_check = false;
            return;
        }
        if (enable_fiq && !in_fiq && bus.audio_fiq_asserted_no_update()) {
            service_fiq();
            return;
        }
        // FIQ is the highest-priority context; do not nest a regular IRQ in
        // it. A pending FIQ may still preempt an ordinary IRQ above.
        if (in_fiq) return;
        if (!enable_irq || (in_irq && !ine) || pri == 0) return;
        const int irq = pending_irq_line();
        if (irq >= 0 && (pri == 8 || uint32_t(irq) < pri)) service_irq(irq);
    }

    uint32_t instruction_cycles(uint16_t op, bool jump_taken, bool pre_ine) const {
        const uint8_t op0 = (op >> 12) & 0xf;
        const uint8_t opa = (op >> 9) & 7;
        const uint8_t op1 = (op >> 6) & 7;
        if (op0 < 0xf && opa == 0x7 && op1 < 2) return jump_taken ? 6 : 3;
        if ((op & 0xf3c0) == 0xf040) return 13; // CALL A22
        if ((op & 0xffc0) == 0xfe80) return 7;  // GOTO A22
        if ((op & 0xffc0) == 0xfec0) return 6;  // GOTO MR
        if ((op & 0xf167) == 0xf161) return 12; // CALL MR
        if ((op & 0xf167) == 0xf163) return 3;  // DIVQ MR, R2
        if (op == 0x9a90) return 11;            // RETF
        if (op == 0x9a98) return pre_ine ? 14 : 11; // RETI
        if ((op & 0xf167) == 0xf165) return 3;  // NOP
        return 2;
    }

    void exec_remaining(uint16_t op) {
        const uint8_t op0 = (op >> 12) & 0xf;
        const uint8_t opa = (op >> 9) & 7;
        const uint8_t op1 = (op >> 6) & 7;
        const uint8_t opn = (op >> 3) & 7;
        const uint8_t opb = op & 7;
        const uint8_t lower = (op1 << 4) | op0;

        if ((op & 0xf800) == 0x5000 || (op & 0xf800) == 0x5800) {
            const uint8_t rd = uint8_t(R1 + ((op >> 9) & 3));
            const bool word_access = (op & 0x0100) != 0;
            const bool store_access = (op & 0x0800) != 0;
            const uint32_t byte_addr = (stack_address(r[BP]) << 1) + (op & 0x3f);
            if (store_access) {
                if (word_access) {
                    write8(byte_addr, uint8_t(r[rd]));
                    write8(byte_addr + 1, uint8_t(r[rd] >> 8));
                } else {
                    write8(byte_addr, uint8_t(r[rd]));
                }
            } else {
                if (word_access) r[rd] = uint16_t(read8(byte_addr) | (uint16_t(read8(byte_addr + 1)) << 8));
                else r[rd] = read8(byte_addr);
                update_nz(r[rd]);
            }
            return;
        }

        if (lower == 0x2d) {
            uint8_t count = opn, src = opa;
            while (count--) push(r[src--], r[opb]);
            return;
        }
        if (lower == 0x29) {
            if (op == 0x9a98) {
                if (ine) set_fr(pop(r[SP]));
                r[SR] = pop(r[SP]); r[PC] = pop(r[SP]);
                if (in_fiq) in_fiq = false;
                else if (in_irq) in_irq = false;
                suppress_interrupt_check = true;
                return;
            }
            uint8_t count = opn, dst = opa;
            const uint32_t pc_before_pop = (lpc() - 1) & kAddrMask;
            const bool returning_from_mba =
                op == 0x9a90 && bus.mba_entry_return_stack(stack_address(r[opb]));
            while (count--) r[++dst] = pop(r[opb]);
            if (returning_from_mba) bus.note_mba_application_return(lpc());
            if (op == 0x9a90 && pc_before_pop == 0x03a19b) {
                if (g_log && bus.cycles >= 0x39d00000ull) {
                    g_log << "IRQ DISPATCH RETURN pc=0x" << std::hex << pc_before_pop
                          << " new_pc=0x" << lpc()
                          << " sp=0x" << r[SP]
                          << " sr=0x" << r[SR]
                          << " fr=0x" << get_fr()
                          << " in_irq=0x" << (in_irq ? 1 : 0)
                          << " in_fiq=0x" << (in_fiq ? 1 : 0)
                          << " cycles=0x" << bus.cycles << std::dec << "\n";
                }
                if (in_fiq) in_fiq = false;
                else if (in_irq) in_irq = false;
                suppress_interrupt_check = true;
            }
            return;
        }

        uint16_t lhs = r[opa], rhs = 0;
        uint32_t store = 0;
        switch (op1) {
        case 0:
            store = (stack_address(r[BP]) + (op & 0x3f)) & kAddrMask;
            if (op0 != 0x0d) rhs = read16(store);
            break;
        case 1: rhs = op & 0x3f; break;
        case 3: {
            const bool use_ds = opn & 4;
            const uint8_t form = opn & 3;
            if (form == 3) { r[opb] = uint16_t(r[opb] + 1); if (use_ds && r[opb] == 0) r[SR] += 0x0400; }
            store = use_ds ? lreg_i(opb) : (r[opb] & 0xffff);
            if (op0 != 0x0d) rhs = read16(store);
            if (form == 1) { r[opb] = uint16_t(r[opb] - 1); if (use_ds && r[opb] == 0xffff) r[SR] -= 0x0400; }
            else if (form == 2) { r[opb] = uint16_t(r[opb] + 1); if (use_ds && r[opb] == 0) r[SR] += 0x0400; }
            break;
        }
        case 4:
            switch (opn) {
            case 0: rhs = r[opb]; break;
            case 1: lhs = r[opb]; rhs = fetch(); break;
            case 2: lhs = r[opb]; store = fetch(); if (op0 != 0x0d) rhs = read16(store); break;
            case 3: rhs = lhs; lhs = r[opb]; store = fetch(); break;
            default: {
                uint32_t sh = (uint16_t(r[opb]) << 4) | (sb & 0xf);
                if (sh & 0x80000) sh |= 0xf00000;
                sh >>= (opn - 3);
                sb = sh & 0xf; rhs = uint16_t(sh >> 4); break;
            }
            }
            break;
        case 5:
            if (opn & 4) {
                const uint32_t sh = ((uint16_t(r[opb]) << 4) | (sb & 0xf)) >> (opn - 3);
                sb = sh & 0xf; rhs = uint16_t(sh >> 4);
            } else {
                const uint32_t sh = (((sb & 0xf) << 16) | uint16_t(r[opb])) << (opn + 1);
                sb = (sh >> 16) & 0xf; rhs = uint16_t(sh);
            }
            break;
        case 6: {
            uint32_t sh = ((((sb & 0xf) << 16) | uint16_t(r[opb])) << 4) | (sb & 0xf);
            if (opn & 4) { sh >>= (opn - 3); sb = sh & 0xf; }
            else { sh <<= (opn + 1); sb = (sh >> 20) & 0xf; }
            rhs = uint16_t(sh >> 4); break;
        }
        case 7: store = op & 0x3f; if (op0 != 0x0d) rhs = read16(store); break;
        default:
            if (op1 == 2 && g_log) {
                g_log << "UNIMPLEMENTED unSP 1.3 byte-memory instruction"
                      << " pc=0x" << std::hex << ((lpc() - 1) & kAddrMask)
                      << " op=0x" << op
                      << " operation=0x" << int(op0)
                      << " rd=0x" << int(opa)
                      << " byte_group=0x" << ((op >> 5) & 0xf)
                      << " update=0x" << ((op >> 3) & 3)
                      << " byte_select=0x" << ((op >> 2) & 1)
                      << " rs2=0x" << (op & 3) << std::dec << "\n";
            }
            unknown(op);
        }

        uint32_t res = 0;
        if (alu(op0, res, lhs, rhs, store, opa != PC)) {
            r[opa] = uint16_t(res);
        }
    }

    void step() {
        if (halted) return;
        maybe_service_interrupts();
        bus.pc_for_log = lpc();
        const uint32_t pc0 = lpc();
        bus.maybe_begin_mba_application(pc0, stack_address(r[SP]));
        bus.maybe_arm_mba_watchdog_handoff(pc0, stack_address(r[SP]));
        if (!watchdog_entry_logged && pc0 >= 0x030043 && pc0 <= 0x03004b && g_log) {
            watchdog_entry_logged = true;
            g_log << "WATCHDOG RESET ROUTINE ENTERED insns=" << std::dec << insns
                  << " pc=0x" << std::hex << pc0
                  << " sp=0x" << r[SP]
                  << " sr=0x" << r[SR]
                  << " fr=0x" << get_fr()
                  << " r1=0x" << r[1]
                  << " r2=0x" << r[2]
                  << " r3=0x" << r[3]
                  << " r4=0x" << r[4]
                  << " bp=0x" << r[BP]
                  << " ss=0x" << ss
                  << " mds=0x" << mds
                  << " ine=0x" << ine
                  << " pri=0x" << pri
                  << std::dec << "\n";
            g_log << "Recent nonsequential PCs:";
            for (uint32_t i = 0; i < recent_transitions.size(); ++i) {
                const uint32_t idx = (recent_transition_pos + i) % uint32_t(recent_transitions.size());
                const PcTransition &t = recent_transitions[idx];
                if (t.from || t.to || t.op) {
                    g_log << " [" << std::hex << t.from << ":" << t.op << "->" << t.to << "]";
                }
            }
            g_log << std::dec << "\n";
            g_log << "Stack words:";
            for (uint32_t i = 0; i < 24; ++i) {
                const uint32_t a = (uint32_t(r[SP]) + i) & kAddrMask;
                g_log << " [" << std::hex << a << "]=" << read16(a);
            }
            g_log << std::dec << "\n";
        }
        if (track_recent_history && previous_pc != UINT32_MAX &&
            pc0 != ((previous_pc + 1) & kAddrMask)) {
            recent_transitions[recent_transition_pos++ % recent_transitions.size()] =
                PcTransition{previous_pc, pc0, previous_op};
            if (trace_transitions && g_log && insns >= trace_start_insn && pc0 != previous_pc &&
                (!trace_range ||
                 (pc0 >= trace_lo && pc0 < trace_hi) ||
                 (previous_pc >= trace_lo && previous_pc < trace_hi)) &&
                (!trace_transition_limit || trace_transition_lines < trace_transition_limit)) {
                ++trace_transition_lines;
                g_log << "PC TRANSITION insns=" << std::dec << insns
                      << " from=0x" << std::hex << previous_pc
                      << " to=0x" << pc0
                      << " op=0x" << previous_op
                      << " sp=0x" << r[SP]
                      << " sr=0x" << r[SR]
                      << " fr=0x" << get_fr()
                      << " in_irq=" << (in_irq ? 1 : 0)
                      << " ine=" << ine
                      << " pri=" << pri
                      << std::dec << "\n";
            }
        }
        if (track_recent_history)
            recent_pc[recent_pos++ % recent_pc.size()] = pc0;
        const uint16_t op = fetch();
        current_op = op;
        if (track_recent_history) {
            previous_pc = pc0;
            previous_op = op;
        }
        const bool trace_this_pc = trace && insns >= trace_start_insn &&
                                   (!trace_range || (pc0 >= trace_lo && pc0 < trace_hi)) &&
                                   (!trace_limit || trace_lines < trace_limit);
        if (trace_this_pc && g_log) {
            ++trace_lines;
            g_log << std::hex << std::setw(6) << std::setfill('0') << pc0
                  << ": " << std::setw(4) << op
                  << " sp=" << std::setw(4) << r[SP] << " r1=" << r[R1]
                  << " r2=" << r[R2] << " r3=" << r[R3] << " r4=" << r[R4]
                  << " bp=" << r[BP] << " ss=" << ss << " sr=" << r[SR]
                  << " fr=" << get_fr()
                  << " virq=" << (bus.mmio[0x7062 - kMmioBase] & bus.mmio[0x7063 - kMmioBase])
                  << std::dec << "\n";
        }

        const uint8_t op0 = (op >> 12) & 0xf;
        const uint8_t opa = (op >> 9) & 7;
        const uint8_t op1 = (op >> 6) & 7;
        const bool pre_ine = ine != 0;
        bool jump_taken = false;
        if (op0 == 0xf) exec_fxxx(op);
        else if (op == 0xee41) exec_exxx(op);
        else if (op0 < 0xf && opa == 0x7 && op1 < 2) jump_taken = exec_jump(op);
        else if (op0 == 0xe) exec_exxx(op);
        else exec_remaining(op);
        ++insns;
        if (insns >= next_progress_print) {
            print_progress_state(pc0, op);
            next_progress_print += kProgressPrintInterval;
        }
        bus.cycles += instruction_cycles(op, jump_taken, pre_ine);
        // Peripheral latches become visible at the instruction boundary. The
        // main loop used to update here and again before interrupt dispatch,
        // making every instruction run the complete timer/RTC/video scheduler
        // twice. Keeping the single synchronization inside Cpu::step also
        // ensures tests and alternate frontends cannot accidentally omit it.
        // Keep the common instruction path to two loads and a comparison.
        // update_periodic_events() is intentionally out-of-line because its
        // uncommon device work is large; calling it before the scheduler's
        // next deadline only repeats the same guard inside that function.
        if (bus.next_periodic_event_cycles == 0 ||
            bus.cycles >= bus.next_periodic_event_cycles) {
            bus.update_periodic_events(false);
        }
    }
};

} // namespace mobigo
