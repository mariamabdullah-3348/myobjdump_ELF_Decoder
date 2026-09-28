#ifndef RISCV_INSTRUCTION_H
#define RISCV_INSTRUCTION_H

#include <cstdint>

enum class InstructionLength {
    Compressed16 = 2,
    Standard32 = 4
};

struct RiscVInstruction {
    uint64_t address = 0;
    uint32_t raw = 0;
    InstructionLength length = InstructionLength::Standard32;
};

#endif
