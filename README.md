# myobjdump

A custom C++ RISC-V ELF disassembler. It reads ELF32 and ELF64 binaries, extracts `.text`, fetches 16/32-bit RISC-V instructions, matches them against a machine-readable opcode database, and formats output to match GNU `objdump -d`. It does not invoke `readelf` or `objdump`.

## Features

- **ELF32 + ELF64** little-endian RISC-V support
- **RV64I/M/C** base + multiply + compressed instruction decoding
- **Pseudo-instruction aliases** (`nop`, `ret`, `li`, `mv`, `j`, `seqz`, `sext.w`, etc.)
- **Symbol resolution** with `<symbol>` labels and branch/jump target comments
- **CSR name resolution** (`mstatus`, `mepc`, etc.)
- **GNU objdump-compatible output** formatting

## Build

```powershell
cmake -S . -B build
cmake --build build
```

### Build with tests

```powershell
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

If CMake's bundled curl has certificate problems on Windows/MinGW, this project sets `CMAKE_TLS_VERIFY OFF` only for the FetchContent download of nlohmann/json.

## Run

```powershell
.\build\myobjdump.exe elf_files/task.elf data/riscv_decoder.json data/riscv_pseudos.json
```

## Data files

- **`data/riscv_decoder.json`** — Generated from the official [riscv/riscv-opcodes](https://github.com/riscv/riscv-opcodes) repository by `tools/generate_decoder_db.py`. The repository includes a pre-generated snapshot; the exact instruction count depends on the database version.
- **`data/riscv_pseudos.json`** — Static rules for pseudo-instruction formatting (e.g., `jalr x0, 0(x1)` → `ret`).

## Testing

| Layer | Tool | Location |
|-------|------|----------|
| Unit tests | Google Test | `tests/unit_tests/` |
| Regression tests | LIT + FileCheck | `tests/regression_tests/` |
| Sanitizers | ASan + UBSan | CMake option `-DMYOBJDUMP_ENABLE_SANITIZERS=ON` |
| CI/CD | GitHub Actions | `.github/workflows/ci.yml` |

### Unit test coverage

| Component | Test file |
|-----------|-----------|
| `RiscVDecoder` | `test_decoder.cpp` |
| `OpcodeDatabase` | `test_opcode_database.cpp` |
| `InstructionFormatter` | `test_instruction_formatter.cpp` |
| `InstructionFetcher` | `test_instruction_fetcher.cpp` |
| `ElfReader` | `test_elf_reader.cpp` |

### Regression test coverage

| Test file | What it tests |
|-----------|--------------|
| `header.test` | ELF header and section banner |
| `rv64_w_instructions.test` | RV64 W-extension instructions |
| `pseudo_instructions.test` | Pseudo-instruction aliases |
| `compressed_branches.test` | Compressed branch/jump targets |
| `compressed_memory.test` | Compressed load/store formatting |
| `elf32_array.test` | ELF32 loading and RV32 decoding |
| `elf32_rv32.test` | ELF32 with CSR, LUI, AUIPC |
| `factorial_symbols.test` | Symbol labels, loops, MUL |

## Optional: regenerate decoder database

```powershell
python tools/generate_decoder_db.py --output data/riscv_decoder.json
```
