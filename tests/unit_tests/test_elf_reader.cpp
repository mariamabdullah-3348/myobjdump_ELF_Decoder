#include <gtest/gtest.h>

#include "elf_reader.h"

#include <fstream>
#include <cstring>

// ===========================================================================
// Helper: read a file into a byte vector (used for crafting test data)
// ===========================================================================
static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(f), {}
    );
}

// ===========================================================================
// ELF64 loading
// ===========================================================================

TEST(ElfReaderTest, LoadsELF64Successfully) {
    ElfReader reader;
    TextSection text;
    ASSERT_TRUE(reader.load("advanced_test.elf", text));

    EXPECT_FALSE(reader.is32Bit());
    EXPECT_GT(text.bytes.size(), 0u);
    EXPECT_NE(text.address, 0u);
}

TEST(ElfReaderTest, ELF64HasSymbols) {
    ElfReader reader;
    TextSection text;
    ASSERT_TRUE(reader.load("advanced_test.elf", text));

    EXPECT_FALSE(reader.symbols().empty());
}

// ===========================================================================
// ELF32 loading — protects the ELF32 regression fix
// ===========================================================================

TEST(ElfReaderTest, LoadsELF32Successfully) {
    ElfReader reader;
    TextSection text;
    ASSERT_TRUE(reader.load("array.elf", text));

    EXPECT_TRUE(reader.is32Bit());
    EXPECT_GT(text.bytes.size(), 0u);
    EXPECT_NE(text.address, 0u);
}

TEST(ElfReaderTest, ELF32HasSymbols) {
    ElfReader reader;
    TextSection text;
    ASSERT_TRUE(reader.load("array.elf", text));

    EXPECT_TRUE(reader.is32Bit());
    EXPECT_FALSE(reader.symbols().empty());
}

TEST(ElfReaderTest, LoadsRV32TestSuccessfully) {
    ElfReader reader;
    TextSection text;
    ASSERT_TRUE(reader.load("rv32_test.elf", text));

    EXPECT_TRUE(reader.is32Bit());
    EXPECT_GT(text.bytes.size(), 0u);
}

// ===========================================================================
// Invalid / missing files
// ===========================================================================

TEST(ElfReaderTest, FailsOnMissingFile) {
    ElfReader reader;
    TextSection text;
    EXPECT_FALSE(reader.load("nonexistent_file.elf", text));
}

TEST(ElfReaderTest, FailsOnEmptyFile) {
    // Create a temporary empty file
    {
        std::ofstream f("data/empty_test.bin", std::ios::binary);
    }
    ElfReader reader;
    TextSection text;
    EXPECT_FALSE(reader.load("data/empty_test.bin", text));
}

TEST(ElfReaderTest, FailsOnTooSmallFile) {
    // A file with only 3 bytes (insufficient for ELF magic)
    {
        std::ofstream f("data/tiny_test.bin", std::ios::binary);
        f.put(0x7f);
        f.put('E');
        f.put('L');
    }
    ElfReader reader;
    TextSection text;
    EXPECT_FALSE(reader.load("data/tiny_test.bin", text));
}

TEST(ElfReaderTest, FailsOnWrongMagic) {
    // Valid-sized file but wrong magic bytes
    {
        std::ofstream f("data/bad_magic.bin", std::ios::binary);
        const char data[] = "NOT_AN_ELF_FILE_AT_ALL_XXXXX";
        f.write(data, sizeof(data));
    }
    ElfReader reader;
    TextSection text;
    EXPECT_FALSE(reader.load("data/bad_magic.bin", text));
}

TEST(ElfReaderTest, FailsOnWrongElfClass) {
    // Craft a minimal ELF header with an invalid EI_CLASS (0xFF)
    std::vector<uint8_t> data(64, 0);
    data[0] = 0x7f; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
    data[4] = 0xFF;  // invalid EI_CLASS
    {
        std::ofstream f("data/bad_class.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    ElfReader reader;
    TextSection text;
    EXPECT_FALSE(reader.load("data/bad_class.bin", text));
}

// ===========================================================================
// ELF validation: wrong machine / endian
// ===========================================================================

TEST(ElfReaderTest, FailsOnBigEndianElf) {
    // ELF header with EI_DATA = ELFDATA2MSB (big endian = 2)
    std::vector<uint8_t> data(128, 0);
    data[0] = 0x7f; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
    data[4] = 2;  // ELFCLASS64
    data[5] = 2;  // ELFDATA2MSB (big endian — we only support little)
    {
        std::ofstream f("data/big_endian.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    ElfReader reader;
    TextSection text;
    EXPECT_FALSE(reader.load("data/big_endian.bin", text));
}

// ===========================================================================
// Symbol resolution
// ===========================================================================

TEST(ElfReaderTest, FindSymbolByAddress) {
    ElfReader reader;
    TextSection text;
    ASSERT_TRUE(reader.load("advanced_test.elf", text));

    // The ELF should have symbols. Find one at the text section start.
    const auto* sym = reader.findSymbol(text.address);
    // It's OK if there's no symbol at the exact start, but this shouldn't crash
    // We just verify it returns nullptr or a valid symbol.
    if (sym) {
        EXPECT_FALSE(sym->name.empty());
        EXPECT_EQ(sym->address, text.address);
    }
}

TEST(ElfReaderTest, FindSymbolReturnsNullForBogusAddress) {
    ElfReader reader;
    TextSection text;
    ASSERT_TRUE(reader.load("advanced_test.elf", text));

    const auto* sym = reader.findSymbol(0xDEADBEEFDEADBEEFULL);
    EXPECT_EQ(sym, nullptr);
}

TEST(ElfReaderTest, SymbolsSortedByAddress) {
    ElfReader reader;
    TextSection text;
    ASSERT_TRUE(reader.load("advanced_test.elf", text));

    const auto& syms = reader.symbols();
    for (size_t i = 1; i < syms.size(); ++i) {
        EXPECT_LE(syms[i - 1].address, syms[i].address)
            << "Symbols not sorted at index " << i;
    }
}

// ===========================================================================
// ELF32 symbol resolution
// ===========================================================================

TEST(ElfReaderTest, ELF32FindsMainSymbol) {
    ElfReader reader;
    TextSection text;
    ASSERT_TRUE(reader.load("array.elf", text));

    // array.elf should have a "main" symbol
    bool foundMain = false;
    for (const auto& sym : reader.symbols()) {
        if (sym.name == "main") {
            foundMain = true;
            break;
        }
    }
    EXPECT_TRUE(foundMain) << "array.elf should contain a 'main' symbol";
}

TEST(ElfReaderTest, ELF32TextAddress) {
    ElfReader reader;
    TextSection text;
    ASSERT_TRUE(reader.load("array.elf", text));

    // ELF32 RISC-V typically has .text at 0x10074 or similar
    EXPECT_GT(text.address, 0u);
    EXPECT_LT(text.address, 0x100000u); // should be a 32-bit address
}

// ===========================================================================
// Multiple ELF loads: reader resets properly
// ===========================================================================

TEST(ElfReaderTest, ReloadsCleanly) {
    ElfReader reader;
    TextSection text1, text2;

    ASSERT_TRUE(reader.load("advanced_test.elf", text1));
    EXPECT_FALSE(reader.is32Bit());

    ASSERT_TRUE(reader.load("array.elf", text2));
    EXPECT_TRUE(reader.is32Bit());

    // Symbols should be from array.elf, not leftover from advanced_test.elf
    bool foundMain = false;
    for (const auto& sym : reader.symbols()) {
        if (sym.name == "main") { foundMain = true; break; }
    }
    EXPECT_TRUE(foundMain);
}
