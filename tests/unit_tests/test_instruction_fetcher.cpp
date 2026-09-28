#include <gtest/gtest.h>

#include "riscv/instruction_fetcher.h"

// ===========================================================================
// 16-bit instruction fetching
// ===========================================================================

TEST(FetcherTest, Single16BitInstruction) {
    // c.nop = 0x0001, stored little-endian: 01 00
    std::vector<uint8_t> text = {0x01, 0x00};
    InstructionFetcher fetcher(0x1000, text);

    ASSERT_TRUE(fetcher.hasNext());
    auto inst = fetcher.fetch();

    EXPECT_EQ(inst.address, 0x1000u);
    EXPECT_EQ(inst.raw, 0x0001u);
    EXPECT_EQ(inst.length, InstructionLength::Compressed16);
    EXPECT_FALSE(fetcher.hasNext());
}

TEST(FetcherTest, Single32BitInstruction) {
    // addi ra, zero, 10 = 0x00a00093, little-endian: 93 00 a0 00
    std::vector<uint8_t> text = {0x93, 0x00, 0xa0, 0x00};
    InstructionFetcher fetcher(0x1000, text);

    ASSERT_TRUE(fetcher.hasNext());
    auto inst = fetcher.fetch();

    EXPECT_EQ(inst.address, 0x1000u);
    EXPECT_EQ(inst.raw, 0x00a00093u);
    EXPECT_EQ(inst.length, InstructionLength::Standard32);
    EXPECT_FALSE(fetcher.hasNext());
}

// ===========================================================================
// Length determination via bits[1:0]
// ===========================================================================

TEST(FetcherTest, LengthDeterminedByLowBits_Compressed) {
    // bits[1:0] = 00 → compressed (e.g. c.addi4spn encoding)
    // 0x0010: bits[1:0] = 00
    std::vector<uint8_t> text = {0x10, 0x00};
    InstructionFetcher fetcher(0x2000, text);

    auto inst = fetcher.fetch();
    EXPECT_EQ(inst.length, InstructionLength::Compressed16);
}

TEST(FetcherTest, LengthDeterminedByLowBits_Standard) {
    // bits[1:0] = 11 → 32-bit standard
    // 0x00000033: add x0,x0,x0
    std::vector<uint8_t> text = {0x33, 0x00, 0x00, 0x00};
    InstructionFetcher fetcher(0x2000, text);

    auto inst = fetcher.fetch();
    EXPECT_EQ(inst.length, InstructionLength::Standard32);
}

// ===========================================================================
// Mixed stream: addresses increment correctly
// ===========================================================================

TEST(FetcherTest, MixedStream_AddressesCorrect) {
    // Stream: c.nop(2 bytes) + c.ebreak(2 bytes) + addi(4 bytes) + c.nop(2 bytes)
    std::vector<uint8_t> text = {
        0x01, 0x00,                     // 0x1000: c.nop     (0x0001)
        0x02, 0x90,                     // 0x1002: c.ebreak  (0x9002)
        0x93, 0x00, 0xa0, 0x00,         // 0x1004: addi      (0x00a00093)
        0x01, 0x00,                     // 0x1008: c.nop     (0x0001)
    };
    InstructionFetcher fetcher(0x1000, text);

    // Instruction 1: c.nop at 0x1000
    ASSERT_TRUE(fetcher.hasNext());
    auto i1 = fetcher.fetch();
    EXPECT_EQ(i1.address, 0x1000u);
    EXPECT_EQ(i1.raw, 0x0001u);
    EXPECT_EQ(i1.length, InstructionLength::Compressed16);

    // Instruction 2: c.ebreak at 0x1002
    ASSERT_TRUE(fetcher.hasNext());
    auto i2 = fetcher.fetch();
    EXPECT_EQ(i2.address, 0x1002u);
    EXPECT_EQ(i2.raw, 0x9002u);
    EXPECT_EQ(i2.length, InstructionLength::Compressed16);

    // Instruction 3: addi at 0x1004
    ASSERT_TRUE(fetcher.hasNext());
    auto i3 = fetcher.fetch();
    EXPECT_EQ(i3.address, 0x1004u);
    EXPECT_EQ(i3.raw, 0x00a00093u);
    EXPECT_EQ(i3.length, InstructionLength::Standard32);

    // Instruction 4: c.nop at 0x1008
    ASSERT_TRUE(fetcher.hasNext());
    auto i4 = fetcher.fetch();
    EXPECT_EQ(i4.address, 0x1008u);
    EXPECT_EQ(i4.raw, 0x0001u);
    EXPECT_EQ(i4.length, InstructionLength::Compressed16);

    EXPECT_FALSE(fetcher.hasNext());
}

TEST(FetcherTest, Two32BitInstructions) {
    // add t0,a0,a1 = 0x00b502b3 + sub t1,a2,a3 = 0x40d60333
    std::vector<uint8_t> text = {
        0xb3, 0x02, 0xb5, 0x00,   // 0x1000: add
        0x33, 0x03, 0xd6, 0x40,   // 0x1004: sub
    };
    InstructionFetcher fetcher(0x1000, text);

    auto i1 = fetcher.fetch();
    EXPECT_EQ(i1.address, 0x1000u);
    EXPECT_EQ(i1.raw, 0x00b502b3u);

    auto i2 = fetcher.fetch();
    EXPECT_EQ(i2.address, 0x1004u);
    EXPECT_EQ(i2.raw, 0x40d60333u);

    EXPECT_FALSE(fetcher.hasNext());
}

// ===========================================================================
// Empty stream
// ===========================================================================

TEST(FetcherTest, EmptyStream_HasNextFalse) {
    std::vector<uint8_t> text = {};
    InstructionFetcher fetcher(0x1000, text);
    EXPECT_FALSE(fetcher.hasNext());
}

TEST(FetcherTest, EmptyStream_FetchThrows) {
    std::vector<uint8_t> text = {};
    InstructionFetcher fetcher(0x1000, text);
    EXPECT_THROW(fetcher.fetch(), std::runtime_error);
}

// ===========================================================================
// Truncated instructions throw
// ===========================================================================

TEST(FetcherTest, TruncatedSingleByte_Throws) {
    // Only 1 byte — can't read a 16-bit halfword
    std::vector<uint8_t> text = {0x93};
    InstructionFetcher fetcher(0x1000, text);

    // hasNext() returns true (there IS data), but fetch() should throw
    EXPECT_TRUE(fetcher.hasNext());
    EXPECT_THROW(fetcher.fetch(), std::runtime_error);
}

TEST(FetcherTest, Truncated32Bit_Only2Bytes_Throws) {
    // bits[1:0] = 11 → 32-bit needed, but only 2 bytes available
    // 0x0013 has bits[1:0] = 11
    std::vector<uint8_t> text = {0x13, 0x00};
    InstructionFetcher fetcher(0x1000, text);

    EXPECT_TRUE(fetcher.hasNext());
    EXPECT_THROW(fetcher.fetch(), std::runtime_error);
}

TEST(FetcherTest, Truncated32Bit_Only3Bytes_Throws) {
    // 32-bit required but only 3 bytes present
    std::vector<uint8_t> text = {0x13, 0x00, 0xa0};
    InstructionFetcher fetcher(0x1000, text);

    EXPECT_TRUE(fetcher.hasNext());
    EXPECT_THROW(fetcher.fetch(), std::runtime_error);
}

// ===========================================================================
// Non-zero base address
// ===========================================================================

TEST(FetcherTest, HighBaseAddress) {
    // Test with a realistic RV64 base address like 0x80000000
    std::vector<uint8_t> text = {0x01, 0x00};  // c.nop
    InstructionFetcher fetcher(0x80000000ULL, text);

    auto inst = fetcher.fetch();
    EXPECT_EQ(inst.address, 0x80000000ULL);
}

// ===========================================================================
// Consecutive 16-bit instructions
// ===========================================================================

TEST(FetcherTest, FourConsecutiveCompressed) {
    std::vector<uint8_t> text = {
        0x01, 0x00,  // c.nop at +0
        0x01, 0x00,  // c.nop at +2
        0x01, 0x00,  // c.nop at +4
        0x01, 0x00,  // c.nop at +6
    };
    InstructionFetcher fetcher(0x1000, text);

    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(fetcher.hasNext());
        auto inst = fetcher.fetch();
        EXPECT_EQ(inst.address, 0x1000u + i * 2);
        EXPECT_EQ(inst.length, InstructionLength::Compressed16);
    }
    EXPECT_FALSE(fetcher.hasNext());
}
