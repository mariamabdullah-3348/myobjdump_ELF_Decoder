#include <gtest/gtest.h>

#include "riscv/opcode_database.h"

#include <algorithm>

// ===== Loading tests =====

TEST(OpcodeDatabaseTest, LoadsSuccessfully) {
    OpcodeDatabase db;
    ASSERT_TRUE(db.load("data/riscv_decoder.json"));
    EXPECT_GT(db.instructions().size(), 0u);
}

TEST(OpcodeDatabaseTest, FailsOnMissingFile) {
    OpcodeDatabase db;
    EXPECT_FALSE(db.load("data/nonexistent_file.json"));
}

TEST(OpcodeDatabaseTest, DatabaseNotEmpty) {
    OpcodeDatabase db;
    ASSERT_TRUE(db.load("data/riscv_decoder.json"));
    // The RISC-V ISA has at least a few hundred instructions
    EXPECT_GT(db.instructions().size(), 100u);
}

// ===== Content verification tests =====

static bool hasMnemonic(const OpcodeDatabase& db, const std::string& name) {
    for (const auto& def : db.instructions()) {
        if (def.mnemonic == name) return true;
    }
    return false;
}

TEST(OpcodeDatabaseTest, ContainsBaseInstructions) {
    OpcodeDatabase db;
    ASSERT_TRUE(db.load("data/riscv_decoder.json"));

    EXPECT_TRUE(hasMnemonic(db, "add"));
    EXPECT_TRUE(hasMnemonic(db, "sub"));
    EXPECT_TRUE(hasMnemonic(db, "addi"));
    EXPECT_TRUE(hasMnemonic(db, "lui"));
    EXPECT_TRUE(hasMnemonic(db, "jal"));
    EXPECT_TRUE(hasMnemonic(db, "jalr"));
    EXPECT_TRUE(hasMnemonic(db, "beq"));
    EXPECT_TRUE(hasMnemonic(db, "ld"));
    EXPECT_TRUE(hasMnemonic(db, "sd"));
}

TEST(OpcodeDatabaseTest, ContainsCompressedInstructions) {
    OpcodeDatabase db;
    ASSERT_TRUE(db.load("data/riscv_decoder.json"));

    EXPECT_TRUE(hasMnemonic(db, "c.nop"));
    EXPECT_TRUE(hasMnemonic(db, "c.addi"));
    EXPECT_TRUE(hasMnemonic(db, "c.ld"));
    EXPECT_TRUE(hasMnemonic(db, "c.sd"));
    EXPECT_TRUE(hasMnemonic(db, "c.j"));
    EXPECT_TRUE(hasMnemonic(db, "c.beqz"));
    EXPECT_TRUE(hasMnemonic(db, "c.ebreak"));
}

TEST(OpcodeDatabaseTest, ContainsMExtension) {
    OpcodeDatabase db;
    ASSERT_TRUE(db.load("data/riscv_decoder.json"));

    EXPECT_TRUE(hasMnemonic(db, "mul"));
    EXPECT_TRUE(hasMnemonic(db, "mulw"));
    EXPECT_TRUE(hasMnemonic(db, "div"));
    EXPECT_TRUE(hasMnemonic(db, "rem"));
}

TEST(OpcodeDatabaseTest, ContainsWInstructions) {
    OpcodeDatabase db;
    ASSERT_TRUE(db.load("data/riscv_decoder.json"));

    EXPECT_TRUE(hasMnemonic(db, "addw"));
    EXPECT_TRUE(hasMnemonic(db, "subw"));
    EXPECT_TRUE(hasMnemonic(db, "slliw"));
    EXPECT_TRUE(hasMnemonic(db, "srliw"));
    EXPECT_TRUE(hasMnemonic(db, "sraiw"));
}

// ===== Structural integrity tests =====

TEST(OpcodeDatabaseTest, AllInstructionsHaveMask) {
    OpcodeDatabase db;
    ASSERT_TRUE(db.load("data/riscv_decoder.json"));

    for (const auto& def : db.instructions()) {
        EXPECT_NE(def.mask, 0u) << "Instruction " << def.mnemonic << " has zero mask";
    }
}

TEST(OpcodeDatabaseTest, MatchBitsWithinMask) {
    OpcodeDatabase db;
    ASSERT_TRUE(db.load("data/riscv_decoder.json"));

    for (const auto& def : db.instructions()) {
        EXPECT_EQ(def.match & def.mask, def.match)
            << "Instruction " << def.mnemonic
            << ": match bits extend beyond mask";
    }
}

TEST(OpcodeDatabaseTest, AddiHasOperands) {
    OpcodeDatabase db;
    ASSERT_TRUE(db.load("data/riscv_decoder.json"));

    for (const auto& def : db.instructions()) {
        if (def.mnemonic == "addi") {
            EXPECT_FALSE(def.operands.empty())
                << "addi should have operands defined";
            return;
        }
    }
    FAIL() << "addi not found in database";
}
