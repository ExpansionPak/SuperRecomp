#include <iostream>
#include "snes_cpu.h"

// This is the first function the tool generated
extern void sub_0x8000(); 

int main() {
    std::cout << "Starting Recompiled SNES Core..." << std::endl;
    
    // Initialize CPU state
    regs.SP = 0x01FF;
    regs.P.M = 1; // Start in 8-bit mode (Standard for SNES boot)
    regs.P.X_flag = 1;

    // Start execution
    sub_0x8000();

    std::cout << "Execution Finished." << std::endl;
    return 0;
}