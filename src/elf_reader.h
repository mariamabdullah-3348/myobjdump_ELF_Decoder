#ifndef ELF_READER_H
#define ELF_READER_H

#include <cstdint>
#include <string>
#include <vector>

#include "elf.h"

struct TextSection {
    uint64_t address = 0;
    std::vector<uint8_t> bytes;
};

struct ElfSymbol {
    std::string name;
    uint64_t address = 0;
    uint64_t size = 0;
    uint8_t type = 0;
    uint8_t binding = 0;
    uint16_t sectionIndex = 0;
};

class ElfReader {
public:
    bool load(
        const std::string& path,
        TextSection& textSection
    );

    bool is32Bit() const { return is32BitClass; }
    const std::vector<ElfSymbol>& symbols() const { return symbolList; }

    // Returns the symbol whose value exactly matches address.  If several
    // symbols share an address, prefer a function symbol over other types.
    const ElfSymbol* findSymbol(uint64_t address) const;

    // Returns a function/notype symbol exactly at address.  These are the
    // symbols that are useful as disassembly labels.
    const ElfSymbol* findCodeSymbol(uint64_t address) const;

private:
    bool isElf(const std::vector<uint8_t>& data) const;
    template <typename Ehdr, typename Shdr, typename Sym>
    bool loadSymbolsTemplate(
        const std::vector<uint8_t>& data,
        const Ehdr* header,
        const Shdr* sections
    );

    template <typename Ehdr, typename Shdr, typename Sym>
    bool loadTemplate(
        const std::vector<uint8_t>& data,
        TextSection& textSection
    );

    std::vector<ElfSymbol> symbolList;
    bool is32BitClass = false;
};

#endif
