cmake_minimum_required(VERSION 3.20)
project(ls C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# LLVM — static linking
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(LLVM_LIBS
    core support irreader
    executionengine orcjit native
    passes target mc
    x86asmparser x86codegen x86desc x86info
)

# CG_DEBUG: opt-in compile-time flag that bakes [cg] str.free / scope.drop /
# vec.grow / etc. printfs into the IR of every compiled program. Useful for
# debugging codegen-internal logic (move tracking, scope cleanup decisions,
# clone paths). For ordinary leak / double-free hunting, prefer
# `--memcheck` + `LS_MEMCHECK_VERBOSE=1` (Phase D.2) — same coverage of
# heap operations with richer site info and no impact on AOT user binaries.
#
# Default OFF so AOT-compiled user programs don't ship with [cg] noise.
option(LS_CG_DEBUG "Inject codegen-internal memory tracing into user IR (developer only)" OFF)
if(LS_CG_DEBUG)
    add_compile_definitions(CG_DEBUG=1)
    message(STATUS "CG_DEBUG=1 (codegen tracing enabled — user binaries will print [cg] events)")
endif()

# Compiler options
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /utf-8)
    # Don't treat warnings as errors for now (LLVM headers may trigger warnings)
    # add_compile_options(/WX)
else()
    add_compile_options(-Wall -Wextra -pedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-fsanitize=address -g -O0)
        add_link_options(-fsanitize=address)
    else()
        add_compile_options(-O2)
    endif()
endif()

# Main executable sources
set(LS_SOURCES
    src/main.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/jit.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)

add_executable(ls ${LS_SOURCES})
target_include_directories(ls PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(ls ${LLVM_LIBS})

# ls_memcheck — standalone static library for AOT-compiled user programs.
# ls.exe still links runtime/memcheck.c directly (above) so JIT can resolve
# ls_mc_* symbols from within the host process. This separate archive is
# what `ls compile --memcheck` passes to clang when linking the user binary.
add_library(ls_memcheck STATIC runtime/memcheck.c)
target_include_directories(ls_memcheck PRIVATE src/ include/)

# Make ls_memcheck.lib end up next to ls.exe so main.c can locate it via
# get_executable_dir() at runtime (works for both single-config Ninja and
# multi-config Visual Studio generators).
set_target_properties(ls_memcheck PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY            "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG      "${CMAKE_BINARY_DIR}/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE    "${CMAKE_BINARY_DIR}/Release"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo"
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel"
)
# Force ls.exe to depend on ls_memcheck so the static archive is always built
# alongside the compiler — AOT --memcheck would fail otherwise.
add_dependencies(ls ls_memcheck)

# Windows: extra system libs needed for static LLVM linking
if(WIN32)
    target_link_libraries(ls
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    # Linux/macOS: dlopen/dlsym for FFI
    target_link_libraries(ls dl)
endif()

# Tests (Phase 1 & 2: no LLVM dependency)
enable_testing()

add_executable(test_scanner tests/test_scanner.c src/scanner.c)
target_include_directories(test_scanner PRIVATE src/ include/)
add_test(NAME test_scanner COMMAND test_scanner)

add_executable(test_parser
    tests/test_parser.c
    src/scanner.c
    src/ast.c
    src/parser.c
)
target_include_directories(test_parser PRIVATE src/ include/)
add_test(NAME test_parser COMMAND test_parser)

add_executable(test_types
    tests/test_types.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/module.c
    src/builtins_math.c
    src/builtins_io.c
)
target_include_directories(test_types PRIVATE src/ include/)
add_test(NAME test_types COMMAND test_types)

# Phase 4 test: codegen (needs LLVM)
add_executable(test_codegen
    tests/test_codegen.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)
target_include_directories(test_codegen PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_codegen ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_codegen
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_codegen dl)
endif()
add_test(NAME test_codegen COMMAND test_codegen)

# Phase 5 test: JIT (needs LLVM)
add_executable(test_jit
    tests/test_jit.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/jit.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)
target_include_directories(test_jit PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_jit ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_jit
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_jit dl)
endif()
add_test(NAME test_jit COMMAND test_jit)

# Phase 6 test: FFI (needs LLVM)
add_executable(test_ffi
    tests/test_ffi.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/jit.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)
target_include_directories(test_ffi PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_ffi ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_ffi
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_ffi dl)
endif()
add_test(NAME test_ffi COMMAND test_ffi)

# Phase 7 test: Module system (needs LLVM)
add_executable(test_module
    tests/test_module.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/jit.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)
target_include_directories(test_module PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_module ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_module
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_module dl)
endif()
add_test(NAME test_module COMMAND test_module)
set_tests_properties(test_module PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Memory management E2E tests (needs LLVM + JIT)
add_executable(test_memory
    tests/test_memory.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/jit.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)
target_include_directories(test_memory PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_memory ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_memory
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_memory dl)
endif()
add_test(NAME test_memory COMMAND test_memory)

# AOT memcheck end-to-end test: compile a sample with `ls compile --memcheck`,
# run the produced binary, assert the runtime report shows "OK clean".
# Driven by a cmake -P script for cross-platform process invocation.
add_test(
    NAME test_memcheck_aot
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/memcheck_phase_a.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_aot.cmake
)
# Order this after test_memory so the JIT memcheck path is already validated.
set_tests_properties(test_memcheck_aot PROPERTIES DEPENDS "test_memory")
