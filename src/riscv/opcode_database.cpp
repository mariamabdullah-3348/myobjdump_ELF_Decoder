#include "riscv/opcode_database.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace {
uint32_t parseUInt(const json& value) {
    if (value.is_number_unsigned()) {
        return value.get<uint32_t>();
    }

    if (value.is_number_integer()) {
        return static_cast<uint32_t>(value.get<int64_t>());
    }

    if (value.is_string()) {
        return static_cast<uint32_t>(
            std::stoul(value.get<std::string>(), nullptr, 0)
        );
    }

    throw std::runtime_error("Invalid integer in decoder database");
}
}

bool OpcodeDatabase::load(
    const std::string& decoderDatabasePath
) {
    instructionList.clear();

    std::ifstream file(decoderDatabasePath);
    if (!file) {
        return false;
    }

    json root;
    file >> root;

    // Support both formats:
    //   Old: root is a flat object  { "mnemonic": { "match": …, … } }
    //   New: root has "instructions" array  [ { "mnemonic": "…", "match": …, … } ]
    const json* entries = nullptr;
    json fallback;

    if (root.is_array()) {
        // root itself is the array
        entries = &root;
    } else if (root.contains("instructions") && root["instructions"].is_array()) {
        // New generated format
        entries = &root["instructions"];
    } else {
        // Old flat-object format: wrap items into a temporary array
        fallback = json::array();
        for (auto& [name, value] : root.items()) {
            json entry = value;
            entry["mnemonic"] = name;
            fallback.push_back(std::move(entry));
        }
        entries = &fallback;
    }

    for (const auto& value : *entries) {
        InstructionDef def;

        // mnemonic is always a field in the new format
        if (value.contains("mnemonic")) {
            def.mnemonic = value["mnemonic"].get<std::string>();
        } else {
            continue; // skip malformed entries
        }

        def.match = parseUInt(value.at("match"));
        def.mask  = parseUInt(value.at("mask"));

        if (value.contains("length")) {
            // length is in bytes; stored but not needed here
            // (used by the fetcher/formatter)
        }

        if (value.contains("encoding")) {
            def.encoding = value["encoding"].get<std::string>();
        }

        if (value.contains("extension")) {
            def.extension = value["extension"].get<std::string>();
        }

        if (value.contains("variableFields")) {
            for (const auto& vf : value["variableFields"]) {
                def.variableFields.push_back(vf.get<std::string>());
            }
        }

        if (value.contains("operands")) {
            for (const auto& operandJson : value["operands"]) {
                OperandDef operand;
                operand.name = operandJson.at("name").get<std::string>();

                for (const auto& pieceJson : operandJson.at("pieces")) {
                    // Support both old key names and new key names
                    int msb = pieceJson.contains("instructionMsb")
                        ? pieceJson["instructionMsb"].get<int>()
                        : pieceJson.at("msb").get<int>();
                    int lsb = pieceJson.contains("instructionLsb")
                        ? pieceJson["instructionLsb"].get<int>()
                        : pieceJson.at("lsb").get<int>();
                    int opLsb = pieceJson.contains("operandLsb")
                        ? pieceJson["operandLsb"].get<int>()
                        : pieceJson.at("operand_lsb").get<int>();

                    operand.pieces.push_back({msb, lsb, opLsb});
                }

                def.operands.push_back(std::move(operand));
            }
        }

        instructionList.push_back(std::move(def));
    }

    return true;
}

const std::vector<InstructionDef>&
OpcodeDatabase::instructions() const {
    return instructionList;
}
