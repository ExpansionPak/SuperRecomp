#ifndef SNES_CPU_H
#define SNES_CPU_H

#include <cstdint>
#include <map>
#include <vector>

typedef void (*RecompiledFunc)();
extern std::map<uint32_t, RecompiledFunc> function_table;
void register_functions();

struct StatusFlags {
    uint8_t C : 1; uint8_t Z : 1; uint8_t I : 1; uint8_t D : 1;
    uint8_t E : 1; uint8_t X : 1; uint8_t X_flag : 1; uint8_t M : 1;
    uint8_t V : 1; uint8_t N : 1;
};

struct CPURegs {
    uint16_t A; uint16_t S; uint16_t D; uint16_t X; uint16_t Y;
    uint16_t SP; uint16_t PC; uint8_t DB; uint8_t DBR; uint8_t PBR;
    StatusFlags P;
};

extern CPURegs regs;
extern std::vector<uint8_t> snes_ram;
extern std::vector<uint8_t> snes_rom;

extern uint8_t palette_data[512];

extern uint32_t next_func_addr;

uint8_t snes_memory_read(uint32_t addr);
void snes_memory_write(uint32_t addr, uint8_t val);

uint32_t snes_to_file(uint32_t addr);
extern std::vector<uint8_t> rom_data;

// --- System & Control ---
void SEI(); void CLI(); void CLC(); void SEC(); void XCE();
void REP(uint8_t val); void SEP(uint8_t val); void XBA(); void NOP();

// --- Load & Store (Standard) ---
// Note: Using 'unsigned short' for 16-bit address parameters to match linker symbols
void LDA_imm(uint16_t val);
void LDA_abs(unsigned short addr);
void LDA_dp(uint8_t offset);
void LDA_abs_x(unsigned short addr);
void LDX_imm(uint16_t val);
void LDX_abs(unsigned short addr);
void LDY_imm(uint16_t val);
void LDY_dp(uint8_t offset);
void STA_abs(unsigned short addr);
void STA_dp(uint8_t offset);
void STA_abs_x(unsigned short addr);
void STX_abs(unsigned short addr);
void STX_dp(uint8_t offset);
void STY_dp(uint8_t offset);
void STZ_abs(unsigned short addr);
void STZ_dp(uint8_t offset);
void STZ_abs_x(unsigned short addr);
void STZ_dp_x(uint8_t offset);

// --- Load & Store (Long/Indirect/Indexed) ---
// Note: Using 'unsigned int' for 24-bit/Long address parameters
void LDA_long(unsigned int addr);
void LDA_ind_x(uint8_t offset);
void LDA_ind_long_y(uint8_t offset);
void STA_long(unsigned int addr);
void STA_long_x(unsigned int addr);
void STA_ind_y(uint8_t offset);
void STA_ind_long(uint8_t offset);
void STA_ind_long_y(uint8_t offset);
void STX_dp_y(uint8_t offset);
void STY_dp_x(uint8_t offset);
void LDX_abs_y(unsigned short addr);

// --- Arithmetic & Logical ---
void ADC_imm(uint16_t val);
void ADC_dp(uint8_t offset);
void ADC_long(unsigned int addr);
void SBC_imm(uint16_t val);
void SBC_long_x(unsigned int addr);
void AND_imm(uint16_t val);
void AND_ind_long_y(uint8_t offset);
void ORA_imm(uint16_t val);
void ORA_dp(uint8_t offset);
void ORA_dp_x(uint8_t offset);
void ORA_long(unsigned int addr);
void ORA_stack(uint8_t offset);
void ORA_ind_x(uint8_t offset);
void ORA_ind_long(uint8_t offset);
void EOR_sr_ind_y(uint8_t offset);
void ASL_acc(); void LSR_acc(); void ROL_dp(uint8_t offset);
void INC_acc(); void INC_dp(uint8_t offset);
void INX(); void INY(); void DEX(); void DEY();
void CMP_abs(unsigned short addr);
void CMP_ind_y(uint8_t offset);
void CPX_imm(uint16_t val);

// --- Stack & Register Transfers ---
void PHA(); void PLA(); void PHP(); void PLP(); void PHX(); void PLX(); void PHY(); void PLY();
void PEA(unsigned short addr);
void TAX(); void TAY(); void TXA(); void TYA(); void TXS(); void TSX(); void TCD(); void TCS(); void TXY();

// --- Branching/Jumping ---
void JML_ind(unsigned short addr);

#endif