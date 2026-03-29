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
        case 0x10: case 0xE6: case 0x84: return 2; // 2-byte
        case 0xCD: return 3;                       // 3-byte
        case 0x92: return 2;                       // 2-byte (Indirect/DP)
        case 0x9D: case 0xBD: return 3; // 3-byte (Absolute, X)
        case 0x74: case 0x94: case 0xA5: return 2; // 2-byte (DP, X / DP)
        case 0x48: case 0x1A: return 1; // 1-byte (Stack/Misc)
        case 0xE0: return state.is16XY ? 3 : 2; // CPX Immediate
        case 0x9E: return 3;                     // STZ Absolute, X
        case 0x29: return state.is16A ? 3 : 2;  // AND Immediate
        case 0x0A: case 0x68: case 0xC8: return 1; // ASL, PLA, INY (1-byte)
        case 0x91: case 0xB7: return 2;          // Indirect Indexed [DP], Y
        case 0xA4: return 2; // LDY Direct Page
        case 0x03: return 2; // ORA Stack Relative
        case 0xDC: return 3; // JML [Absolute] (Indirect Long)
        case 0xAA: return 1; // TAX (Transfer A to X)
        case 0xFF: return 4; // SBC Absolute Long, X
        case 0x01: return 2; // ORA (DP, X)
        case 0x05: return 2; // ORA Direct Page
        case 0xF4: return 3; // PEA (Push Effective Absolute Address)
        case 0x65: return 2; // ADC Direct Page
        case 0x26: return 2; // ROL Direct Page
        case 0x07: return 2; // ORA [Direct Page] (Indirect Long)
        case 0x4A: return 1; // LSR Accumulator
        case 0x09: return state.is16A ? 3 : 2; // ORA Immediate
        case 0xEB: return 1; // XBA (Exchange B and A)
        case 0xE8: return 1; // INX (Increment X)
        case 0x8E: return 3; // STX absolute
        case 0x15: return 2; // ORA Direct Page, X
        case 0x43: return 2; // EOR Stack Relative Indirect Indexed
        case 0x9B: case 0x1B: case 0x5B: case 0x08: case 0x28: return 1;
        case 0x86: return 2;
        case 0x0F: case 0x6F: case 0xAF: return 4; // Absolute Long [24-bit]
        case 0xAE: case 0xBE: return 3;           // Absolute / Absolute, Y
        case 0x96: case 0x97: case 0xD1: case 0xA1: case 0x87: return 2; // DP variants
        case 0x9A: case 0xFA: return 1;           // Stack / Register transfers
        case 0x37: return 2; // AND [Direct Page] Indirect Long, Y
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
        case 0x9D: { // STA Absolute, X
            uint16_t addr = rom[pc+1] | (rom[pc+2] << 8);
            out << "    STA_abs_x(0x" << std::hex << addr << ");\n";
            break;
        }
        case 0x74: { // STZ Direct Page, X
            out << "    STZ_dp_x(0x" << std::hex << (int)rom[pc+1] << ");\n";
            break;
        }
        case 0x29: { // AND Immediate
            uint16_t val = state.is16A ? (rom[pc+1] | (rom[pc+2] << 8)) : rom[pc+1];
            out << "    AND_imm(0x" << std::hex << val << ");\n";
            break;
        }
        case 0x48: out << "    PHA();\n"; break;   // Push Accumulator
        case 0x1A: out << "    INC_acc();\n"; break; // Increment Accumulator
        case 0x94: out << "    STY_dp_x(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0xAD: { // LDA Absolute
            uint16_t addr = rom[pc+1] | (rom[pc+2] << 8);
            out << "    LDA_abs(0x" << std::hex << addr << ");\n";
            break;
        }

        case 0xE0: { // CPX (Compare X)
            uint16_t val = state.is16XY ? (rom[pc+1] | (rom[pc+2] << 8)) : rom[pc+1];
            out << "    CPX_imm(0x" << std::hex << val << ");\n";
            break;
        }
        case 0x9E: { // STZ Absolute, X
            uint16_t addr = rom[pc+1] | (rom[pc+2] << 8);
            out << "    STZ_abs_x(0x" << std::hex << addr << ");\n";
            break;
        }
        case 0x0A: out << "    ASL_acc();\n"; break; // Shift Left (Multiply by 2)
        case 0x68: out << "    PLA();\n"; break;     // Pull Accumulator from Stack
        case 0xC8: out << "    INY();\n"; break;     // Increment Y
        case 0xB7: out << "    LDA_ind_long_y(0x" << std::hex << (int)rom[pc+1] << ");\n"; break; 
        case 0x91: out << "    STA_ind_y(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;

        case 0xCA: out << "    DEX();\n"; break;
        case 0xA8: out << "    TAY();\n"; break;
        case 0xEA: out << "    NOP();\n"; break;
        case 0x58: out << "    CLI();\n"; break; // Clear Interrupts
        case 0x7A: out << "    PLY();\n"; break; // Pull Y from Stack
        case 0xE6: out << "    INC_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0xA5: out << "    LDA_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0xBD: out << "    LDA_abs_x(0x" << std::hex << (rom[pc+1]|(rom[pc+2]<<8)) << ");\n"; break;
        case 0x84: out << "    STY_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;

        case 0xA4: out << "    LDY_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0x03: out << "    ORA_stack(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0xDC: out << "    JML_ind(0x" << std::hex << (rom[pc+1]|(rom[pc+2]<<8)) << ");\n"; break;
        case 0xAA: out << "    TAX();\n"; break;
        case 0x01: out << "    ORA_ind_x(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0xFF: {
            uint32_t addr = rom[pc+1] | (rom[pc+2] << 8) | (rom[pc+3] << 16);
            out << "    SBC_long_x(0x" << std::hex << addr << ");\n"; 
            break;
        }

        case 0x05: out << "    ORA_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0xF4: out << "    PEA(0x" << std::hex << (rom[pc+1]|(rom[pc+2]<<8)) << ");\n"; break;
        case 0x65: out << "    ADC_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0x26: out << "    ROL_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0x07: out << "    ORA_ind_long(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0x4A: out << "    LSR_acc();\n"; break;
        case 0x09: {
            uint16_t val = state.is16A ? (rom[pc+1]|(rom[pc+2]<<8)) : rom[pc+1];
            out << "    ORA_imm(0x" << std::hex << val << ");\n"; 
            break;
        }

        case 0xE8: out << "    INX();\n"; break;
        case 0x8E: out << "    STX_abs(0x" << std::hex << (rom[pc+1]|(rom[pc+2]<<8)) << ");\n"; break;
        case 0x15: out << "    ORA_dp_x(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0x86: out << "    STX_dp(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0x43: out << "    EOR_sr_ind_y(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0xCD: { // Handling that pesky TODO in sub_0x82
            uint16_t addr = rom[pc+1]|(rom[pc+2]<<8);
            out << "    CMP_abs(0x" << std::hex << addr << ");\n"; 
            break;
        }

        case 0x20: // JSR (Absolute)
        case 0x22: { // JSL (Long)
            uint32_t raw_addr = rom[pc+1] | (rom[pc+2] << 8) | (op == 0x22 ? (rom[pc+3] << 16) : 0);
            uint32_t target = snes_to_file(raw_addr);
            
            out << "    sub_0x" << std::hex << target << "();\n";
            
            // Check if we already registered this address as a function
            if (target != 0xFFFFFFFF) {
                queue.push_back(target); 
            }
            break;
        }

        // Long Addressing ($######)
        case 0x0F: {
            uint32_t addr = rom[pc+1] | (rom[pc+2] << 8) | (rom[pc+3] << 16);
            out << "    ORA_long(0x" << std::hex << addr << ");\n";
            break;
        }
        case 0x6F: {
            uint32_t addr = rom[pc+1] | (rom[pc+2] << 8) | (rom[pc+3] << 16);
            out << "    ADC_long(0x" << std::hex << addr << ");\n";
            break;
        }

        // X Register Ops
        case 0xAE: out << "    LDX_abs(0x" << std::hex << (rom[pc+1]|(rom[pc+2]<<8)) << ");\n"; break;
        case 0xBE: out << "    LDX_abs_y(0x" << std::hex << (rom[pc+1]|(rom[pc+2]<<8)) << ");\n"; break;
        case 0x96: out << "    STX_dp_y(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0xFA: out << "    PLX();\n"; break;
        case 0x9A: out << "    TXS();\n"; break;

        // Indirect / Indexed
        case 0xD1: out << "    CMP_ind_y(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0xA1: out << "    LDA_ind_x(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0x87: out << "    STA_ind_long(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;
        case 0x97: out << "    STA_ind_long_y(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;

        case 0xEB: out << "    XBA();\n"; break;

        // Transfers
        case 0x5B: out << "TCD();\n"; break;
        case 0x1B: out << "TCS();\n"; break;

        case 0x9B: out << "    TXY();\n"; break; // Transfer X to Y
        case 0x37: out << "    AND_ind_long_y(0x" << std::hex << (int)rom[pc+1] << ");\n"; break;

        default: 
            out << "// TODO: Implement Opcode 0x" << std::hex << (int)op << "\n"; 
            break;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) { std::cerr << "Failed to open ROM\n"; return 1; }
    
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    uint32_t snesReset = rom[0x7FFC] | (rom[0x7FFD] << 8);
    uint32_t startAddr = snes_to_file(snesReset);

    std::ofstream out("recompiled_rom.cpp");
    out << "#include \"snes_cpu.h\"\n\n";

    std::deque<uint32_t> queue = { startAddr };
    std::set<uint32_t> visited;
    std::vector<uint32_t> discovered_subs;

    while (!queue.empty()) {
        uint32_t addr = queue.front();
        queue.pop_front();
        
        if (visited.count(addr) || addr >= rom.size() || addr == 0xFFFFFFFF) continue;
        
        visited.insert(addr);
        discovered_subs.push_back(addr);

        out << "void sub_0x" << std::hex << addr << "() {\n";
        uint32_t pc = addr;
        CPUState state; 
        bool end = false;

        while (!end && pc < rom.size()) {
            uint8_t op = rom[pc];
            if (pc >= 0x7FB0 && pc <= 0x7FFF) break;
            if (op == 0x00) { out << "    return; // Padding\n"; break; }

            // Handling Jumps/Calls
            if (op == 0x20 || op == 0x22 || op == 0x4C || op == 0x5C) {
                uint32_t target = snes_to_file(rom[pc+1] | (rom[pc+2] << 8) | (op == 0x22 || op == 0x5C ? (rom[pc+3] << 16) : 0));
                out << "    sub_0x" << std::hex << target << "();\n";
                queue.push_back(target);
                if (op == 0x4C || op == 0x5C) end = true;
                pc += (op == 0x20 || op == 0x4C ? 3 : 4);
            } else if (op == 0x60 || op == 0x6B) {
                out << "    return;\n";
                end = true;
            } else if (op == 0x10 || op == 0x80 || op == 0xD0 || op == 0xF0) {
                int8_t offset = (int8_t)rom[pc + 1];
                uint32_t target = (pc & ~0x7FFF) | ((pc + 2 + offset) & 0x7FFF);
                if (op == 0x80) { out << "    goto addr_0x" << std::hex << target << ";\n"; end = true; }
                else if (op == 0x10) { out << "    if (!regs.P.N) goto addr_0x" << std::hex << target << ";\n"; }
                else if (op == 0xD0) { out << "    if (!regs.P.Z) goto addr_0x" << std::hex << target << ";\n"; }
                else { out << "    if (regs.P.Z) goto addr_0x" << std::hex << target << ";\n"; }
                queue.push_back(target); 
                pc += 2;
            } else {
                emitC(out, rom, pc, state, queue);
                pc += getOpSize(op, state);
            }
        }
        out << "}\n\n";
    }

    // This is the part that was inside the loop previously:
    std::ofstream reg("function_registry.inc");
    for (uint32_t addr : discovered_subs) {
        reg << "function_table[0x" << std::hex << addr << "] = &sub_0x" << addr << ";\n";
    }
    reg.close();

    std::cout << "Generated function_registry.inc with " << std::dec << discovered_subs.size() << " entries." << std::endl;

    std::ofstream proto("function_prototypes.inc");
    for (uint32_t addr : discovered_subs) {
        proto << "void sub_0x" << std::hex << addr << "();\n";
    }
    proto.close();

    std::cout << "Generated function_prototypes.inc with " << std::dec << discovered_subs.size() << " entries." << std::endl;
    
    return 0;
}