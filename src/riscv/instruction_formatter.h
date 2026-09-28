#ifndef RISCV_INSTRUCTION_FORMATTER_H
#define RISCV_INSTRUCTION_FORMATTER_H

#include "decoder.h"
#include "../elf_reader.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct PseudoRule {
    std::string name;
    std::string base;
    std::unordered_map<std::string, uint64_t> equals;
    std::vector<std::string> operands;
};

class PseudoDatabase {
public:
    bool load(const std::string& path);
    const std::vector<PseudoRule>& rules() const { return ruleList; }

private:
    std::vector<PseudoRule> ruleList;
};

class InstructionFormatter {
public:
    explicit InstructionFormatter(const PseudoDatabase& pseudoDatabase,
                                  const std::vector<ElfSymbol>& symbols);

    std::string format(const DecodedInstruction& instruction) const;

    // Formats the optional GNU-objdump-style address comment for an address
    // computed from an instruction sequence, e.g.:
    //   # 800001f8 <dbg_mtime_start>
    // or:
    //   # 2004000 <start-0x7dffc000>
    std::string formatAddressComment(uint64_t address) const;

    static uint64_t get(const DecodedInstruction& instruction, const std::string& name);
    static bool has(const DecodedInstruction& instruction, const std::string& name);
    static int64_t signExtend(uint64_t value, unsigned bits);
    static const std::string& resolveAlias(const std::string& name);

private:
    const PseudoDatabase& pseudoDatabase;
    const std::vector<ElfSymbol>& symbols;

    static const DecodedOperand* findOperand(
        const DecodedInstruction& instruction,
        const std::string& name
    );

    static bool matches(
        const DecodedInstruction& instruction,
        const PseudoRule& rule
    );

    static std::string registerName(uint64_t index);
    static std::string csrName(uint64_t csr);
    static std::string formatImmediate(int64_t value);
    static std::string formatUnsignedHex(uint64_t value);

    const ElfSymbol* findSymbol(uint64_t address) const;
    const ElfSymbol* findNearestSymbol(uint64_t address) const;
    static bool isMappingSymbol(const ElfSymbol& symbol);
    static std::string formatTargetAddress(uint64_t address,
                                           const ElfSymbol* symbol);

    std::string formatBaseInstruction(
        const DecodedInstruction& instruction
    ) const;

    std::string formatWithRule(
        const DecodedInstruction& instruction,
        const PseudoRule& rule
    ) const;

    std::string formatOperand(
        const DecodedInstruction& instruction,
        const std::string& operand,
        bool targetAllowed
    ) const;
    static uint64_t branchOrJumpTarget(
        const DecodedInstruction& instruction
    );
};

#endif
