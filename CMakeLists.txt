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
    src/repl_edit.c
    src/repl_term.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_perf.c
    src/builtins_perf_cg.c
    runtime/memcheck.c
    runtime/profiler.c
    runtime/builtins.c
    runtime/ls_regex.c
    ${LS_OS_SOURCE}
)

add_executable(ls ${LS_SOURCES})
target_include_directories(ls PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(ls ${LLVM_LIBS})

# Keep the lib/std/ directory in the output folder up-to-date with the source tree.
# CMake's file(COPY) only runs at configure time; POST_BUILD runs every build.
# We copy the whole lib/ tree so ls.exe can find lib/std/*.ls when LS_HOME is not set.
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
    src/types.c
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
    src/builtins_perf.c
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
    src/builtins_perf.c
    src/builtins_perf_cg.c
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
    src/repl_edit.c
    src/repl_term.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_perf.c
    src/builtins_perf_cg.c
    runtime/memcheck.c
    runtime/profiler.c
    runtime/builtins.c
    runtime/ls_regex.c
    ${LS_OS_SOURCE}
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
    src/repl_edit.c
    src/repl_term.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_perf.c
    src/builtins_perf_cg.c
    runtime/memcheck.c
    runtime/profiler.c
    runtime/builtins.c
    runtime/ls_regex.c
    ${LS_OS_SOURCE}
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
    src/repl_edit.c
    src/repl_term.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_perf.c
    src/builtins_perf_cg.c
    runtime/memcheck.c
    runtime/profiler.c
    runtime/builtins.c
    runtime/ls_regex.c
    ${LS_OS_SOURCE}
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
    src/repl_edit.c
    src/repl_term.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_perf.c
    src/builtins_perf_cg.c
    runtime/memcheck.c
    runtime/profiler.c
    runtime/builtins.c
    runtime/ls_regex.c
    ${LS_OS_SOURCE}
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

# REPL helper unit tests (classification / completeness / highlighting).
# Pure logic over the scanner — no LLVM needed.
add_executable(test_repl tests/test_repl.c src/scanner.c src/repl_edit.c src/repl_term.c)
target_include_directories(test_repl PRIVATE src/)
add_test(NAME test_repl COMMAND test_repl)

# REPL import-persistence E2E: pipe `import math` + a math call into `ls repl`
# (non-TTY → fgets fallback) and assert the result is computed.
add_test(
    NAME test_repl_import
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/samples/repl_import.in
        -DEXPECT=4
        -P ${CMAKE_SOURCE_DIR}/tests/test_repl_pipe.cmake
)

# std.md (Markdown writer) Phase A: build a doc with every builder + render,
# under JIT memcheck — asserts the run is leak/double-free clean.
add_test(
    NAME test_std_md_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/md_build.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=std_md
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)

# std.md Phase B (parser): parse a Markdown doc into MdDoc + round-trip render,
# under JIT memcheck.
add_test(
    NAME test_std_md_parse_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/md_parse.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=std_md_parse
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)

# std.md Phase C (inline parsing + extract helpers).
add_test(
    NAME test_std_md_inline_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/md_inline.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=std_md_inline
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)

# std.html Phase H1 (generation + render): functional bottom-up construction,
# escaping, void tags, fmt_tag. JIT + AOT correctness + memcheck.
add_test(
    NAME test_std_html_write
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/html_write.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=std_html_write
        -P ${CMAKE_SOURCE_DIR}/tests/test_std_html.cmake
)

# std.html Phase H2 (parsing): recursive-descent tolerant parser, attributes
# (quoted/single/unquoted/boolean), void/self-closing, comments, DOCTYPE,
# <script>/<style> raw text, entity decode (named + numeric), round-trip, and
# query helpers (to_text / extract_links / find_by_tag / get_attr).
# JIT + AOT correctness + memcheck. Reuses the std.html driver ("HTML PASS").
add_test(
    NAME test_std_html_parse
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/html_parse.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=std_html_parse
        -P ${CMAKE_SOURCE_DIR}/tests/test_std_html.cmake
)

# std.md Phase H3 (Markdown -> HTML): md.to_html / to_html_full map MdBlock /
# MdInline to HTML (headings, paragraphs, bold/italic/code, links, lists, code
# blocks, hr, escaping, full-document wrapper). Lives in std.md (no std.html
# dependency). JIT + AOT correctness + memcheck. Reuses the std.html driver.
add_test(
    NAME test_md_to_html
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/md_to_html.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=md_to_html
        -P ${CMAKE_SOURCE_DIR}/tests/test_std_html.cmake
)

# std/plotfmt.ls (plot infra Phase 0.2): pure-LS formatting helpers
# (fmt_fixed/auto/sci/time, pad, clamp, rgb/hsv -> hex). Self-verifying sample
# prints "PLOTFMT PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_plotfmt
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plotfmt_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plotfmt
        -DMARKER=PLOTFMT
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# f-string format specifiers (plot infra Phase 0.3): {expr:.2f} / {n:03d} etc.
# scanner+parser+ast+codegen; runtime unchanged. Self-verifying sample prints
# "FSPEC PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_fstring_spec
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/fstring_spec_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=fstring_spec
        -DMARKER=FSPEC
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# Identifier predicate/unsafe suffixes: `name?` and `name!`, while preserving
# `!=` and prefix `!expr`. Self-verifying sample prints "IDENTSUF PASS".
add_test(
    NAME test_ident_suffix
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/ident_suffix_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=ident_suffix
        -DMARKER=IDENTSUF
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

add_test(
    NAME test_force_unwrap
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/force_unwrap_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=force_unwrap
        -DMARKER=FORCEUNWRAP
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# V1 bit-pattern matching (features/bit_pattern_match.md): MSB-first field
# extraction + binding, match-value arms, OR-pattern, wildcard, bool 1-bit field,
# int/u16/u8/u64 subjects. JIT + AOT correctness + memcheck 0/0/0.
add_test(
    NAME test_bit_match
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/bit_match_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=bit_match
        -DMARKER=BITMATCH
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)
# Bit-pattern negative checks: strict total-width mismatch and non-integer
# subject must be rejected at compile time.
add_test(
    NAME test_bit_match_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_bit_match_reject.cmake
)

# L-013 match-result-ownership: a match yielding an owned heap payload (string /
# Vec / has_drop struct·enum) has exactly one owner across every consumer
# (var-decl / call-arg / return / nested / discard). JIT+AOT correctness +
# memcheck 0/0/0 (docs/plan_match_result_ownership.md §8).
add_test(
    NAME test_match_result_own
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/match_result_own_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=match_result_own
        -DMARKER=MATCHOWN
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib P1: a string LITERAL in a `Str`-expecting position lowers to a
# static Str {data:.rodata, len, cap:0} (no malloc). Since P5-2 the literal
# default IS Str; this pins the static-Str lowering across all positions
# (var-decl / param / return / struct field / Vec element). JIT+AOT+
# memcheck 0/0/0 (docs/plan_string_to_stdlib.md §5.1).
add_test(
    NAME test_str_p1
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_p1_litcoerce.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_p1
        -DMARKER=STRP1
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib P2: an f-string with interpolations produces an OWNED Str
# rvalue (cap>0, the formatted heap buffer wrapped as Str, zero-copy), routed
# through the unified has_drop temp/drop path. Since P5-2 the f-string default
# IS Str. JIT+AOT+memcheck 0/0/0 (docs/plan_string_to_stdlib.md §5.2).
add_test(
    NAME test_str_p2
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_p2_fstring.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_p2
        -DMARKER=STRP2
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib P3: `print` accepts a `Str` and writes its raw text (printf
# "%.*s", length-bounded). Owned Str rvalues passed to print are dropped clean.
# v1; the @print sigil + Printable trait are deferred (v2). JIT+AOT+memcheck
# 0/0/0 (docs/plan_string_to_stdlib.md §5.3).
add_test(
    NAME test_str_p3
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_p3_print.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_p3
        -DMARKER=STRP3
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib: common builtin-string methods reimplemented as pure-LS
# `impl Str` over the byte buffer (find/contains?/starts_with?/ends_with?/substr/
# upper/lower/trim/concat/repeat). Prerequisite to P5 (removing builtin string).
# Pure LS, no compiler change. JIT+AOT+memcheck 0/0/0.
add_test(
    NAME test_str_methods
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_methods.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_methods
        -DMARKER=STRM
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib: Str method-port batch 2 — rfind/count/compare/replace/
# pad_left/pad_right + collection returners bytes/split/lines (Vec(Str)/Vec(int)).
# Pure LS, no compiler change. JIT+AOT+memcheck 0/0/0.
add_test(
    NAME test_str_methods2
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_methods2.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_methods2
        -DMARKER=STRM2
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib: Str method-port batch 3 — parsing to_int/to_i64/to_float/
# to_bool returning Result(T, Str). Also exercises string-literal -> Str coercion
# in enum-payload position (Err("msg")). JIT+AOT+memcheck 0/0/0.
add_test(
    NAME test_str_methods3
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_methods3.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_methods3
        -DMARKER=STRM3
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# strbench P0/P1/P2 optimization regression — the runtime-accelerated methods
# (find/contains?/count via __ls_str_find, replace single-byte fast-path,
# substr/concat/__clone/+/repeat via __ls_bytecopy, split_view zero-copy views)
# must stay byte-for-byte correct. JIT+AOT+memcheck 0/0/0.
add_test(
    NAME test_str_perf_methods
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_perf_methods.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_perf_methods
        -DMARKER=STRPERF
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib gap①: a string literal passed where a read-only `&Str` is
# expected coerces to a static Str and auto-borrows (method-arg + free-fn-arg).
# JIT+AOT+memcheck 0/0/0 (docs/plan_string_to_stdlib.md §5.1).
add_test(
    NAME test_str_lit_borrow
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_lit_borrow.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_lit_borrow
        -DMARKER=STRLB
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib: f-string interpolation of `Str` values — `f"...{str}..."`
# formats via "%.*s" (length-bounded). Borrowed interps not dropped; owned Str
# rvalue interps dropped after the result is built. Both the f-string-producing
# path and the inline print() path. JIT+AOT+memcheck 0/0/0.
add_test(
    NAME test_str_fstring_interp
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_fstring_interp.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_fstring_interp
        -DMARKER=STRFI
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib P7-mig: impl Hash for Str (std/str.ls, FxHash via std.hash)
# + operator == (impl Eq for Str) => Str usable as Map key (Hash + Eq bounds);
# covers set/get/grow/overwrite/remove with has_drop Str keys. 0/0/0.
add_test(
    NAME test_str_hash_mapkey
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_hash_mapkey.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_hash_mapkey
        -DMARKER=STRHK
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# P5-0 (docs/plan_p5_remove_builtin_string.md §4): Str.c_str() — NUL-terminated
# view for FFI char* consumers, verified through CRT strlen in every state
# (static literal zero-copy / owned / grown / empty / zero-init nil data).
# Unfreezes std/c.ls + std/os.ls migration (P5-3). JIT+AOT+memcheck 0/0/0.
add_test(
    NAME test_str_cstr
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_cstr_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_cstr
        -DMARKER=CSTR
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# Regression: owned Str cloned inside a MODULE function (by-value Map.set arg).
# Guards the has_user_clone forward-declaration in emit_struct_clone_val —
# without it the clone fell back to field-wise shallow copy of the raw *u8
# buffer => double-free (found migrating std/env to Str). 0/0/0.
add_test(
    NAME test_str_modfn_clone
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_modfn_clone/main.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_modfn_clone
        -DMARKER=STRMC
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# C1: Option/Result combinators (unwrap/expect/unwrap_or/is_some?/is_none?/
# is_ok?/is_err?), compiler-lowered like try/force-unwrap. Positive JIT+AOT+
# memcheck + negative unwrap-None-abort + use-after-move compile reject
# (docs/plan_container_access_safety.md §5.3).
add_test(
    NAME test_opt_combinator
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_opt_combinator.cmake
)
set_tests_properties(test_opt_combinator PROPERTIES
    DEPENDS "test_force_unwrap"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Vec(T).sort / sort_by — O(n log n) stable merge sort (bugs/27_vec.txt #2):
# larger n, explicit stability, has_drop elements, degenerate sizes.
add_test(
    NAME test_vec_sort
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/vec_sort_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=vec_sort
        -DMARKER=VECSORT
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# A-1 (docs/plan_runtime_primitives.md): std.c.{malloc,realloc,free,abort}
# reachable by canonical path, incl. from generic method bodies instantiated
# at the consumer site. JIT + AOT + memcheck.
add_test(
    NAME test_stdc_prim
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/stdc_prim_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=stdc_prim
        -DMARKER=STDCPRIM
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)
set_tests_properties(test_ident_suffix PROPERTIES DEPENDS "test_fstring_spec")

# std/plot.ls (plot Phase 1 skeleton): data model + builder API + summary output.
# Exercises deeply-nested has_drop (Figure -> vec(Axes) -> vec(LineStyle) ->
# vec(f64)), Axes MOVE into Figure, and deep-copy reads. Self-verifying sample
# prints "PLOT PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_plot_skeleton
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_skeleton_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_skeleton
        -DMARKER=PLOT
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plot.ls tick engine (plot Phase 2): standard Heckbert nice-numbers,
# generate_ticks, map_x/map_y, update_limits, finalize (margins + ticks).
# Self-verifying sample prints "TICKS PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_plot_ticks
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_ticks_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_ticks
        -DMARKER=TICKS
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plot.ls SVG backend (plot Phase 2b): layout, polyline, axes, ticks, grid,
# title/labels (escaped), scatter circles. Self-verifying sample prints
# "SVG PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_plot_svg
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_svg_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_svg
        -DMARKER=SVG
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plot.ls Text/ASCII backend (plot Phase 2c): vec(string) grid, _put_char,
# DDA line rasterization, y/x labels + axes. Self-verifying sample prints
# "TEXT PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_plot_text
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_text_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_text
        -DMARKER=TEXT
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plottl.ls timeline (plot TL-1): swimlane SVG (rects + <title> + time
# ticks) and text backend. Self-verifying sample prints "TL PASS".
# JIT + AOT correctness + memcheck.
add_test(
    NAME test_plot_timeline
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_timeline_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_timeline
        -DMARKER=TL
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plottl.ls CSV input (plot TL-1.5): parse_timeline_csv (header/blank skip,
# auto palette color), load_timeline_csv. Self-verifying sample prints
# "CSV PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_plot_csv
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_csv_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_csv
        -DMARKER=CSV
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plottl.ls CPU timeline (plot TL-2): CpuSchedEvent/CpuTopology, HT coloring
# (cpu_hue/cpu_color via plotfmt.hsv_to_hex), SVG swimlanes + diagonal-stripe
# HT patterns + CPU legend, text backend. Self-verifying sample prints
# "TL2 PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_plot_cpu
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_cpu_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_cpu
        -DMARKER=TL2
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plottl.ls scrollable HTML wrapper (plot TL-2b): cpu_timeline_html emits a
# self-contained single-file HTML with a fixed lane column + horizontally
# scrollable wide SVG (pure CSS overflow, zero JS). Self-verifying sample prints
# "HTML PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_plot_html
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_html_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_html
        -DMARKER=HTML
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plottl.ls aggregated CPU timeline (plot TL-3): cpu_timeline_aggregated
# buckets time into windows and renders the per-(thread,window) dominant CPU.
# Self-verifying sample prints "AGG PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_plot_agg
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_agg_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_agg
        -DMARKER=AGG
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# B-4 cross-module struct literal `mod.Type{...}` + struct field defaults
# (options-struct pattern). main.ls imports opt.ls and constructs opt.PlotOpts{}.
# Self-verifying sample prints "MSL PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_mod_struct_literal
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/modstructlit/main.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=mod_struct_literal
        -DMARKER=MSL
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# struct field defaults + partial initialization (v1: literal defaults). Omitted
# fields fall back to their declared default; has_drop struct (string default +
# explicit vec field) stays memcheck-clean. Self-verifying sample prints
# "SFDEF PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_struct_field_defaults
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/struct_field_defaults_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=struct_field_defaults
        -DMARKER=SFDEF
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# struct field defaults v2: empty/literal vec defaults (vec(T) x = [] / [1,2,3])
# built in place at the construction site, and nested struct defaults (Sub{}).
# Self-verifying sample prints "SFD2 PASS". JIT + AOT correctness + memcheck.
add_test(
    NAME test_struct_field_defaults_v2
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/struct_field_defaults_v2_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=struct_field_defaults_v2
        -DMARKER=SFD2
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# VR-LIM-012: user Vec(T) struct field defaults use __from_list / zero init.
add_test(
    NAME test_struct_field_defaults_uservec
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/struct_field_defaults_uservec_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=struct_field_defaults_uservec
        -DMARKER=USERVEC_FIELD_DEFAULTS
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)
set_tests_properties(test_struct_field_defaults_uservec PROPERTIES
    DEPENDS "test_struct_field_defaults_v2")

# function positional default parameters (档1): fn f(x, y="x", z=1); trailing
# params with literal/struct defaults may be omitted at the call site (checker
# appends cloned default exprs). Self-verifying sample prints "FNDEF PASS".
# JIT + AOT correctness + memcheck.
add_test(
    NAME test_fn_default_params
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/fn_default_params_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=fn_default_params
        -DMARKER=FNDEF
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# map.keys() / map.values() iteration + bound vec result. Regression for the
# silent zero-iteration bug. JIT + AOT correctness + memcheck.
add_test(
    NAME test_map_keys
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/map_keys.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=map_keys
        -P ${CMAKE_SOURCE_DIR}/tests/test_map_keys.cmake
)

# Short-circuit (&& / ||) temp-flush: a string temp in a short-circuit operand
# whose result is assigned to a bool/POD var must be freed (regression for the
# bool-assign path that reset the temp count without freeing). JIT + AOT + memcheck.
add_test(
    NAME test_shortcircuit_temp
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/shortcircuit_temp.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=shortcircuit_temp
        -P ${CMAKE_SOURCE_DIR}/tests/test_shortcircuit_temp.cmake
)

# Container value-semantics matrix (vec/map first-class — Phase 0 safety net).
# Baseline-clean cases, locked under JIT memcheck. Broken target cases (D/F) are
# tracked in tests/samples/cmatrix/MATRIX.md and registered as they turn green.
foreach(cm
        b01_vec_scope b02_vec_nested_get b03_vec_rvalue_arg
        b04_struct_vec_drop b05_enum_vec b06_map_scope t04_map_vec_value
        t03_enum_nested_vec
        t01_struct_field_push t02_struct_field_index t05_struct_map t06_field_assign
        t07_match_owned_temp t08_match_return_call)
    add_test(
        NAME test_cmatrix_${cm}
        COMMAND ${CMAKE_COMMAND}
            -DLS_EXE=$<TARGET_FILE:ls>
            -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/cmatrix/${cm}.ls
            -DWORK_DIR=${CMAKE_BINARY_DIR}
            -DTEST_NAME=cmatrix_${cm}
            -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
    )
endforeach()

# Move-elision (Q4): owned movable sources are moved (not cloned) into a new
# var / assignment when the checker confirms ownership transfer. Asserts JIT +
# AOT value correctness and JIT memcheck cleanliness in one driver.
add_test(
    NAME test_move_elision
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/cmatrix/me01_move_elision.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=move_elision
        -P ${CMAKE_SOURCE_DIR}/tests/test_move_elision.cmake
)

# Phase G (Block env deep-clone, resolves L-007): copying a Block out of a vec
# element / struct field / map value deep-clones the closure env so the
# destination owns an independent one. Asserts JIT + AOT correctness + memcheck.
add_test(
    NAME test_phase_g_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/closure_g.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=phase_g_closure
        -P ${CMAKE_SOURCE_DIR}/tests/test_phase_g_closure.cmake
)

# By-value has_drop struct argument ownership: passing a struct whose fields own
# heap (vec/map/string/nested struct) by value deep-clones at the call site so
# the callee owns an independent copy — no shared-buffer double-free between
# callee scope_drop and caller scope_drop. Asserts JIT + AOT correctness +
# memcheck. Regression for the vec/map field shallow-copy double-free.
add_test(
    NAME test_struct_byval_arg
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/struct_byval_arg.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=struct_byval_arg
        -P ${CMAKE_SOURCE_DIR}/tests/test_struct_byval.cmake
)

# Transient has_drop struct field read-through: reading through an intermediate
# struct field (e.g. `o.inner.items.length`) borrows the field's address via GEP
# instead of deep-cloning the whole intermediate struct, so its vec/map/string
# heap is not leaked. A terminal binding (`Box b = o.inner`) still deep-clones.
# Asserts JIT + AOT correctness (incl. consume-independence + mutate-through) +
# memcheck.
add_test(
    NAME test_struct_field_readthrough
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/struct_field_readthrough.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=struct_field_readthrough
        -P ${CMAKE_SOURCE_DIR}/tests/test_struct_readthrough.cmake
)

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

# Bug-6 回归：memcheck_edge.ls（Phase B 极端场景）— JIT memcheck OK clean
# 修复：移除 self-recursive enum 参数的 is_borrowed=true hack，callee 正确 drop 深拷贝
add_test(
    NAME test_memcheck_edge_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/memcheck_edge.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_memcheck_edge_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_memcheck_edge_jit PROPERTIES DEPENDS "test_memory")

# M-3: 统一所有权转移 API memcheck 验证
add_test(
    NAME test_mem_m3_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m3_xfer_unified.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_mem_m3_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_mem_m3_jit PROPERTIES DEPENDS "test_memcheck_edge_jit")

# M-4: 所有权转移审计矩阵 — 覆盖 AST_VAR_DECL/ASSIGN/RETURN × 各类型，
# vec/map 字面量，函数参数 by-value，match binder，嵌套容器等
# 包含 BF-038 修复（fn 返回 map IDENT double-free）回归
add_test(
    NAME test_mem_m4_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m4_matrix.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_mem_m4_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_mem_m4_jit PROPERTIES DEPENDS "test_mem_m3_jit")

# M-4.5 回归：vec[i].field 中 has_drop struct 临时深拷贝在语句结束被 drop
# （取字段后丢弃的临时 struct，其它 string 字段此前泄漏）。JIT path。
add_test(
    NAME test_mem_m4_5_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m4_5_drop_temp.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_mem_m4_5_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_mem_m4_5_jit PROPERTIES DEPENDS "test_mem_m4_jit")

# M-4.5 回归：AOT path（ls compile --memcheck && exec）
add_test(
    NAME test_mem_m4_5_aot
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m4_5_drop_temp.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_aot.cmake
)
set_tests_properties(test_mem_m4_5_aot PROPERTIES DEPENDS "test_mem_m4_5_jit")

# M-5 负向：move-after-use（绑定 move / 分支 MAYBE_MOVED）必须编译期拒绝
add_test(
    NAME test_mem_m5_neg
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_mem_m5_neg.cmake
)
set_tests_properties(test_mem_m5_neg PROPERTIES DEPENDS "test_mem_m4_5_jit")

# Phase 3 P3-1: builtin vec(T) syntax is no longer accepted by the frontend.
add_test(
    NAME test_vec_builtin_syntax_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/vec_builtin_syntax_reject.ls
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_builtin_syntax_reject.cmake
)
set_tests_properties(test_vec_builtin_syntax_reject PROPERTIES DEPENDS "test_mem_m5_neg")

# M6-1: builtin map(K,V) syntax is no longer accepted by the frontend.
add_test(
    NAME test_map_builtin_syntax_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/map_builtin_syntax_reject.ls
        -P ${CMAKE_SOURCE_DIR}/tests/test_map_builtin_syntax_reject.cmake
)
set_tests_properties(test_map_builtin_syntax_reject PROPERTIES DEPENDS "test_vec_builtin_syntax_reject")

# P5-4 S-1 (docs/plan_p5_remove_builtin_string.md §8): the builtin `string` type
# keyword was removed from the frontend — `string x` must be rejected.
add_test(
    NAME test_string_type_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/string_type_reject.ls
        -P ${CMAKE_SOURCE_DIR}/tests/test_string_type_reject.cmake
)
set_tests_properties(test_string_type_reject PROPERTIES DEPENDS "test_map_builtin_syntax_reject")

# A-1 (docs/bugs_deferred_p5_4.md §1): nested fn/struct/impl/enum/trait/module
# definitions inside a function body are rejected cleanly at parse time.
add_test(
    NAME test_nested_decl_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/nested_fn_decl_reject.ls
        -P ${CMAKE_SOURCE_DIR}/tests/test_nested_decl_reject.cmake
)
set_tests_properties(test_nested_decl_reject PROPERTIES DEPENDS "test_string_type_reject")

# A-2 (docs/bugs_deferred_p5_4.md §2): explicit `.__drop()` calls are rejected.
add_test(
    NAME test_explicit_drop_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/explicit_drop_reject.ls
        -P ${CMAKE_SOURCE_DIR}/tests/test_explicit_drop_reject.cmake
)
set_tests_properties(test_explicit_drop_reject PROPERTIES DEPENDS "test_nested_decl_reject")

# A-3 (docs/bugs_deferred_p5_4.md §3): forward struct field reference must error
# gracefully (clean "unknown type"), not segfault.
add_test(
    NAME test_forward_ref_struct_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/forward_ref_struct_reject.ls
        -P ${CMAKE_SOURCE_DIR}/tests/test_forward_ref_struct_reject.cmake
)
set_tests_properties(test_forward_ref_struct_reject PROPERTIES DEPENDS "test_explicit_drop_reject")

# A-4 (docs/bugs_deferred_p5_4.md §4): chained-op receiver spill inside a
# match-arm `if` body must not free uninitialized stack on the fall-through path.
# JIT + AOT correctness + memcheck 0/0/0.
add_test(
    NAME test_match_concat_temp
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/match_concat_temp_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=match_concat_temp
        -DMARKER=MATCHCAT
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)
set_tests_properties(test_match_concat_temp PROPERTIES DEPENDS "test_forward_ref_struct_reject")

# B-3 (docs/bugs_deferred_p5_4.md §B-3): user `impl` on an imported struct emits
# method symbols under the struct's prefixed llvm_name. JIT+AOT+memcheck 0/0/0.
add_test(
    NAME test_impl_imported_struct
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/impl_imported_struct_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=impl_imported_struct
        -DMARKER=IMPLIMP
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)
set_tests_properties(test_impl_imported_struct PROPERTIES DEPENDS "test_match_concat_temp")

# C-1 (docs/bugs_deferred_p5_4.md §C-1): test_fs.cmake was an orphan driver never
# registered in CMakeLists — fs_test never ran under ctest. Register it (now with
# FAIL-rejection + memcheck added per C-2). It mkdir/chdir/writes under
# WORK_DIR/fs_test_tmp, so serialize it via a RESOURCE_LOCK to stay -j safe.
add_test(
    NAME test_fs
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/fs_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_fs.cmake
)
set_tests_properties(test_fs PROPERTIES RESOURCE_LOCK "fs_scratch_dir")

# D-1 (docs/bugs_deferred_p5_4.md §D-1): struct auto-print renders Str fields as
# quoted text (not Str{data=ptr,...}). JIT+AOT exact-format + memcheck 0/0/0.
add_test(
    NAME test_struct_print_str
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/deep_copy_all_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_struct_print_str.cmake
)
set_tests_properties(test_struct_print_str PROPERTIES DEPENDS "test_fs")

# A-FLIP (docs/plan_runtime_primitives.md): bare malloc/free/realloc/abort are no
# longer global builtins (moved to std.c) — a bare malloc(...) must be rejected.
add_test(
    NAME test_malloc_builtin_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/malloc_builtin_reject.ls
        -P ${CMAKE_SOURCE_DIR}/tests/test_malloc_builtin_reject.cmake
)

# M-6 汇总回归套件：覆盖 M-1~M-5 所有修复路径的 30+ 极端场景，永久内存安全守护。JIT path。
add_test(
    NAME test_mem_overhaul_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/memcheck_overhaul.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_mem_overhaul_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_mem_overhaul_jit PROPERTIES DEPENDS "test_mem_m4_5_jit")

# M-6 汇总回归套件：AOT path
add_test(
    NAME test_mem_overhaul_aot
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/memcheck_overhaul.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_aot.cmake
)
set_tests_properties(test_mem_overhaul_aot PROPERTIES DEPENDS "test_mem_overhaul_jit")

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
# On Windows the full chain runs through test_extern_struct_byval.
# On Linux/macOS that test is skipped, so depend directly on test_extern_struct.
if(WIN32)
    set_tests_properties(test_e3_errno PROPERTIES DEPENDS "test_extern_struct_byval")
else()
    set_tests_properties(test_e3_errno PROPERTIES DEPENDS "test_extern_struct")
endif()

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

# Phase C.7 (Vec/Map/struct(drop) captures): by-move closure env ownership.
# Memcheck verifies 0 leaks / 0 dfree.
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

add_test(
    NAME test_vec_functional_v1
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_functional_v1.cmake
)
set_tests_properties(test_vec_functional_v1 PROPERTIES DEPENDS "test_phase_f7_stress")

add_test(
    NAME test_fn_as_block
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_fn_as_block.cmake
)
set_tests_properties(test_fn_as_block PROPERTIES DEPENDS "test_vec_functional_v1")

add_test(
    NAME test_vec_functional_v2
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_functional_v2.cmake
)
set_tests_properties(test_vec_functional_v2 PROPERTIES DEPENDS "test_vec_functional_v1")

add_test(
    NAME test_vec_functional_v3
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_functional_v3.cmake
)
set_tests_properties(test_vec_functional_v3 PROPERTIES DEPENDS "test_vec_functional_v2")

add_test(
    NAME test_vec_functional_v4
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_functional_v4.cmake
)
set_tests_properties(test_vec_functional_v4 PROPERTIES DEPENDS "test_vec_functional_v3")

add_test(
    NAME test_vec_functional_v5
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_functional_v5.cmake
)
set_tests_properties(test_vec_functional_v5 PROPERTIES DEPENDS "test_vec_functional_v4")

add_test(
    NAME test_string_parse
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_string_parse.cmake
)
set_tests_properties(test_string_parse PROPERTIES DEPENDS "test_vec_functional_v5")

add_test(
    NAME test_strconv
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_strconv.cmake
)
set_tests_properties(test_strconv PROPERTIES DEPENDS "test_string_parse")

add_test(
    NAME test_perf
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_perf.cmake
)
set_tests_properties(test_perf PROPERTIES DEPENDS "test_strconv")

add_test(
    NAME test_std_perf
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_std_perf.cmake
)
set_tests_properties(test_std_perf PROPERTIES DEPENDS "test_perf")

add_test(
    NAME test_generics_g1
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_generics_g1.cmake
)
set_tests_properties(test_generics_g1 PROPERTIES DEPENDS "test_codegen")

add_test(
    NAME test_generics_g2
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_generics_g2.cmake
)
set_tests_properties(test_generics_g2 PROPERTIES DEPENDS "test_generics_g1")

# Cross-module generic container (Step 0): `import std.stack` + Stack(int)/Stack(string)
# instantiated at the importer's call site. JIT + AOT correctness + memcheck.
add_test(
    NAME test_stack
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/stack_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_stack.cmake
)
set_tests_properties(test_stack PROPERTIES DEPENDS "test_generics_g2")

# Cross-module / transitive generic container: a module that imports std.stack
# and uses Stack(int) internally, imported alongside a direct Stack(string) use.
# Regression for idempotent template registration + on-demand generic-method
# forward-declaration. Reuses test_stack.cmake (STACK PASS sentinel).
add_test(
    NAME test_stack_xmod
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/stack_xmod/main.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -DTEST_NAME=stack_xmod
        -P ${CMAKE_SOURCE_DIR}/tests/test_stack.cmake
)
set_tests_properties(test_stack_xmod PROPERTIES DEPENDS "test_stack")

# Module-qualified generic type `st.Stack(int)` (single-owner). JIT+AOT+memcheck.
add_test(
    NAME test_stack_qual
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/stack_qual.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -DTEST_NAME=stack_qual
        -P ${CMAKE_SOURCE_DIR}/tests/test_stack.cmake
)
set_tests_properties(test_stack_qual PROPERTIES DEPENDS "test_stack")

# Cross-module same-name generic → clear ambiguity error on bare use (negative).
add_test(
    NAME test_generic_ambig
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_generic_ambig.cmake
)
set_tests_properties(test_generic_ambig PROPERTIES DEPENDS "test_stack_qual")

# std.ring — rte_ring-style fixed-capacity ring (single-threaded). Exercises
# vec(Option(T)) slot ownership, wrap-around, full/empty, burst, clear, with both
# Ring(int) and Ring(string) coexisting. JIT + AOT + memcheck.
add_test(
    NAME test_ring
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/ring_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_ring.cmake
)
set_tests_properties(test_ring PROPERTIES DEPENDS "test_generic_ambig")

# RawVec Step 1 — realloc() exposed to LS surface; memcheck tracks the realloc
# chain (malloc -> realloc x3 -> free) as a single migrating object. 0/0/0.
add_test(
    NAME test_vec_realloc
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_realloc.cmake
)
set_tests_properties(test_vec_realloc PROPERTIES DEPENDS "test_ring")

# RawVec Step 2 — sizeof(T) compile-time evaluation (primitive/pointer sizes +
# generic struct method monomorphization + arithmetic). JIT + AOT.
add_test(
    NAME test_vec_sizeof
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_sizeof.cmake
)
set_tests_properties(test_vec_sizeof PROPERTIES DEPENDS "test_vec_realloc")

# RawVec Step 3 — typed *T pointer indexing p[i] (read + raw-store write). POD +
# padded struct stride + field access on index read + *u8 bytes. JIT+AOT+memcheck.
add_test(
    NAME test_vec_ptr_index
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_ptr_index.cmake
)
set_tests_properties(test_vec_ptr_index PROPERTIES DEPENDS "test_vec_sizeof")

# RawVec Step 4 / Gate M0 — hand-written RawVecI (POD) over raw malloc/realloc/
# free + sizeof + p[i]; __drop frees once; memcheck 0/0/0 through realloc chain.
add_test(
    NAME test_vec_poc
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_poc.cmake
)
set_tests_properties(test_vec_poc PROPERTIES DEPENDS "test_vec_ptr_index")

# RawVec Step 5 / Gate M1 — has_drop element ownership: move-in/out, per-element
# recursive drop, nested drop (RawVec of struct / RawVec of RawVec). __drop_at
# intrinsic + __move. memcheck 0/0/0.
add_test(
    NAME test_vec_m1
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_m1.cmake
)
set_tests_properties(test_vec_m1 PROPERTIES DEPENDS "test_vec_poc")

# RawVec Step 6 / Gate M2 — generic std.vec Vec(T) (int/string/Pt) under
# monomorphization, matching vec semantics. Imports std.vec (LS_HOME=repo).
add_test(
    NAME test_vec_m2
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_m2.cmake
)
set_tests_properties(test_vec_m2 PROPERTIES DEPENDS "test_vec_m1")

# RawVec owned-param / move-into-container — rvalue & __move string args MOVE into
# the container (no clone); named-var args borrow (clone, caller stays valid).
add_test(
    NAME test_vec_move
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_move.cmake
)
set_tests_properties(test_vec_move PROPERTIES DEPENDS "test_vec_m2")

# Inferred aggregate init: `Type v = {}` zero-inits a struct (LHS-inferred),
# (historic: replaced new_rawvec(T)() with Vec(T) v = {}; today both Vec/Map support it).
add_test(
    NAME test_inferred_init
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_inferred_init.cmake
)
set_tests_properties(test_inferred_init PROPERTIES DEPENDS "test_vec_move")

# Comprehensive RawVec(T) API parity with vec (insert/remove/swap/reverse/first/
# last/index_of/contains/count/resize/copy/truncate/shrink_to_fit), int + string.
add_test(
    NAME test_vec_api
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_api.cmake
)
set_tests_properties(test_vec_api PROPERTIES DEPENDS "test_inferred_init")

add_test(
    NAME test_vec_global_drop
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/vec_global_drop_test.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_global_drop.cmake
)
set_tests_properties(test_vec_global_drop PROPERTIES DEPENDS "test_vec_api")

# KI-D: RawVec(Pt) without Eq stays usable until an equality-search method is
# called; calling one reports the method-level `where T: Eq` bound.
add_test(
    NAME test_vec_kid
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_kid.cmake
)
set_tests_properties(test_vec_kid PROPERTIES DEPENDS "test_vec_api")

add_test(
    NAME test_vec_parity_p1
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_parity_p1.cmake
)
set_tests_properties(test_vec_parity_p1 PROPERTIES DEPENDS "test_vec_kid")

add_test(
    NAME test_vec_functional_p3
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_functional_p3.cmake
)
set_tests_properties(test_vec_functional_p3 PROPERTIES DEPENDS "test_vec_parity_p1")

# RawVec method-level generics — map(U) / reduce(U) parity with builtin vec,
# incl. chained `v.map(U)(...).reduce(U)(...)` (rvalue-receiver self drop +
# closure body temp-stack isolation). int/string/struct elems, JIT+AOT+memcheck.
add_test(
    NAME test_vec_map_reduce
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_map_reduce.cmake
)
set_tests_properties(test_vec_map_reduce PROPERTIES DEPENDS "test_vec_functional_p3")

# Vec(T) element ownership / clone / drop correctness (plan_vec_ownership_drop.md):
# §008 index-read-through of has_drop struct (v[i].field / v[i].inner.field /
# f(v[i])) + §009 rvalue string moved into the container (push/insert/set).
# JIT + AOT + memcheck 0/0/0.
add_test(
    NAME test_vec_owndrop
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_owndrop.cmake
)
set_tests_properties(test_vec_owndrop PROPERTIES DEPENDS "test_vec_map_reduce")

# `for x in v` over pure-LS Vec(T) via the Iterator(T) protocol
# (docs/plan_userdef_for_in.md). JIT + AOT + memcheck 0/0/0.
add_test(
    NAME test_iter_protocol
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_iter_protocol.cmake
)
set_tests_properties(test_iter_protocol PROPERTIES DEPENDS "test_vec_owndrop")

# std.task — generic structured concurrency: `spawn.task(T)(|| body)` runs the
# body on an OS worker thread; `t.join()` waits + MOVEs the T result back.
# MOVE-capture isolates each task (single owner, no auto-drop double-free across
# the thread boundary). Covers POD + aggregate results, fork/join over
# Vec(Task(Vec(f64))), and Task as a first-class field.
# JIT + repeated AOT (soundness); no memcheck (tracker not thread-safe yet).
add_test(
    NAME test_task_generic
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_task_generic.cmake
)
set_tests_properties(test_task_generic PROPERTIES DEPENDS "test_iter_protocol")

# std.atomic — lock-free atomic scalars (Atomic(T)). Single-threaded method
# correctness (JIT+AOT+memcheck 0/0/0) + N-worker shared-global counter proving
# real cross-thread atomicity (JIT + repeated AOT; no memcheck — tracker not
# thread-safe). Atomic methods lower to one inline LLVM atomic instruction.
add_test(
    NAME test_atomic
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_atomic.cmake
)
set_tests_properties(test_atomic PROPERTIES DEPENDS "test_task_generic")

# std.sync — Mutex(T) (OS lock behind opaque handle) + SpinLock(T) (Atomic flag
# + pause hint). Single-threaded with/raw/trylock + clean teardown (JIT+AOT+
# memcheck 0/0/0) + N-worker mutual-exclusion (non-atomic RMW serialised to the
# exact count; JIT + repeated AOT, no memcheck). Mutex/spin runtime reached via
# __mutex_*/__cpu_relax intrinsics (survive generic-method instantiation).
add_test(
    NAME test_sync
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_sync.cmake
)
set_tests_properties(test_sync PROPERTIES DEPENDS "test_atomic")

# Closure-foundation Phase A — capture another Block by-clone (deep-copy its env
# into the capturing closure's env; source stays live). Single-threaded
# capture-and-call (JIT+AOT+memcheck 0/0/0 for env clone/drop balance) +
# std.par.par_for data-parallel loop on top (JIT + repeated AOT, no memcheck —
# tracker not thread-safe, same as task/sync).
add_test(
    NAME test_par_for
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_par_for.cmake
)
set_tests_properties(test_par_for PROPERTIES DEPENDS "test_sync")

# Closure-foundation Phase B: nested closure literals (transitive capture).
# Single-threaded JIT+AOT+memcheck (0/0/0) + threaded Task integration (repeated
# AOT, no memcheck — tracker not thread-safe) + negative transitive-by-move reject.
add_test(
    NAME test_nested_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_nested_closure.cmake
)
set_tests_properties(test_nested_closure PROPERTIES DEPENDS "test_par_for")

# M-DEF (std.map prereq): implicit empty/default init — `T v` ≡ `T v = {}` for
# any type whose `= {}` is already legal (docs/plan_std_map.md §F2). JIT+AOT+memcheck.
add_test(
    NAME test_implicit_empty_init
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_implicit_empty_init.cmake
)
set_tests_properties(test_implicit_empty_init PROPERTIES DEPENDS "test_iter_protocol")

# M-H (std.map prereq): Hash trait + FxHash + impl Hash for int/string/...
# (docs/plan_std_map.md §3). JIT+AOT+memcheck + negative where-bound rejection.
add_test(
    NAME test_hash
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_hash.cmake
)
set_tests_properties(test_hash PROPERTIES DEPENDS "test_implicit_empty_init")

# std.map M-0: pure-LS Map(K,V) (Robin Hood open addressing) — construct +
# set/get/has?/len + grow/rehash for POD K/V (docs/plan_std_map.md). JIT+AOT+memcheck.
add_test(
    NAME test_map_basic
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_map_basic.cmake
)
set_tests_properties(test_map_basic PROPERTIES DEPENDS "test_hash")

# std.map M-2: has_drop K/V ownership (string/Vec/nested-map keys+values) —
# set/overwrite/get/remove/clear/grow/auto-drop all memcheck 0/0/0
# (docs/plan_std_map.md §8). Also covers the owned-rvalue-enum match double-drop fix.
add_test(
    NAME test_map_owndrop
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_map_owndrop.cmake
)
set_tests_properties(test_map_owndrop PROPERTIES DEPENDS "test_map_basic")

# std.map M-3: MapIter + keys/values/each + `for e in m` (Iterator protocol,
# yields Entry(K,V)), POD + has_drop keys/values (docs/plan_std_map.md §7). JIT+AOT+memcheck.
add_test(
    NAME test_map_iter
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_map_iter.cmake
)
set_tests_properties(test_map_iter PROPERTIES DEPENDS "test_map_owndrop")

# std.map M-LIT: `{ k: v, ... }` key-value literals (frontend §F1) routed via the
# __from_pairs protocol (docs/plan_std_map.md §F1). JIT+AOT+memcheck.
add_test(
    NAME test_map_literal
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_map_literal.cmake
)
set_tests_properties(test_map_literal PROPERTIES DEPENDS "test_map_iter")

# std.map M-4: composition verification (Map as struct field/global/enum payload
# and nested Map values). JIT+AOT+memcheck.
add_test(
    NAME test_map_compose
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_map_compose.cmake
)
set_tests_properties(test_map_compose PROPERTIES DEPENDS "test_map_literal")

# Regression for B-MAP-OPT-001: owned rvalue Option(has_drop) match subject
# dropped exactly once across nested-control-flow arms (idempotent emit_enum_drop).
add_test(
    NAME test_map_option_payload
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_map_option_payload.cmake
)
set_tests_properties(test_map_option_payload PROPERTIES DEPENDS "test_map_compose")

# std.map index protocol: m[k] (read, panic-on-miss) + m[k]=v (insert/update),
# aligning Map with Vec's `v[i]` three-tier model. Positive JIT+AOT+memcheck +
# negative missing-key abort (docs/plan_container_access_safety.md §6).
add_test(
    NAME test_map_index
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_map_index.cmake
)
set_tests_properties(test_map_index PROPERTIES
    DEPENDS "test_map_basic"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Regression for B-MAP-M5-002: Block/closure params of reference type `&T`
# (Block(&Map)/Block(&struct)) use the pointer borrow ABI end-to-end.
add_test(
    NAME test_block_ref_param
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_block_ref_param.cmake
)
set_tests_properties(test_block_ref_param PROPERTIES DEPENDS "test_map_option_payload")

add_test(
    NAME test_string_ord
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_string_ord.cmake
)
set_tests_properties(test_string_ord PROPERTIES DEPENDS "test_vec_map_reduce")

add_test(
    NAME test_trait_parse
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_trait_parse.cmake
)
set_tests_properties(test_trait_parse PROPERTIES DEPENDS "test_generics_g2")

add_test(
    NAME test_impl_trait_parse
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_impl_trait_parse.cmake
)
set_tests_properties(test_impl_trait_parse PROPERTIES DEPENDS "test_trait_parse")

add_test(
    NAME test_trait_call
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_trait_call.cmake
)
set_tests_properties(test_trait_call PROPERTIES DEPENDS "test_impl_trait_parse")

add_test(
    NAME test_trait_bounds_parse
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_trait_bounds_parse.cmake
)
set_tests_properties(test_trait_bounds_parse PROPERTIES DEPENDS "test_trait_call")

add_test(
    NAME test_trait_constraint
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_trait_constraint.cmake
)
set_tests_properties(test_trait_constraint PROPERTIES DEPENDS "test_trait_bounds_parse")

add_test(
    NAME test_trait_basic
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_trait_basic.cmake
)
set_tests_properties(test_trait_basic PROPERTIES DEPENDS "test_trait_constraint")

add_test(
    NAME test_trait_self
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_trait_self.cmake
)
set_tests_properties(test_trait_self PROPERTIES DEPENDS "test_trait_basic")

add_test(
    NAME test_trait_builtin_impl
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_trait_builtin_impl.cmake
)
set_tests_properties(test_trait_builtin_impl PROPERTIES DEPENDS "test_trait_self")

add_test(
    NAME test_trait_struct_bound
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_trait_struct_bound.cmake
)
set_tests_properties(test_trait_struct_bound PROPERTIES DEPENDS "test_trait_builtin_impl")

# ---- Operator overloading (Add/Sub/Mul/Div/Rem/Eq/Ord) ----
add_test(
    NAME test_operator_overload
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_operator_overload.cmake
)
set_tests_properties(test_operator_overload PROPERTIES DEPENDS "test_trait_struct_bound")

# Operator method on has_drop struct (string field) — JIT memcheck must be clean.
add_test(
    NAME test_operator_overload_memcheck
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/operator_overload_memcheck.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=operator_overload_memcheck
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_operator_overload_memcheck PROPERTIES DEPENDS "test_operator_overload")

# Parser BF: `Type name = a * b` decl-initializer must parse as multiplication,
# not a spurious `*b` pointer declaration (affects POD + operator-overload `c = a * b`).
add_test(
    NAME test_decl_init_mul
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_decl_init_mul.cmake
)
set_tests_properties(test_decl_init_mul PROPERTIES DEPENDS "test_operator_overload")

# ---- L-006 fix: enum with vec/map payload ----
add_test(
    NAME test_enum_vec_map_payload
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_vec_map_payload.cmake
)
set_tests_properties(test_enum_vec_map_payload PROPERTIES DEPENDS "test_memory")

# ---- enum with user-defined Vec(T) payload ----
add_test(
    NAME test_enum_user_vec_payload
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_user_vec_payload.cmake
)
# ---- VR-LIM-020: Vec(has_drop_enum) full ownership matrix + match move-out ----
add_test(
    NAME test_enum_has_drop_vec
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_has_drop_vec.cmake
)
set_tests_properties(test_enum_has_drop_vec PROPERTIES DEPENDS "test_enum_vec_map_payload")

set_tests_properties(test_enum_user_vec_payload PROPERTIES
    DEPENDS "test_enum_vec_map_payload"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- std.json module ----
add_test(
    NAME test_std_json
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_std_json.cmake
)
set_tests_properties(test_std_json PROPERTIES
    DEPENDS "test_enum_user_vec_payload"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- L-009 cross-module function name mangling ----
add_test(
    NAME test_l009_mangle
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_l009_mangle.cmake
)
set_tests_properties(test_l009_mangle PROPERTIES
    DEPENDS "test_std_json"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- L-009.1 module generics (A1 instantiation + A2 cross-module mangling) ----
add_test(
    NAME test_l0091_modgen
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_l0091_modgen.cmake
)
set_tests_properties(test_l0091_modgen PROPERTIES
    DEPENDS "test_l009_mangle"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- match OR-pattern (bugs/18 fix) ----
add_test(
    NAME test_match_or_pattern
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_match_or_pattern.cmake
)
set_tests_properties(test_match_or_pattern PROPERTIES DEPENDS "test_std_json")

# ---- char literal support ----
add_test(
    NAME test_char_lit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_char_lit.cmake
)
set_tests_properties(test_char_lit PROPERTIES DEPENDS "test_match_or_pattern")

# ---- enum impl methods ----
add_test(
    NAME test_enum_method_basic
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_method_basic.cmake
)
set_tests_properties(test_enum_method_basic PROPERTIES DEPENDS "test_char_lit")

add_test(
    NAME test_enum_method_mut
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_method_mut.cmake
)
set_tests_properties(test_enum_method_mut PROPERTIES DEPENDS "test_enum_method_basic")

add_test(
    NAME test_enum_method_static
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_method_static.cmake
)
set_tests_properties(test_enum_method_static PROPERTIES DEPENDS "test_enum_method_mut")

add_test(
    NAME test_enum_method_has_drop
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_method_has_drop.cmake
)
set_tests_properties(test_enum_method_has_drop PROPERTIES DEPENDS "test_enum_method_static")

# ---- Part 1: module global variables (P1-1 ~ P1-4) ----
add_test(
    NAME test_modvar
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_modvar.cmake
)
set_tests_properties(test_modvar PROPERTIES
    DEPENDS "test_enum_method_has_drop"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- B-1: same type name from multiple modules → clear compile error ----
add_test(
    NAME test_modtype_conflict
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_modtype_conflict.cmake
)
set_tests_properties(test_modtype_conflict PROPERTIES
    DEPENDS "test_modvar"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- BF-042: global POD vec literal initializer ----
add_test(
    NAME test_global_vec_lit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_global_vec_lit.cmake
)
set_tests_properties(test_global_vec_lit PROPERTIES
    DEPENDS "test_modtype_conflict"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- BF-044: has_drop vec[i].field on RHS of short-circuit && / || ----
add_test(
    NAME test_bf044_shortcircuit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_bf044_shortcircuit.cmake
)
set_tests_properties(test_bf044_shortcircuit PROPERTIES
    DEPENDS "test_global_vec_lit"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- std.regex (Pike VM NFA engine, runtime/ls_regex.c) — JIT + AOT ----
# Uses std.regex's migrated Vec(string) return types (find_all/capture/split).
add_test(
    NAME test_regex
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_regex.cmake
)
set_tests_properties(test_regex PROPERTIES
    DEPENDS "test_bf044_shortcircuit"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- VR-LIM-018 / F6: cross-module generic method resolution ----
# Consumer matches an imported enum's Vec(T) payload and calls Vec methods on
# the binder, without importing std.vec directly (transitive template pull).
add_test(
    NAME test_xmod_generic
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_xmod_generic.cmake
)
set_tests_properties(test_xmod_generic PROPERTIES
    DEPENDS "test_regex"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- B-2 + B-3: module struct/enum type namespace (llvm_name prefixing + impl methods) ----
add_test(
    NAME test_modtype_ns
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_modtype_ns.cmake
)
set_tests_properties(test_modtype_ns PROPERTIES
    DEPENDS "test_bf044_shortcircuit"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- B-4: module-qualified types (mod.Type / alias.Type) disambiguate ----
add_test(
    NAME test_modtype_qualified
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_modtype_qualified.cmake
)
set_tests_properties(test_modtype_qualified PROPERTIES
    DEPENDS "test_modtype_ns"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- B-4.1: same-named struct/enum WITH methods across modules ----
add_test(
    NAME test_modtype_methods
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_modtype_methods.cmake
)
set_tests_properties(test_modtype_methods PROPERTIES
    DEPENDS "test_modtype_qualified"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- B-5: cross-module same-named enum + same variant names (type-context resolve) ----
add_test(
    NAME test_modtype_enum_variants
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_modtype_enum_variants.cmake
)
set_tests_properties(test_modtype_enum_variants PROPERTIES
    DEPENDS "test_modtype_methods"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- B-6: has_drop same-named struct/enum across modules — comprehensive memcheck ----
add_test(
    NAME test_modtype_memcheck
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_modtype_memcheck.cmake
)
set_tests_properties(test_modtype_memcheck PROPERTIES
    DEPENDS "test_modtype_enum_variants"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- BF-045: owned string param into returned struct field / return must clone ----
add_test(
    NAME test_bf045_string_param
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_bf045_string_param.cmake
)
set_tests_properties(test_bf045_string_param PROPERTIES
    DEPENDS "test_modtype_memcheck"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- BF-046: map.set temp has_drop struct/enum value drop ----
add_test(
    NAME test_bf046_map_struct_val
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_bf046_map_struct_val.cmake
)
set_tests_properties(test_bf046_map_struct_val PROPERTIES
    DEPENDS "test_bf045_string_param"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- Bug #22: proc.args() works in AOT (not just JIT) ----
add_test(
    NAME test_proc_args
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_proc_args.cmake
)
set_tests_properties(test_proc_args PROPERTIES
    DEPENDS "test_bf046_map_struct_val"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- Bug #23: int literals > i32 range no longer truncated ----
add_test(
    NAME test_i64_literal
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_i64_literal.cmake
)
set_tests_properties(test_i64_literal PROPERTIES
    DEPENDS "test_proc_args"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- Bug #24: struct literal alloca in loop → JIT stack overflow ----
add_test(
    NAME test_struct_loop
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_struct_loop.cmake
)
set_tests_properties(test_struct_loop PROPERTIES
    DEPENDS "test_i64_literal"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- Bug #25: enum payload alignment (f64/i64 fast access) ----
add_test(
    NAME test_enum_align
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_align.cmake
)
set_tests_properties(test_enum_align PROPERTIES
    DEPENDS "test_struct_loop"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- vec.get_unsafe(i) — unchecked index load ----
add_test(
    NAME test_vec_get_unsafe
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_get_unsafe.cmake
)
set_tests_properties(test_vec_get_unsafe PROPERTIES
    DEPENDS "test_enum_align"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- Vec(T) bounds checking: v[i]/get/set checked (abort on OOB), get!/set! raw ----
add_test(
    NAME test_vec_oob
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_oob.cmake
)
set_tests_properties(test_vec_oob PROPERTIES
    DEPENDS "test_vec_get_unsafe"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- Vec.get(i) -> Option(T): recoverable read, ownership sweep ----
add_test(
    NAME test_vec_get_option
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_get_option.cmake
)
set_tests_properties(test_vec_get_option PROPERTIES
    DEPENDS "test_vec_oob"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- plan_std_map §13 挂账双修：borrow-match binder Vec 下标 + 显式 &局部 实参 ----
add_test(
    NAME test_s13_borrow_gaps
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_s13_borrow_gaps.cmake
)
set_tests_properties(test_s13_borrow_gaps PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- Block(&!T) 可写借用块参数（plan_std_map §13 收官） ----
add_test(
    NAME test_block_mutref
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_block_mutref.cmake
)
set_tests_properties(test_block_mutref PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- Bug #26: loop-body temp allocas → JIT stack overflow (entry-block fix) ----
add_test(
    NAME test_string_loop
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_string_loop.cmake
)
set_tests_properties(test_string_loop PROPERTIES
    DEPENDS "test_vec_get_unsafe"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME test_enum_borrow
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_borrow.cmake
)
set_tests_properties(test_enum_borrow PROPERTIES
    DEPENDS "test_string_loop"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME test_enum_borrow_b
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_borrow_b.cmake
)
set_tests_properties(test_enum_borrow_b PROPERTIES
    DEPENDS "test_enum_borrow"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# std.array 堆 Tensor 阶段 0 地基探针（plan_ndarray_stdlib.md §-1）
add_test(
    NAME test_tensor_phase0
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_tensor_phase0.cmake
)
set_tests_properties(test_tensor_phase0 PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# std.array 阶段 1：泛型 Tensor(T) + 运行时 shape/strides（plan_ndarray_stdlib.md §-1）
add_test(
    NAME test_tensor_phase1
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_tensor_phase1.cmake
)
set_tests_properties(test_tensor_phase1 PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_tensor_phase0"
)

# std.tensor 阶段 3a：数值核心（elementwise/reduction/matmul/transpose/relu）
add_test(
    NAME test_tensor_phase3
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_tensor_phase3.cmake
)
set_tests_properties(test_tensor_phase3 PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_tensor_phase1"
)

# std.tensor 阶段 3b：broadcasting + 按轴 reduction + float 激活
add_test(
    NAME test_tensor_phase3b
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_tensor_phase3b.cmake
)
set_tests_properties(test_tensor_phase3b PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_tensor_phase3"
)

# std.tensor 阶段 3c：std.rand + 随机初始化 + tanh + 按轴 mean/max
add_test(
    NAME test_tensor_phase3c
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_tensor_phase3c.cmake
)
set_tests_properties(test_tensor_phase3c PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_tensor_phase3b"
)

# std.tensor 阶段 2：多下标 t[i,j,k]（arity 派发协议 __index{N}/__index_set{N}）
add_test(
    NAME test_tensor_phase2
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_tensor_phase2.cmake
)
set_tests_properties(test_tensor_phase2 PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_tensor_phase3c"
)

# std.tensor 阶段 3d：安全拷贝切片 row/col/slice + argmax_rows/min/neg/abs/sqrt/log/clamp/mse
add_test(
    NAME test_tensor_phase3d
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_tensor_phase3d.cmake
)
set_tests_properties(test_tensor_phase3d PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_tensor_phase2"
)

# std.tensor 端到端 MLP demo（集成 showcase：随机初始化 + 一行构造 + 前向 + softmax 分类）
add_test(
    NAME test_tensor_mlp
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_tensor_mlp.cmake
)
set_tests_properties(test_tensor_mlp PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_tensor_phase3d"
)

# std.complex/std.fft Phase 0: compiler foundation for `T.zero()` — static trait
# methods + static dispatch on a generic type parameter (plan_fft_stdlib.md §1).
add_test(
    NAME test_zero_trait
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_zero_trait.cmake
)
set_tests_properties(test_zero_trait PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_tensor_mlp"
)

# std.complex Phase 1: generic Complex(T) + operator overloads (generic trait
# impls) + conj/norm/abs/exp + free-fn constructors (plan_fft_stdlib.md §1).
add_test(
    NAME test_complex
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_complex.cmake
)
set_tests_properties(test_complex PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_zero_trait"
)

# Tensor(Complex(f64)) integration: nested generics + T.zero() dispatch to
# Complex(f64).zero() + Complex operators inside tensor elementwise.
add_test(
    NAME test_tensor_complex
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_tensor_complex.cmake
)
set_tests_properties(test_tensor_complex PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_complex"
)

# std.fft Phase 2: radix-2 Cooley-Tukey FFT/ifft over Vec(Complex(f64)),
# round-trip identity (plan_fft_stdlib.md §2.2).
add_test(
    NAME test_fft
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_fft.cmake
)
set_tests_properties(test_fft PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_tensor_complex"
)

# std.fft Phase 3: arbitrary-N FFT via Bluestein (chirp-z), validated against a
# naive O(N^2) DFT for prime/composite N (plan_fft_stdlib.md §2.2 Bluestein).
add_test(
    NAME test_fft_arbitrary
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_fft_arbitrary.cmake
)
set_tests_properties(test_fft_arbitrary PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_fft"
)

# std.fft Phase 4: rfft/irfft (real-input) + DCT-II/III (plan_fft_stdlib.md §2.3/§2.4).
add_test(
    NAME test_fft_real_dct
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_fft_real_dct.cmake
)
set_tests_properties(test_fft_real_dct PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_fft_arbitrary"
)

# std.fft Phase 5: multi-dimensional fft2/fftn over Tensor(Complex(f64)) via
# multi-pass gather/scatter (plan_fft_stdlib.md §2.5).
add_test(
    NAME test_fft_nd
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_fft_nd.cmake
)
set_tests_properties(test_fft_nd PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS "test_fft_real_dct"
)
