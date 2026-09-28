#ifndef RISCV_INSTRUCTION_FETCHER_H
#define RISCV_INSTRUCTION_FETCHER_H

#include "instruction.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class InstructionFetcher {
public:
    InstructionFetcher(
        uint64_t textAddress,
        const std::vector<uint8_t>& text
    );

    bool hasNext() const;
    RiscVInstruction fetch();

private:
    uint64_t textAddress;
    const std::vector<uint8_t>& text;
    std::size_t offset = 0;

    uint16_t read16(std::size_t position) const;
    uint32_t read32(std::size_t position) const;

    InstructionLength determineLength(uint16_t firstHalfword) const;
};

#endif
