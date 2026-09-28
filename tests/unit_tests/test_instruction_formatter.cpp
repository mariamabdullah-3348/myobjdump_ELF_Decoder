#include <gtest/gtest.h>

#include "riscv/instruction_formatter.h"

// ===========================================================================
// Static helper tests (no database or symbols needed)
// ===========================================================================

// ---- signExtend -----------------------------------------------------------

TEST(FormatterTest, SignExtend12BitPositive) {
    EXPECT_EQ(InstructionFormatter::signExtend(0x7FF, 12), 2047);
}

TEST(FormatterTest, SignExtend12BitNegative) {
    // 0xFFF with 12 bits is -1
    EXPECT_EQ(InstructionFormatter::signExtend(0xFFF, 12), -1);
}

TEST(FormatterTest, SignExtend12BitMinimum) {
    // 0x800 with 12 bits is -2048
    EXPECT_EQ(InstructionFormatter::signExtend(0x800, 12), -2048);
}

TEST(FormatterTest, SignExtend12BitZero) {
    EXPECT_EQ(InstructionFormatter::signExtend(0, 12), 0);
}

TEST(FormatterTest, SignExtend6BitPositive) {
    EXPECT_EQ(InstructionFormatter::signExtend(0x1F, 6), 31);
}

TEST(FormatterTest, SignExtend6BitNegative) {
    // 0x3F with 6 bits is -1
    EXPECT_EQ(InstructionFormatter::signExtend(0x3F, 6), -1);
}

TEST(FormatterTest, SignExtend20BitPositive) {
    EXPECT_EQ(InstructionFormatter::signExtend(0x7FFFF, 20), 0x7FFFF);
}

TEST(FormatterTest, SignExtend20BitNegative) {
    EXPECT_EQ(InstructionFormatter::signExtend(0xFFFFF, 20), -1);
}

TEST(FormatterTest, SignExtendZeroBits) {
    // Edge case: 0 bits means no sign extension
    EXPECT_EQ(InstructionFormatter::signExtend(42, 0), 42);
}

// ---- resolveAlias ---------------------------------------------------------

TEST(FormatterTest, ResolveAliasImm12) {
    EXPECT_EQ(InstructionFormatter::resolveAlias("imm12"), "imm");
}

TEST(FormatterTest, ResolveAliasImm20) {
    EXPECT_EQ(InstructionFormatter::resolveAlias("imm20"), "imm");
}

TEST(FormatterTest, ResolveAliasJimm20) {
    EXPECT_EQ(InstructionFormatter::resolveAlias("jimm20"), "imm");
}

TEST(FormatterTest, ResolveAliasBimm12) {
    EXPECT_EQ(InstructionFormatter::resolveAlias("bimm12"), "imm");
}

TEST(FormatterTest, ResolveAliasShamt) {
    EXPECT_EQ(InstructionFormatter::resolveAlias("shamt"), "imm");
}

TEST(FormatterTest, ResolveAliasRdP) {
    EXPECT_EQ(InstructionFormatter::resolveAlias("rd_p"), "rd");
}

TEST(FormatterTest, ResolveAliasRs1P) {
    EXPECT_EQ(InstructionFormatter::resolveAlias("rs1_p"), "rs1");
}

TEST(FormatterTest, ResolveAliasRs2P) {
    EXPECT_EQ(InstructionFormatter::resolveAlias("rs2_p"), "rs2");
}

TEST(FormatterTest, ResolveAliasPassThrough) {
    // Names that are already canonical should be returned as-is
    EXPECT_EQ(InstructionFormatter::resolveAlias("rd"), "rd");
    EXPECT_EQ(InstructionFormatter::resolveAlias("rs1"), "rs1");
    EXPECT_EQ(InstructionFormatter::resolveAlias("rs2"), "rs2");
    EXPECT_EQ(InstructionFormatter::resolveAlias("imm"), "imm");
}

TEST(FormatterTest, ResolveAliasCompressedImm) {
    EXPECT_EQ(InstructionFormatter::resolveAlias("c_imm6hi"), "imm");
    EXPECT_EQ(InstructionFormatter::resolveAlias("c_imm6lo"), "imm");
    EXPECT_EQ(InstructionFormatter::resolveAlias("c_bimm9hi"), "imm");
    EXPECT_EQ(InstructionFormatter::resolveAlias("c_bimm9lo"), "imm");
    EXPECT_EQ(InstructionFormatter::resolveAlias("c_uimm8hi"), "imm");
    EXPECT_EQ(InstructionFormatter::resolveAlias("c_uimm8lo"), "imm");
}

// ---- has / get on DecodedInstruction --------------------------------------

TEST(FormatterTest, HasFindsOperand) {
    DecodedInstruction inst;
    inst.valid = true;
    inst.mnemonic = "addi";
    inst.operands.push_back({"rd", 5});
    inst.operands.push_back({"rs1", 0});
    inst.operands.push_back({"imm12", 10});

    EXPECT_TRUE(InstructionFormatter::has(inst, "rd"));
    EXPECT_TRUE(InstructionFormatter::has(inst, "rs1"));
    // "imm12" resolves to "imm" via alias
    EXPECT_TRUE(InstructionFormatter::has(inst, "imm"));
    EXPECT_FALSE(InstructionFormatter::has(inst, "rs2"));
}

TEST(FormatterTest, GetReturnsOperandValue) {
    DecodedInstruction inst;
    inst.valid = true;
    inst.mnemonic = "addi";
    inst.operands.push_back({"rd", 5});
    inst.operands.push_back({"rs1", 0});
    inst.operands.push_back({"imm12", 10});

    EXPECT_EQ(InstructionFormatter::get(inst, "rd"), 5u);
    EXPECT_EQ(InstructionFormatter::get(inst, "rs1"), 0u);
    EXPECT_EQ(InstructionFormatter::get(inst, "imm"), 10u);
}

TEST(FormatterTest, GetCompressedRegisterAdds8) {
    // Compressed register fields like rd_p are in range 0-7 and map to x8-x15
    DecodedInstruction inst;
    inst.valid = true;
    inst.mnemonic = "c.ld";
    inst.operands.push_back({"rd_p", 2});    // should become x10 (a2)
    inst.operands.push_back({"rs1_p", 5});   // should become x13 (a3)

    EXPECT_EQ(InstructionFormatter::get(inst, "rd"), 10u);   // 2 + 8
    EXPECT_EQ(InstructionFormatter::get(inst, "rs1"), 13u);  // 5 + 8
}

// ---- PseudoDatabase -------------------------------------------------------

TEST(FormatterTest, PseudoDatabaseLoads) {
    PseudoDatabase db;
    ASSERT_TRUE(db.load("data/riscv_pseudos.json"));
    EXPECT_GT(db.rules().size(), 0u);
}

TEST(FormatterTest, PseudoDatabaseFailsOnMissing) {
    PseudoDatabase db;
    EXPECT_FALSE(db.load("data/nonexistent_pseudos.json"));
}

// ---- Full formatting with database ----------------------------------------

class FormatterWithDBTest : public ::testing::Test {
protected:
    static OpcodeDatabase opcodeDb;
    static PseudoDatabase pseudoDb;
    static bool loaded;
    std::vector<ElfSymbol> emptySymbols;

    static void SetUpTestSuite() {
        loaded = opcodeDb.load("data/riscv_decoder.json") &&
                 pseudoDb.load("data/riscv_pseudos.json");
    }

    void SetUp() override {
        ASSERT_TRUE(loaded) << "Could not load databases";
    }

    std::string decodeAndFormat(uint32_t raw, InstructionLength len = InstructionLength::Standard32) {
        RiscVDecoder decoder(opcodeDb);
        RiscVInstruction inst{0x10000, raw, len};
        auto decoded = decoder.decode(inst);
        InstructionFormatter formatter(pseudoDb, emptySymbols);
        return formatter.format(decoded);
    }
};

OpcodeDatabase FormatterWithDBTest::opcodeDb;
PseudoDatabase FormatterWithDBTest::pseudoDb;
bool FormatterWithDBTest::loaded = false;

TEST_F(FormatterWithDBTest, FormatNop) {
    // c.nop = 0x0001
    std::string text = decodeAndFormat(0x0001, InstructionLength::Compressed16);
    EXPECT_EQ(text, "nop");
}

TEST_F(FormatterWithDBTest, FormatEbreak) {
    // c.ebreak = 0x9002
    std::string text = decodeAndFormat(0x9002, InstructionLength::Compressed16);
    EXPECT_EQ(text, "ebreak");
}
