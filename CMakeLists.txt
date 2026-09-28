cmake_minimum_required(VERSION 3.15)
project(myobjdump VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(MYOBJDUMP_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(MYOBJDUMP_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "MYOBJDUMP_ENABLE_SANITIZERS is not supported with MSVC")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

option(BUILD_TESTS "Build unit tests" OFF)

set(CMAKE_TLS_VERIFY OFF)
include(FetchContent)

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# ---------------------------------------------------------------------------
# Core library — shared between the main executable and the test executable
# ---------------------------------------------------------------------------
add_library(myobjdump_core STATIC
    src/elf_reader.cpp
    src/riscv/instruction_fetcher.cpp
    src/riscv/opcode_database.cpp
    src/riscv/decoder.cpp
    src/riscv/instruction_formatter.cpp
)
target_include_directories(myobjdump_core PUBLIC src)
target_link_libraries(myobjdump_core PUBLIC nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(myobjdump src/main.cpp)
target_link_libraries(myobjdump PRIVATE myobjdump_core)

# ---------------------------------------------------------------------------
# Testing
# ---------------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit_tests)
endif()
