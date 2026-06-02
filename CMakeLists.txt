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
    ${LS_OS_SOURCE}
)

add_executable(ls ${LS_SOURCES})
target_include_directories(ls PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(ls ${LLVM_LIBS})

# Keep the std/ directory in the output folder up-to-date with the source tree.
# CMake's file(COPY) only runs at configure time; POST_BUILD runs every build.
# We copy the whole std/ tree so ls.exe can find std/*.ls when LS_HOME is not set.
add_custom_command(TARGET ls POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/std"
        "$<TARGET_FILE_DIR:ls>/std"
    COMMENT "Copying std/ -> output/std/"
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
add_library(ls_os_backend STATIC ${LS_OS_SOURCE} runtime/builtins.c)
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

# M-1 回归：消除双轨临时值跟踪后，print 各类动态 string 参数不 leak 不 double-free
# JIT path（ls run --memcheck）
add_test(
    NAME test_mem_m1_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m1_temp_unify.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=mem_m1
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_mem_m1_jit PROPERTIES DEPENDS "test_memory")

# M-1 回归：AOT path（ls compile --memcheck && exec）
add_test(
    NAME test_mem_m1_aot
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m1_temp_unify.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_aot.cmake
)
set_tests_properties(test_mem_m1_aot PROPERTIES DEPENDS "test_mem_m1_jit")

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
set_tests_properties(test_memcheck_edge_jit PROPERTIES DEPENDS "test_mem_m1_aot")

# M-2 回归：cap=-2 borrowed semantics — JIT memcheck OK clean
# 验证 borrowed string 参数存入 enum/struct/f-string 时正确 clone，不 leak 不 dfree
add_test(
    NAME test_mem_m2_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m2_cap_borrowed.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_mem_m2_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_mem_m2_jit PROPERTIES DEPENDS "test_memcheck_edge_jit")

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
set_tests_properties(test_mem_m3_jit PROPERTIES DEPENDS "test_mem_m2_jit")

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

# M-5 正向：checker move 分析与 codegen 所有权语义一致性（move 语义 move 后不用、
# clone 语义 ctor 后源仍 live）。含 BF-039（map[key]/m.get value clone 临时泄漏）回归。JIT path。
add_test(
    NAME test_mem_m5_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m5_move_ok.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_mem_m5_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_mem_m5_jit PROPERTIES DEPENDS "test_mem_m4_5_jit")

# M-5 正向：AOT path
add_test(
    NAME test_mem_m5_aot
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m5_move_ok.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_aot.cmake
)
set_tests_properties(test_mem_m5_aot PROPERTIES DEPENDS "test_mem_m5_jit")

# M-5 负向：move-after-use（vec.push / v[i]= / 分支 MAYBE_MOVED）必须编译期拒绝
add_test(
    NAME test_mem_m5_neg
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_mem_m5_neg.cmake
)
set_tests_properties(test_mem_m5_neg PROPERTIES DEPENDS "test_mem_m5_jit")

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
set_tests_properties(test_mem_overhaul_jit PROPERTIES DEPENDS "test_mem_m5_jit")

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
    NAME test_string_utils
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_string_utils.cmake
)
set_tests_properties(test_string_utils PROPERTIES DEPENDS "test_string_parse")

add_test(
    NAME test_strconv
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_strconv.cmake
)
set_tests_properties(test_strconv PROPERTIES DEPENDS "test_string_utils")

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
    DEPENDS "test_enum_vec_map_payload"
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
