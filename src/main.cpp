#include "elf_reader.h"
#include "riscv/decoder.h"
#include "riscv/instruction_fetcher.h"
#include "riscv/instruction_formatter.h"
#include "riscv/opcode_database.h"

#include <array>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

struct KnownRegisterValue {
    bool valid = false;
    uint64_t value = 0;
};

using IF = InstructionFormatter;

std::optional<uint64_t> addressComputedByInstruction(
    const DecodedInstruction& instruction,
    const std::array<KnownRegisterValue, 32>& registers
) {
    const std::string& m = instruction.mnemonic;

    // A LUI creates an absolute sign-extended 32-bit value.
    if (m == "lui" && IF::has(instruction, "rd") && IF::has(instruction, "imm")) {
        const uint64_t imm = IF::get(instruction, "imm");
        const uint64_t value32 = (imm & 0xfffffULL) << 12;
        return static_cast<uint64_t>(IF::signExtend(value32, 32));
    }

    // AUIPC creates PC + sign-extended U-immediate.
    if (m == "auipc" && IF::has(instruction, "rd") && IF::has(instruction, "imm")) {
        const uint64_t imm = IF::get(instruction, "imm");
        const int64_t upper = IF::signExtend((imm & 0xfffffULL) << 12, 32);
        return static_cast<uint64_t>(static_cast<int64_t>(instruction.address) + upper);
    }

    if (m == "c.lui" && IF::has(instruction, "rd") && IF::has(instruction, "imm")) {
        return static_cast<uint64_t>(IF::signExtend(IF::get(instruction, "imm"), 18));
    }

    // ADDI/ADDIW can finish an address formed by LUI/AUIPC.
    if ((m == "addi" || m == "addiw" || m == "c.addi" || m == "c.addiw") &&
        IF::has(instruction, "rd") && IF::has(instruction, "imm")) {
        
        bool has_rs1 = true;
        uint64_t rs1 = 0;
        if (m == "c.addi" || m == "c.addiw") {
            rs1 = IF::get(instruction, "rd"); // For these, rs1 == rd
        } else if (IF::has(instruction, "rs1")) {
            rs1 = IF::get(instruction, "rs1");
        } else {
            has_rs1 = false;
        }

        if (has_rs1) {
            const unsigned bits = (m == "c.addi" || m == "c.addiw") ? 6 : 12;
            const int64_t imm = IF::signExtend(IF::get(instruction, "imm"), bits);
            if (rs1 > 0 && rs1 < registers.size() && registers[rs1].valid) {
                const uint64_t value = registers[rs1].value + static_cast<uint64_t>(imm);
                if (m == "addiw" || m == "c.addiw") {
                    return static_cast<uint64_t>(IF::signExtend(value & 0xffffffffULL, 32));
                }
                return value;
            }
        }
    }

    // For loads/stores, the effective address is rs1 + immediate.
    if ((m == "ld" || m == "lw" || m == "lwu" || m == "lh" || m == "lhu" ||
         m == "lb" || m == "lbu" || m == "sd" || m == "sw" || m == "sh" || m == "sb" ||
         m == "fld" || m == "fsw" || m == "fsd" || m == "flw") &&
        IF::has(instruction, "rs1") && IF::has(instruction, "imm")) {
        const uint64_t rs1 = IF::get(instruction, "rs1");
        const int64_t imm = IF::signExtend(IF::get(instruction, "imm"), 12);
        if (rs1 == 0) {
            return static_cast<uint64_t>(imm);
        }
        if (rs1 < registers.size() && registers[rs1].valid) {
            return registers[rs1].value + static_cast<uint64_t>(imm);
        }
    }

    return std::nullopt;
}

void updateRegisterKnowledge(
    const DecodedInstruction& instruction,
    const std::array<KnownRegisterValue, 32>& before,
    std::array<KnownRegisterValue, 32>& after
) {
    after = before;

    if (!IF::has(instruction, "rd")) {
        return;
    }

    const uint64_t rd = IF::get(instruction, "rd");
    if (rd >= after.size()) {
        return;
    }

    // x0 is always zero and cannot be overwritten.
    if (rd == 0) {
        after[0] = {true, 0};
        return;
    }

    const std::string& m = instruction.mnemonic;
    if (m == "lui" || m == "auipc" || m == "addi" || m == "addiw" ||
        m == "c.addi" || m == "c.addiw" || m == "c.lui") {
        const auto value = addressComputedByInstruction(instruction, before);
        if (value.has_value()) {
            after[rd] = {true, *value};
            return;
        }
    }

    // Any other instruction that writes rd makes our simple address tracking
    // unknown. This prevents stale values from generating false comments.
    after[rd] = {false, 0};
}

std::string formatDisassemblyLine(
    const DecodedInstruction& decoded,
    const std::string& text,
    const std::string& comment
) {
    std::ostringstream out;
    out << "    " << std::hex << std::nouppercase
        << std::setw(8) << std::setfill('0') << decoded.address
        << ":   "
        << std::setw(decoded.length == InstructionLength::Compressed16 ? 4 : 8)
        << decoded.raw
        << std::string(decoded.length == InstructionLength::Compressed16 ? 20 : 16, ' ')
        << std::left << std::setfill(' ') << std::setw(0)
        << text;

    if (!comment.empty()) {
        out << " " << comment;
    }
    return out.str();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: myobjdump <elf-file> [decoder-database.json] [pseudo-database.json]\n";
        return 1;
    }

    const std::string elfPath = argv[1];
    const std::string databasePath =
        (argc >= 3) ? argv[2] : "data/riscv_decoder.json";
    const std::string pseudoPath =
        (argc >= 4) ? argv[3] : "data/riscv_pseudos.json";

    TextSection text;
    ElfReader reader;
    if (!reader.load(elfPath, text)) {
        std::cerr << "Failed to load ELF file: " << elfPath << "\n";
        return 1;
    }

    OpcodeDatabase database;
    if (!database.load(databasePath)) {
        std::cerr << "Failed to load decoder database: " << databasePath << "\n";
        return 1;
    }

    PseudoDatabase pseudoDatabase;
    if (!pseudoDatabase.load(pseudoPath)) {
        std::cerr << "Failed to load pseudo database: " << pseudoPath << "\n";
        return 1;
    }

    InstructionFetcher fetcher(text.address, text.bytes);
    RiscVDecoder decoder(database);
    InstructionFormatter formatter(pseudoDatabase, reader.symbols());

    std::array<KnownRegisterValue, 32> knownRegisters{};
    knownRegisters[0] = {true, 0};

    const std::string fileName = std::filesystem::path(elfPath).filename().string();
    std::cout << fileName << ":     file format "
              << (reader.is32Bit() ? "elf32-littleriscv" : "elf64-littleriscv") << "\n\n";
    std::cout << "Disassembly of section .text:\n\n";

    while (fetcher.hasNext()) {
        const RiscVInstruction instruction = fetcher.fetch();
        const DecodedInstruction decoded = decoder.decode(instruction);

        if (const ElfSymbol* symbol = reader.findCodeSymbol(instruction.address)) {
            std::cout << std::hex << std::setw(reader.is32Bit() ? 8 : 16) << std::setfill('0')
                      << instruction.address << std::dec
                      << " <" << symbol->name << ">:\n";
        }

        std::string comment;
        if (const auto address = addressComputedByInstruction(decoded, knownRegisters)) {
            // Comments are useful for address-producing and memory-reference
            // instructions, but not for every arithmetic instruction.
            const bool isAddressInstruction =
                decoded.mnemonic == "addi" || decoded.mnemonic == "addiw" ||
                decoded.mnemonic == "c.addi" || decoded.mnemonic == "c.addiw";
            if (isAddressInstruction) {
                comment = formatter.formatAddressComment(*address);
            }
        }

        const std::string formatted = formatter.format(decoded);
        std::cout << formatDisassemblyLine(decoded, formatted, comment) << "\n";

        std::array<KnownRegisterValue, 32> nextRegisters{};
        updateRegisterKnowledge(decoded, knownRegisters, nextRegisters);
        knownRegisters = nextRegisters;
    }

    return 0;
}
