#include "riscv/instruction_formatter.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

bool PseudoDatabase::load(const std::string& path) {
    ruleList.clear();

    std::ifstream file(path);
    if (!file) {
        return false;
    }

    try {
        json root;
        file >> root;

        if (!root.contains("aliases") || !root["aliases"].is_array()) {
            return false;
        }

        for (const auto& item : root["aliases"]) {
            PseudoRule rule;
            rule.name = item.at("name").get<std::string>();
            rule.base = item.at("base").get<std::string>();

            if (item.contains("when")) {
                for (auto it = item["when"].begin(); it != item["when"].end(); ++it) {
                    if (!it.value().is_number_unsigned() && !it.value().is_number_integer()) {
                        return false;
                    }
                    rule.equals[it.key()] = it.value().get<uint64_t>();
                }
            }

            if (item.contains("operands")) {
                rule.operands = item["operands"].get<std::vector<std::string>>();
            }

            ruleList.push_back(std::move(rule));
        }
    } catch (...) {
        ruleList.clear();
        return false;
    }

    return true;
}

InstructionFormatter::InstructionFormatter(
    const PseudoDatabase& pseudoDatabase,
    const std::vector<ElfSymbol>& symbols
)
    : pseudoDatabase(pseudoDatabase),
      symbols(symbols) {
}

// Map from real ISA field names used in the JSON to the logical names
// that the formatter uses internally ("imm", "rd", "rs1", "rs2").
const std::string& InstructionFormatter::resolveAlias(const std::string& name) {
    // Returns the same string if it's already a logical name, or the
    // canonical alias otherwise.
    static const std::unordered_map<std::string, std::string> ALIASES = {
        // Immediate fields -> "imm"
        {"imm12",    "imm"}, {"imm20",    "imm"}, {"jimm20",  "imm"},
        {"bimm12",  "imm"}, {"imm12s",  "imm"},
        {"shamtd",  "imm"}, {"shamtw",  "imm"}, {"shamtq",  "imm"},
        {"shamtw4", "imm"}, {"shamt",   "imm"},
        // c.* immediate pieces -> "imm"
        {"c_imm6hi",      "imm"}, {"c_imm6lo",      "imm"},
        {"c_nzimm6hi",    "imm"}, {"c_nzimm6lo",    "imm"},
        {"c_imm12",       "imm"},
        {"c_bimm9hi",     "imm"}, {"c_bimm9lo",     "imm"},
        {"c_nzuimm10",    "imm"},
        {"c_nzimm10hi",   "imm"}, {"c_nzimm10lo",   "imm"},
        {"c_nzimm18hi",   "imm"}, {"c_nzimm18lo",   "imm"},
        {"c_uimm7hi",     "imm"}, {"c_uimm7lo",     "imm"},
        {"c_uimm8hi",     "imm"}, {"c_uimm8lo",     "imm"},
        {"c_uimm9hi",     "imm"}, {"c_uimm9lo",     "imm"},
        {"c_nzuimm5",     "imm"}, {"c_nzuimm6hi",   "imm"}, {"c_nzuimm6lo", "imm"},
        {"c_uimm8sphi",   "imm"}, {"c_uimm8splo",   "imm"}, {"c_uimm8sp_s", "imm"},
        {"c_uimm9sphi",   "imm"}, {"c_uimm9splo",   "imm"}, {"c_uimm9sp_s", "imm"},
        {"c_uimm10sphi",  "imm"}, {"c_uimm10splo",  "imm"}, {"c_uimm10sp_s","imm"},
        {"c_uimm1",       "imm"}, {"c_uimm2",       "imm"},
        // merged compressed split immediates
        {"c_nzimm6",  "imm"}, {"c_imm6",    "imm"}, {"c_nzuimm6", "imm"},
        {"c_bimm9",   "imm"}, {"c_imm12_j", "imm"},
        {"c_nzimm10", "imm"}, {"c_nzimm18", "imm"},
        {"c_uimm8sp_s","imm"},{"c_uimm9sp_s","imm"},
        // rd aliases
        {"rd_n0",     "rd"}, {"rd_n2",     "rd"}, {"rd_p",   "rd"},
        {"rd_rs1",    "rd"}, {"rd_rs1_n0", "rd"}, {"rd_rs1_p","rd"},
        // rs1 aliases
        {"rs1_p",  "rs1"}, {"rs1_n0", "rs1"}, {"c_rs1_n0", "rs1"},
        {"c_sreg1", "rs1"},
        // rs2 aliases
        {"rs2_p",  "rs2"}, {"c_rs2",  "rs2"}, {"c_rs2_n0", "rs2"},
        {"c_sreg2", "rs2"},
    };
    const auto it = ALIASES.find(name);
    return it != ALIASES.end() ? it->second : name;
}

const DecodedOperand* InstructionFormatter::findOperand(
    const DecodedInstruction& instruction,
    const std::string& name
) {
    // Direct match first
    for (const auto& operand : instruction.operands) {
        if (operand.name == name) {
            return &operand;
        }
    }
    // Alias match: look for any operand whose real name resolves to 'name'
    for (const auto& operand : instruction.operands) {
        if (resolveAlias(operand.name) == name) {
            return &operand;
        }
    }
    return nullptr;
}

uint64_t InstructionFormatter::get(
    const DecodedInstruction& instruction,
    const std::string& name
) {
    const auto* operand = findOperand(instruction, name);
    if (!operand) return 0;
    uint64_t val = operand->value;
    if (operand->name == "rd_p" || operand->name == "rs1_p" || operand->name == "rs2_p" ||
        operand->name == "rd_rs1_p" ||
        operand->name == "c_sreg1" || operand->name == "c_sreg2") {
        val += 8;
    }
    return val;
}

bool InstructionFormatter::has(
    const DecodedInstruction& instruction,
    const std::string& name
) {
    return findOperand(instruction, name) != nullptr;
}

bool InstructionFormatter::matches(
    const DecodedInstruction& instruction,
    const PseudoRule& rule
) {
    if (instruction.mnemonic != rule.base) {
        return false;
    }

    for (const auto& [name, expected] : rule.equals) {
        const auto* operand = findOperand(instruction, name);
        if (!operand || operand->value != expected) {
            return false;
        }
    }

    return true;
}

std::string InstructionFormatter::registerName(uint64_t index) {
    static const char* names[] = {
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
        "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
        "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
    };

    if (index < 32) {
        return names[index];
    }

    return "x" + std::to_string(index);
}

std::string InstructionFormatter::csrName(uint64_t csr) {
    static const std::unordered_map<uint64_t, std::string> names = {
        {0x100, "sstatus"}, {0x14d, "stimecmp"}, {0x104, "sie"},     {0x105, "stvec"},
        {0x106, "scounteren"}, {0x140, "sscratch"}, {0x141, "sepc"},
        {0x142, "scause"}, {0x143, "stval"}, {0x144, "sip"},
        {0x180, "satp"},
        {0x300, "mstatus"}, {0x301, "misa"}, {0x302, "medeleg"},
        {0x303, "mideleg"}, {0x304, "mie"}, {0x305, "mtvec"},
        {0x306, "mcounteren"}, {0x340, "mscratch"}, {0x341, "mepc"},
        {0x342, "mcause"}, {0x343, "mtval"}, {0x344, "mip"},
        {0xB00, "mcycle"}, {0xB02, "minstret"},
        {0xB80, "mcycleh"}, {0xB82, "minstreth"}
    };

    const auto it = names.find(csr);
    if (it != names.end()) {
        return it->second;
    }

    std::ostringstream out;
    out << "0x" << std::hex << csr;
    return out.str();
}

int64_t InstructionFormatter::signExtend(uint64_t value, unsigned bits) {
    if (bits == 0 || bits >= 64) {
        return static_cast<int64_t>(value);
    }

    const uint64_t mask = 1ULL << (bits - 1);
    const uint64_t full = (1ULL << bits) - 1ULL;
    value &= full;

    if (value & mask) {
        value |= ~full;
    }

    return static_cast<int64_t>(value);
}

std::string InstructionFormatter::formatImmediate(int64_t value) {
    return std::to_string(value);
}

std::string InstructionFormatter::formatUnsignedHex(uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << value;
    return out.str();
}

bool InstructionFormatter::isMappingSymbol(const ElfSymbol& symbol) {
    return !symbol.name.empty() && symbol.name[0] == '$';
}

const ElfSymbol* InstructionFormatter::findSymbol(uint64_t address) const {
    const ElfSymbol* fallback = nullptr;
    for (const auto& symbol : symbols) {
        if (symbol.address < address) {
            continue;
        }
        if (symbol.address > address) {
            break;
        }
        if (isMappingSymbol(symbol)) {
            continue;
        }
        if (symbol.type == 2) {
            return &symbol;
        }
        if (fallback == nullptr) {
            fallback = &symbol;
        }
    }
    return fallback;
}

const ElfSymbol* InstructionFormatter::findNearestSymbol(uint64_t address) const {
    const ElfSymbol* nearest = nullptr;
    for (const auto& symbol : symbols) {
        if (symbol.address > address) {
            break;
        }
        if (isMappingSymbol(symbol) || symbol.name.empty() || symbol.address == 0) {
            continue;
        }
        if (nearest == nullptr ||
            (symbol.type == 2 && nearest->type != 2) ||
            (symbol.address == nearest->address && symbol.type == 2)) {
            nearest = &symbol;
        } else if (symbol.address > nearest->address) {
            nearest = &symbol;
        }
    }
    return nearest;
}

std::string InstructionFormatter::formatAddressComment(uint64_t address) const {
    const ElfSymbol* symbol = findNearestSymbol(address);
    if (symbol == nullptr) {
        return {};
    }

    std::ostringstream out;

    if (address != symbol->address) {
        const uint64_t delta = address > symbol->address
            ? address - symbol->address
            : symbol->address - address;
        
        out << "# " << std::hex << std::nouppercase << address << " <"
            << symbol->name << (address > symbol->address ? "+0x" : "-0x")
            << std::hex << delta << ">";
    } else {
        out << "# " << std::hex << std::nouppercase << address << " <"
            << symbol->name << ">";
    }

    return out.str();
}

std::string InstructionFormatter::formatTargetAddress(
    uint64_t address,
    const ElfSymbol* symbol
) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << address;
    if (symbol != nullptr) {
        out << " <" << symbol->name;
        if (address != symbol->address) {
            uint64_t delta = address > symbol->address ? address - symbol->address : symbol->address - address;
            out << (address > symbol->address ? "+0x" : "-0x") << delta;
        }
        out << ">";
    }
    return out.str();
}

uint64_t InstructionFormatter::branchOrJumpTarget(
    const DecodedInstruction& instruction
) {
    // Determine sign-extension width from the actual operand name in the decoded instruction
    unsigned bits = 13; // default: B-immediate (branches)
    for (const auto& op : instruction.operands) {
        const std::string& real = op.name;
        if (real == "jimm20" || real == "imm20") { bits = 21; break; }
        if (real == "bimm12")                     { bits = 13; break; }
        if (real == "c_imm12_j")                  { bits = 12; break; } // c.j
        if (real == "c_bimm9")                    { bits =  9; break; } // c.beqz/c.bnez
    }
    // Also honour the mnemonic as a fallback
    if (instruction.mnemonic == "jal") bits = 21;
    const int64_t offset = signExtend(get(instruction, "imm"), bits);
    return static_cast<uint64_t>(static_cast<int64_t>(instruction.address) + offset);
}

std::string InstructionFormatter::formatOperand(
    const DecodedInstruction& instruction,
    const std::string& operand,
    bool targetAllowed
) const {
    const std::string displayMnemonic =
        instruction.mnemonic.rfind("c.", 0) == 0
            ? instruction.mnemonic.substr(2)
            : instruction.mnemonic;

    if (operand == "rd" || operand == "rs1" || operand == "rs2") {
        return registerName(get(instruction, operand));
    }

    if (operand == "csr") {
        return csrName(get(instruction, operand));
    }

    if (operand == "imm") {
        if (targetAllowed &&
            (displayMnemonic == "beq" || displayMnemonic == "bne" ||
             displayMnemonic == "blt" || displayMnemonic == "bge" ||
             displayMnemonic == "bltu" || displayMnemonic == "bgeu" ||
             displayMnemonic == "beqz" || displayMnemonic == "bnez" ||
             displayMnemonic == "jal")) {
            const uint64_t target = branchOrJumpTarget(instruction);
            return formatTargetAddress(target, findNearestSymbol(target));
        }

        const bool shiftImmediate =
            displayMnemonic == "slli" || displayMnemonic == "srli" ||
            displayMnemonic == "srai" || displayMnemonic == "slliw" ||
            displayMnemonic == "srliw" || displayMnemonic == "sraiw";

        if (shiftImmediate) {
            return formatUnsignedHex(get(instruction, operand));
        }

        const unsigned bits =
            displayMnemonic == "lui" || displayMnemonic == "auipc" ? 20 :
            (instruction.mnemonic == "c.addi" || instruction.mnemonic == "c.addiw" ||
             instruction.mnemonic == "c.li") ? 6 : 12;

        return formatImmediate(signExtend(get(instruction, operand), bits));
    }

    if (operand == "zimm") {
        return std::to_string(get(instruction, operand));
    }

    return std::to_string(get(instruction, operand));
}

std::string InstructionFormatter::formatWithRule(
    const DecodedInstruction& instruction,
    const PseudoRule& rule
) const {
    std::ostringstream out;
    out << rule.name;

    if (!rule.operands.empty()) {
        out << "\t";
        for (size_t i = 0; i < rule.operands.size(); ++i) {
            if (i != 0) {
                out << ",";
            }
            const auto& operand = rule.operands[i];
            if (operand == "target") {
                uint64_t target = 0;
                if (instruction.mnemonic == "c.j") {
                    target = static_cast<uint64_t>(
                        static_cast<int64_t>(instruction.address) +
                        signExtend(get(instruction, "imm"), 12));
                } else {
                    target = branchOrJumpTarget(instruction);
                }
                out << formatTargetAddress(target, findNearestSymbol(target));
            } else {
                out << formatOperand(instruction, operand, true);
            }
        }
    }

    return out.str();
}

std::string InstructionFormatter::formatBaseInstruction(
    const DecodedInstruction& instruction
) const {
    const std::string& mnemonic = instruction.mnemonic;
    std::ostringstream out;
    const std::string displayMnemonic =
        mnemonic.rfind("c.", 0) == 0 ? mnemonic.substr(2) : mnemonic;
    out << displayMnemonic;

    if (mnemonic == "c.j") {
        const int64_t offset = signExtend(get(instruction, "imm"), 12);
        const uint64_t target = static_cast<uint64_t>(
            static_cast<int64_t>(instruction.address) + offset);
        out << "\t" << formatTargetAddress(target, findSymbol(target));
        return out.str();
    }

    if (mnemonic == "c.ld" || mnemonic == "c.sd" || mnemonic == "c.fld" || mnemonic == "c.fsd") {
        uint64_t lo = get(instruction, "c_uimm8lo");
        uint64_t hi = get(instruction, "c_uimm8hi");
        uint64_t imm = (lo << 6) | (hi << 3);
        if (mnemonic == "c.ld" || mnemonic == "c.fld") {
            out << "\t" << registerName(get(instruction, "rd_p"))
                << "," << imm << "(" << registerName(get(instruction, "rs1_p")) << ")";
        } else {
            out << "\t" << registerName(get(instruction, "rs2_p"))
                << "," << imm << "(" << registerName(get(instruction, "rs1_p")) << ")";
        }
        return out.str();
    }

    if (mnemonic == "c.lw" || mnemonic == "c.sw" || mnemonic == "c.flw" || mnemonic == "c.fsw") {
        uint64_t lo = get(instruction, "c_uimm7lo");
        uint64_t hi = get(instruction, "c_uimm7hi");
        uint64_t imm = (hi << 3) | ((lo & 2) << 1) | ((lo & 1) << 6);
        if (mnemonic == "c.lw" || mnemonic == "c.flw") {
            out << "\t" << registerName(get(instruction, "rd_p"))
                << "," << imm << "(" << registerName(get(instruction, "rs1_p")) << ")";
        } else {
            out << "\t" << registerName(get(instruction, "rs2_p"))
                << "," << imm << "(" << registerName(get(instruction, "rs1_p")) << ")";
        }
        return out.str();
    }

    if (mnemonic == "c.ldsp" || mnemonic == "c.fldsp") {
        uint64_t lo = get(instruction, "c_uimm9splo");
        uint64_t hi = get(instruction, "c_uimm9sphi");
        uint64_t imm = (hi << 5) | ((lo >> 3) << 3) | ((lo & 7) << 6);
        uint64_t rd = has(instruction, "rd") ? get(instruction, "rd") : get(instruction, "rd_n0");
        out << "\t" << registerName(rd)
            << "," << imm << "(" << registerName(2) << ")";
        return out.str();
    }

    if (mnemonic == "c.sdsp" || mnemonic == "c.fsdsp") {
        uint64_t v = get(instruction, "c_uimm9sp_s");
        uint64_t imm = ((v >> 3) << 3) | ((v & 7) << 6);
        out << "\t" << registerName(get(instruction, "c_rs2"))
            << "," << imm << "(" << registerName(2) << ")";
        return out.str();
    }

    if (mnemonic == "c.lwsp" || mnemonic == "c.flwsp") {
        uint64_t lo = get(instruction, "c_uimm8splo");
        uint64_t hi = get(instruction, "c_uimm8sphi");
        uint64_t imm = (hi << 5) | ((lo >> 2) << 2) | ((lo & 3) << 6);
        uint64_t rd = has(instruction, "rd") ? get(instruction, "rd") : get(instruction, "rd_n0");
        out << "\t" << registerName(rd)
            << "," << imm << "(" << registerName(2) << ")";
        return out.str();
    }

    if (mnemonic == "c.swsp" || mnemonic == "c.fswsp") {
        uint64_t v = get(instruction, "c_uimm8sp_s");
        uint64_t imm = ((v >> 2) << 2) | ((v & 3) << 6);
        out << "\t" << registerName(get(instruction, "c_rs2"))
            << "," << imm << "(" << registerName(2) << ")";
        return out.str();
    }

    if (mnemonic == "c.slli" || mnemonic == "c.srli" || mnemonic == "c.srai") {
        std::string reg = registerName(get(instruction, "rd"));
        out << "\t" << reg << "," << reg << "," << formatUnsignedHex(get(instruction, "imm"));
        return out.str();
    }

    if (mnemonic == "c.addi") {
        const int64_t imm = signExtend(get(instruction, "imm"), 6);
        // rd_rs1_n0 means the register is both source and destination
        const std::string reg = registerName(get(instruction, "rd"));
        int padding = std::max(1, 8 - static_cast<int>(displayMnemonic.length()));
        out << std::string(padding, ' ') << reg << "," << reg << "," << imm;
        return out.str();
    }

    if (mnemonic == "c.addiw") {
        const int64_t imm = signExtend(get(instruction, "imm"), 6);
        const std::string reg = registerName(get(instruction, "rd"));
        int padding = std::max(1, 8 - static_cast<int>(displayMnemonic.length()));
        out << std::string(padding, ' ') << reg << "," << reg << "," << imm;
        return out.str();
    }

    if (mnemonic == "c.li") {
        const int64_t imm = signExtend(get(instruction, "imm"), 6);
        const std::string reg = registerName(get(instruction, "rd"));
        out << "\t" << reg << "," << imm;
        return out.str();
    }

    if (mnemonic == "c.lui") {
        int64_t raw_imm = signExtend(get(instruction, "imm"), 18);
        uint64_t page = (raw_imm >> 12) & 0xfffff;
        const std::string reg = registerName(get(instruction, "rd"));
        out << "\t" << reg << "," << formatUnsignedHex(page);
        return out.str();
    }

    if (mnemonic == "lui") {
        bool new_schema = false;
        for (const auto& op : instruction.operands) {
            if (op.name == "imm20") new_schema = true;
        }
        uint64_t raw = get(instruction, "imm");
        uint64_t page = new_schema ? raw : (raw >> 12);
        out << "\t" << registerName(get(instruction, "rd"))
            << "," << formatUnsignedHex(page);
        return out.str();
    }

    if (mnemonic == "auipc") {
        bool new_schema = false;
        for (const auto& op : instruction.operands) {
            if (op.name == "imm20") new_schema = true;
        }
        uint64_t raw = get(instruction, "imm");
        uint64_t page = new_schema ? raw : (raw >> 12);
        out << "\t" << registerName(get(instruction, "rd"))
            << "," << formatUnsignedHex(page);
        return out.str();
    }

    if (mnemonic == "ld" || mnemonic == "lw" || mnemonic == "lwu" ||
        mnemonic == "lh" || mnemonic == "lhu" || mnemonic == "lb" ||
        mnemonic == "lbu") {
        const int64_t imm = signExtend(get(instruction, "imm"), 12);
        out << "\t" << registerName(get(instruction, "rd"))
            << "," << imm << "(" << registerName(get(instruction, "rs1")) << ")";
        return out.str();
    }

    if (mnemonic == "sd" || mnemonic == "sw" || mnemonic == "sh" ||
        mnemonic == "sb" || mnemonic == "fsw" || mnemonic == "fsd") {
        // S-type stores use imm12s (split field) in the new JSON
        const int64_t imm = signExtend(get(instruction, "imm"), 12);
        out << "\t" << registerName(get(instruction, "rs2"))
            << "," << imm << "(" << registerName(get(instruction, "rs1")) << ")";
        return out.str();
    }

    if (mnemonic == "beq" || mnemonic == "bne" || mnemonic == "blt" ||
        mnemonic == "bge" || mnemonic == "bltu" || mnemonic == "bgeu") {
        const uint64_t target = branchOrJumpTarget(instruction);
        out << "\t" << registerName(get(instruction, "rs1"))
            << "," << registerName(get(instruction, "rs2"))
            << "," << formatTargetAddress(target, findNearestSymbol(target));
        return out.str();
    }

    if (mnemonic == "jal") {
        const uint64_t target = branchOrJumpTarget(instruction);
        uint64_t rd = get(instruction, "rd");
        if (rd == 1) {
            out << "\t" << formatTargetAddress(target, findNearestSymbol(target));
        } else {
            out << "\t" << registerName(rd)
                << "," << formatTargetAddress(target, findNearestSymbol(target));
        }
        return out.str();
    }

    if (mnemonic == "jalr") {
        const int64_t imm = signExtend(get(instruction, "imm"), 12);
        const uint64_t rd = get(instruction, "rd");
        if (rd == 0) {
            if (imm == 0 && get(instruction, "rs1") == 1) {
                return "ret";
            } else {
                if (imm == 0) {
                    return "jr\t" + registerName(get(instruction, "rs1"));
                } else {
                    return "jr\t" + std::to_string(imm) + "(" + registerName(get(instruction, "rs1")) + ")";
                }
            }
        } else {
            out << "\t" << registerName(rd)
                << "," << imm << "(" << registerName(get(instruction, "rs1")) << ")";
        }
        return out.str();
    }

    if (mnemonic == "csrrs" || mnemonic == "csrrw" || mnemonic == "csrrc") {
        out << "\t" << registerName(get(instruction, "rd"))
            << "," << csrName(get(instruction, "csr"))
            << "," << registerName(get(instruction, "rs1"));
        return out.str();
    }

    if (mnemonic == "csrrsi" || mnemonic == "csrrwi" || mnemonic == "csrrci") {
        out << "\t" << registerName(get(instruction, "rd"))
            << "," << csrName(get(instruction, "csr"))
            << "," << get(instruction, "zimm");
        return out.str();
    }

    if (mnemonic == "ecall" || mnemonic == "ebreak" || mnemonic == "mret" ||
        mnemonic == "sret" || mnemonic == "wfi" || mnemonic == "c.nop" || mnemonic == "nop") {
        return out.str();
    }

    for (size_t i = 0; i < instruction.operands.size(); ++i) {
        if (i != 0) {
            out << ",";
        } else {
            out << "\t";
        }

        const auto& operand = instruction.operands[i];
        out << formatOperand(instruction, resolveAlias(operand.name), true);
    }

    return out.str();
}

std::string InstructionFormatter::format(
    const DecodedInstruction& instruction
) const {
    if (!instruction.valid) {
        return "unknown";
    }

    // Rules are ordered by the database. More specific aliases should appear first.
    for (const auto& rule : pseudoDatabase.rules()) {
        if (matches(instruction, rule)) {
            return formatWithRule(instruction, rule);
        }
    }

    return formatBaseInstruction(instruction);
}
