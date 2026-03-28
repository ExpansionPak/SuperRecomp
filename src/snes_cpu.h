#ifndef SNES_CPU_H
#define SNES_CPU_H

#include <cstdint>
#include <vector>

// CPU Status Flags
struct StatusFlags {
    uint8_t C : 1; // Carry
    uint8_t Z : 1; // Zero
    uint8_t I : 1; // IRQ Disable
    uint8_t D : 1; // Decimal
    uint8_t X_flag : 1; // Index register size (0=16-bit, 1=8-bit)
    uint8_t M : 1; // Accumulator size (0=16-bit, 1=8-bit)
    uint8_t V : 1; // Overflow
    uint8_t N : 1; // Negative
};

// CPU Registers
struct CPURegs {
    uint16_t A;
    uint16_t X;
    uint16_t Y;
    uint16_t SP;
    uint16_t PC;
    uint8_t  DB; // Data Bank
    StatusFlags P;
};

extern CPURegs regs;
extern std::vector<uint8_t> snes_ram;
extern std::vector<uint8_t> snes_rom;

// Memory Interface
uint8_t snes_memory_read(uint32_t addr);
void snes_memory_write(uint32_t addr, uint8_t val);

// Opcode Functions (Add new ones here as we implement them)
void SEI();
void CLC();
void SEC();
void XCE();
void REP(uint8_t val);
void SEP(uint8_t val);
void TAX();
void TXA();
void DEX();
void INX();

// Instructions
void LDA_imm(uint16_t val);
void LDA_abs(uint32_t addr);
void LDA_abs_x(uint32_t addr);
void STA_long(uint32_t addr);
void STA_dp(uint8_t offset);
void LDX_imm(uint16_t val);
void STA_abs(uint32_t addr);
void STA_abs_x(uint32_t addr);
void STZ_abs(uint32_t addr);

#endif