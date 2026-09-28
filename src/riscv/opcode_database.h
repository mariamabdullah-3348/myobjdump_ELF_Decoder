#ifndef RISCV_OPCODE_DATABASE_H
#define RISCV_OPCODE_DATABASE_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct BitPiece {
    int instructionMsb;
    int instructionLsb;
    int operandLsb;
};

struct OperandDef {
    std::string name;
    std::vector<BitPiece> pieces;
};

struct InstructionDef {
    std::string mnemonic;
    uint32_t match = 0;
    uint32_t mask = 0;
    std::string encoding;
    std::string extension;
    std::vector<std::string> variableFields;
    std::vector<OperandDef> operands;
};

class OpcodeDatabase {
public:
    bool load(
        const std::string& decoderDatabasePath
    );

    const std::vector<InstructionDef>& instructions() const;

private:
    std::vector<InstructionDef> instructionList;
};

#endif
