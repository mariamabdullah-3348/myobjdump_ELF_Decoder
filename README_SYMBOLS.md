# Symbol-aware disassembly update

This version adds ELF symbol-table loading and uses symbols during formatting.

## What changed

1. `ElfReader` now reads `.symtab`/`.dynsym` entries and their linked string table.
2. Symbols store name, address, size, type, binding, and section index.
3. Exact code symbols are printed as labels before the instruction at that address.
4. Branch/jump targets are printed as `address <symbol>` when an exact symbol exists.
5. `srli`/`slli`/`srai` and W-variant shift amounts are formatted as unsigned hexadecimal values, so `0x3f` is no longer displayed as `-1`.
6. The CSR alias `csrrw rd=x0` is displayed as `csrw`, matching GNU objdump's alias in the reference output.

## Expected examples

```text
0000000080000000 <start>:
...
0000000080000110 <user_loop>:
...
ffdff06f  j 80000110 <user_loop>
...
02639263  bne t2,t1,800001b4 <error_loop>
...
03f2d393  srli t2,t0,0x3f
...
34131073  csrw mepc,t1
```

The ELF symbol representation follows the standard ELF64 symbol fields (`st_name`, `st_info`, `st_other`, `st_shndx`, `st_value`, `st_size`). RISC-V does not redefine the basic ELF symbol-table layout. See the RISC-V ELF psABI and generic ELF documentation for the format details.

## Build and run on Windows/PowerShell

```powershell
cmake -S . -B build -G Ninja
cmake --build build
.\build\myobjdump.exe task.elf
```

Optional database arguments:

```powershell
.\build\myobjdump.exe task.elf data\riscv_decoder.json data\riscv_pseudos.json
```
