FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(unit_tests
    test_decoder.cpp
    test_opcode_database.cpp
    test_instruction_formatter.cpp
    test_instruction_fetcher.cpp
    test_elf_reader.cpp
)
target_link_libraries(unit_tests PRIVATE myobjdump_core GTest::gtest_main)

# Copy data/ next to the test binary so tests can find it
add_custom_command(TARGET unit_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:unit_tests>/data
)

# Copy ELF test files next to the test binary for ElfReader tests
set(ELF_TEST_FILES
    ${CMAKE_SOURCE_DIR}/elf_files/advanced_test.elf
    ${CMAKE_SOURCE_DIR}/elf_files/array.elf
    ${CMAKE_SOURCE_DIR}/elf_files/rv32_test.elf
    ${CMAKE_SOURCE_DIR}/elf_files/factorial.elf
)
foreach(ELF_FILE ${ELF_TEST_FILES})
    if(EXISTS ${ELF_FILE})
        add_custom_command(TARGET unit_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${ELF_FILE}
                $<TARGET_FILE_DIR:unit_tests>/
        )
    endif()
endforeach()

include(GoogleTest)
gtest_discover_tests(unit_tests WORKING_DIRECTORY $<TARGET_FILE_DIR:unit_tests>)
