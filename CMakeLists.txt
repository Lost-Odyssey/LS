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
    # -rdynamic: without it, ELF executables don't export their own symbols to
    # their own dynamic symbol table, so LLJIT's process-symbol resolver can't
    # find builtins (e.g. ls_mc_ensure_report from runtime/memcheck.c) compiled
    # into the same binary — "Symbols not found" at JIT materialization time.
    # Windows has no equivalent restriction, which is why this never surfaced there.
    set(LS_UNIX_EXTRA_LIBS dl m stdc++ Threads::Threads -rdynamic)
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

# Opt-in tracking of the COMPILER's own heap (distinct from `ls run --memcheck`,
# which instruments the generated program). common.h macro-redirects the
# compiler's malloc/free to a tracker that dumps unfreed blocks at exit. Measure
# with `ls parse`/`ls check` to isolate frontend leaks from LLVM. See leakcheck.h.
option(LS_LEAKCHECK "Track the compiler's own heap and report leaks at exit (developer only)" OFF)

# Compiler options
if(MSVC)
    # Use dynamic CRT (/MD) so that ls.exe and LLJIT-compiled code share the
    # same ucrtbase.dll instance.  With /MT the JIT resolves fopen from the
    # dynamic CRT while ls_os_ftell64 calls the static CRT's _ftelli64 —
    # mixing FILE* across CRT instances causes STATUS_STACK_BUFFER_OVERRUN.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
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

# Platform-specific OS backend (defines ls_os_* symbols used by std.* modules)
if(WIN32)
    set(LS_OS_SOURCE runtime/os_win32.c)
else()
    set(LS_OS_SOURCE runtime/os_posix.c)
endif()

# parser translation units (split per Task 4.3,
# docs/plan_arch_round2_backlog.md Batch 4). Listed once so every target
# referencing ${LS_PARSER_SOURCES} picks up new TUs.
set(LS_PARSER_SOURCES
    src/parser.c
    src/parser_expr.c
)

# checker translation units (split per docs/plan_checker_split.md). Listed once
# so every target referencing ${LS_CHECKER_SOURCES} picks up new TUs.
#
# src/mangle.c lives here (rather than its own list) because every test
# target that pulls in ${LS_CHECKER_SOURCES} needs it (checker.c calls
# mangle_module_symbol), and every target that pulls in
# ${LS_CODEGEN_SOURCES} also pulls in ${LS_CHECKER_SOURCES} (grep
# tests/tests.cmake before changing this — Task 2.1 / plan_arch_round2_backlog.md).
set(LS_CHECKER_SOURCES
    src/mangle.c
    src/checker.c
    src/checker_borrow.c
    src/checker_lower.c
    src/checker_decl.c
    src/checker_elide.c
    src/checker_comptime.c
    src/checker_derive.c
)

# codegen translation units (split per docs/plan_codegen_split.md).
# Listed once here so every target that compiles codegen picks up new TUs
# by referencing ${LS_CODEGEN_SOURCES} instead of repeating the file list.
set(LS_CODEGEN_SOURCES
    src/codegen.c
    src/codegen_own.c
    src/codegen_match.c
    src/codegen_decl.c
    src/codegen_stmt.c
    src/codegen_closure.c
    src/codegen_expr.c
    src/codegen_noalias.c
    src/codegen_di.c
    src/codegen_lifetime.c
    src/optpipe.c
)

# Main executable sources
set(LS_SOURCES
    src/main.c
    src/test_driver.c
    src/link_driver.c
    src/leakcheck.c
    src/scanner.c
    src/format.c
    src/doc_assets.c
    src/emit_c.c
    src/ast.c
    ${LS_PARSER_SOURCES}
    src/diag.c
    src/types.c
    src/symtable.c
    ${LS_CHECKER_SOURCES}
    ${LS_CODEGEN_SOURCES}
    src/debug.c
    src/jit.c
    src/repl_edit.c
    src/repl_term.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_perf.c
    src/builtins_perf_cg.c
    src/builtins_intrinsic_cg.c
    runtime/memcheck.c
    runtime/profiler.c
    runtime/builtins.c
    runtime/ls_regex.c
    ${LS_OS_SOURCE}
)

add_executable(ls ${LS_SOURCES})
if(LS_LEAKCHECK)
    target_compile_definitions(ls PRIVATE LS_LEAKCHECK)
    message(STATUS "LS_LEAKCHECK=ON (ls.exe tracks its own heap; reports leaks at exit)")
endif()
target_include_directories(ls PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(ls ${LLVM_LIBS})

# Emit the binary as `lls` (LS = Low-level Script); bare `ls` collides with the
# Unix list-directory command. The CMake target keeps the short name `ls`, so
# existing $<TARGET_FILE:ls> test references need no change.
set_target_properties(ls PROPERTIES OUTPUT_NAME lls)

# Keep the lib/std/ directory in the output folder up-to-date with the source tree.
# CMake's file(COPY) only runs at configure time; POST_BUILD runs every build.
# We copy the whole lib/ tree so ls.exe can find lib/std/*.lls when LS_HOME is not set.
add_custom_command(TARGET ls POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/lib"
        "$<TARGET_FILE_DIR:ls>/lib"
    COMMENT "Copying lib/ -> output/lib/"
)

# ls_memcheck — standalone static library for AOT-compiled user programs.
# ls.exe still links runtime/memcheck.c directly (above) so JIT can resolve
# ls_mc_* symbols from within the host process. This separate archive is
# what `ls compile --memcheck` passes to clang when linking the user binary.
add_library(ls_memcheck STATIC runtime/memcheck.c)
target_include_directories(ls_memcheck PRIVATE src/ include/)

# Make ls_memcheck.lib end up next to ls.exe so link_driver.c can locate it
# via module_executable_dir() at runtime (works for both single-config Ninja
# and multi-config Visual Studio generators).
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

# ls_profiler — standalone static library for AOT-compiled user programs.
# Contains profiler.c + a minimal perf_now implementation so the user binary
# can resolve ls_prof_enter/leave/report + ls_os_perf_now without ls.exe.
add_library(ls_profiler STATIC runtime/profiler.c runtime/perf_now.c)
target_include_directories(ls_profiler PRIVATE src/ include/)
set_target_properties(ls_profiler PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY            "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG      "${CMAKE_BINARY_DIR}/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE    "${CMAKE_BINARY_DIR}/Release"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo"
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel"
)
add_dependencies(ls ls_profiler)

# ls_os_backend — standalone static library for AOT-compiled user programs.
# Contains os_win32.c or os_posix.c (defines ls_os_* symbols used by std.os /
# std.time). ls.exe already links the same file via LS_SOURCES; this archive is
# passed unconditionally by `ls compile` so any AOT binary can import std.time.
add_library(ls_os_backend STATIC ${LS_OS_SOURCE} runtime/builtins.c runtime/ls_regex.c)
target_include_directories(ls_os_backend PRIVATE src/ include/)
set_target_properties(ls_os_backend PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY            "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG      "${CMAKE_BINARY_DIR}/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE    "${CMAKE_BINARY_DIR}/Release"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo"
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel"
)
add_dependencies(ls ls_os_backend)

# Windows: extra system libs needed for static LLVM linking
if(WIN32)
    target_link_libraries(ls
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(ls ${LS_UNIX_EXTRA_LIBS})
endif()

# Tests — all test executables + ctest registration live in tests/tests.cmake
# (plain include(): same variable scope, same directory context; see the header
# comment in that file before moving anything back or switching to
# add_subdirectory).
include(${CMAKE_SOURCE_DIR}/tests/tests.cmake)
