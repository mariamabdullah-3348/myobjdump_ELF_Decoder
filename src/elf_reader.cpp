#include "elf_reader.h"

#include "elf.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {

constexpr uint32_t SHT_SYMTAB = 2;
constexpr uint32_t SHT_DYNSYM = 11;
constexpr uint8_t STT_NOTYPE = 0;
constexpr uint8_t STT_OBJECT = 1;
constexpr uint8_t STT_FUNC = 2;

bool isMappingSymbol(const ElfSymbol& symbol) {
    return !symbol.name.empty() && symbol.name[0] == '$';
}

uint8_t symbolType(uint8_t info) {
    return static_cast<uint8_t>(info & 0x0f);
}

uint8_t symbolBinding(uint8_t info) {
    return static_cast<uint8_t>(info >> 4);
}

bool rangeInside(uint64_t offset, uint64_t size, size_t total) {
    return offset <= total && size <= static_cast<uint64_t>(total) - offset;
}

} // namespace

bool ElfReader::isElf(
    const std::vector<uint8_t>& data
) const {
    return data.size() >= 4 &&
           data[0] == 0x7f &&
           data[1] == 'E' &&
           data[2] == 'L' &&
           data[3] == 'F';
}

// Removed non-templated loadSymbols

const ElfSymbol* ElfReader::findSymbol(uint64_t address) const {
    const ElfSymbol* fallback = nullptr;

    for (const auto& symbol : symbolList) {
        if (symbol.address < address) {
            continue;
        }
        if (symbol.address > address) {
            break;
        }

        // RISC-V mapping symbols such as $xrv64i2p1_f2p2_zicsr2p0 describe
        // instruction regions.  They are useful internally but are not the
        // human-readable labels we want to display.
        if (isMappingSymbol(symbol)) {
            continue;
        }

        if (symbol.type == STT_FUNC) {
            return &symbol;
        }
        if (fallback == nullptr) {
            fallback = &symbol;
        }
    }

    return fallback;
}

const ElfSymbol* ElfReader::findCodeSymbol(uint64_t address) const {
    const ElfSymbol* fallback = nullptr;

    for (const auto& symbol : symbolList) {
        if (symbol.address < address) {
            continue;
        }
        if (symbol.address > address) {
            break;
        }

        if (isMappingSymbol(symbol)) {
            continue;
        }

        if (symbol.type == STT_FUNC) {
            return &symbol;
        }
        if (symbol.type == STT_NOTYPE && fallback == nullptr) {
            fallback = &symbol;
        }
    }

    return fallback;
}

template <typename Ehdr, typename Shdr, typename Sym>
bool ElfReader::loadSymbolsTemplate(
    const std::vector<uint8_t>& data,
    const Ehdr* header,
    const Shdr* sections
) {
    symbolList.clear();

    for (uint16_t i = 0; i < header->e_shnum; ++i) {
        const Shdr& symSection = sections[i];

        if (symSection.sh_type != SHT_SYMTAB &&
            symSection.sh_type != SHT_DYNSYM) {
            continue;
        }

        if (symSection.sh_entsize != sizeof(Sym) ||
            symSection.sh_entsize == 0 ||
            symSection.sh_link >= header->e_shnum ||
            !rangeInside(symSection.sh_offset, symSection.sh_size, data.size())) {
            continue;
        }

        const Shdr& stringSection = sections[symSection.sh_link];
        if (!rangeInside(stringSection.sh_offset,
                         stringSection.sh_size,
                         data.size())) {
            continue;
        }

        const auto* symbols = reinterpret_cast<const Sym*>(
            data.data() + symSection.sh_offset
        );
        const char* strings = reinterpret_cast<const char*>(
            data.data() + stringSection.sh_offset
        );

        const uint64_t count = symSection.sh_size / symSection.sh_entsize;
        for (uint64_t j = 0; j < count; ++j) {
            const Sym& sym = symbols[j];
            if (sym.st_name >= stringSection.sh_size || sym.st_value == 0) {
                continue;
            }

            const char* name = strings + sym.st_name;
            const size_t remaining =
                static_cast<size_t>(stringSection.sh_size - sym.st_name);
            if (std::memchr(name, '\0', remaining) == nullptr || *name == '\0') {
                continue;
            }

            ElfSymbol result;
            result.name = name;
            result.address = sym.st_value;
            result.size = sym.st_size;
            result.type = symbolType(sym.st_info);
            result.binding = symbolBinding(sym.st_info);
            result.sectionIndex = sym.st_shndx;
            symbolList.push_back(std::move(result));
        }
    }

    std::sort(symbolList.begin(), symbolList.end(),
              [](const ElfSymbol& a, const ElfSymbol& b) {
                  if (a.address != b.address) {
                      return a.address < b.address;
                  }
                  if (a.type != b.type) {
                      return a.type == STT_FUNC;
                  }
                  return a.name < b.name;
              });

    return true;
}

template <typename Ehdr, typename Shdr, typename Sym>
bool ElfReader::loadTemplate(
    const std::vector<uint8_t>& data,
    TextSection& textSection
) {
    if (data.size() < sizeof(Ehdr)) {
        return false;
    }

    const auto* header =
        reinterpret_cast<const Ehdr*>(data.data());

    if (header->e_ident[EI_DATA] != ELFDATA2LSB ||
        header->e_machine != EM_RISCV) {
        return false;
    }

    if (header->e_shoff == 0 ||
        header->e_shentsize != sizeof(Shdr) ||
        header->e_shnum == 0) {
        return false;
    }

    const uint64_t sectionTableSize =
        static_cast<uint64_t>(header->e_shnum) * header->e_shentsize;
    if (!rangeInside(header->e_shoff, sectionTableSize, data.size())) {
        return false;
    }

    const auto* sections = reinterpret_cast<const Shdr*>(
        data.data() + header->e_shoff
    );

    if (header->e_shstrndx >= header->e_shnum) {
        return false;
    }

    const Shdr& stringSection = sections[header->e_shstrndx];
    if (!rangeInside(stringSection.sh_offset,
                     stringSection.sh_size,
                     data.size())) {
        return false;
    }

    const char* names = reinterpret_cast<const char*>(
        data.data() + stringSection.sh_offset
    );

    bool foundText = false;

    for (uint16_t i = 0; i < header->e_shnum; ++i) {
        const Shdr& section = sections[i];
        if (section.sh_name >= stringSection.sh_size) {
            continue;
        }

        const char* name = names + section.sh_name;
        const size_t remaining =
            static_cast<size_t>(stringSection.sh_size - section.sh_name);
        if (std::memchr(name, '\0', remaining) == nullptr) {
            continue;
        }

        if (std::string(name) != ".text") {
            continue;
        }

        if (!rangeInside(section.sh_offset, section.sh_size, data.size())) {
            return false;
        }

        textSection.address = section.sh_addr;
        textSection.bytes.assign(
            data.begin() + section.sh_offset,
            data.begin() + section.sh_offset + section.sh_size
        );
        foundText = true;
        break;
    }

    if (!foundText) {
        return false;
    }

    loadSymbolsTemplate<Ehdr, Shdr, Sym>(data, header, sections);

    return true;
}

bool ElfReader::load(
    const std::string& path,
    TextSection& textSection
) {
    symbolList.clear();
    textSection = {};
    is32BitClass = false;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    std::vector<uint8_t> data(
        std::istreambuf_iterator<char>(file),
        {}
    );

    if (!isElf(data)) {
        return false;
    }

    if (data.size() < EI_NIDENT) {
        return false;
    }

    const uint8_t elfClass = data[EI_CLASS];
    if (elfClass == ELFCLASS32) {
        is32BitClass = true;
        return loadTemplate<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>(data, textSection);
    } else if (elfClass == ELFCLASS64) {
        is32BitClass = false;
        return loadTemplate<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>(data, textSection);
    }

    return false;
}
