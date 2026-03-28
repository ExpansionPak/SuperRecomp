#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include <deque>
#include <cstdint>
#include <iomanip>
#include <map>

struct CPUState {
    bool is16A = false;
    bool is16XY = false;
};

uint32_t snes_to_file(uint32_t addr) {
    uint8_t bank = (addr >> 16) & 0xFF;
    uint32_t offset = addr & 0xFFFF;
    if (offset < 0x8000 && bank < 0x7E) return 0xFFFFFFFF; 
    return ((bank & 0x7F) << 15) | (offset & 0x7FFF);
}

// Global to track what state each address should be compiled with
std::map<uint32_t, CPUState> addr_states;

int getOpSize(uint8_t op, const CPUState& state) {
    switch (op) {
        // 3-byte ops
        case 0x9C: case 0x8D: case 0xAD: case 0x20: case 0x4C: return 3;
        // 2-byte ops
        case 0xC2: case 0xE2: case 0x64: case 0x85: return 2;
        // State-dependent immediate ops
        case 0xA9: return state.is16A ? 3 : 2;
        case 0x8F: case 0x22: case 0x5C: return 4; // 4-byte instructions
        case 0xA2: case 0xA0: return state.is16XY ? 3 : 2;
        case 0x9F: return 4;                     // STA Absolute Long, X
        case 0xE9: return state.is16A ? 3 : 2;  // SBC Immediate
        case 0x80: return 2;                     // BRA (Branch Always)
        case 0x98: return 1;                     // TYA (Transfer Y to A)
        case 0x38: return 1;                     // SEC (Set Carry)
        case 0xA8: case 0xCA: case 0xEA: case 0x58: case 0x7A: return 1; // 1-byte
        case 0x10: case 0xA5: case 0xE6: case 0x74: case 0x84: return 2; // 2-byte
        case 0xBD: case 0x9E: case 0xCD: return 3;                       // 3-byte
        case 0x92: case 0x91: case 0xA1: return 2;                       // 2-byte (Indirect/DP)
        // 1-byte ops
        default: return 1;
    }
}

void emitC(std::ofstream& out, const std::vector<uint8_t>& rom, uint32_t pc, CPUState& state, std::deque<uint32_t>& queue) {
    uint8_t op = rom[pc];
    out << "    addr_0x" << std::hex << pc << ": ";

    switch (op) {
        case 0x78: out << "SEI();\n"; break;
        case 0x18: out << "CLC();\n"; break;
        case 0xFB: out << "XCE();\n"; break;
        
        // STZ (Store Zero) - Very common in bootup
        case 0x9C: out << "STZ_abs(0x" << std::hex << (rom[pc+1]|(rom[pc+2]<<8)) << ");\n"; break;
        case 0x64: out << "STZ_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;

        // PHP / PLP (Push/Pull Processor Status)
        case 0x08: out << "PHP();\n"; break;
        case 0x28: out << "PLP();\n"; break;

        // State Changes
        case 0xC2: {
            uint8_t val = rom[pc+1];
            if (val & 0x20) state.is16A = true;
            if (val & 0x10) state.is16XY = true;
            out << "REP(0x" << std::hex << (int)val << ");\n";
            break;
        }
        case 0xE2: {
            uint8_t val = rom[pc+1];
            if (val & 0x20) state.is16A = false;
            if (val & 0x10) state.is16XY = false;
            out << "SEP(0x" << std::hex << (int)val << ");\n";
            break;
        }

        // LDA / STA
        case 0xA9: {
            uint16_t val = state.is16A ? (rom[pc+1] | (rom[pc+2] << 8)) : rom[pc+1];
            out << "LDA_imm(0x" << std::hex << val << ");\n";
            break;
        }
        case 0x8D: out << "STA_abs(0x" << std::hex << (rom[pc+1]|(rom[pc+2]<<8)) << ");\n"; break;
        case 0x85: out << "STA_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;

        case 0xA0: { // LDY Immediate
            uint16_t val = state.is16XY ? (rom[pc+1] | (rom[pc+2] << 8)) : rom[pc+1];
            out << "    LDY_imm(0x" << std::hex << val << ");\n";
            break;
        }
        case 0x9F: { // STA Absolute Long, X
            uint32_t addr = rom[pc+1] | (rom[pc+2] << 8) | (rom[pc+3] << 16);
            out << "    STA_long_x(0x" << std::hex << addr << ");\n";
            break;
        }
        case 0x98: out << "    TYA();\n"; break;
        case 0x38: out << "    SEC();\n"; break;
        case 0xE9: { // SBC Immediate
            uint16_t val = state.is16A ? (rom[pc+1] | (rom[pc+2] << 8)) : rom[pc+1];
            out << "    SBC_imm(0x" << std::hex << val << ");\n";
            break;
        }

        case 0x8F: { // STA Absolute Long ($AAAAAA)
            uint32_t target = rom[pc+1] | (rom[pc+2] << 8) | (rom[pc+3] << 16);
            out << "    STA_long(0x" << std::hex << target << ");\n";
            break;
        }
        case 0xA2: { // LDX Immediate
            uint16_t val = state.is16XY ? (rom[pc+1] | (rom[pc+2] << 8)) : rom[pc+1];
            out << "    LDX_imm(0x" << std::hex << val << ");\n";
            break;
        }
        case 0x22: { // JSL (Jump to Subroutine Long)
            uint32_t target = snes_to_file(rom[pc+1] | (rom[pc+2] << 8) | (rom[pc+3] << 16));
            out << "    sub_0x" << std::hex << target << "();\n";
            queue.push_back(target);
            break;
        }

        case 0xCA: out << "    DEX();\n"; break;
        case 0xA8: out << "    TAY();\n"; break;
        case 0xEA: out << "    NOP();\n"; break;
        case 0x58: out << "    CLI();\n"; break; // Clear Interrupts
        case 0x7A: out << "    PLY();\n"; break; // Pull Y from Stack
        case 0xE6: out << "    INC_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0xA5: out << "    LDA_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0xBD: out << "    LDA_abs_x(0x" << std::hex << (rom[pc+1]|(rom[pc+2]<<8)) << ");\n"; break;
        case 0x84: out << "    STY_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;

        // Transfers
        case 0x5B: out << "TCD();\n"; break;
        case 0x1B: out << "TCS();\n"; break;

        default: 
            out << "// TODO: Implement Opcode 0x" << std::hex << (int)op << "\n"; 
            break;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    uint32_t snesReset = rom[0x7FFC] | (rom[0x7FFD] << 8);
    uint32_t startAddr = snes_to_file(snesReset);

    std::ofstream out("recompiled_rom.cpp");
    out << "#include \"snes_cpu.h\"\n\n";

    std::deque<uint32_t> queue = { startAddr };
    std::set<uint32_t> visited;

    while (!queue.empty()) {
        uint32_t addr = queue.front();
        queue.pop_front();
        if (visited.count(addr) || addr >= rom.size() || addr == 0xFFFFFFFF) continue;
        visited.insert(addr);

        out << "void sub_0x" << std::hex << addr << "() {\n";
        uint32_t pc = addr;
        CPUState state; // Reset to 8-bit on every function entry (typical SNES behavior)
        bool end = false;

        while (!end && pc < rom.size()) {
            uint8_t op = rom[pc];
            
            // Hard stop on Header or known data areas
            if (pc >= 0x7FB0 && pc <= 0x7FFF) break;
            if (op == 0x00) { out << "    return; // Padding\n"; break; }

            // Handle Jumps
            if (op == 0x20 || op == 0x22 || op == 0x4C || op == 0x5C) {
                uint32_t target = snes_to_file(rom[pc+1] | (rom[pc+2] << 8) | (op == 0x22 || op == 0x5C ? (rom[pc+3] << 16) : 0));
                
                // 1. Output the C call
                out << "    sub_0x" << std::hex << target << "();\n";
                
                // 2. Add to the work list
                queue.push_back(target);
                
                if (op == 0x4C || op == 0x5C) end = true; // JMP/JML ends current flow
                pc += (op == 0x20 || op == 0x4C ? 3 : 4);
            } else if (op == 0x60 || op == 0x6B) {
                out << "    return;\n";
                end = true;
            } else if (op == 0x10) { // BPL (Branch on Plus / Positive)
                int8_t offset = (int8_t)rom[pc + 1];
                uint32_t target = (pc & ~0x7FFF) | ((pc + 2 + offset) & 0x7FFF);
                out << "    if (!regs.P.N) goto addr_0x" << std::hex << target << ";\n";
                queue.push_back(target);
                pc += 2;
            } else if (op == 0x80 || op == 0xD0 || op == 0xF0) { // Branches
                int8_t offset = (int8_t)rom[pc + 1];
                // This stays within the 32KB bank
                uint32_t target = (pc & ~0x7FFF) | ((pc + 2 + offset) & 0x7FFF);
                
                if (op == 0x80) {
                    out << "    goto addr_0x" << std::hex << target << ";\n";
                    end = true; // BRA is the end of this linear block
                } else if (op == 0xD0) {
                    out << "    if (!regs.P.Z) goto addr_0x" << std::hex << target << ";\n";
                } else {
                    out << "    if (regs.P.Z) goto addr_0x" << std::hex << target << ";\n";
                }
                
                // Make sure we recompile the place we're jumping to!
                queue.push_back(target); 
                pc += 2;
            } else {
                emitC(out, rom, pc, state, queue);
                pc += getOpSize(op, state);
            }
        }
        out << "}\n\n";
    }
    return 0;
}