#ifndef RISCV_DECODER_H
#define RISCV_DECODER_H

#include "instruction.h"
#include "opcode_database.h"

#include <cstdint>
#include <string>
#include <vector>

struct DecodedOperand {
    std::string name;
    uint64_t value = 0;
};

struct DecodedInstruction {
    bool valid = false;
    std::string mnemonic;
    uint64_t address = 0;
    uint32_t raw = 0;
    InstructionLength length = InstructionLength::Standard32;
    std::vector<DecodedOperand> operands;
};

class RiscVDecoder {
public:
    explicit RiscVDecoder(const OpcodeDatabase& database);

    DecodedInstruction decode(
        const RiscVInstruction& instruction
    ) const;

private:
    const OpcodeDatabase& database;

    static uint32_t extractBits(
        uint32_t value,
        int msb,
        int lsb
    );

    static uint64_t assembleOperand(
        uint32_t instruction,
        const OperandDef& operand
    );
};

#endif
