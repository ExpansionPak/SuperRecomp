#include "snes_cpu.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

// Global State
CPURegs regs;
std::vector<uint8_t> snes_ram(0x20000, 0); // 128KB WRAM
uint8_t snes_vram[0x10000];               // 64KB VRAM
uint8_t cgram_address = 0;
uint8_t palette_data[512]; // 256 colors * 2 bytes

uint32_t dma_src_addr[8] = {0};
uint16_t dma_transfer_size[8] = {0};
uint8_t dma_dest_reg[8] = {0};
uint8_t dma_parameters[8] = {0};

std::map<uint32_t, RecompiledFunc> function_table;
bool cgram_flip_flop = false; // False = Low byte, True = High byte

int call_depth = 0;

extern std::vector<uint8_t> rom_data;

uint32_t snes_to_file(uint32_t addr) {
    uint8_t bank = (addr >> 16) & 0xFF;
    uint32_t offset = addr & 0xFFFF;
    if (offset < 0x8000 && bank < 0x7E) return 0xFFFFFFFF; 
    return ((bank & 0x7F) << 15) | (offset & 0x7FFF);
}

void call_snes_sub(uint32_t addr) {
    if (function_table.count(addr) && function_table[addr] != nullptr) {
        function_table[addr]();
    } else {
        std::cerr << "[Error] Attempted to call unknown address: 0x" << std::hex << addr << std::endl;
        exit(1); // Stop before it crashes hard
    }
}

void update_nz(uint16_t val, bool is8bit) {
    if (is8bit) {
        regs.P.Z = ((uint8_t)val == 0);
        regs.P.N = (((uint8_t)val & 0x80) != 0);
    } else {
        regs.P.Z = (val == 0);
        regs.P.N = ((val & 0x8000) != 0);
    }
}

uint32_t next_func_addr = 0;

uint8_t pack_status() {
    uint8_t res = 0;
    if (regs.P.C) res |= 0x01;
    if (regs.P.Z) res |= 0x02;
    if (regs.P.I) res |= 0x04;
    if (regs.P.D) res |= 0x08;
    if (regs.P.X_flag) res |= 0x10;
    if (regs.P.M) res |= 0x20;
    if (regs.P.V) res |= 0x40;
    if (regs.P.N) res |= 0x80;
    return res;
}

void unpack_status(uint8_t res) {
    regs.P.C = res & 0x01;
    regs.P.Z = res & 0x02;
    regs.P.I = res & 0x04;
    regs.P.D = res & 0x08;
    regs.P.X_flag = res & 0x10;
    regs.P.M = res & 0x20;
    regs.P.V = res & 0x40;
    regs.P.N = res & 0x80;
}

// Memory System
uint8_t snes_memory_read(uint32_t addr) {
    uint8_t bank = (addr >> 16) & 0xFF;
    uint32_t offset = addr & 0xFFFF;

    // --- MIRRORING FIX ---
    // Banks $00-$3F and $80-$BF mirror WRAM at $0000-$1FFF
    if ((bank < 0x40 || (bank >= 0x80 && bank < 0xC0)) && offset < 0x2000) {
        return snes_ram[offset];
    }

    // Official WRAM Banks
    if (bank == 0x7E || bank == 0x7F) {
        uint32_t ram_addr = ((bank & 0x01) << 16) | offset;
        if (ram_addr < snes_ram.size()) return snes_ram[ram_addr];
    }

    // ROM Access (LoROM)
    if (bank < 0x40 && offset >= 0x8000) {
        uint32_t file_addr = snes_to_file(addr);
        if (file_addr < rom_data.size()) return rom_data[file_addr];
    }

    // Hardware Registers
    if (bank < 0x40) {
        if (offset == 0x4212) return 0x81; // V-Blank + Joypad Ready
        if (offset == 0x213F) return 0x80; // PPU Status
    }

    return 0;
}

void execute_dma(int ch) {
    uint32_t src = dma_src_addr[ch];
    uint32_t size = dma_transfer_size[ch] == 0 ? 0x10000 : dma_transfer_size[ch];
    uint8_t dest_reg = dma_dest_reg[ch];

    // Log the transfer
    std::cout << "[DMA] Ch" << ch << ": 0x" << std::hex << src 
              << " -> PPU Reg 0x21" << (int)dest_reg << " Size: 0x" << size << std::endl;

    for (uint32_t i = 0; i < size; i++) {
        uint8_t data = 0;
        uint8_t bank = (src >> 16) & 0xFF;
        uint32_t offset = src & 0xFFFF;

        // READ: From ROM or RAM
        if (bank < 0x40 && offset >= 0x8000) {
            uint32_t file_addr = snes_to_file(src);
            if (file_addr < rom_data.size()) data = rom_data[file_addr];
        } else {
            data = snes_memory_read(src);
        }

        // WRITE: To PPU Registers
        // 0x22 is Palette Data, 0x18 is VRAM Data, etc.
        if (dest_reg == 0x22) { // Palette Data
            if (!cgram_flip_flop) {
                palette_data[cgram_address * 2] = data; // Use 'data', NOT 'regs.A'
                cgram_flip_flop = true;
            } else {
                palette_data[cgram_address * 2 + 1] = data;
                if (cgram_address == 0) {
                    uint16_t color = palette_data[0] | (palette_data[1] << 8);
                    std::cout << "[PPU] DMA Background Set: 0x" << std::hex << color << std::endl;
                }
                cgram_address++;
                cgram_flip_flop = false;
            }
        }
        // $2118 is VRAM Data - SMW uses this for the logo tiles
        else if (dest_reg == 0x18) {
             // Future: snes_vram[vram_addr] = data;
        }
        
        // Handle address increment (standard DMA mode)
        if (!(dma_parameters[ch] & 0x08)) src++;
    }
}

void snes_memory_write(uint32_t addr, uint8_t val) {
    uint8_t bank = (addr >> 16) & 0xFF;
    uint32_t offset = addr & 0xFFFF;

    // 1. Intercept DMA Channel Setup ($4300 - $437F)
    if ((addr & 0xFF80) == 0x4300) {
        int ch = (addr >> 4) & 0x7; // Get channel 0-7
        int reg = addr & 0xF;       // Get register 0-F
        
        switch(reg) {
            case 0x0: dma_parameters[ch] = val; break;
            case 0x1: dma_dest_reg[ch] = val; break;
            case 0x2: dma_src_addr[ch] = (dma_src_addr[ch] & 0xFFFF00) | val; break;
            case 0x3: dma_src_addr[ch] = (dma_src_addr[ch] & 0xFF00FF) | (val << 8); break;
            case 0x4: dma_src_addr[ch] = (dma_src_addr[ch] & 0x00FFFF) | (val << 16); break;
            case 0x5: dma_transfer_size[ch] = (dma_transfer_size[ch] & 0xFF00) | val; break;
            case 0x6: dma_transfer_size[ch] = (dma_transfer_size[ch] & 0x00FF) | (val << 8); break;
        }
    }

    // 2. Intercept DMA Trigger ($420B)
    if (addr == 0x420b) {
        // If val is 0, let's see what is actually in the Accumulator
        if (val == 0) {
            std::cout << "[Debug] DMA Triggered with 0. Reg A is: 0x" << std::hex << regs.A << std::endl;
            // Force a channel 0 trigger if the game seems to be stuck
            val = (regs.A & 0xFF); 
        }
        
        for (int i = 0; i < 8; i++) {
            if (val & (1 << i)) execute_dma(i);
        }
    }

    // ... your existing WRAM/RAM write logic ...
    if (bank == 0x7E || bank == 0x7F) {
        uint32_t ram_addr = ((bank & 0x01) << 16) | offset;
        if (ram_addr < snes_ram.size()) snes_ram[ram_addr] = val;
    } 
    else if (bank < 0x40 && offset < 0x2000) {
        snes_ram[offset] = val;
    }
}

void sub_0xffffffff() {
    // This is a landing pad for broken jumps
    std::cerr << "[CPU] Error: Jumped to an invalid address (0xFFFFFFFF)" << std::endl;
}

// --- System & Control ---
void SEI() { regs.P.I = 1; }
void CLI() { regs.P.I = 0; }
void CLC() { regs.P.C = 0; }
void SEC() { regs.P.C = 1; }
void XCE() { bool temp = regs.P.C; regs.P.C = regs.P.E; regs.P.E = temp; }
void REP(uint8_t val) { if (val & 0x20) regs.P.M = 0; if (val & 0x10) regs.P.X_flag = 0; }
void SEP(uint8_t val) { if (val & 0x20) regs.P.M = 1; if (val & 0x10) regs.P.X_flag = 1; }
void XBA() { uint8_t low = regs.A & 0xFF; uint8_t high = (regs.A >> 8) & 0xFF; regs.A = (low << 8) | high; }
void NOP() {}

// --- Load & Store ---
void LDA_imm(uint16_t val) { 
    regs.A = val; 
    update_nz(regs.A, regs.P.M); 
    // std::cout << "[CPU] LDA Imm: 0x" << std::hex << val << std::endl; 
}
void LDA_abs(unsigned short addr) {
    regs.A = snes_memory_read(addr);
    if (!regs.P.M) {
        regs.A |= (snes_memory_read(addr + 1) << 8);
    }
    update_nz(regs.A, regs.P.M);
}
void LDA_dp(unsigned char offset) {
    // If the game is polling $10 (V-blank wait), slow it down 
    // so the UI thread has time to update snes_ram[0x10]
    if (offset == 0x10) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    regs.A = snes_memory_read(regs.D + offset);
    update_nz(regs.A, regs.P.M);
}
void LDA_abs_x(unsigned short addr) { regs.A = snes_memory_read(addr + regs.X); update_nz(regs.A, regs.P.M); }
void LDX_imm(uint16_t val) { regs.X = val; update_nz(regs.X, regs.P.X_flag); }
void LDX_abs(unsigned short addr) { regs.X = snes_memory_read(addr); update_nz(regs.X, regs.P.X_flag); }
void LDY_imm(uint16_t val) { regs.Y = val; update_nz(regs.Y, regs.P.X_flag); }
void LDY_dp(unsigned char offset) { regs.Y = snes_memory_read(regs.D + offset); update_nz(regs.Y, regs.P.X_flag); }

void STA_abs(unsigned short addr) {
    uint8_t val = regs.A & 0xFF;
    snes_memory_write(addr, val);

    // Log EVERY write to the PPU registers (0x2100 range)
    if (addr >= 0x2100 && addr <= 0x2133) {
        std::cout << "[Debug] PPU Write to 0x" << std::hex << addr << " Value: 0x" << (int)(regs.A & 0xFF) << std::endl;
    }

    if (addr == 0x2100) {
        if (val & 0x80) {
            // Screen is forced to black
        } else {
            std::cout << "[PPU] Screen Enabled! Brightness: " << (val & 0x0F) << std::endl;
        }
    }

    // 2. Hardware Register Interception
    // $2121: CGRAM Address (Palette Index)
    if (addr == 0x2121) {
        cgram_address = (regs.A & 0xFF);
        cgram_flip_flop = false; // Reset flip-flop on address change
    }
    // $2122: CGRAM Data (Palette Color)
    else if (addr == 0x2122) {
        uint8_t val = (regs.A & 0xFF);
        // ... (your existing flip-flop logic) ...
        if (cgram_address == 0 && cgram_flip_flop == false) {
             uint16_t color = palette_data[0] | (palette_data[1] << 8);
             // THIS LOG IS CRITICAL. If you see this, the screen MUST change color.
             std::cout << "[PPU] Background Color updated to: 0x" << std::hex << color << std::endl;
        }
    }
}
void STA_dp(unsigned char offset) { snes_memory_write(regs.D + offset, regs.A & 0xFF); }
void STA_abs_x(unsigned short addr) { snes_memory_write(addr + regs.X, regs.A & 0xFF); }
void STX_abs(unsigned short addr) { snes_memory_write(addr, regs.X & 0xFF); }
void STX_dp(unsigned char offset) { snes_memory_write(regs.D + offset, regs.X & 0xFF); }
void STY_dp(unsigned char offset) { snes_memory_write(regs.D + offset, regs.Y & 0xFF); }

void STZ_abs(unsigned short addr) { snes_memory_write(addr, 0); snes_memory_write(addr + 1, 0); }
void STZ_dp(unsigned char offset) { snes_memory_write(regs.D + offset, 0); }
void STZ_abs_x(unsigned short addr) { snes_memory_write(addr + regs.X, 0); }
void STZ_dp_x(unsigned char offset) { snes_memory_write(regs.D + offset + regs.X, 0); }

// --- Transfers & Stack (FIXED) ---
void TAX() { regs.X = regs.A; update_nz(regs.X, regs.P.X_flag); }
void TAY() { regs.Y = regs.A; update_nz(regs.Y, regs.P.X_flag); }
void TXA() { regs.A = regs.X; update_nz(regs.A, regs.P.M); }
void TYA() { regs.A = regs.Y; update_nz(regs.A, regs.P.M); }
void TSX() { regs.X = regs.S; update_nz(regs.X, regs.P.X_flag); }
void TXS() { regs.S = regs.X; }
void TXY() { regs.Y = regs.X; update_nz(regs.Y, regs.P.X_flag); }
void TCD() { regs.D = regs.A; }
void TCS() { regs.S = regs.A; }

// Use snes_memory_write to ensure the stack stays in WRAM Bank 0
void PHA() { snes_memory_write(regs.S--, regs.A & 0xFF); if(!regs.P.M) snes_memory_write(regs.S--, (regs.A >> 8) & 0xFF); }
void PLA() { if(!regs.P.M) regs.A = (snes_memory_read(++regs.S) << 8); regs.A |= snes_memory_read(++regs.S); update_nz(regs.A, regs.P.M); }
void PHX() { snes_memory_write(regs.S--, regs.X & 0xFF); if(!regs.P.X_flag) snes_memory_write(regs.S--, (regs.X >> 8) & 0xFF); }
void PLX() { if(!regs.P.X_flag) regs.X = (snes_memory_read(++regs.S) << 8); regs.X |= snes_memory_read(++regs.S); update_nz(regs.X, regs.P.X_flag); }
void PHY() { snes_memory_write(regs.S--, regs.Y & 0xFF); if(!regs.P.X_flag) snes_memory_write(regs.S--, (regs.Y >> 8) & 0xFF); }
void PLY() { if(!regs.P.X_flag) regs.Y = (snes_memory_read(++regs.S) << 8); regs.Y |= snes_memory_read(++regs.S); update_nz(regs.Y, regs.P.X_flag); }
void PHP() { 
    snes_memory_write(regs.S--, pack_status()); 
}

void PLP() { 
    unpack_status(snes_memory_read(++regs.S)); 
}

void PEA(unsigned short addr) { 
    snes_memory_write(regs.S--, (addr >> 8) & 0xFF); 
    snes_memory_write(regs.S--, addr & 0xFF); 
}

// --- Arithmetic & Logical ---
void INX() { regs.X++; update_nz(regs.X, regs.P.X_flag); }
void INY() { regs.Y++; update_nz(regs.Y, regs.P.X_flag); }
void DEX() { regs.X--; update_nz(regs.X, regs.P.X_flag); }
void DEY() { regs.Y--; update_nz(regs.Y, regs.P.X_flag); }
void INC_acc() { regs.A++; update_nz(regs.A, regs.P.M); }
void INC_dp(unsigned char offset) { /* Implement */ }

void AND_imm(uint16_t val) { regs.A &= val; update_nz(regs.A, regs.P.M); }
void ORA_imm(uint16_t val) { regs.A |= val; update_nz(regs.A, regs.P.M); }
void ADC_imm(uint16_t val) { /* Implement */ }
void SBC_imm(uint16_t val) { /* Implement */ }

void CMP_abs(unsigned short addr) { /* Implement */ }
void CMP_ind_y(unsigned char offset) { /* Implement */ }
void CPX_imm(uint16_t val) { /* Implement */ }

// --- Long & Indirect ---
void LDA_long(unsigned int addr) { regs.A = snes_memory_read(addr); update_nz(regs.A, regs.P.M); }
void STA_long(unsigned int addr) { snes_memory_write(addr, regs.A & 0xFF); }
void STA_long_x(unsigned int addr) { snes_memory_write(addr + regs.X, regs.A & 0xFF); }
void LDA_ind_x(unsigned char offset) {}
void LDA_ind_long_y(unsigned char offset) {}
void STA_ind_y(unsigned char offset) {}
void STA_ind_long(unsigned char offset) {}
void STA_ind_long_y(unsigned char offset) {}
void STX_dp_y(unsigned char offset) {}
void STY_dp_x(unsigned char offset) {}
void LDX_abs_y(unsigned short addr) {}
void ADC_dp(unsigned char offset) {}
void ADC_long(unsigned int addr) {}
void SBC_long_x(unsigned int addr) {}
void AND_ind_long_y(unsigned char offset) {}
void ORA_dp(unsigned char offset) {}
void ORA_dp_x(unsigned char offset) {}
void ORA_long(unsigned int addr) {}
void ORA_stack(unsigned char offset) {}
void ORA_ind_x(unsigned char offset) {}
void ORA_ind_long(unsigned char offset) {}
void EOR_sr_ind_y(unsigned char offset) {}

void ASL_acc() { regs.A <<= 1; update_nz(regs.A, regs.P.M); }
void LSR_acc() { regs.A >>= 1; update_nz(regs.A, regs.P.M); }
void ROL_dp(unsigned char offset) {}
void JML_ind(unsigned short addr) {}

void register_functions() {
    std::cout << "[System] Initializing Function Registry..." << std::endl;
#if __has_include("function_registry.inc")
    #include "function_registry.inc"
    std::cout << "[System] " << function_table.size() << " functions registered." << std::endl;
#else
    std::cout << "[System] Warning: function_registry.inc not found. Run the RecompTool first!" << std::endl;
#endif
}