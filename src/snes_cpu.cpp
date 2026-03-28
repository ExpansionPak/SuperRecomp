#include "snes_cpu.h"
#include <iostream>

CPURegs regs;
std::vector<uint8_t> snes_ram(0x20000, 0); // 128KB WRAM
std::vector<uint8_t> snes_rom;            // Will be loaded from file

uint8_t snes_memory_read(uint32_t addr) {
    // Basic LoROM Mapping
    uint8_t bank = (addr >> 16) & 0xFF;
    uint32_t offset = addr & 0xFFFF;

    // Work RAM (Banks 7E-7F)
    if (bank == 0x7E || bank == 0x7F) {
        return snes_ram[((bank & 1) << 16) | offset];
    }
    
    // ROM Mirroring (Simplified)
    if (offset >= 0x8000) {
        uint32_t rom_addr = ((bank & 0x7F) << 15) | (offset & 0x7FFF);
        if (rom_addr < snes_rom.size()) return snes_rom[rom_addr];
    }

    // Hardware Registers (PPU, DMA, etc.)
    // For now, return 0 to prevent crashes
    return 0;
}

void snes_memory_write(uint32_t addr, uint8_t val) {
    uint8_t bank = (addr >> 16) & 0xFF;
    uint32_t offset = addr & 0xFFFF;

    // Write to WRAM
    if (bank == 0x7E || bank == 0x7F) {
        snes_ram[((bank & 1) << 16) | offset] = val;
    }
    
    // Log writes to hardware registers for debugging
    if (offset >= 0x2100 && offset <= 0x43FF) {
        // std::cout << "Hardware Write: 0x" << std::hex << offset << " = " << (int)val << std::endl;
    }
}

uint8_t snes_stack[0x200];

void LDA_imm(uint16_t val) {
    regs.A = val;
    regs.P.Z = (regs.A == 0);
    // Use the M flag to check if we are in 8-bit or 16-bit mode
    regs.P.N = (regs.P.M ? (regs.A & 0x80) : (regs.A & 0x8000)) ? 0x80 : 0;
}

void DEX() {
    regs.X--;
    regs.P.Z = (regs.X == 0);
    regs.P.N = (regs.P.X_flag ? (regs.X & 0x80) : (regs.X & 0x8000)) ? 0x80 : 0;
}

void SBC_imm(uint16_t val) {
    uint32_t max_val = regs.P.M ? 0xFF : 0xFFFF;
    uint32_t sign_bit = regs.P.M ? 0x80 : 0x8000;
    
    uint32_t temp = (uint32_t)regs.A - (uint32_t)val - (regs.P.C ? 0 : 1);
    
    regs.P.V = (((regs.A ^ val) & (regs.A ^ temp)) & sign_bit) ? 1 : 0;
    regs.A = (uint16_t)(temp & max_val);
    regs.P.C = (temp <= max_val);
    regs.P.Z = (regs.A == 0);
    regs.P.N = (regs.A & sign_bit) ? 0x80 : 0;
}

void SEI() { regs.P.I = 1; }
void CLC() { regs.P.C = 0; }
void SEC() { regs.P.C = 1; }
void XCE() { 
    uint8_t temp = regs.P.C; 
    regs.P.C = 0; // Simplified: Assumes switching to Native Mode
}

void REP(uint8_t val) {
    if (val & 0x20) regs.P.M = 0; // 16-bit Accumulator
    if (val & 0x10) regs.P.X_flag = 0; // 16-bit Index
}

void SEP(uint8_t val) {
    if (val & 0x20) regs.P.M = 1; // 8-bit Accumulator
    if (val & 0x10) regs.P.X_flag = 1; // 8-bit Index
}

void STA_abs(uint16_t addr) {
    snes_memory_write(addr, regs.A & 0xFF);
    if (!regs.P.M) snes_memory_write(addr + 1, regs.A >> 8);
}

void STZ_abs(uint16_t addr) {
    snes_memory_write(addr, 0);
    // STZ behavior depends on M flag for 16-bit writes
    if (!regs.P.M) snes_memory_write(addr + 1, 0);
}

void LDX_imm(uint8_t val) {
    regs.X = val;
    regs.P.Z = (regs.X == 0);
    regs.P.N = (regs.X & 0x80) != 0;
}

void LDA_abs_x(uint16_t addr) {
    regs.A = snes_memory_read(addr + regs.X);
    regs.P.Z = (regs.A == 0);
    regs.P.N = (regs.A & 0x80) != 0;
}

void STA_abs_x(uint16_t addr) {
    snes_memory_write(addr + regs.X, regs.A & 0xFF);
    if (!regs.P.M) snes_memory_write(addr + regs.X + 1, regs.A >> 8);
}