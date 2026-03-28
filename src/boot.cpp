#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>

// Logic to track the state of the M and X flags for accurate instruction length determination
bool is16bitA = false; 
bool is16bitXY = false; 

// Function to write the C header and initial setup
void writeCHeader(std::ofstream& out) {
    out << "#include <stdint.h>\n\n";
    out << "uint8_t snes_stack[0x200]; // Simulate stack page\n"; // Added this
    out << "struct Registers {\n";
    out << "    uint16_t A, X, Y, SP, PC;\n";
    out << "    struct { uint8_t C, Z, I, D, X, M, V, N; } P;\n";
    out << "} regs;\n\n";
    out << "void internal_adc_imm() { /* Logic here */ }\n\n";
    out << "void start_recompiled_code() {\n";
}

struct OpcodeInfo {
    const char* name;
    int len; // Total length in bytes (opcode + operands)
};

// A simplified opcode table for demonstration purposes
OpcodeInfo opTable[256];

std::vector<uint16_t> functionsToBuild;

void emitC(std::ofstream& out, std::vector<uint8_t>& buffer, size_t index, bool is16bitA) {
    uint8_t opcode = buffer[index];
    OpcodeInfo info = opTable[opcode];
    
    out << "    addr_0x" << std::hex << index << ":\n"; 
    out << "    /* Address: 0x" << std::hex << index << " */\n";

    switch(opcode) {
        // Only keep cases that need special C code logic
        case 0xA9: { // LDA Immediate
            uint16_t val = is16bitA ? (buffer[index+1] | (buffer[index+2] << 8)) : buffer[index+1];
            out << "    regs.A = 0x" << std::hex << val << ";\n";
            out << "    regs.P.Z = (regs.A == 0);\n";
            // Check bit 15 for 16-bit, or bit 7 for 8-bit
            if (is16bitA) {
                out << "    regs.P.N = (regs.A & 0x8000) ? 0x80 : 0;\n";
            } else {
                out << "    regs.P.N = (regs.A & 0x80) ? 0x80 : 0;\n";
            }
            break;
        }
        case 0xA2: { // LDX Immediate
            uint16_t val = is16bitXY ? (buffer[index+1] | (buffer[index+2] << 8)) : buffer[index+1];
            out << "    regs.X = 0x" << std::hex << val << ";\n";
            out << "    regs.P.Z = (regs.X == 0);\n";
            out << "    regs.P.N = (is16bitXY ? (regs.X & 0x8000) : (regs.X & 0x80)) ? 0x80 : 0;\n";
            break;
        }
        case 0xA0: { // LDY Immediate
            uint16_t val = is16bitXY ? (buffer[index+1] | (buffer[index+2] << 8)) : buffer[index+1];
            out << "    regs.Y = 0x" << std::hex << val << ";\n";
            out << "    regs.P.Z = (regs.Y == 0);\n";
            out << "    regs.P.N = (is16bitXY ? (regs.Y & 0x8000) : (regs.Y & 0x80)) ? 0x80 : 0;\n";
            break;
        }
        case 0x38: { // SEC (Set Carry)
            out << "    regs.P.C = 1;\n";
            break;
        }
        case 0xE9: { // SBC Immediate
            uint16_t val = is16bitA ? (buffer[index+1] | (buffer[index+2] << 8)) : buffer[index+1];
            out << "    {\n";
            out << "        uint32_t temp = (uint32_t)regs.A - (uint32_t)0x" << std::hex << val << " - (regs.P.C ? 0 : 1);\n";
            out << "        regs.P.V = (((regs.A ^ 0x" << std::hex << val << ") & (regs.A ^ temp)) & " << (is16bitA ? "0x8000" : "0x80") << ") ? 1 : 0;\n";
            out << "        regs.A = (uint16_t)temp" << (is16bitA ? "" : " & 0xFF") << ";\n";
            out << "        regs.P.C = (temp <= " << (is16bitA ? "0xFFFF" : "0xFF") << ");\n";
            out << "        regs.P.Z = (regs.A == 0);\n";
            out << "        regs.P.N = (regs.A & " << (is16bitA ? "0x8000" : "0x80") << ") ? 0x80 : 0;\n";
            out << "    }\n";
            break;
        }
        case 0x9C: { // STZ Absolute
            uint16_t addr = buffer[index+1] | (buffer[index+2] << 8);
            out << "    snes_memory_write(0x" << std::hex << addr << ", 0x00);\n";
            break;
        }
        case 0x9F: {
            uint32_t addr = buffer[index+1] | (buffer[index+2] << 8) | (buffer[index+3] << 16);
            out << "    snes_memory_write_long(0x" << std::hex << addr << " + regs.X, regs.A);\n";
            break;
        }
        case 0x85: { // STA Direct Page
            uint8_t dp_offset = buffer[index+1];
            out << "    snes_memory_write(regs.D + 0x" << std::hex << (int)dp_offset << ", regs.A);\n";
            break;
        }
        case 0x8D: { // STA Absolute
            uint16_t addr = buffer[index+1] | (buffer[index+2] << 8);
            out << "    snes_memory_write(0x" << std::hex << addr << ", regs.A);\n";
            break;
        }
        case 0x8F: { // STA Long
            uint32_t addr = buffer[index+1] | (buffer[index+2] << 8) | (buffer[index+3] << 16);
            out << "    snes_memory_write_long(0x" << std::hex << addr << ", regs.A);\n";
            break;
        }
        case 0xC2: { // REP
            uint8_t val = buffer[index+1];
            out << "    // REP #$" << std::hex << (int)val << "\n";
            if (val & 0x20) out << "    regs.P.M = 0; // Accumulator 16-bit\n";
            if (val & 0x10) out << "    regs.P.X = 0; // Index 16-bit\n";
            break;
        }
        case 0xE2: { // SEP
            uint8_t val = buffer[index+1];
            out << "    // SEP #$" << std::hex << (int)val << "\n";
            if (val & 0x20) out << "    regs.P.M = 1; // Accumulator 8-bit\n";
            if (val & 0x10) out << "    regs.P.X = 1; // Index 8-bit\n";
            break;
        }
        case 0x4C: { // JMP Absolute
            uint16_t target = buffer[index+1] | (buffer[index+2] << 8);
            // If it jumps to itself, it's a "spin loop"
            if (target == index) {
                out << "    while(1); // Infinite loop detected\n";
            } else {
                out << "    goto block_0x" << std::hex << target << ";\n";
            }
            break;
        }
        case 0x20: { // JSR Absolute
            uint16_t target = buffer[index+1] | (buffer[index+2] << 8);
            functionsToBuild.push_back(target);
            out << "    // JSR to 0x" << std::hex << target << "\n";
            out << "    sub_0x" << std::hex << target << "();\n"; 
            break;
        }
        case 0x60: { // RTS - Return from Subroutine
            out << "    return; // RTS (Near)\n";
            break;
        }
        case 0x6B: { // RTL - Return from Subroutine Long
            out << "    return; // RTL (Far)\n";
            break;
        }
        case 0x10: { // BPL (Branch if Plus / N flag is 0)
            int8_t offset = (int8_t)buffer[index+1];
            uint16_t target = (index + 2) + offset;
            out << "    if (!(regs.P.N & 0x80)) goto addr_0x" << std::hex << target << ";\n";
            break;
        }
        case 0xD0: { // BNE (Branch if Not Equal / Z flag is 0)
            int8_t offset = (int8_t)buffer[index+1];
            uint16_t target = (index + 2) + offset;
            out << "    if (!regs.P.Z) goto addr_0x" << std::hex << target << ";\n";
            break;
        }
        case 0xCA: { // DEX
            out << "    regs.X--;\n";
            out << "    regs.P.Z = (regs.X == 0);\n";
            out << "    regs.P.N = (regs.X & 0x8000) ? 0x80 : 0;\n"; 
            break;
        }
        case 0x5B: { // TCD (Transfer A to Direct Page)
            out << "    regs.D = regs.A;\n"; 
            break;
        }
        case 0x1B: { // TCS (Transfer A to Stack Pointer)
            out << "    regs.SP = regs.A;\n";
            break;
        }
        case 0xA8: { // TAY (Transfer A to Y)
            out << "    regs.Y = regs.A;\n";
            out << "    regs.P.Z = (regs.Y == 0);\n";
            out << "    regs.P.N = (is16bitXY ? (regs.Y & 0x8000) : (regs.Y & 0x80)) ? 0x80 : 0;\n";
            break;
        }
        case 0x08: { // PHP (Push Processor Status)
            out << "    // Push P to stack\n";
            out << "    snes_stack[regs.SP--] = (regs.P.N | regs.P.V | (regs.P.M << 5) | (regs.P.X << 4) | (regs.P.D << 3) | (regs.P.I << 2) | (regs.P.Z << 1) | regs.P.C);\n";
            break;
        }
        case 0x28: { // PLP (Pull Processor Status)
            out << "    {\n";
            out << "        uint8_t p = snes_stack[++regs.SP];\n";
            out << "        regs.P.N = p & 0x80; regs.P.V = p & 0x40; regs.P.M = (p >> 5) & 1;\n";
            out << "        regs.P.X = (p >> 4) & 1; regs.P.D = (p >> 3) & 1; regs.P.I = (p >> 2) & 1;\n";
            out << "        regs.P.Z = (p >> 1) & 1; regs.P.C = p & 0x01;\n";
            out << "    }\n";
            break;
        }
        // The "Magic" Default: Use the table for everything else!
        default: {
            if (info.name != nullptr && std::string(info.name) != "UNK") {
                out << "    // " << info.name << " (Opcode 0x" << std::hex << (int)opcode << ") detected\n";
            } else {
                out << "    // Unknown/Unimplemented: 0x" << std::hex << (int)opcode << "\n";
            }
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    
    for (int i = 0; i < 256; i++) {
        opTable[i] = { "UNK", 1 }; 
    }

    opTable[0x08] = { "PHP", 1 },
    opTable[0x18] = { "CLC", 1 };
    opTable[0xFB] = { "XCE", 1 };
    opTable[0x10] = { "BPL", 2 };
    opTable[0x30] = { "BMI", 2 };
    opTable[0xCA] = { "DEX", 1 };
    opTable[0x88] = { "DEY", 1 };
    opTable[0x4C] = { "JMP", 3 };
    opTable[0x20] = { "JSR", 3 };
    opTable[0x22] = { "JSL", 4 };
    opTable[0xA2] = { "LDX", 2 }; // Length depends on M flag
    opTable[0xA0] = { "LDY", 2 }; // Length depends on M flag
    opTable[0xEA] = { "NOP", 1 };
    opTable[0xC2] = { "REP", 2 };
    opTable[0xE9] = { "SBC", 2 }; // Length depends on M flag
    opTable[0x38] = { "SEC", 1 };
    opTable[0xE2] = { "SEP", 2 };
    opTable[0x8D] = { "STA", 3 };
    opTable[0x8F] = { "STA", 4 };
    opTable[0x85] = { "STA", 2 };
    opTable[0x9F] = { "STA", 4 }; // STA Absolute Long, X
    opTable[0x9E] = { "STZ", 3 }; // STZ Absolute, X
    opTable[0x9D] = { "STA", 3 }; // STA Absolute, X
    opTable[0xA9] = { "LDA", 2 };
    opTable[0x9C] = { "STZ", 3 };
    opTable[0x64] = { "STZ", 2 };
    opTable[0xA8] = { "TAY", 1 };
    opTable[0x98] = { "TYA", 1 };
    opTable[0x9B] = { "TXY", 1 };
    opTable[0xBB] = { "TYX", 1 };
    opTable[0x5B] = { "TCD", 1 };
    opTable[0x1B] = { "TCS", 1 };
    opTable[0x7B] = { "TDC", 1 };
    opTable[0x3B] = { "TSC", 1 };
    opTable[0x78] = { "SEI", 1 };

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <rom_file>" << std::endl;
        return 1;
    }

    std::ifstream romFile(argv[1], std::ios::binary | std::ios::ate);
    if (!romFile) {
        std::cerr << "Error! Could not open ROM." << std::endl;
        return 1;
    }

    std::streamsize size = romFile.tellg();
    int headerOffset = (size % 0x8000 == 512) ? 512 : 0;
    
    // Load ROM into memory, skipping the SMC header if it exists
    std::vector<uint8_t> buffer(size - headerOffset);
    romFile.seekg(headerOffset, std::ios::beg);
    romFile.read(reinterpret_cast<char*>(buffer.data()), buffer.size());

    std::cout << "ROM Loaded. Size: " << buffer.size() << " bytes." << std::endl;

    std::ofstream outFile("output.c");
    writeCHeader(outFile);

    for (size_t i = 0; i < 100 && i < buffer.size(); ) {
        uint8_t opcode = buffer[i];

        // 1. Update State
        if (opcode == 0xC2) { // REP
            uint8_t operand = buffer[i+1];
            if (operand & 0x20) is16bitA = true;
            if (operand & 0x10) is16bitXY = true;
        } 
        else if (opcode == 0xE2) { // SEP
            uint8_t operand = buffer[i+1];
            if (operand & 0x20) is16bitA = false;
            if (operand & 0x10) is16bitXY = false;
        }

        // 2. Determine Length
        int length = opTable[opcode].len;

        // 16-bit Accumulator (M flag) affects LDA, SBC, ADC, CMP
        if (is16bitA) {
            if (opcode == 0xA9 || opcode == 0xE9 || opcode == 0x69 || opcode == 0xC9) {
                length = 3; 
            }
        }

        // 16-bit Index (X flag) affects LDX, LDY, CPX, CPY
        if (is16bitXY) {
            if (opcode == 0xA2 || opcode == 0xA0 || opcode == 0xE0 || opcode == 0xC0) {
                length = 3; 
            }
        }
        
        if (length == 0) length = 1; 

        emitC(outFile, buffer, i, is16bitA);
        
        i += length; 
    }

    outFile << "}\n";

    outFile << "// Discovered Subroutines\n";
    for (uint16_t addr : functionsToBuild) {
        outFile << "void sub_0x" << std::hex << addr << "() { /* Recompile later */ }\n";
    }
    
    std::cout << "Recompilation (Partial) complete! Check output.c" << std::endl;

    return 0;
}