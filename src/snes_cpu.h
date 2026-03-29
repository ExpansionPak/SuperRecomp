#ifndef SNES_CPU_H
#define SNES_CPU_H

#include <cstdint>
#include <map>
#include <vector>

// Define a type for the recompiled functions
typedef void (*RecompiledFunc)();

// The global map of SNES Address -> C++ Function
extern std::map<uint32_t, RecompiledFunc> function_table;

// Helper to register functions
void register_functions();

// CPU Status Flags
struct StatusFlags {
    uint8_t C : 1; // Carry
    uint8_t Z : 1; // Zero
    uint8_t I : 1; // IRQ Disable
    uint8_t D : 1; // Decimal
    uint8_t E : 1; // Emulation Mode
    uint8_t X : 1; // Index Register Size (0=16-bit, 1=8-bit)
    uint8_t X_flag : 1; // Index register size (0=16-bit, 1=8-bit)
    uint8_t M : 1; // Accumulator size (0=16-bit, 1=8-bit)
    uint8_t V : 1; // Overflow
    uint8_t N : 1; // Negative
};

// CPU Registers
struct CPURegs {
    uint16_t A;
    uint16_t S; // Stack Pointer
    uint16_t D; // Direct Page Register
    uint16_t X;
    uint16_t Y;
    uint16_t SP;
    uint16_t PC;
    uint8_t DB; // Data Bank
    uint8_t DBR; // Data Bank Register
    uint8_t PBR; // Program Bank Register
    StatusFlags P;
};

extern CPURegs regs;
extern std::vector<uint8_t> snes_ram;
extern std::vector<uint8_t> snes_rom;

// Memory Interface
uint8_t snes_memory_read(uint32_t addr);
void snes_memory_write(uint32_t addr, uint8_t val);

// --- System & Control ---
void SEI();     // Disable Interrupts
void CLI();     // Enable Interrupts
void CLC();     // Clear Carry
void SEC();     // Set Carry
void XCE();     // Exchange Carry and Emulation
void REP(uint8_t val); // Reset Processor Status bits
void SEP(uint8_t val); // Set Processor Status bits
void XBA();     // Exchange B and A (8-bit swap)
void NOP();     // No Operation

// --- Load & Store (Standard) ---
void LDA_imm(uint16_t val);
void LDA_abs(uint16_t addr);
void LDA_dp(uint8_t offset);
void LDA_abs_x(uint16_t addr);
void LDX_imm(uint16_t val);
void LDX_abs(uint16_t addr);
void LDY_imm(uint16_t val);
void LDY_dp(uint8_t offset);
void STA_abs(uint16_t addr);
void STA_dp(uint8_t offset);
void STA_abs_x(uint16_t addr);
void STX_abs(uint16_t addr);
void STX_dp(uint8_t offset);
void STY_dp(uint8_t offset);
void STZ_abs(uint16_t addr);
void STZ_dp(uint8_t offset);
void STZ_abs_x(uint16_t addr);
void STZ_dp_x(uint8_t offset);

// --- Load & Store (Long/Indirect/Indexed) ---
void LDA_long(uint32_t addr);
void LDA_ind_x(uint8_t offset);      // (dp, x)
void LDA_ind_long_y(uint8_t offset); // [dp], y
void STA_long(uint32_t addr);
void STA_long_x(uint32_t addr);
void STA_ind_y(uint8_t offset);      // (dp), y
void STA_ind_long(uint8_t offset);   // [dp]
void STA_ind_long_y(uint8_t offset); // [dp], y
void STX_dp_y(uint8_t offset);
void STY_dp_x(uint8_t offset);
void LDX_abs_y(uint16_t addr);

// --- Arithmetic & Logical ---
void ADC_imm(uint16_t val);
void ADC_dp(uint8_t offset);
void ADC_long(uint32_t addr);
void SBC_imm(uint16_t val);
void SBC_long_x(uint32_t addr);
void AND_imm(uint16_t val);
void AND_ind_long_y(uint8_t offset);
void ORA_imm(uint16_t val);
void ORA_dp(uint8_t offset);
void ORA_dp_x(uint8_t offset);
void ORA_long(uint32_t addr);
void ORA_stack(uint8_t offset);
void ORA_ind_x(uint8_t offset);
void ORA_ind_long(uint8_t offset);
void EOR_sr_ind_y(uint8_t offset);
void ASL_acc();
void LSR_acc();
void ROL_dp(uint8_t offset);
void INC_acc();
void INC_dp(uint8_t offset);
void INX();
void INY();
void DEX();
void DEY();
void CMP_abs(uint16_t addr);
void CMP_ind_y(uint8_t offset);
void CPX_imm(uint16_t val);

// --- Stack & Register Transfers ---
void PHA();     // Push A
void PLA();     // Pull A
void PHP();     // Push P
void PLP();     // Pull P
void PHX();     // Push X
void PLX();     // Pull X
void PHY();     // Push Y
void PLY();     // Pull Y
void PEA(uint16_t addr); // Push Effective Absolute Address
void TAX();     // Transfer A to X
void TAY();     // Transfer A to Y
void TXA();     // Transfer X to A
void TYA();     // Transfer Y to A
void TXS();     // Transfer X to Stack
void TSX();     // Transfer Stack to X
void TCD();     // Transfer C to Direct Page
void TCS();     // Transfer C to Stack
void TXY();     // Transfer X to Y

// --- Branching/Jumping (Usually handled by recompiler logic, but declared if used as opcodes) ---
void JML_ind(uint16_t addr);

#endif