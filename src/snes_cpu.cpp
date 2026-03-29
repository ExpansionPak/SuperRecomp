#include "snes_cpu.h"
#include <iostream>
#include <vector>

CPURegs regs;
std::vector<uint8_t> snes_ram(0x20000, 0); // 128KB WRAM
std::vector<uint8_t> snes_rom;            // Loaded from file

// DMA State
struct DMAChannel {
    uint8_t control, dest_reg, source_bk;
    uint16_t source, size;
} dma[8];

// --- Core Memory System ---

uint8_t snes_memory_read(uint32_t addr) {
    uint8_t bank = (addr >> 16) & 0xFF;
    uint32_t offset = addr & 0xFFFF;

    // Work RAM (7E:0000-7F:FFFF)
    if (bank == 0x7E || bank == 0x7F) {
        return snes_ram[((bank & 1) << 16) | offset];
    }
    
    // LoROM Mirroring
    if (offset >= 0x8000) {
        uint32_t rom_addr = ((bank & 0x7F) << 15) | (offset & 0x7FFF);
        if (rom_addr < snes_rom.size()) return snes_rom[rom_addr];
    }

    // Direct Page / Registers mirroring in low banks
    if (bank < 0x40 && offset < 0x2000) {
        return snes_ram[offset]; // Mirror first 8KB of WRAM
    }

    return 0;
}

// Pull a single byte from the stack
uint8_t snes_stack_pull() {
    // In 65816, we increment the stack pointer first, then read
    regs.S++;
    
    // In Emulation mode (E=1), the high byte of S is forced to 0x01
    if (regs.P.E) {
        regs.S = (regs.S & 0xFF) | 0x0100;
    }
    
    return snes_memory_read(regs.S);
}

// Push a single byte onto the stack
void snes_stack_push(uint8_t val) {
    snes_memory_write(regs.S, val);
    
    // Decrement stack pointer
    regs.S--;
    
    // In Emulation mode, keep S within page 1
    if (regs.P.E) {
        regs.S = (regs.S & 0xFF) | 0x0100;
    }
}

uint32_t get_addr_ind_long_y(uint8_t dp_offset) {
    uint32_t base = snes_memory_read(regs.D + dp_offset);
    base |= (snes_memory_read(regs.D + dp_offset + 1) << 8);
    base |= (snes_memory_read(regs.D + dp_offset + 2) << 16);
    return base + regs.Y;
}

void snes_memory_write(uint32_t addr, uint8_t val) {
    uint8_t bank = (addr >> 16) & 0xFF;
    uint32_t offset = addr & 0xFFFF;

    // WRAM Write
    if (bank == 0x7E || bank == 0x7F) {
        snes_ram[((bank & 1) << 16) | offset] = val;
        return;
    }

    // Mirror low WRAM
    if (bank < 0x40 && offset < 0x2000) {
        snes_ram[offset] = val;
        return;
    }

    // DMA Register Logic
    if ((offset & 0xFF80) == 0x4300) {
        int ch = (offset >> 4) & 0x7;
        switch (offset & 0xF) {
            case 0x0: dma[ch].control = val; break;
            case 0x1: dma[ch].dest_reg = val; break;
            case 0x2: dma[ch].source = (dma[ch].source & 0xFF00) | val; break;
            case 0x3: dma[ch].source = (dma[ch].source & 0x00FF) | (val << 8); break;
            case 0x4: dma[ch].source_bk = val; break;
            case 0x5: dma[ch].size = (dma[ch].size & 0xFF00) | val; break;
            case 0x6: dma[ch].size = (dma[ch].size & 0x00FF) | (val << 8); break;
        }
    }

    if (offset == 0x420b && (val & 0x01)) execute_dma(0);
}

// --- Helpers ---

inline void update_nz(uint16_t val, bool is_8bit) {
    regs.P.Z = (val == 0);
    regs.P.N = (is_8bit ? (val & 0x80) : (val & 0x8000)) ? 0x80 : 0;
}

void snes_stack_push(uint16_t val, bool is_8bit) {
    snes_memory_write(regs.S--, val & 0xFF);
    if (!is_8bit) snes_memory_write(regs.S--, val >> 8);
}

uint16_t snes_stack_pop(bool is_8bit) {
    uint16_t val = 0;
    if (!is_8bit) val = snes_memory_read(++regs.S) << 8;
    val |= snes_memory_read(++regs.S);
    return val;
}

// Opcode 0x5C: JML long indirect [addr]
// Used in sub_0x6df for dynamic jumps. 
// NOTE: In a recompiler, we return the target address or use a function pointer map.
uint32_t JML_ind(uint8_t dp_offset) {
    uint32_t target = snes_memory_read(regs.D + dp_offset);
    target |= (snes_memory_read(regs.D + dp_offset + 1) << 8);
    target |= (snes_memory_read(regs.D + dp_offset + 2) << 16);
    return target; 
}

// --- Instructions ---

void LDA_imm(uint16_t val) {
    regs.A = val;
    update_nz(regs.A, regs.P.M);
}

void LDX_imm(uint16_t val) {
    regs.X = val;
    update_nz(regs.X, regs.P.X_flag);
}

void LDY_imm(uint16_t val) {
    regs.Y = val;
    update_nz(regs.Y, regs.P.X_flag);
}

void STA_long(uint32_t addr) {
    snes_memory_write(addr, regs.A & 0xFF);
    if (!regs.P.M) snes_memory_write(addr + 1, regs.A >> 8);
}

void STA_long_x(uint32_t addr) {
    STA_long(addr + regs.X);
}

void ADC_imm(uint16_t val) {
    uint32_t a = regs.A;
    uint32_t sum = a + val + (regs.P.C ? 1 : 0);
    uint32_t mask = regs.P.M ? 0xFF : 0xFFFF;
    uint32_t sign = regs.P.M ? 0x80 : 0x8000;

    regs.P.V = (~(a ^ val) & (a ^ sum) & sign) ? 1 : 0;
    regs.P.C = sum > mask;
    regs.A = sum & mask;
    update_nz(regs.A, regs.P.M);
}

void SBC_imm(uint16_t val) {
    ADC_imm(~val & (regs.P.M ? 0xFF : 0xFFFF));
}

void DEX() {
    regs.X = (regs.X - 1) & (regs.P.X_flag ? 0xFF : 0xFFFF);
    update_nz(regs.X, regs.P.X_flag);
}

void DEY() {
    regs.Y = (regs.Y - 1) & (regs.P.X_flag ? 0xFF : 0xFFFF);
    update_nz(regs.Y, regs.P.X_flag);
}

void TXS() { regs.S = regs.X; } // Note: Native S is 16-bit
void TXY() { regs.Y = regs.X; update_nz(regs.Y, regs.P.X_flag); }
void TYA() { regs.A = regs.Y; update_nz(regs.A, regs.P.M); }

void PLX() { regs.X = snes_stack_pop(regs.P.X_flag); update_nz(regs.X, regs.P.X_flag); }
void PLY() { regs.Y = snes_stack_pop(regs.P.X_flag); update_nz(regs.Y, regs.P.X_flag); }

void REP(uint8_t val) {
    if (val & 0x20) regs.P.M = 0;
    if (val & 0x10) regs.P.X_flag = 0;
}

void SEP(uint8_t val) {
    if (val & 0x20) regs.P.M = 1;
    if (val & 0x10) regs.P.X_flag = 1;
}

void XCE() {
    bool temp = regs.P.C;
    regs.P.C = 0; // Switching to Native (Standard for SMW)
    // In a full emu, this would swap M/X flags, but we assume Native.
}

// Opcode 0x05: ORA dp (OR with Direct Page)
void ORA_dp(uint8_t offset) {
    uint16_t val = snes_memory_read(regs.D + offset);
    if (!regs.P.M) val |= (snes_memory_read(regs.D + offset + 1) << 8);
    regs.A |= val;
    update_nz(regs.A, regs.P.M);
}

// Opcode 0x10: BPL (Branch if Plus / N flag is 0)
// This is handled by the recompiler's "if (!regs.P.N) goto ..." logic, so no separate function is needed.

// Opcode 0x2A: ROL Accumulator
void ROL_acc() {
    uint32_t mask = regs.P.M ? 0x80 : 0x8000;
    uint32_t limit = regs.P.M ? 0xFF : 0xFFFF;
    uint8_t old_carry = regs.P.C;
    regs.P.C = (regs.A & mask) ? 1 : 0;
    regs.A = ((regs.A << 1) | old_carry) & limit;
    update_nz(regs.A, regs.P.M);
}

// Opcode 0x3A: DEC Accumulator
void DEC_acc() {
    regs.A = (regs.A - 1) & (regs.P.M ? 0xFF : 0xFFFF);
    update_nz(regs.A, regs.P.M);
}

// Opcode 0x1A: INC Accumulator
void INC_acc() {
    regs.A = (regs.A + 1) & (regs.P.M ? 0xFF : 0xFFFF);
    update_nz(regs.A, regs.P.M);
}

// Opcode 0xEB: XBA (Exchange B and A - swaps high and low bytes of A)
void XBA() {
    uint8_t low = regs.A & 0xFF;
    uint8_t high = (regs.A >> 8) & 0xFF;
    regs.A = (low << 8) | high;
    // XBA only updates N/Z based on the NEW low byte (the old high byte)
    update_nz(low, true); 
}

// Opcode 0x0F: ORA long (24-bit address)
void ORA_long(uint32_t addr) {
    uint16_t val = snes_memory_read(addr);
    if (!regs.P.M) val |= (snes_memory_read(addr + 1) << 8);
    regs.A |= val;
    update_nz(regs.A, regs.P.M);
}

// Opcode 0xAF: LDA long (24-bit address)
void LDA_long(uint32_t addr) {
    regs.A = snes_memory_read(addr);
    if (!regs.P.M) regs.A |= (snes_memory_read(addr + 1) << 8);
    update_nz(regs.A, regs.P.M);
}

// Opcode 0xB7: LDA [dp], y
void LDA_ind_long_y(uint8_t dp_offset) {
    uint32_t addr = get_addr_ind_long_y(dp_offset);
    regs.A = snes_memory_read(addr);
    if (!regs.P.M) regs.A |= (snes_memory_read(addr + 1) << 8);
    update_nz(regs.A, regs.P.M);
}

// Opcode 0x97: STA [dp], y
void STA_ind_long_y(uint8_t dp_offset) {
    uint32_t addr = get_addr_ind_long_y(dp_offset);
    snes_memory_write(addr, regs.A & 0xFF);
    if (!regs.P.M) snes_memory_write(addr + 1, regs.A >> 8);
}

// Opcode 0x66: ROL dp
void ROL_dp(uint8_t dp_offset) {
    uint16_t val = snes_memory_read(regs.D + dp_offset);
    uint8_t old_carry = regs.P.C;
    regs.P.C = (val & 0x80) ? 1 : 0;
    val = ((val << 1) | old_carry) & 0xFF;
    snes_memory_write(regs.D + dp_offset, (uint8_t)val);
    update_nz(val, true);
}

// Opcode 0x0F: ORA long (24-bit)
void ORA_long(uint32_t addr) {
    uint16_t val = snes_memory_read(addr);
    if (!regs.P.M) val |= (snes_memory_read(addr + 1) << 8);
    regs.A |= val;
    update_nz(regs.A, regs.P.M);
}

// Opcode 0x9A: TXS (Transfer X to Stack)
void TXS() {
    regs.S = regs.X;
    // Note: TXS does not affect flags
}

// Opcode 0x6F: ADC long (24-bit)
void ADC_long(uint32_t addr) {
    uint32_t val = snes_memory_read(addr);
    if (!regs.P.M) val |= (snes_memory_read(addr + 1) << 8);
    
    uint32_t sum = regs.A + val + regs.P.C;
    regs.P.V = (~(regs.A ^ val) & (regs.A ^ sum) & (regs.P.M ? 0x80 : 0x8000)) ? 1 : 0;
    regs.A = sum & (regs.P.M ? 0xFF : 0xFFFF);
    regs.P.C = (sum > (regs.P.M ? 0xFF : 0xFFFF)) ? 1 : 0;
    update_nz(regs.A, regs.P.M);
}

// Opcode 0x9B: TXY (Transfer X to Y)
void TXY() {
    regs.Y = regs.X;
    update_nz(regs.Y, regs.P.X);
}

// Opcode 0xFA: PLX (Pull Index X)
void PLX() {
    regs.X = snes_stack_pull(); // Pull Low Byte
    if (!regs.P.X) {
        // If 16-bit, pull High Byte and combine
        regs.X |= (snes_stack_pull() << 8); 
    }
    update_nz(regs.X, regs.P.X);
}

// Opcode 0x87: STA [dp] (Direct Page Indirect Long)
void STA_dp_ind_long(uint8_t dp_offset) {
    uint32_t addr = snes_memory_read(regs.D + dp_offset);
    addr |= (snes_memory_read(regs.D + dp_offset + 1) << 8);
    addr |= (snes_memory_read(regs.D + dp_offset + 2) << 16);
    
    snes_memory_write(addr, regs.A & 0xFF);
    if (!regs.P.M) snes_memory_write(addr + 1, regs.A >> 8);
}

// Opcode 0x96: STX dp, Y (Store X to Direct Page, Y-indexed)
void STX_dp_y(uint8_t dp_offset) {
    uint16_t addr = (regs.D + dp_offset + regs.Y) & 0xFFFF;
    snes_memory_write(addr, regs.X & 0xFF);
    if (!regs.P.X) snes_memory_write(addr + 1, regs.X >> 8);
}

// Opcode 0x91: STA (dp), y
void STA_ind_y(uint8_t offset) {
    uint32_t base = snes_memory_read(regs.D + offset);
    base |= (snes_memory_read(regs.D + offset + 1) << 8);
    // Use the Data Bank Register (DBR) for the high byte
    uint32_t addr = (regs.DBR << 16) | base;
    addr += regs.Y;

    snes_memory_write(addr, regs.A & 0xFF);
    if (!regs.P.M) snes_memory_write(addr + 1, regs.A >> 8);
}

// Opcode 0x97: STA [dp], Y (Direct Page Indirect Long, Y-indexed)
void STA_ind_long_y(uint8_t dp_offset) {
    uint32_t addr = snes_memory_read(regs.D + dp_offset);
    addr |= (snes_memory_read(regs.D + dp_offset + 1) << 8);
    addr |= (snes_memory_read(regs.D + dp_offset + 2) << 16);
    addr += regs.Y;
    
    snes_memory_write(addr, regs.A & 0xFF);
    if (!regs.P.M) snes_memory_write(addr + 1, regs.A >> 8);
}

void JML_ind(uint16_t addr) {
    // Read the 24-bit pointer from memory (usually in Bank 0 or WRAM)
    uint32_t target = snes_memory_read(addr);
    target |= (snes_memory_read(addr + 1) << 8);
    target |= (snes_memory_read(addr + 2) << 16);

    // Dynamic Dispatch: Convert the SNES jump into a C++ function call
    if (function_table.count(target)) {
        function_table[target]();
    } else {
        printf("FAILED: Jumped to uncompiled address 0x%06X\n", target);
    }
}

void LDA_dp(uint8_t offset) {
    // HACK: If the game is checking address 0x10, simulate a V-Blank happened
    if (offset == 0x10) snes_memory_write(0x10, 1); 

    regs.A = snes_memory_read(regs.D + offset);
    if (!regs.P.M) regs.A |= (snes_memory_read(regs.D + offset + 1) << 8);
    update_nz(regs.A, regs.P.M);
}

void execute_dma(int ch) {
    uint32_t src = (dma[ch].source_bk << 16) | dma[ch].source;
    uint32_t dest = 0x2100 + dma[ch].dest_reg;
    uint32_t len = dma[ch].size == 0 ? 0x10000 : dma[ch].size;

    for (uint32_t i = 0; i < len; i++) {
        snes_memory_write(dest, snes_memory_read(src + i));
    }
}