#include "riscv/decoder.h"

RiscVDecoder::RiscVDecoder(
    const OpcodeDatabase& database
)
    : database(database) {
}

uint32_t RiscVDecoder::extractBits(
    uint32_t value,
    int msb,
    int lsb
) {
    const int width = msb - lsb + 1;

    if (width <= 0) {
        return 0;
    }

    if (width == 32) {
        return value;
    }

    const uint32_t mask = (1u << width) - 1u;
    return (value >> lsb) & mask;
}

uint64_t RiscVDecoder::assembleOperand(
    uint32_t instruction,
    const OperandDef& operand
) {
    uint64_t result = 0;

    for (const BitPiece& piece : operand.pieces) {
        const uint32_t bits = extractBits(
            instruction,
            piece.instructionMsb,
            piece.instructionLsb
        );

        result |= static_cast<uint64_t>(bits)
                  << piece.operandLsb;
    }

    return result;
}

DecodedInstruction RiscVDecoder::decode(
    const RiscVInstruction& instruction
) const {
    DecodedInstruction result;
    result.address = instruction.address;
    result.raw = instruction.raw;
    result.length = instruction.length;

    const uint32_t value =
        instruction.raw &
        (instruction.length == InstructionLength::Compressed16
             ? 0xFFFFu
             : 0xFFFFFFFFu);

    for (const InstructionDef& def : database.instructions()) {
        if ((value & def.mask) != def.match) {
            continue;
        }

        result.valid = true;
        result.mnemonic = def.mnemonic;

        for (const OperandDef& operand : def.operands) {
            result.operands.push_back({
                operand.name,
                assembleOperand(value, operand)
            });
        }

        return result;
    }

    return result;
}
