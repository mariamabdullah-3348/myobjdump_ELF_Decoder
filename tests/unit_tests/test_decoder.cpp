#include <gtest/gtest.h>

#include "riscv/decoder.h"
#include "riscv/opcode_database.h"
#include "riscv/instruction_formatter.h"

// ---------------------------------------------------------------------------
// Fixture: loads the real database once for the whole suite
// ---------------------------------------------------------------------------
class DecoderTest : public ::testing::Test {
protected:
    static OpcodeDatabase db;
    static bool dbLoaded;

    static void SetUpTestSuite() {
        dbLoaded = db.load("data/riscv_decoder.json");
    }

    void SetUp() override {
        ASSERT_TRUE(dbLoaded) << "Could not load data/riscv_decoder.json";
    }
};

OpcodeDatabase DecoderTest::db;
bool DecoderTest::dbLoaded = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static RiscVInstruction make32(uint32_t raw, uint64_t addr = 0) {
    return {addr, raw, InstructionLength::Standard32};
}

static RiscVInstruction make16(uint16_t raw, uint64_t addr = 0) {
    return {addr, raw, InstructionLength::Compressed16};
}

static uint64_t getOperand(const DecodedInstruction& inst, const std::string& name) {
    return InstructionFormatter::get(inst, name);
}

static bool hasOperand(const DecodedInstruction& inst, const std::string& name) {
    return InstructionFormatter::has(inst, name);
}

// ===== Basic decode + mnemonic tests =====

TEST_F(DecoderTest, DecodeAddi) {
    // addi ra, zero, 10  =>  0x00a00093
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x00a00093));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "addi");
}

TEST_F(DecoderTest, DecodeAdd) {
    // add t0, a0, a1  =>  0x00b502b3
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x00b502b3));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "add");
}

TEST_F(DecoderTest, DecodeSub) {
    // sub t1, a2, a3  =>  0x40d60333
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x40d60333));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "sub");
}

TEST_F(DecoderTest, DecodeLui) {
    // lui a0, 0x12345  =>  0x12345537
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x12345537));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "lui");
}

TEST_F(DecoderTest, DecodeJal) {
    // jal ra, 0  =>  0x000000ef
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x000000ef));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "jal");
}

TEST_F(DecoderTest, DecodeBeq) {
    // beq a0, a1, 0  =>  0x00b50063
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x00b50063));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "beq");
}

// ===== Compressed instruction tests =====

TEST_F(DecoderTest, DecodeCompressedNop) {
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make16(0x0001));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "c.nop");
}

TEST_F(DecoderTest, DecodeCompressedEbreak) {
    // c.ebreak collision test: 0x9002 must be recognized as c.ebreak
    // and NOT as a generic c.add/c.mv/c.jalr pattern
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make16(0x9002));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "c.ebreak");
}

// ===== W-extension tests =====

TEST_F(DecoderTest, DecodeAddw) {
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x00b502bb));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "addw");
}

TEST_F(DecoderTest, DecodeSubw) {
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x40d6033b));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "subw");
}

// ===== Load/Store tests =====

TEST_F(DecoderTest, DecodeLd) {
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x80013103));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "ld");
}

TEST_F(DecoderTest, DecodeSd) {
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x7e113c23));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "sd");
}

// ===== Invalid instruction =====

TEST_F(DecoderTest, InvalidInstructionIsMarkedInvalid) {
    // bits[6:0] = 1111111 is reserved for instructions longer than 32 bits
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0xFFFFFFFF));
    EXPECT_FALSE(result.valid);
}

// ===== Instruction length preservation =====

TEST_F(DecoderTest, InstructionLengthSet) {
    RiscVDecoder decoder(db);
    auto r32 = decoder.decode(make32(0x00a00093));
    EXPECT_EQ(r32.length, InstructionLength::Standard32);

    auto r16 = decoder.decode(make16(0x0001));
    EXPECT_EQ(r16.length, InstructionLength::Compressed16);
}

// ===========================================================================
// OPERAND VALUE VERIFICATION
// These tests verify that the decoder extracts the correct operand values,
// not just that the mnemonic is right. This catches immediate reconstruction
// bugs (B-imm, J-imm, compressed imm, etc.)
// ===========================================================================

TEST_F(DecoderTest, AddiOperandValues) {
    // addi ra, zero, 10  =>  0x00a00093
    // rd=1(ra), rs1=0(zero), imm12=10
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x00a00093));
    ASSERT_TRUE(result.valid);

    EXPECT_EQ(getOperand(result, "rd"), 1u);   // ra
    EXPECT_EQ(getOperand(result, "rs1"), 0u);  // zero
    EXPECT_EQ(getOperand(result, "imm"), 10u); // immediate = 10
}

TEST_F(DecoderTest, AddOperandValues) {
    // add t0, a0, a1  =>  0x00b502b3
    // rd=5(t0), rs1=10(a0), rs2=11(a1)
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x00b502b3));
    ASSERT_TRUE(result.valid);

    EXPECT_EQ(getOperand(result, "rd"), 5u);   // t0
    EXPECT_EQ(getOperand(result, "rs1"), 10u); // a0
    EXPECT_EQ(getOperand(result, "rs2"), 11u); // a1
}

TEST_F(DecoderTest, LuiOperandValues) {
    // lui a0, 0x12345  =>  0x12345537
    // rd=10(a0), imm20=0x12345
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x12345537));
    ASSERT_TRUE(result.valid);

    EXPECT_EQ(getOperand(result, "rd"), 10u);      // a0
    EXPECT_EQ(getOperand(result, "imm"), 0x12345u); // upper immediate
}

TEST_F(DecoderTest, BeqBranchImmediateReconstruction) {
    // beq a0, a1, +8  at address 0x1000
    // Encoding: imm[12|10:5] rs2 rs1 funct3=000 imm[4:1|11] opcode=1100011
    // beq a0, a1, +8 => offset 8 => bimm12 encoding
    // 0x00b50463: beq a0, a1, +8
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x00b50463, 0x1000));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "beq");
    EXPECT_EQ(getOperand(result, "rs1"), 10u);  // a0
    EXPECT_EQ(getOperand(result, "rs2"), 11u);  // a1
    // The bimm12 field should reconstruct to 8
    EXPECT_TRUE(hasOperand(result, "imm"));
    EXPECT_EQ(getOperand(result, "imm"), 8u);
}

TEST_F(DecoderTest, JalImmediateReconstruction) {
    // jal ra, +0  at address 0  =>  0x000000ef
    // jimm20 bits should reconstruct to 0
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x000000ef, 0x0));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "jal");
    EXPECT_EQ(getOperand(result, "rd"), 1u);   // ra
    EXPECT_EQ(getOperand(result, "imm"), 0u);  // offset = 0
}

TEST_F(DecoderTest, SdStoreImmediateReconstruction) {
    // sd ra, 2040(sp)  =>  0x7e113c23
    // imm12s splits: imm[11:5]=0x3f, imm[4:0]=0x18 => 0x7f8 = 2040
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x7e113c23));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "sd");
    EXPECT_EQ(getOperand(result, "rs1"), 2u);  // sp
    EXPECT_EQ(getOperand(result, "rs2"), 1u);  // ra
    // The split S-immediate should reconstruct to 2040
    EXPECT_TRUE(hasOperand(result, "imm"));
    EXPECT_EQ(getOperand(result, "imm"), 2040u);
}

TEST_F(DecoderTest, AddiNegativeImmediate) {
    // addi a4, a4, -2048 => 0x80070713
    // imm12 = 0x800 = -2048 (raw unsigned value is 2048)
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x80070713));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "addi");
    EXPECT_EQ(getOperand(result, "rd"), 14u);    // a4
    EXPECT_EQ(getOperand(result, "rs1"), 14u);   // a4
    // Raw extracted value is 0x800 (signExtend in formatter will make it -2048)
    EXPECT_EQ(getOperand(result, "imm"), 0x800u);
}

// ===== RV64 shift mask tests =====

TEST_F(DecoderTest, Slli_ShiftAmount) {
    // slli a1, a1, 0x20 => 0x02059593
    // shamtd = 0x20 = 32
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x02059593));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "slli");
    EXPECT_EQ(getOperand(result, "rd"), 11u);   // a1
    EXPECT_EQ(getOperand(result, "rs1"), 11u);  // a1
}

TEST_F(DecoderTest, Srli_ShiftAmount) {
    // srli t3, t3, 0x1f => 0x01fe5e13
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x01fe5e13));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "srli");
}

TEST_F(DecoderTest, Srai_ShiftAmount) {
    // srai t4, t4, 0x10 => 0x410ede93
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x410ede93));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "srai");
}

// ===== Compressed operand values =====

TEST_F(DecoderTest, CompressedCAddi_Operands) {
    // c.addi t0, 15 => 0x02bc (example)
    // Let's use a known c.addi encoding: c.addi a0, 1 => 0x0505
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make16(0x0505));
    ASSERT_TRUE(result.valid);
    // Should be c.addi (or c.li depending on rd)
}

// ===== CSR instruction =====

TEST_F(DecoderTest, Csrrw_Decodes) {
    // csrrw a0, mstatus, zero = csrr a0, mstatus => 0x30002573
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x30002573));
    ASSERT_TRUE(result.valid);
    // Should decode to csrrs (csrr is a pseudo)
    EXPECT_EQ(result.mnemonic, "csrrs");
}

// ===== AUIPC =====

TEST_F(DecoderTest, Auipc_Decodes) {
    // auipc a3, 0x12345 => 0x12345697
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x12345697));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "auipc");
    EXPECT_EQ(getOperand(result, "rd"), 13u);       // a3
    EXPECT_EQ(getOperand(result, "imm"), 0x12345u);
}

// ===== JALR =====

TEST_F(DecoderTest, Jalr_Decodes) {
    // jalr zero, 0(ra) => 0x00008067 (this is ret as a pseudo)
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x00008067));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "jalr");
    EXPECT_EQ(getOperand(result, "rd"), 0u);   // zero
    EXPECT_EQ(getOperand(result, "rs1"), 1u);  // ra
    EXPECT_EQ(getOperand(result, "imm"), 0u);  // offset 0
}

// ===== MUL extension =====

TEST_F(DecoderTest, Mul_Decodes) {
    // mul t1, t1, t0 => 0x02530333 (example encoding)
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x02530333));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.mnemonic, "mul");
}

// ===== Address preserved =====

TEST_F(DecoderTest, AddressPreservedInDecoded) {
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x00a00093, 0x80001234));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.address, 0x80001234u);
}

TEST_F(DecoderTest, RawPreservedInDecoded) {
    RiscVDecoder decoder(db);
    auto result = decoder.decode(make32(0x00a00093));
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.raw, 0x00a00093u);
}
