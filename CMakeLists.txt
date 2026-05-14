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

# Linux/macOS: collect system libraries required for LLVM static linking + FFI.
# LLVM_SYSTEM_LIBS (exported by the LLVM cmake config) typically contains
# -lz -lzstd -lrt -ldl -lpthread -lm on Ubuntu/Debian.
# We always add dl (dlopen for FFI) and m explicitly as a safety net.
if(NOT WIN32)
    find_package(Threads REQUIRED)
    # stdc++ is needed because LLVM is a C++ library; project(ls C) uses the C
    # linker which does not pull it in automatically on all distros.
    set(LS_UNIX_EXTRA_LIBS dl m stdc++ Threads::Threads)
    if(LLVM_SYSTEM_LIBS)
        list(APPEND LS_UNIX_EXTRA_LIBS ${LLVM_SYSTEM_LIBS})
    endif()
endif()

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
    target_link_libraries(ls ${LS_UNIX_EXTRA_LIBS})
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
    runtime/memcheck.c
)
target_include_directories(test_codegen PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_codegen ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_codegen
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_codegen ${LS_UNIX_EXTRA_LIBS})
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
    runtime/memcheck.c
)
target_include_directories(test_jit PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_jit ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_jit
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_jit ${LS_UNIX_EXTRA_LIBS})
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
    runtime/memcheck.c
)
target_include_directories(test_ffi PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_ffi ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_ffi
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_ffi ${LS_UNIX_EXTRA_LIBS})
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
    runtime/memcheck.c
)
target_include_directories(test_module PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_module ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_module
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_module ${LS_UNIX_EXTRA_LIBS})
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
    runtime/memcheck.c
)
target_include_directories(test_memory PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_memory ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_memory
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_memory ${LS_UNIX_EXTRA_LIBS})
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

# Phase E.1 end-to-end test: extern struct + extern { } block + extern fn without 'from'
add_test(
    NAME test_extern_struct
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/extern_struct_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_extern_struct.cmake
)
set_tests_properties(test_extern_struct PROPERTIES DEPENDS "test_memory")

# Phase E.2 end-to-end test: Windows x64 ABI lowering — extern struct
# returned in iN register (small) and via sret slot (large).
# Skip on Linux/macOS: System V AMD64 ABI differs from Windows x64; the
# codegen ABI-lowering path is Windows-specific.
if(WIN32)
add_test(
    NAME test_extern_struct_byval
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/extern_struct_byval.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_extern_struct_byval.cmake
)
set_tests_properties(test_extern_struct_byval PROPERTIES DEPENDS "test_extern_struct")
endif()

# Phase E.3.3: glue functions (from_cstr / to_cstr / vec.as_ptr)
add_test(
    NAME test_e3_glue
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/glue_funcs_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=glue_funcs
        -P ${CMAKE_SOURCE_DIR}/tests/test_e3_glue.cmake
)
# On Windows the full chain runs through test_extern_struct_byval.
# On Linux/macOS that test is skipped, so depend directly on test_extern_struct.
if(WIN32)
    set_tests_properties(test_e3_glue PROPERTIES DEPENDS "test_extern_struct_byval")
else()
    set_tests_properties(test_e3_glue PROPERTIES DEPENDS "test_extern_struct")
endif()

# Phase E.3.1: errno() builtin
add_test(
    NAME test_e3_errno
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/errno_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=errno
        -P ${CMAKE_SOURCE_DIR}/tests/test_e3_glue.cmake
)
set_tests_properties(test_e3_errno PROPERTIES DEPENDS "test_e3_glue")

# Phase E.3.2: conditional compilation #if WINDOWS / LINUX / MACOS
add_test(
    NAME test_e3_condcomp
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/condcomp_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=condcomp
        -P ${CMAKE_SOURCE_DIR}/tests/test_e3_glue.cmake
)
set_tests_properties(test_e3_condcomp PROPERTIES DEPENDS "test_e3_errno")

# Phase E.3.4: stdlib path resolution (LS_HOME/stdlib lookup)
add_test(
    NAME test_e3_stdlib_path
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/stdlib_path_test/main.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=stdlib_path
        -P ${CMAKE_SOURCE_DIR}/tests/test_e3_glue.cmake
)
set_tests_properties(test_e3_stdlib_path PROPERTIES DEPENDS "test_e3_condcomp")

# Phase E.4: pure-LS io stdlib (replaces builtins_io.c). Same .ls test files
# the built-in version used to pass; output must include "ALL PASS".
add_test(
    NAME test_e4_io_basic
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/io_basic_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=io_basic
        -P ${CMAKE_SOURCE_DIR}/tests/test_e3_glue.cmake
)
set_tests_properties(test_e4_io_basic PROPERTIES DEPENDS "test_e3_stdlib_path")

add_test(
    NAME test_e4_io_seek
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/io_seek_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=io_seek
        -P ${CMAKE_SOURCE_DIR}/tests/test_e3_glue.cmake
)
set_tests_properties(test_e4_io_seek PROPERTIES DEPENDS "test_e4_io_basic")

# Phase A (closure prerequisite): type alias + Block keyword + closure literal
# parsing + return/field-position rejection. No closure codegen yet; the test
# verifies the parser/checker contract documented in docs/closures_plan.md.
add_test(
    NAME test_phase_a_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_a_closure.cmake
)
set_tests_properties(test_phase_a_closure PROPERTIES DEPENDS "test_e4_io_seek")

# Phase B (closure codegen, no captures): synthesised __closure_N + indirect
# call through {fn_ptr, env_ptr} fat pointer. AOT + JIT verified.
add_test(
    NAME test_phase_b_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_b_closure.cmake
)
set_tests_properties(test_phase_b_closure PROPERTIES DEPENDS "test_phase_a_closure")

# Phase C (closure POD captures + heap env + RAII): make_adder pattern works,
# multiple captures, mixed POD types, env freed at scope cleanup.
# Memcheck verifies 0 leaks.
add_test(
    NAME test_phase_c_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_c_closure.cmake
)
set_tests_properties(test_phase_c_closure PROPERTIES DEPENDS "test_phase_b_closure")

# Phase C.5 (string by-move captures + per-closure env_drop + Block-param
# borrowing): make_greeter pattern with owned + static + mixed string +
# POD captures. Memcheck verifies 0 leaks / 0 double-free.
add_test(
    NAME test_phase_c5_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_c5_closure.cmake
)
set_tests_properties(test_phase_c5_closure PROPERTIES DEPENDS "test_phase_c_closure")

# Phase C.7 (vec/map/struct(drop) captures): borrow semantics for vec/map
# (env doesn't free), ownership transfer for struct(drop) (env owns,
# caller marked moved via i1 flag). Memcheck verifies 0 leaks / 0 dfree.
add_test(
    NAME test_phase_c7_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_c7_closure.cmake
)
set_tests_properties(test_phase_c7_closure PROPERTIES DEPENDS "test_phase_c5_closure")

# Phase E.1 (by-ref capture: vec/map captures store pointer to outer alloca;
# mutations after capture visible in closure body; no double-free / no clone leak).
add_test(
    NAME test_phase_e1_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_e1_closure.cmake
)
set_tests_properties(test_phase_e1_closure PROPERTIES DEPENDS "test_phase_c7_closure")

# Phase E.2 + E.4 (closure param borrowed for vec/map; array(POD,N) by-value capture).
add_test(
    NAME test_phase_e2_e4_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_e2_e4_closure.cmake
)
set_tests_properties(test_phase_e2_e4_closure PROPERTIES DEPENDS "test_phase_e1_closure")

# Phase F.1 ([move v] capture spec + vec/map by-move): factory pattern solves
# dangling-pointer issue; [move nums] captures vec by-value (owns data in env).
add_test(
    NAME test_phase_f1_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_f1_closure.cmake
)
set_tests_properties(test_phase_f1_closure PROPERTIES DEPENDS "test_phase_e2_e4_closure")

add_test(
    NAME test_phase_f2_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_f2_closure.cmake
)
set_tests_properties(test_phase_f2_closure PROPERTIES DEPENDS "test_phase_f1_closure")

add_test(
    NAME test_phase_f3_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_f3_closure.cmake
)
set_tests_properties(test_phase_f3_closure PROPERTIES DEPENDS "test_phase_f2_closure")

add_test(
    NAME test_phase_f4_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_f4_closure.cmake
)
set_tests_properties(test_phase_f4_closure PROPERTIES DEPENDS "test_phase_f3_closure")

add_test(
    NAME test_phase_f5_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_f5_closure.cmake
)
set_tests_properties(test_phase_f5_closure PROPERTIES DEPENDS "test_phase_f4_closure")

add_test(
    NAME test_phase_f7_stress
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_f7_stress.cmake
)
set_tests_properties(test_phase_f7_stress PROPERTIES DEPENDS "test_phase_f5_closure")
