#include "riscv/instruction_fetcher.h"

#include <stdexcept>

InstructionFetcher::InstructionFetcher(
    uint64_t textAddress,
    const std::vector<uint8_t>& text
)
    : textAddress(textAddress), text(text) {
}

bool InstructionFetcher::hasNext() const {
    return offset < text.size();
}

uint16_t InstructionFetcher::read16(std::size_t position) const {
    if (position + 1 >= text.size()) {
        throw std::runtime_error("Incomplete 16-bit instruction");
    }

    return static_cast<uint16_t>(
        text[position] |
        (static_cast<uint16_t>(text[position + 1]) << 8)
    );
}

uint32_t InstructionFetcher::read32(std::size_t position) const {
    if (position + 3 >= text.size()) {
        throw std::runtime_error("Incomplete 32-bit instruction");
    }

    return static_cast<uint32_t>(
        text[position] |
        (static_cast<uint32_t>(text[position + 1]) << 8) |
        (static_cast<uint32_t>(text[position + 2]) << 16) |
        (static_cast<uint32_t>(text[position + 3]) << 24)
    );
}

InstructionLength InstructionFetcher::determineLength(
    uint16_t firstHalfword
) const {
    // Project scope: support 16-bit and 32-bit instructions.
    // RISC-V uses the low two bits to distinguish these cases.
    return ((firstHalfword & 0x3u) == 0x3u)
        ? InstructionLength::Standard32
        : InstructionLength::Compressed16;
}

RiscVInstruction InstructionFetcher::fetch() {
    if (!hasNext()) {
        throw std::runtime_error("No instruction available");
    }

    const uint16_t firstHalfword = read16(offset);
    const InstructionLength length =
        determineLength(firstHalfword);

    RiscVInstruction result;
    result.address = textAddress + offset;
    result.length = length;

    if (length == InstructionLength::Compressed16) {
        result.raw = firstHalfword;
        offset += 2;
    } else {
        result.raw = read32(offset);
        offset += 4;
    }

    return result;
}
