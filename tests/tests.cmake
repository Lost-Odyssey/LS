# tests/tests.cmake — all test registration for the LS compiler.
#
# Included from the top-level CMakeLists.txt via include(); this file runs in
# the SAME variable scope and directory context as the top-level file, so
# ${LLVM_LIBS}, ${LS_UNIX_EXTRA_LIBS}, $<TARGET_FILE:ls> and every
# CMAKE_SOURCE_DIR/CMAKE_BINARY_DIR reference behave exactly as they did when
# this content lived in the top-level file. Do NOT convert this to
# add_subdirectory(): that would change CMAKE_CURRENT_SOURCE_DIR semantics.
#
# All paths in here are absolute (CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR), never
# CMAKE_CURRENT_*, so the include location is irrelevant to behavior.
#
# ---------------------------------------------------------------------------
# Parallel-safety audit (2026-07-05) — shared-resource inventory & disposition
# ---------------------------------------------------------------------------
# The suite runs with `ctest -j N`; every test that writes files, sets env vars
# or spawns processes was audited for cross-test interference. Findings:
#
# | tests                          | resource                        | disposition |
# |--------------------------------|---------------------------------|-------------|
# | test_e4_io_basic, test_io_raii | <cwd>/io_basic_test.tmp (name   | FIXED: test_io_raii runs the
# |                                | hardcoded in io_basic_test.lls, | sample in a private scratch dir
# |                                | both tests ran it in build/)    | (io_raii_scratch/) -> unique
# |                                |                                 | path; the old DEPENDS-chain
# |                                |                                 | serialization was dropped
# | test_memcheck_aot,             | AOT --memcheck exe/.o output    | already fixed 2026-07-05 as
# | test_mem_m4_5_aot,             | (three tests once shared one    | aot_mc_<sample-stem>.* in
# | test_mem_overhaul_aot          | hardcoded name, truncated .o)   | test_memcheck_aot.cmake;
# |                                |                                 | verified intact, no action
# | all AOT-compiling drivers      | ${WORK_DIR}/<name>_aot[.exe]    | verified unique: names derive
# | (~82 registrations)            | (+ linker .o next to it)        | from TEST_NAME/TN/sample stem,
# |                                |                                 | all distinct; literal names
# |                                |                                 | cross-checked against every
# |                                |                                 | TEST_NAME-derived name -> no
# |                                |                                 | overlap; no action
# | drivers using set(ENV{...})    | LS_HOME, LS_MEMCHECK_STRICT,    | process-local: env is set in
# | (memcheck_jit, e3_glue, sim,   | LS_NO_*, LS_FORCE_NOALIAS,      | the per-test `cmake -P` child,
# | opt_parity, lifetime, ...)     | ...                             | never at ctest level (no
# |                                |                                 | ENVIRONMENT test property in
# |                                |                                 | the suite); no action
# | runtime-writing samples (csv   | csv_rt_tmp.csv, fs_test_tmp/,   | one writer test per file, all
# | fs/io/json/sink/sim tests)     | io_*.tmp, json_rt_tmp.json,     | basenames verified unique
# |                                | sink_*.tmp, <repo>/tmp/sim_*    | suite-wide; no action
# | test_parse_depth, test_fmt,    | parse_depth_scratch/,           | single-owner unique paths;
# | test_block_protocol_lint,      | fmt_fixture_copy.lls,           | no action
# | test_emit_c, test_debug_info   | bpl_std_import.lls, emit_c_*,   |
# |                                | debug_info_aot.exe/.pdb         |
# | test_mem_m4_5_*/_overhaul_*    | same .lls sample read by JIT +  | read-only sharing (samples
# | and all fixture main.lls dirs  | AOT test pair                   | never modified); no action
#
# No test needs RESOURCE_LOCK: every conflict was resolved by unique output
# paths, which keeps full -j parallelism. If you add a test that writes files,
# derive the name from the test name / sample stem (never a fixed literal that
# another test might pick) and extend this table.
# ---------------------------------------------------------------------------

# Tests (Phase 1 & 2: no LLVM dependency)
enable_testing()

add_executable(test_scanner tests/test_scanner.c src/scanner.c)
target_include_directories(test_scanner PRIVATE src/ include/)
# Scanner unit test: token kinds, and the line/column carried on each one.
#
# Positions matter more here than the token kinds do. Every diagnostic in the
# compiler renders a source snippet with a caret, so a scanner that is one column
# off does not fail anything -- it just points every error at the wrong place.
#
# @subsystem frontend/lexer
# @guards scanner token + line/col unit coverage
# @sources scanner.c:scanner_next
add_test(NAME test_scanner COMMAND test_scanner)

# C2-2 did-you-mean: diag_suggest unit tests (pure logic, no LLVM)
add_executable(test_diag_suggest tests/test_diag_suggest.c src/diag.c)
target_include_directories(test_diag_suggest PRIVATE src/ include/)
# did-you-mean unit test: the edit-distance suggester, pure logic and no LLVM.
#
# It pins the thresholds as much as the algorithm -- suggesting nothing is fine,
# but suggesting the WRONG identifier confidently is worse than staying quiet,
# so ties and very short names deliberately produce no suggestion.
#
# @subsystem diagnostics
# @guards C2-2 did-you-mean suggestions
# @sources diag.c:diag_suggest
add_test(NAME test_diag_suggest COMMAND test_diag_suggest)

add_executable(test_parser
    tests/test_parser.c
    src/scanner.c
    src/ast.c
    ${LS_PARSER_SOURCES}
    src/diag.c
    src/types.c
)
target_include_directories(test_parser PRIVATE src/ include/)
# Parser unit test: the Pratt expression parser and declaration forms, checked
# on the AST directly rather than through a compiled program.
#
# This is the only level at which precedence and associativity can be asserted
# cleanly; by the time a program runs, a mis-parsed expression usually still
# produces a number, just the wrong one.
#
# @subsystem frontend/parser
# @guards Pratt parser unit coverage
# @sources parser.c:parse
add_test(NAME test_parser COMMAND test_parser)

add_executable(test_types
    tests/test_types.c
    src/scanner.c
    src/ast.c
    ${LS_PARSER_SOURCES}
    src/diag.c
    src/types.c
    src/symtable.c
    ${LS_CHECKER_SOURCES}
    src/module.c
    src/builtins_math.c
    src/builtins_perf.c
)
target_include_directories(test_types PRIVATE src/ include/)
if(NOT WIN32)
    target_link_libraries(test_types ${LS_UNIX_EXTRA_LIBS})
endif()
# Type-system unit test: equality, assignability and the type registry, without
# LLVM in the picture.
#
# It links the checker but not codegen, which is what makes it useful as a fast
# gate -- and also what made it go stale unnoticed once (it kept asserting that
# by-value array parameters were accepted after policy A banned them).
#
# @subsystem checker/types
# @guards type system unit coverage
# @sources types.c:type_equals
add_test(NAME test_types COMMAND test_types)

# Phase 4 test: codegen (needs LLVM)
add_executable(test_codegen
    tests/test_codegen.c
    src/scanner.c
    src/ast.c
    ${LS_PARSER_SOURCES}
    src/diag.c
    src/types.c
    src/symtable.c
    ${LS_CHECKER_SOURCES}
    ${LS_CODEGEN_SOURCES}
    src/debug.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_intrinsic_cg.c
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
# Codegen unit test: drives the emitter directly and inspects the module,
# instead of running a program.
#
# Useful for shapes whose failure is structural (a missing terminator, a value
# that does not dominate its uses) rather than behavioural -- those show up as a
# verifier complaint with no source location when found end-to-end.
#
# @subsystem codegen/core
# @guards codegen unit coverage
# @sources codegen.c:codegen_compile
add_test(NAME test_codegen COMMAND test_codegen)

# Phase 5 test: JIT (needs LLVM)
add_executable(test_jit
    tests/test_jit.c
    src/scanner.c
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
    src/builtins_intrinsic_cg.c
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
# LLJIT unit test: incremental compilation and symbol resolution.
#
# The REPL is built on this, and its historical failures (L-010) were all in the
# same area -- re-emitting a module whose earlier definitions are still
# referenced, then stripping the old bodies without leaving dangling block
# references behind.
#
# @subsystem codegen/core
# @guards LLJIT incremental compilation unit coverage
# @sources jit.c
add_test(NAME test_jit COMMAND test_jit)

# Phase 6 test: FFI (needs LLVM)
add_executable(test_ffi
    tests/test_ffi.c
    src/scanner.c
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
    src/builtins_intrinsic_cg.c
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
# FFI unit test: loading a shared library and resolving symbols out of it.
#
# Platform-specific by nature (LoadLibrary/GetProcAddress on Windows), which is
# why it is exercised at the C level rather than only through `load()` in LS.
#
# @subsystem codegen/ffi
# @guards dynamic library loading unit coverage
# @sources ffi.c
add_test(NAME test_ffi COMMAND test_ffi)

# Phase 7 test: Module system (needs LLVM)
add_executable(test_module
    tests/test_module.c
    src/scanner.c
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
    src/builtins_intrinsic_cg.c
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
# Module-loader unit test: resolution order, the import registry and dedup.
#
# Deduplication is the part worth pinning: two import spellings that resolve to
# the same file must produce ONE module, or the same functions get emitted twice
# under two different symbol prefixes.
#
# @subsystem modules
# @guards module loader unit coverage
# @sources module.c:module_load
add_test(NAME test_module COMMAND test_module)
set_tests_properties(test_module PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Memory management E2E tests (needs LLVM + JIT)
add_executable(test_memory
    tests/test_memory.c
    src/scanner.c
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
    src/builtins_intrinsic_cg.c
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
# Memcheck tracker unit test: the allocation table itself.
#
# Everything the suite claims about leaks and double frees rests on this. If the
# tracker miscounts, every memcheck-gated test is either falsely green or falsely
# red -- so it is verified in isolation, not only through the programs it
# instruments.
#
# @subsystem runtime/memcheck
# @guards memcheck tracker unit coverage
# @sources runtime/memcheck.c:ls_mc_report
add_test(NAME test_memory COMMAND test_memory)

# REPL helper unit tests (classification / completeness / highlighting).
# Pure logic over the scanner — no LLVM needed.
add_executable(test_repl tests/test_repl.c src/scanner.c src/repl_edit.c src/repl_term.c)
target_include_directories(test_repl PRIVATE src/)
# REPL unit test: the line editor and multi-line input handling.
#
# The editor is hand-written rather than readline-based, so cursor movement,
# history and continuation of an unfinished construct are all our own code.
#
# @subsystem tooling/repl
# @guards REPL unit coverage
# @sources jit.c
add_test(NAME test_repl COMMAND test_repl)

# REPL import-persistence E2E: pipe `import math` + a math call into `ls repl`
# (non-TTY → fgets fallback) and assert the result is computed.
# @subsystem tooling/repl
# @guards REPL import persistence
# @sources jit.c
add_test(
    NAME test_repl_import
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/samples/repl_import.in
        -DEXPECT=4
        -P ${CMAKE_SOURCE_DIR}/tests/test_repl_pipe.cmake
)

# Static reflection Stage 1.5 REPL: define a struct + method, then `:methods P`
# must list the user method's signature. Reuses the piped-REPL driver.
# @subsystem tooling/repl
# @guards REPL :methods inspection
# @sources checker.c:checker_inspect
add_test(
    NAME test_repl_inspect
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/samples/repl_inspect.in
        "-DEXPECT=def get(&self) -> int"
        -P ${CMAKE_SOURCE_DIR}/tests/test_repl_pipe.cmake
)

# L-010 regression: cross-snippet has_drop values (std.json) in `ls repl` must
# not crash. Loops the repro 60x asserting every run exits 0 (the bug was a
# flaky ~8%/run heap UAF in the body-strip's basic-block deletion).
add_test(
    NAME test_repl_l010
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/samples/repl_l010.in
        -P ${CMAKE_SOURCE_DIR}/tests/test_repl_l010.cmake
)

# REPL variable-state persistence (Phase 1, POD scalars): a mutation to a
# persisted scalar must survive across input lines. `int i = 1; i += 41;
# print(i)` must print 42 (pre-fix it reset to 1 each line and printed 1).
# @subsystem tooling/repl
# @guards REPL variable-state persistence Phase 1 (POD scalars)
# @sources jit.c
add_test(
    NAME test_repl_persist
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/samples/repl_persist.in
        -DEXPECT=42
        -P ${CMAKE_SOURCE_DIR}/tests/test_repl_pipe.cmake
)

# REPL variable-state persistence (Phase 2, containers/has_drop): a Vec global
# must survive across input lines so pushes accumulate. `Vec(int) v=[1,2,3]` then
# two pushes then `print(v.len())` must print 5 (pre-fix M-DEF reset it each line).
# @subsystem tooling/repl
# @guards REPL variable-state persistence Phase 2 (containers)
# @sources jit.c
add_test(
    NAME test_repl_persist_vec
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/samples/repl_persist_vec.in
        -DEXPECT=5
        -P ${CMAKE_SOURCE_DIR}/tests/test_repl_pipe.cmake
)

# REPL import re-emission flake (residual L-010 fix): an explicit `import std.vec`
# followed by later snippets re-emits and strips imported module functions —
# including loopy ones like std_str__Str.to_float. Deleting their blocks left a
# dangling back-edge block reference → ~10% release-build UAF. Soak it: 60 runs,
# FAIL on >= 2 crashes. (Pre-fix ~5/60; post-fix 0.)
add_test(
    NAME test_repl_import_strip
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/samples/repl_import_strip.in
        -DEXPECT=200
        -P ${CMAKE_SOURCE_DIR}/tests/test_repl_soak.cmake
)

# std.md (Markdown writer) Phase A: build a doc with every builder + render,
# under JIT memcheck — asserts the run is leak/double-free clean.
# @subsystem stdlib/text
# @guards std.md Phase A builder + render
# @sources lib/std/text/md.lls
add_test(
    NAME test_std_md_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/md_build.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=std_md
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)

# std.md Phase B (parser): parse a Markdown doc into MdDoc + round-trip render,
# under JIT memcheck.
# @subsystem stdlib/text
# @guards std.md Phase B parser + round-trip
# @sources lib/std/text/md.lls
add_test(
    NAME test_std_md_parse_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/md_parse.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=std_md_parse
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)

# std.md Phase C (inline parsing + extract helpers).
# @subsystem stdlib/text
# @guards std.md Phase C inline parsing
# @sources lib/std/text/md.lls
add_test(
    NAME test_std_md_inline_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/md_inline.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=std_md_inline
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)

# @-sigil intrinsics parse/checker/codegen parity with legacy __ spellings.
# (Full JIT+AOT parity is exercised across the whole suite once the stdlib is
# migrated to @-names in Phase 2; this is a focused JIT+memcheck smoke test.)
# @subsystem language/syntax
# @guards @-sigil intrinsics vs the legacy __ spellings
# @sources checker_call.c:intrinsic_retired_spelling
add_test(
    NAME test_intrinsic_sigil
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/intrinsic_sigil.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=intrinsic_sigil
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)

# FromList protocol interface: list-literal init `Vec(T) v = [..]` via the facade.
# @subsystem checker/traits
# @guards FromList marker protocol interface
# @sources checker_decl.c:check_impl_trait_decl
add_test(
    NAME test_protocol_fromlist
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/protocol_fromlist.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=protocol_fromlist
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)

# FromPairs protocol interface: map-literal init `Map(K,V) m = {k:v}` via the facade.
# @subsystem checker/traits
# @guards FromPairs marker protocol interface
# @sources checker_decl.c:check_impl_trait_decl
add_test(
    NAME test_protocol_frompairs
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/protocol_frompairs.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=protocol_frompairs
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)

# The retired `__take` spelling must be rejected at compile time.
#
# When the intrinsics moved to @-sigils the old `__` spellings were kept
# recognisable specifically so they could be REFUSED with a clear message rather
# than silently becoming undefined identifiers. Registered with WILL_FAIL, so the
# oracle is the non-zero exit code.
#
# @subsystem language/syntax
# @guards retired __take spelling must be rejected
# @sources checker_call.c:intrinsic_retired_spelling
add_test(NAME test_retired_take
    COMMAND $<TARGET_FILE:ls> run ${CMAKE_SOURCE_DIR}/tests/samples/retired_take.lls)
set_tests_properties(test_retired_take PROPERTIES WILL_FAIL TRUE)

# Hand-writing the reserved `__from_list` method name must be rejected.
#
# It is the container-literal protocol hook, synthesised by the compiler. A user
# definition would silently shadow the generated one and change what every list
# literal of that type does. WILL_FAIL is the oracle.
#
# @subsystem language/syntax
# @guards hand-writing the reserved __from_list name must be rejected
# @sources checker_decl.c:check_impl_decl
add_test(NAME test_retired_fromlist
    COMMAND $<TARGET_FILE:ls> run ${CMAKE_SOURCE_DIR}/tests/samples/retired_fromlist.lls)
set_tests_properties(test_retired_fromlist PROPERTIES WILL_FAIL TRUE)

# The official `.lls` source extension after the LS -> lls rename.
#
# Paired with the legacy-extension test below: the resolver tries `.lls` first
# and `.ls` second, and both legs run a program that also imports a stdlib module,
# so the extension logic is exercised for imports and not just for the entry file.
#
# @subsystem language/syntax
# @guards the .lls extension after the LS->lls rename
# @sources main.c
add_test(NAME test_ext_lls_smoke
    COMMAND $<TARGET_FILE:ls> run ${CMAKE_SOURCE_DIR}/tests/samples/rename_smoke.lls)
# The legacy `.ls` extension must keep compiling.
#
# The rename kept `.ls` working on purpose; this is the guard that it stays that
# way. Same program shape as the `.lls` twin.
#
# @subsystem language/syntax
# @guards the legacy .ls extension keeps compiling
# @sources main.c
add_test(NAME test_ext_ls_legacy
    COMMAND $<TARGET_FILE:ls> run ${CMAKE_SOURCE_DIR}/tests/samples/legacy_ext_compat.ls)

# std.html Phase H1 (generation + render): functional bottom-up construction,
# escaping, void tags, fmt_tag. JIT + AOT correctness + memcheck.
# @subsystem stdlib/text
# @guards std.html H1 generation + render
# @sources lib/std/text/html.lls
add_test(
    NAME test_std_html_write
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/html_write.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=std_html_write
        -P ${CMAKE_SOURCE_DIR}/tests/test_std_html.cmake
)

# std.html Phase H2 (parsing): recursive-descent tolerant parser, attributes
# (quoted/single/unquoted/boolean), void/self-closing, comments, DOCTYPE,
# <script>/<style> raw text, entity decode (named + numeric), round-trip, and
# query helpers (to_text / extract_links / find_by_tag / get_attr).
# JIT + AOT correctness + memcheck. Reuses the std.html driver ("HTML PASS").
# @subsystem stdlib/text
# @guards std.html H2 tolerant recursive-descent parser
# @sources lib/std/text/html.lls
add_test(
    NAME test_std_html_parse
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/html_parse.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=std_html_parse
        -P ${CMAKE_SOURCE_DIR}/tests/test_std_html.cmake
)

# std.md Phase H3 (Markdown -> HTML): md.to_html / to_html_full map MdBlock /
# MdInline to HTML (headings, paragraphs, bold/italic/code, links, lists, code
# blocks, hr, escaping, full-document wrapper). Lives in std.md (no std.html
# dependency). JIT + AOT correctness + memcheck. Reuses the std.html driver.
# @subsystem stdlib/text
# @guards std.md Phase H3 Markdown -> HTML
# @sources lib/std/text/md.lls
add_test(
    NAME test_md_to_html
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/md_to_html.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=md_to_html
        -P ${CMAKE_SOURCE_DIR}/tests/test_std_html.cmake
)

# std.text.csv (CSV reader/writer, RFC 4180): byte-level state-machine parser +
# quoting serializer over Str/Vec; Csv table with by-name column access; file
# round-trip via io. Self-verifying sample prints "ALL DONE" (never "FAIL").
# JIT + AOT correctness + memcheck.
add_test(
    NAME test_std_csv
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/csv_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=std_csv
        -P ${CMAKE_SOURCE_DIR}/tests/test_std_csv.cmake
)

# std/plotfmt.lls (plot infra Phase 0.2): pure-LS formatting helpers
# (fmt_fixed/auto/sci/time, pad, clamp, rgb/hsv -> hex). Self-verifying sample
# prints "PLOTFMT PASS". JIT + AOT correctness + memcheck.
# @subsystem stdlib/plot
# @guards plot infra 0.2 pure-LS formatting helpers
# @sources lib/std/chart/plotfmt.lls
add_test(
    NAME test_plotfmt
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plotfmt_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plotfmt
        -DMARKER=PLOTFMT
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# f-string format specifiers (plot infra Phase 0.3): {expr:.2f} / {n:03d} etc.
# scanner+parser+ast+codegen; runtime unchanged. Self-verifying sample prints
# "FSPEC PASS". JIT + AOT correctness + memcheck.
# @subsystem stdlib/string
# @guards f-string format specifiers {expr:.2f}
# @sources codegen_print.c:codegen_format_string
add_test(
    NAME test_fstring_spec
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/fstring_spec_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=fstring_spec
        -DMARKER=FSPEC
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# Merged std.core.math: pure-LS derived helpers (radians/degrees,
# lib/std/core/math.lls) folded into the built-in math namespace alongside C
# primitives. Sample prints "MATHEXT PASS". JIT + AOT correctness + memcheck.
# @subsystem stdlib/numeric
# @guards merged std.core.math derived helpers
# @sources lib/std/core/math.lls
add_test(
    NAME test_math_ext
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/math_ext_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=math_ext
        -DMARKER=MATHEXT
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# Retirement: built-in math moved to std.core.math. Bare `import math` (no user
# math.lls present) must be a clean compile error pointing at the new path.
add_test(
    NAME test_bare_math_import_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/bare_math_import_reject.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_bare_math_import_reject.cmake
)

# Identifier predicate/unsafe suffixes: `name?` and `name!`, while preserving
# `!=` and prefix `!expr`. Self-verifying sample prints "IDENTSUF PASS".
# @subsystem frontend/lexer
# @guards identifier ? / ! suffixes
# @sources scanner.c:scanner_next
add_test(
    NAME test_ident_suffix
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/ident_suffix_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=ident_suffix
        -DMARKER=IDENTSUF
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# Postfix `!` -- force-unwrap on Option/Result.
#
# It lowers to the same machinery as `try` and as a `match`: read the tag, take
# the payload on the success path, abort with a source location on the failure
# path. That makes it an ownership operation, not just a read. On success the
# payload must be MOVED out -- unwrapping and then also dropping the container
# would free the payload twice -- and on failure the diagnostic has to carry the
# line and column, so the aborting form is exercised too.
#
# Note the syntax detail the corpus pins: a bare variable needs `(x)!`, because
# `x!` is lexed as an identifier with a `!` suffix.
#
# @subsystem codegen/match
# @guards postfix `!` force-unwrap
# @sources codegen_match.c:codegen_try_expr
add_test(
    NAME test_force_unwrap
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/force_unwrap_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=force_unwrap
        -DMARKER=FORCEUNWRAP
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# V1 bit-pattern matching (features/bit_pattern_match.md): MSB-first field
# extraction + binding, match-value arms, OR-pattern, wildcard, bool 1-bit field,
# int/u16/u8/u64 subjects. JIT + AOT correctness + memcheck 0/0/0.
# @subsystem codegen/match
# @guards V1 bit-pattern matching
# @sources codegen_match.c:cg_match_lower_enum
add_test(
    NAME test_bit_match
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/bit_match_test.lls
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

# Static reflection: @derive on user-defined generic structs (Equal/Hash/Order +
# Reflect via alias; Show/Serialize/Deserialize rejected on generics).
add_test(
    NAME test_derive_generic
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_derive_generic.cmake
)

# Static reflection Stage 3: @derive(Reflect) -> runtime TypeInfo (fields +
# method signatures). See docs/plan_static_reflection.md §7.
add_test(
    NAME test_reflect
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_reflect.cmake
)

# Static reflection Stage 2: @derive(Serialize) -> neutral Value tree ->
# json.encode (one derive, format-agnostic). See docs/plan_static_reflection.md §6.
add_test(
    NAME test_serialize
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_serialize.cmake
)

# Static reflection Stage 1: @derive(Equal) synthesizes `==` field-by-field
# (parse @attr → reparse synthesized impl → check + codegen). See
# docs/plan_static_reflection.md §4.
add_test(
    NAME test_derive
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_derive.cmake
)

# Static reflection Stage 3b (step 2): comptime field iteration — `comptime for f
# in fields(T)` unrolls per field (f.name/index/type_name → literals, v.(f) →
# concrete field access). See docs/plan_comptime.md.
add_test(
    NAME test_comptime
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_comptime.cmake
)

# Compile-time constant evaluation (comptime const-eval), Step 2 — scalar consts
# folded to literals (int/bool/char/f64; arithmetic/bit/shift/compare/cast/math.*).
# See docs/plan_comptime_consteval.md.
add_test(
    NAME test_comptime_const
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_comptime_const.cmake
)

# Generic free-function call ergonomics: value-arg type inference (Gap 1) and
# `&T` borrow params with auto-borrow / explicit `&v` (Gap 2). Comptime-independent.
add_test(
    NAME test_generic_free_fn_borrow
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_generic_free_fn_borrow.cmake
)

# comptime v2: generic construction T{}/T{...} + writable fields, enum
# variants(T), and f.type as a type value (f.type.from_value -> write-once deser).
add_test(
    NAME test_comptime_v2
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_comptime_v2.cmake
)

# comptime v3 ①: comptime match (active-variant value dispatch) — write-once enum
# show/serialize/visitor over any enum, no @derive.
add_test(
    NAME test_comptime_match
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_comptime_match.cmake
)

# Per-function codegen inspection: `ls ir <fn>` / `ls asm <fn>` print one
# function's LLVM IR / assembly (optimized, fuzzy name match) for manual tuning.
add_test(
    NAME test_irasm
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_irasm.cmake
)

# Static reflection Stage 1.5: `ls inspect <Type> <file>` prints a struct/enum's
# fields + methods (signatures, [static]/[Destroy]/[Clone] tags). See
# docs/plan_static_reflection.md §5.
add_test(
    NAME test_inspect
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_inspect.cmake
)

# Parser robustness: malformed input must never make the parser infinite-loop.
# Corpus = fuzz-found hangs + minimized repros (tests/fuzz/regress). Guards the
# recover_in_body() forward-progress fix (src/parser.c). See docs/plan_fuzzing.md.
add_test(
    NAME test_parser_hang_regress
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DREGRESS_DIR=${CMAKE_SOURCE_DIR}/tests/fuzz/regress
        -P ${CMAKE_SOURCE_DIR}/tests/test_parser_hang_regress.cmake
)

# Ownership/drop regression guard: a curated corpus of grammar-fuzzer-generated
# programs (tests/fuzz/genfuzz.py) over owned-type paths, each must run AND be
# memcheck-clean. python-free at test time. See docs/plan_fuzzing.md.
add_test(
    NAME test_owned_fuzz_corpus
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DCORPUS_DIR=${CMAKE_SOURCE_DIR}/tests/fuzz/owned_corpus
        -DLS_HOME=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_owned_fuzz_corpus.cmake
)

# @derive / static-reflection robustness guard: edge & malformed @derive programs
# (tests/fuzz/derive_corpus) must not crash `ls emit-ir` — exit 0/1 only. Stresses
# the new @derive parser + checker + synthesis (emit path). See docs/plan_fuzzing.md.
add_test(
    NAME test_derive_fuzz_corpus
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DCORPUS_DIR=${CMAKE_SOURCE_DIR}/tests/fuzz/derive_corpus
        -P ${CMAKE_SOURCE_DIR}/tests/test_derive_fuzz_corpus.cmake
)

# Thread-safe memcheck guard: multi-threaded programs (Atomic/Guard/Chan workers)
# must run AND be memcheck-clean — validates the locked, thread-local-frame
# tracker (runtime/memcheck.c). See docs/plan_fuzzing.md / known_limitations.
add_test(
    NAME test_thread_memcheck
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DLS_HOME=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_thread_memcheck.cmake
)

# std.bytes (features/bit_pattern_match.md §10): byte-buffer load primitives +
# stateful Reader cursor, feeding a bit-pattern match. Parses a synthetic O-RAN /
# eCPRI fronthaul packet (be_u32/u16/u8 + le_u32 + owned of_bytes path).
# JIT + AOT correctness + memcheck 0/0/0.
# @subsystem stdlib/text
# @guards std.bytes byte-buffer load primitives
# @sources lib/std/text/bytes.lls
add_test(
    NAME test_bytes
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/bytes_oran_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=bytes
        -DMARKER=ORAN
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)
# Reader bounds check: reading past the end of the buffer must abort.
add_test(
    NAME test_bytes_oob
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/bytes_reader_oob.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_bytes_oob.cmake
)

# 通用 mod.fn 解析 Phase 1 (docs/plan_module_fn_resolution.md): canonical
# module-path calls `std.x.fn()` resolve without an alias; aliased imports allow
# both alias and canonical spellings. JIT + AOT + memcheck.
# @subsystem modules
# @guards canonical mod.fn call resolution (Phase 1)
# @sources checker_call.c:check_expr_call
add_test(
    NAME test_modfn_canonical
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/modfn_canonical_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=modfn_canonical
        -DMARKER=MODFN
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# 通用 mod.fn 解析 Phase 2 (docs/plan_module_fn_resolution.md): a generic struct
# defined in module A, whose methods call module functions via alias / canonical
# path, instantiated in consumer B that never imported those modules. A's import
# env is bound when the generic method bodies are checked in B. main.lls imports a
# sibling defmod.lls (resolved relative to the source dir). JIT + AOT + memcheck.
# @subsystem modules
# @guards generic method instantiation sees the defining module's imports (Phase 2)
# @sources checker_generics.c:instantiate_template
add_test(
    NAME test_modfn_generic
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/modfn_generic/main.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=modfn_generic
        -DMARKER=MODFN2
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# L-013: the value a `match` produces is owned by whoever consumes it.
#
# The rule that makes this delicate is stated in the match codegen guide: a
# has_drop match result must NEVER be registered in the statement's temp table.
# The consumer (a binding, a call argument, a return) takes it, and a second
# registration means a second drop of the same heap block. This corpus is the
# guard on that specific asymmetry.
#
# @subsystem codegen/ownership
# @guards L-013
# @sources codegen_match.c:cg_match_emit_arm_body
add_test(
    NAME test_match_result_own
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/match_result_own_test.lls
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
# @subsystem stdlib/string
# @guards P1 string literal lowered to Str
# @sources checker_expr.c:check_expr_ident
add_test(
    NAME test_str_p1
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_p1_litcoerce.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_p1
        -DMARKER=STRP1
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib P2: an f-string with interpolations produces an OWNED Str
# rvalue (cap>0, the formatted heap buffer wrapped as Str, zero-copy), routed
# through the unified has_drop temp/drop path. Since P5-2 the f-string default
# IS Str. JIT+AOT+memcheck 0/0/0 (docs/plan_string_to_stdlib.md §5.2).
# @subsystem stdlib/string
# @guards P2 f-string with interpolations produces an owned Str
# @sources codegen_print.c:codegen_format_string
add_test(
    NAME test_str_p2
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_p2_fstring.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_p2
        -DMARKER=STRP2
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib P3: `print` accepts a `Str` and writes its raw text (printf
# "%.*s", length-bounded). Owned Str rvalues passed to print are dropped clean.
# v1; the @print sigil + Printable trait are deferred (v2). JIT+AOT+memcheck
# 0/0/0 (docs/plan_string_to_stdlib.md §5.3).
# @subsystem stdlib/string
# @guards P3 print accepts a Str
# @sources codegen_print.c
add_test(
    NAME test_str_p3
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_p3_print.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_p3
        -DMARKER=STRP3
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib: common builtin-string methods reimplemented as pure-LS
# `impl Str` over the byte buffer (find/contains?/starts_with?/ends_with?/substr/
# upper/lower/trim/concat/repeat). Prerequisite to P5 (removing builtin string).
# Pure LS, no compiler change. JIT+AOT+memcheck 0/0/0.
# @subsystem stdlib/string
# @guards builtin string methods reimplemented as pure LS
# @sources lib/std/core/str.lls
add_test(
    NAME test_str_methods
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_methods.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_methods
        -DMARKER=STRM
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib: Str method-port batch 2 — rfind/count/compare/replace/
# pad_left/pad_right + collection returners bytes/split/lines (Vec(Str)/Vec(int)).
# Pure LS, no compiler change. JIT+AOT+memcheck 0/0/0.
# @subsystem stdlib/string
# @guards Str method port batch 2 (rfind/count/compare/replace)
# @sources lib/std/core/str.lls
add_test(
    NAME test_str_methods2
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_methods2.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_methods2
        -DMARKER=STRM2
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib: Str method-port batch 3 — parsing to_int/to_i64/to_float/
# to_bool returning Result(T, Str). Also exercises string-literal -> Str coercion
# in enum-payload position (Err("msg")). JIT+AOT+memcheck 0/0/0.
# @subsystem stdlib/string
# @guards Str method port batch 3 (to_int/to_i64/to_float)
# @sources lib/std/core/str.lls
add_test(
    NAME test_str_methods3
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_methods3.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_methods3
        -DMARKER=STRM3
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# strbench P0/P1/P2 optimization regression — the runtime-accelerated methods
# (find/contains?/count via __ls_str_find, replace single-byte fast-path,
# substr/concat/__clone/+/repeat via __ls_bytecopy, split_view zero-copy views)
# must stay byte-for-byte correct. JIT+AOT+memcheck 0/0/0.
# @subsystem stdlib/string
# @guards strbench P0/P1/P2 runtime-accelerated methods
# @sources runtime/builtins.c
add_test(
    NAME test_str_perf_methods
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_perf_methods.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_perf_methods
        -DMARKER=STRPERF
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib gap①: a string literal passed where a read-only `&Str` is
# expected coerces to a static Str and auto-borrows (method-arg + free-fn-arg).
# JIT+AOT+memcheck 0/0/0 (docs/plan_string_to_stdlib.md §5.1).
# @subsystem stdlib/string
# @guards string literal passed where a read-only &Str is expected
# @sources checker_expr.c:check_expr_ident
add_test(
    NAME test_str_lit_borrow
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_lit_borrow.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_lit_borrow
        -DMARKER=STRLB
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib: f-string interpolation of `Str` values — `f"...{str}..."`
# formats via "%.*s" (length-bounded). Borrowed interps not dropped; owned Str
# rvalue interps dropped after the result is built. Both the f-string-producing
# path and the inline print() path. JIT+AOT+memcheck 0/0/0.
# @subsystem stdlib/string
# @guards f-string interpolation of Str values
# @sources codegen_print.c:codegen_format_string
add_test(
    NAME test_str_fstring_interp
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_fstring_interp.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_fstring_interp
        -DMARKER=STRFI
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# string-to-stdlib P7-mig: impl Hash for Str (std/str.lls, FxHash via std.hash)
# + operator == (impl Eq for Str) => Str usable as Map key (Hash + Eq bounds);
# covers set/get/grow/overwrite/remove with has_drop Str keys. 0/0/0.
# @subsystem stdlib/string
# @guards impl Hash for Str (FxHash) as a map key
# @sources lib/std/core/str.lls
add_test(
    NAME test_str_hash_mapkey
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_hash_mapkey.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_hash_mapkey
        -DMARKER=STRHK
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# P5-0 (docs/plan_p5_remove_builtin_string.md §4): Str.c_str() — NUL-terminated
# view for FFI char* consumers, verified through CRT strlen in every state
# (static literal zero-copy / owned / grown / empty / zero-init nil data).
# Unfreezes std/c.lls + std/os.lls migration (P5-3). JIT+AOT+memcheck 0/0/0.
# @subsystem stdlib/string
# @guards P5-0 Str.c_str() NUL-terminated view
# @sources lib/std/core/str.lls
add_test(
    NAME test_str_cstr
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_cstr_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=str_cstr
        -DMARKER=CSTR
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# Regression: owned Str cloned inside a MODULE function (by-value Map.set arg).
# Guards the has_user_clone forward-declaration in emit_struct_clone_val —
# without it the clone fell back to field-wise shallow copy of the raw *u8
# buffer => double-free (found migrating std/env to Str). 0/0/0.
# @subsystem stdlib/string
# @guards owned Str cloned inside a module function
# @sources codegen_own.c:emit_clone_value
add_test(
    NAME test_str_modfn_clone
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/str_modfn_clone/main.lls
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

# L-013 follow-up: owned (has_drop Str) combinator results consumed as bare
# rvalues (print / discard / chain) must drop at the consuming site, and the
# identity-closure `map(|x| x)` (block-wrapped binder) must not double-free.
add_test(
    NAME test_opt_owned_rvalue
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_opt_owned_rvalue.cmake
)
set_tests_properties(test_opt_owned_rvalue PROPERTIES
    DEPENDS "test_opt_combinator"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Ownership regressions found by memcheck: self-recursive enum ctor must
# transfer ownership of named enum args (was: shared boxes double-freed), and
# a bare `return` inside a match arm over an owned rvalue subject must flush
# the subject temp (was: payload leaked).
add_test(
    NAME test_enum_own_regress
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_own_regress.cmake
)
set_tests_properties(test_enum_own_regress PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Dead-tag sentinel contract (docs/plan_enum_drop_sentinel.md): after an
# enum drop the slot's tag is set to variant_count (a value no live enum
# carries) instead of zeroing the whole slot. Legal re-drop paths land in
# the synthesized __drop's switch default and stay silent.
add_test(
    NAME test_enum_drop_sentinel
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_drop_sentinel.cmake
)
set_tests_properties(test_enum_drop_sentinel PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Ownership regression found by memcheck (2026-07-04): `match struct.field` where
# the field holds a has_drop enum (Option(Str) / user enum with Str payload). The
# AST_FIELD read cloned only has_drop structs, so the enum field read aliased the
# struct's payload heap -> match (owned-rvalue subject) double-freed it against the
# struct's own drop. Fixed by cloning the enum field read (emit_enum_clone_val).
add_test(
    NAME test_field_enum_subject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_field_enum_subject.cmake
)
set_tests_properties(test_field_enum_subject PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# match × ownership interaction stress guardrails (nested match, match in
# loop, early/bare returns from arms, int-switch & cond-chain has_drop
# results, block-tail owned locals, borrow-match clone-into-result) plus
# the aggregate-subject negative check — guardrail corpus for any
# codegen_match.c change (docs/match_codegen_guide.md).
add_test(
    NAME test_match_own_stress
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_match_own_stress.cmake
)
set_tests_properties(test_match_own_stress PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# struct/array with a Block field: a by-value CLONE must DEEP-CLONE the Block
# field's env, else the clone and the source release the same refcount=1 env at
# scope exit -> UAF + double-release. memcheck is blind to the double-release
# (audit B-3), so the sample value-checks captured ints after heap churn.
# Guards emit_struct_clone_val / emit_array_clone_val Block-field deep-clone
# (audit B-1 / BUG-2, symmetric with the enum path 03386d5).
add_test(
    NAME test_struct_block_field_own
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_struct_block_field_own.cmake
)
set_tests_properties(test_struct_block_field_own PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Vec(Block) element-copy ownership: copy()/extend() deep-clone each element's
# closure env (a shared env double-frees at scope exit). Guards the @dup(Block)
# env-clone + Vec.extend-via-@dup fix (match_codegen_guide §7.A).
add_test(
    NAME test_vec_block_own
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_block_own.cmake
)
set_tests_properties(test_vec_block_own PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# OWN-1 (plan_footgun_remediation phase 3): unified owned-rvalue predicate.
# The 7 per-site consumer whitelists ("does this expr yield a fresh owned
# rvalue the consumer must drop?") had drifted; every gap was a real leak
# (combinators in @print's inline f-string, FIELD clones passed to print,
# AST_TRY everywhere, bare f-string/FIELD/INDEX/lowered-`+` discards, owned
# MATCH/TRY objects spilled for field reads). Guards
# cg_expr_yields_owned_rvalue / cg_expr_is_fresh_rvalue_kind.
add_test(
    NAME test_own_rvalue_sites
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_own_rvalue_sites.cmake
)
set_tests_properties(test_own_rvalue_sites PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Array-literal element ownership + single evaluation: the AST_ARRAY_LIT
# const-fold probe used to emit every element to test LLVMIsConstant, then
# the var-decl fallback re-emitted them (double side effects; the first
# emission's owned results leaked). Guards the AST pre-scan fix.
add_test(
    NAME test_array_lit_own
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_array_lit_own.cmake
)
set_tests_properties(test_array_lit_own PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Block container-ownership protocol lint (stage 5, audit B-2): user methods
# that collide with the reserved container method names (block_protocol.h)
# while carrying a Block in the signature get a checker WARNING (not error);
# std modules are path-exempt. Pins 3 firing shapes + 2 silent shapes +
# unaffected runtime + std exemption.
add_test(
    NAME test_block_protocol_lint
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_block_protocol_lint.cmake
)
set_tests_properties(test_block_protocol_lint PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Stage 12b (audit M-7), upgraded to an ERROR by L-020: a value-producing
# scalar match without a `_` arm is rejected — an unmatched subject would
# silently yield a zeroed result. Pins 2 reject shapes (int/char) + 4 exempt
# shapes + pinned runtime of the exempt program.
add_test(
    NAME test_match_scalar_exhaust
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_match_scalar_exhaust.cmake
)
set_tests_properties(test_match_scalar_exhaust PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# L-019 (audit OWN-6): has_drop enum bindings MOVE like every other has_drop
# type; @dup keeps an explicit deep copy; use-after-move is a checker error.
add_test(
    NAME test_enum_move_semantics
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_move_semantics.cmake
)
set_tests_properties(test_enum_move_semantics PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# __ls_bytecopy → llvm.memcpy primitive (form ③): IR lowering both modes,
# runtime parity under LS_NO_MEMCPY_PRIM, memcheck clean.
add_test(
    NAME test_memcpy_prim
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcpy_prim.cmake
)
set_tests_properties(test_memcpy_prim PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Owned Block rvalue lifetime (P3 block-refcount) + Map(Block) rehash (O2). A
# factory call / force-unwrap of a container-get yields an OWNED closure env
# tracked as a statement temp: discarded => released, bound => owner releases.
# Guards O1 (make()() leak), O3 (Vec/Map get!() consistency) and O2 (Map rehash
# relocation double-free). JIT + AOT + memcheck 0/0/0.
add_test(
    NAME test_block_refcount
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_block_refcount.cmake
)
set_tests_properties(test_block_refcount PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Kill-switch A/B parity for optimization passes (LS_NO_ELIDE /
# LS_NO_INTERNALIZE / LS_NO_ENUM_RANGE): the "off" path must produce
# identical program output — a silently-broken pass fails loudly here.
add_test(
    NAME test_opt_parity
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_opt_parity.cmake
)
set_tests_properties(test_opt_parity PROPERTIES
    DEPENDS "test_match_own_stress"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Move-only resource types (Destroy + raw ptr/object field, no Clone) matched
# out of an owned enum subject by move (not clone) — enables RAII handles like
# io.File with the inline `match open(p) { Ok(f) => ... }`.
add_test(
    NAME test_move_only
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_move_only.cmake
)
set_tests_properties(test_move_only PROPERTIES
    DEPENDS "test_destroy"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# print(enum) readable rendering (Variant / Variant(payload), Option/Result,
# enum fields in structs) + owned enum rvalue not leaked.
add_test(
    NAME test_enum_print
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_print.cmake
)
set_tests_properties(test_enum_print PROPERTIES
    DEPENDS "test_move_only"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# io.File RAII auto-close under memcheck. Runs io_basic_test.lls (same sample
# as test_e4_io_basic, which writes io_basic_test.tmp relative to cwd); the
# driver isolates itself in ${CMAKE_BINARY_DIR}/io_raii_scratch so the two
# tests never share the tmp file — the old DEPENDS-on-the-io-chain serialization
# is no longer needed (parallel-safety audit 2026-07-05, see header of this file).
add_test(
    NAME test_io_raii
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_io_raii.cmake
)
set_tests_properties(test_io_raii PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# std.core.sink Stage A: Sink write helpers + __sink_flush redirect (stdout/
# stderr/file) + io.file(&!File) ownership bridge + close-on-switch, memcheck.
# (docs/plan_print_sink.md). Writes sink_smoke_*.tmp in the build dir.
add_test(
    NAME test_sink_basic
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_sink_basic.cmake
)
set_tests_properties(test_sink_basic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# std.core.sink Stage C-1: set_sink redirects print() itself (emit_printf ->
# __ls_printf -> current stream). Byte-exact captured content + close-on-switch.
add_test(
    NAME test_sink_redirect
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_sink_redirect.cmake
)
set_tests_properties(test_sink_redirect PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# std.core.sink Stage C-2: print(x) honors Show (Show struct -> Show format; plain
# struct -> structural; POD/Str fast path). Exact output + memcheck.
add_test(
    NAME test_sink_print_show
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_sink_print_show.cmake
)
set_tests_properties(test_sink_print_show PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# std.core.sink Stage D: f-string interpolation honors Show (f"{showStruct}").
add_test(
    NAME test_sink_fstring_show
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_sink_fstring_show.cmake
)
set_tests_properties(test_sink_fstring_show PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# std.core.sink Stage F: `@print` syntax (dual-track with bare print).
add_test(
    NAME test_sink_atprint
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_sink_atprint.cmake
)
set_tests_properties(test_sink_atprint PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# std.core.sink Stage F: bare `print(...)` is retired -> clean compile error.
add_test(
    NAME test_bare_print_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/bare_print_reject.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_bare_print_reject.cmake
)

# `lls fmt` must be behavior-preserving. An fmt round-trip sweep over all 634
# samples found two inverse @-token spacing bugs in format.c space_between():
# the complete tokens @time/@bench were glued to their operand (producing
# `@timefib(10)`, which does not parse), while the @print call form was NOT
# glued (`@print (v)`). Oracle: run(fmt(x)) stdout == run(x) stdout, plus
# idempotence.
add_test(
    NAME test_fmt_roundtrip
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/fmt_roundtrip_attime.lls
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_fmt_roundtrip.cmake
)

# `fmt --stdout` on a file the formatter refuses (preprocessor directives) used
# to write zero bytes and exit 0, so `lls fmt f --stdout > f` destroyed it.
# In-place mode was always correct. Pins verbatim pass-through.
add_test(
    NAME test_fmt_directive_stdout
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/fmt_directive_stdout.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_fmt_directive_stdout.cmake
)

# Two ownership diagnostics whose text documents the movable-type set and the
# capture strategies to the user. Both had drifted away from the code they
# describe (retired builtin type names, a by-ref capture strategy that no longer
# exists, missing has_drop-enum/Block movable cases, retired __move spelling).
# Pins the true wording and asserts the stale wording never returns.
add_test(
    NAME test_diag_ownership_wording
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/diag_ownership_wording_reject.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_diag_ownership_wording.cmake
)

# Vec(T).sort / sort_by — O(n log n) stable merge sort (bugs/27_vec.txt #2):
# larger n, explicit stability, has_drop elements, degenerate sizes.
# @subsystem stdlib/containers
# @guards bugs/27 #2 Vec.sort / sort_by O(n log n) stable merge sort
# @sources lib/std/core/vec.lls
add_test(
    NAME test_vec_sort
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/vec_sort_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=vec_sort
        -DMARKER=VECSORT
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# A-1 (docs/plan_runtime_primitives.md): std.c.{malloc,realloc,free,abort}
# reachable by canonical path, incl. from generic method bodies instantiated
# at the consumer site. JIT + AOT + memcheck.
# @subsystem codegen/ffi
# @guards plan_runtime_primitives A-1 std.c.{malloc,realloc,free,abort}
# @sources codegen_call.c:cg_expr_call
add_test(
    NAME test_stdc_prim
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/stdc_prim_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=stdc_prim
        -DMARKER=STDCPRIM
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# A+B (docs/plan_fma_coldpath.md): FMA contract on FP arithmetic + noreturn/cold
# on the abort sink. FP dot product (contract) + in-bounds v[i] (cold path).
# JIT+AOT+memcheck; FMA must not change the tolerance-checked result.
# @subsystem codegen/optimization
# @guards FMA contract on FP arithmetic + noreturn/cold paths
# @sources optpipe.c:ls_opt_run_passes
add_test(
    NAME test_fma_coldpath
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/fma_coldpath_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=fma_coldpath
        -DMARKER=FMACOLD
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# #1 borrow → LLVM param attributes (docs/plan_borrow_noalias.md): functions
# with &T/&!T/&self/&!self borrows get nonnull/dereferenceable/align/readonly/
# nocapture stamped. Self-verifying correctness gate (the attrs must not
# miscompile). JIT+AOT+memcheck.
# @subsystem codegen/optimization
# @guards borrows lowered to LLVM parameter attributes
# @sources codegen_noalias.c
add_test(
    NAME test_borrow_attrs
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_attrs_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=borrow_attrs
        -DMARKER=BORROWATTR
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# Regression: `for i in 0..n` / `for i in n` with an i64 bound. The range loop
# counter is i32; codegen must coerce i64 bounds to i32 or the cond block emits
# a mismatched `icmp i32, i64` and fails module verification. JIT+AOT+memcheck.
# @subsystem stdlib/containers
# @guards `for i in 0..n` with an i64 bound
# @sources checker_stmt.c:check_stmt
add_test(
    NAME test_forin_i64_bound
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/forin_i64_bound.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=forin_i64_bound
        -DMARKER=FORINI64
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# Optimization-pipeline (#2, docs/plan_opt_pipeline.md): a self-verifying
# reduction loop must produce identical output across -O0/-O2/-O3 (JIT) and
# -O0 / -O3 --native (AOT). Guards the LsOptConfig plumbing + CLI parsing.
add_test(
    NAME test_opt_levels
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/opt_levels_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=opt_levels
        -P ${CMAKE_SOURCE_DIR}/tests/test_opt_levels.cmake
)
set_tests_properties(test_ident_suffix PROPERTIES DEPENDS "test_fstring_spec")

# std/plot.lls (plot Phase 1 skeleton): data model + builder API + summary output.
# Exercises deeply-nested has_drop (Figure -> vec(Axes) -> vec(LineStyle) ->
# vec(f64)), Axes MOVE into Figure, and deep-copy reads. Self-verifying sample
# prints "PLOT PASS". JIT + AOT correctness + memcheck.
# @subsystem stdlib/plot
# @guards plot Phase 1 data model + builder API
# @sources lib/std/chart/plot.lls
add_test(
    NAME test_plot_skeleton
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_skeleton_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_skeleton
        -DMARKER=PLOT
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plot.lls tick engine (plot Phase 2): standard Heckbert nice-numbers,
# generate_ticks, map_x/map_y, update_limits, finalize (margins + ticks).
# Self-verifying sample prints "TICKS PASS". JIT + AOT correctness + memcheck.
# @subsystem stdlib/plot
# @guards plot Phase 2 Heckbert nice-numbers tick engine
# @sources lib/std/chart/plot.lls
add_test(
    NAME test_plot_ticks
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_ticks_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_ticks
        -DMARKER=TICKS
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plot.lls SVG backend (plot Phase 2b): layout, polyline, axes, ticks, grid,
# title/labels (escaped), scatter circles. Self-verifying sample prints
# "SVG PASS". JIT + AOT correctness + memcheck.
# @subsystem stdlib/plot
# @guards plot Phase 2b SVG backend
# @sources lib/std/chart/plot.lls
add_test(
    NAME test_plot_svg
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_svg_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_svg
        -DMARKER=SVG
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plot.lls Text/ASCII backend (plot Phase 2c): vec(string) grid, _put_char,
# DDA line rasterization, y/x labels + axes. Self-verifying sample prints
# "TEXT PASS". JIT + AOT correctness + memcheck.
# @subsystem stdlib/plot
# @guards plot Phase 2c text/ASCII backend
# @sources lib/std/chart/plot.lls
add_test(
    NAME test_plot_text
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_text_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_text
        -DMARKER=TEXT
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plottl.lls timeline (plot TL-1): swimlane SVG (rects + <title> + time
# ticks) and text backend. Self-verifying sample prints "TL PASS".
# JIT + AOT correctness + memcheck.
# @subsystem stdlib/plot
# @guards plot TL-1 swimlane SVG timeline
# @sources lib/std/chart/plottl.lls
add_test(
    NAME test_plot_timeline
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_timeline_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_timeline
        -DMARKER=TL
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plottl.lls CSV input (plot TL-1.5): parse_timeline_csv (header/blank skip,
# auto palette color), load_timeline_csv. Self-verifying sample prints
# "CSV PASS". JIT + AOT correctness + memcheck.
# @subsystem stdlib/plot
# @guards plot TL-1.5 parse_timeline_csv
# @sources lib/std/chart/plottl.lls
add_test(
    NAME test_plot_csv
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_csv_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_csv
        -DMARKER=CSV
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plottl.lls CPU timeline (plot TL-2): CpuSchedEvent/CpuTopology, HT coloring
# (cpu_hue/cpu_color via plotfmt.hsv_to_hex), SVG swimlanes + diagonal-stripe
# HT patterns + CPU legend, text backend. Self-verifying sample prints
# "TL2 PASS". JIT + AOT correctness + memcheck.
# @subsystem stdlib/plot
# @guards plot TL-2 CPU timeline + HT colouring
# @sources lib/std/chart/plottl.lls
add_test(
    NAME test_plot_cpu
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_cpu_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_cpu
        -DMARKER=TL2
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plottl.lls scrollable HTML wrapper (plot TL-2b): cpu_timeline_html emits a
# self-contained single-file HTML with a fixed lane column + horizontally
# scrollable wide SVG (pure CSS overflow, zero JS). Self-verifying sample prints
# "HTML PASS". JIT + AOT correctness + memcheck.
# @subsystem stdlib/plot
# @guards plot TL-2b scrollable HTML wrapper
# @sources lib/std/chart/plottl.lls
add_test(
    NAME test_plot_html
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_html_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_html
        -DMARKER=HTML
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# std/plottl.lls aggregated CPU timeline (plot TL-3): cpu_timeline_aggregated
# buckets time into windows and renders the per-(thread,window) dominant CPU.
# Self-verifying sample prints "AGG PASS". JIT + AOT correctness + memcheck.
# @subsystem stdlib/plot
# @guards plot TL-3 aggregated CPU timeline
# @sources lib/std/chart/plottl.lls
add_test(
    NAME test_plot_agg
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/plot_agg_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=plot_agg
        -DMARKER=AGG
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# B-4 cross-module struct literal `mod.Type{...}` + struct field defaults
# (options-struct pattern). main.lls imports opt.lls and constructs opt.PlotOpts{}.
# Self-verifying sample prints "MSL PASS". JIT + AOT correctness + memcheck.
# @subsystem modules
# @guards B-4 cross-module struct literal mod.Type{...}
# @sources checker_expr.c:check_expr_new_expr
add_test(
    NAME test_mod_struct_literal
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/modstructlit/main.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=mod_struct_literal
        -DMARKER=MSL
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# Struct field defaults with partial initialisation: `S { a: 1 }` where the
# remaining fields carry declared defaults.
#
# The subtlety is that omitted fields are not left alone -- the literal must
# still produce a fully initialised value, so the compiler synthesises the
# missing stores from the declaration. Miss one and the field holds whatever was
# on the stack, which for a has_drop field means the destructor later frees a
# garbage pointer.
#
# @subsystem codegen/struct
# @guards struct field defaults + partial initialization
# @sources checker_expr.c:check_expr_new_expr
add_test(
    NAME test_struct_field_defaults
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/struct_field_defaults_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=struct_field_defaults
        -DMARKER=SFDEF
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# struct field defaults v2: empty/literal vec defaults (vec(T) x = [] / [1,2,3])
# built in place at the construction site, and nested struct defaults (Sub{}).
# Self-verifying sample prints "SFD2 PASS". JIT + AOT correctness + memcheck.
# @subsystem codegen/struct
# @guards struct field defaults v2 (empty/literal vec defaults)
# @sources checker_expr.c:check_expr_new_expr
add_test(
    NAME test_struct_field_defaults_v2
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/struct_field_defaults_v2_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=struct_field_defaults_v2
        -DMARKER=SFD2
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)

# VR-LIM-012: user Vec(T) struct field defaults use __from_list / zero init.
# @subsystem codegen/struct
# @guards VR-LIM-012 user Vec(T) field defaults via __from_list
# @sources checker_expr.c:check_expr_new_expr
add_test(
    NAME test_struct_field_defaults_uservec
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/struct_field_defaults_uservec_test.lls
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
# @subsystem language/syntax
# @guards positional default parameters
# @sources checker_call.c:check_expr_call
add_test(
    NAME test_fn_default_params
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/fn_default_params_test.lls
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
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/map_keys.lls
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
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/shortcircuit_temp.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=shortcircuit_temp
        -P ${CMAKE_SOURCE_DIR}/tests/test_shortcircuit_temp.cmake
)

# Container value-semantics matrix (vec/map first-class — Phase 0 safety net).
# Baseline-clean cases, locked under JIT memcheck. Broken target cases (D/F) are
# tracked in tests/samples/cmatrix/MATRIX.md and registered as they turn green.
# @subsystem stdlib/containers
# @guards container value-semantics matrix (vec/map first-class Phase 0 safety net)
# @sources codegen_own.c:cg_store_owned, codegen_own.c:emit_drop_value
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
            -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/cmatrix/${cm}.lls
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
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/cmatrix/me01_move_elision.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=move_elision
        -P ${CMAKE_SOURCE_DIR}/tests/test_move_elision.cmake
)

# A1 clone-elision: when a has_drop local's by-value consumption is its LAST
# use, the defensive clone is downgraded to a move.
#
# The corpus was written BEFORE the pass existed, against the pure-clone
# semantics, so its output is the contract the optimisation must preserve
# byte for byte. It also pins the exclusions -- a later re-read, a loop back
# edge, a one-arm read, a closure capture, twin arguments -- because each of
# those makes the last-use claim false, and eliding there is a use-after-free
# rather than a speedup.
#
# Note that clone elision is observable through destructor side effects (a
# program that prints in `~` sees fewer calls), so `LS_NO_ELIDE=1` is a
# documented output difference, not a parity failure.
#
# @subsystem codegen/ownership
# @guards A1 clone-elision
# @sources checker_elide.c:checker_elide_last_use
add_test(
    NAME test_clone_elision
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/clone_elision.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=clone_elision
        -P ${CMAKE_SOURCE_DIR}/tests/test_clone_elision.cmake
)

# A1-v2 extends clone-elision to owned by-value has_drop PARAMETERS whose last
# use forwards them onward. The forwarding chain through `Map.upsert` was the
# motivating case: it cloned at every hop.
#
# The pitfall this corpus guards is ordering, not semantics -- a generic method
# instance must have its function type stamped before the elision pass runs, or
# the pass sees no type and silently declines to elide.
#
# @subsystem codegen/ownership
# @guards A1-v2 clone-elision param coverage
# @sources checker_elide.c:checker_elide_last_use
add_test(
    NAME test_clone_elision_param
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/clone_elision_param.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=clone_elision_param
        "-DPASS_TOKEN=CEP PASS"
        -P ${CMAKE_SOURCE_DIR}/tests/test_clone_elision.cmake
)

# Phase G (Block env deep-clone, resolves L-007): copying a Block out of a vec
# element / struct field / map value deep-clones the closure env so the
# destination owns an independent one. Asserts JIT + AOT correctness + memcheck.
add_test(
    NAME test_phase_g_closure
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/closure_g.lls
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
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/struct_byval_arg.lls
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
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/struct_field_readthrough.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=struct_field_readthrough
        -P ${CMAKE_SOURCE_DIR}/tests/test_struct_readthrough.cmake
)

# @subsystem runtime/memcheck
# @guards AOT --memcheck link + atexit report path
# @sources runtime/memcheck.c:ls_mc_report
#
# AOT memcheck end-to-end test: compile a sample with `ls compile --memcheck`,
# run the produced binary, assert the runtime report shows "OK clean".
# Driven by a cmake -P script for cross-platform process invocation.
add_test(
    NAME test_memcheck_aot
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/memcheck_phase_a.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_aot.cmake
)
# Order this after test_memory so the JIT memcheck path is already validated.
set_tests_properties(test_memcheck_aot PROPERTIES DEPENDS "test_memory")

# Edge cases for the allocator tracker itself rather than for a language
# feature: allocation patterns that stress the tracking table (many small
# allocations, reallocation chains, frees in a different order than the
# allocations). A tracker that miscounts here would make every other memcheck
# test either falsely green or falsely red.
#
# @subsystem runtime/memcheck
# @guards memcheck edge cases under JIT
# @sources runtime/memcheck.c
add_test(
    NAME test_memcheck_edge_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/memcheck_edge.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_memcheck_edge_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_memcheck_edge_jit PROPERTIES DEPENDS "test_memory")

# M-3 (docs/memory_model_overhaul.md): the unified ownership-transfer API.
#
# Before M-3 every container operation that took ownership -- vec.push,
# vec.insert, map.set, `v[i]=`, `a[i]=`, enum ctor, struct ctor, match binder,
# closure capture -- hand-wrote its own move/clone/mark-moved branch. Adding a
# container operation meant editing five or more places, and missing one was
# a leak or a double free rather than a compile error. M-3 collapsed those into
# a single decision point; this corpus drives the transfer paths through it and
# requires the allocator report to balance.
#
# @subsystem runtime/memcheck
# @guards M-3 unified ownership-transfer API under memcheck
# @sources runtime/memcheck.c:ls_mc_report
add_test(
    NAME test_mem_m3_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m3_xfer_unified.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_mem_m3_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_mem_m3_jit PROPERTIES DEPENDS "test_memcheck_edge_jit")

# M-4: the ownership-transfer decision table, applied symmetrically.
#
# M-3 gave the transfer one entry point; M-4 audited the callers. The corpus
# walks var-decl / assignment / return over every type kind, because the
# historical bugs were never in the common case -- they were the one branch
# somebody forgot when a new type kind appeared (`v[i]=b` for a has_drop
# element being the canonical example).
#
# @subsystem runtime/memcheck
# @guards M-4 ownership-transfer decision table
# @sources codegen_own.c:cg_store_owned
add_test(
    NAME test_mem_m4_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m4_matrix.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_mem_m4_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_mem_m4_jit PROPERTIES DEPENDS "test_mem_m3_jit")

# M-4.5: a has_drop struct temporary reached through `vec[i].field`.
#
# Reading a field out of an element produces an intermediate struct value that
# nothing names. It has to be dropped at the end of the statement, and before
# the fix it simply was not -- its Str field leaked on every read. The shape is
# worth its own corpus because the leak is invisible without memcheck: the
# value read is correct, the program prints the right thing, and only the
# allocator knows.
#
# @subsystem runtime/memcheck
# @guards M-4.5 vec[i].field has_drop struct temporaries
# @sources codegen_own.c:cg_push_temp_drop
add_test(
    NAME test_mem_m4_5_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m4_5_drop_temp.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_mem_m4_5_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_mem_m4_5_jit PROPERTIES DEPENDS "test_mem_m4_jit")

# M-4.5 again, on the AOT path (`lls compile --memcheck` + run the exe).
#
# Same corpus as the JIT twin on purpose. The two paths link different runtimes
# and lay out temporaries differently, so an ownership fix that lands in one and
# not the other is a real and previously-seen failure mode.
#
# @subsystem runtime/memcheck
# @guards M-4.5 same corpus on the AOT --memcheck path
# @sources runtime/memcheck.c:ls_mc_report
add_test(
    NAME test_mem_m4_5_aot
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/test_mem_m4_5_drop_temp.lls
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
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/vec_builtin_syntax_reject.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_vec_builtin_syntax_reject.cmake
)
set_tests_properties(test_vec_builtin_syntax_reject PROPERTIES DEPENDS "test_mem_m5_neg")

# M6-1: builtin map(K,V) syntax is no longer accepted by the frontend.
add_test(
    NAME test_map_builtin_syntax_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/map_builtin_syntax_reject.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_map_builtin_syntax_reject.cmake
)
set_tests_properties(test_map_builtin_syntax_reject PROPERTIES DEPENDS "test_vec_builtin_syntax_reject")

# P5-4 S-1 (docs/plan_p5_remove_builtin_string.md §8): the builtin `string` type
# keyword was removed from the frontend — `string x` must be rejected.
add_test(
    NAME test_string_type_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/string_type_reject.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_string_type_reject.cmake
)
set_tests_properties(test_string_type_reject PROPERTIES DEPENDS "test_map_builtin_syntax_reject")

# A-1 (docs/bugs_deferred_p5_4.md §1): nested fn/struct/impl/enum/trait/module
# definitions inside a function body are rejected cleanly at parse time.
add_test(
    NAME test_nested_decl_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/nested_fn_decl_reject.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_nested_decl_reject.cmake
)
set_tests_properties(test_nested_decl_reject PROPERTIES DEPENDS "test_string_type_reject")

# A-2 (docs/bugs_deferred_p5_4.md §2): explicit `.__drop()` calls are rejected.
add_test(
    NAME test_explicit_drop_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/explicit_drop_reject.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_explicit_drop_reject.cmake
)
set_tests_properties(test_explicit_drop_reject PROPERTIES DEPENDS "test_nested_decl_reject")

# A-3 (docs/bugs_deferred_p5_4.md §3): forward struct field reference must error
# gracefully (clean "unknown type"), not segfault.
add_test(
    NAME test_forward_ref_struct_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/forward_ref_struct_reject.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_forward_ref_struct_reject.cmake
)
set_tests_properties(test_forward_ref_struct_reject PROPERTIES DEPENDS "test_explicit_drop_reject")

# A-4 (docs/bugs_deferred_p5_4.md §4): chained-op receiver spill inside a
# match-arm `if` body must not free uninitialized stack on the fall-through path.
# JIT + AOT correctness + memcheck 0/0/0.
# @subsystem codegen/match
# @guards bugs_deferred_p5_4 A-4 chained-op receiver spill inside a match
# @sources codegen_own.c:cg_spill_owned_rvalue
add_test(
    NAME test_match_concat_temp
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/match_concat_temp_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=match_concat_temp
        -DMARKER=MATCHCAT
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)
set_tests_properties(test_match_concat_temp PROPERTIES DEPENDS "test_forward_ref_struct_reject")

# B-3 (docs/bugs_deferred_p5_4.md §B-3): user `impl` on an imported struct emits
# method symbols under the struct's prefixed llvm_name. JIT+AOT+memcheck 0/0/0.
# @subsystem modules
# @guards bugs_deferred_p5_4 B-3 user impl on an imported struct
# @sources codegen_decl.c:cg_struct_llvm_by_bare
add_test(
    NAME test_impl_imported_struct
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/impl_imported_struct_test.lls
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
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/fs_test.lls
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
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/deep_copy_all_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_struct_print_str.cmake
)
set_tests_properties(test_struct_print_str PROPERTIES DEPENDS "test_fs")

# Regression: compiler-internal private constant vs user global NAME collision
# (heap corruption / "invalid free"). A global named fmt/Strlit/rawstr must not
# alias an internal .rodata constant. JIT+AOT exact output + memcheck 0/0/0.
add_test(
    NAME test_global_name_collision
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/global_name_collision_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_global_name_collision.cmake
)

# A-FLIP (docs/plan_runtime_primitives.md): bare malloc/free/realloc/abort are no
# longer global builtins (moved to std.c) — a bare malloc(...) must be rejected.
add_test(
    NAME test_malloc_builtin_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/malloc_builtin_reject.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_malloc_builtin_reject.cmake
)

# Phase 0 borrow-extension (docs/plan_borrow_extension.md §3): borrows escaping
# the parameter position must be a clean compile-time rejection, never a latent
# IR crash or a silently-accepted dangling landmine. One driver, five samples.
# @subsystem checker/borrow
# @guards Phase 0 escaping borrows rejected
# @sources checker_borrow.c:checker_reject_borrow_return
add_test(
    NAME test_borrow_return_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_return_reject.lls
        "-DEXPECT=borrows cannot escape via return"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# test_borrow_method_return_reject
#
# @subsystem checker/borrow
# @guards method returning a borrow rejected
# @sources checker_borrow.c:checker_reject_borrow_return
add_test(
    NAME test_borrow_method_return_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_method_return_reject.lls
        "-DEXPECT=must derive from"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# test_borrow_field_reject
#
# @subsystem checker/borrow
# @guards escaping field borrow rejected
# @sources checker_borrow.c:checker_reject_borrow_return
add_test(
    NAME test_borrow_field_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_field_reject.lls
        "-DEXPECT=struct fields cannot be borrows yet"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# test_borrow_enum_payload_reject
#
# @subsystem checker/borrow
# @guards escaping enum-payload borrow rejected
# @sources checker_borrow.c:checker_reject_borrow_return
add_test(
    NAME test_borrow_enum_payload_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_enum_payload_reject.lls
        "-DEXPECT=enum payloads cannot be borrows yet"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# test_borrow_typearg_reject
#
# @subsystem checker/borrow
# @guards borrow used as a type argument rejected
# @sources checker_generics.c:resolve_type_node
add_test(
    NAME test_borrow_typearg_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_typearg_reject.lls
        "-DEXPECT=a borrow type cannot be a generic type argument"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)

# Phase 1 borrow-extension (docs/plan_borrow_extension.md §3): named non-escaping
# local borrows `&T r = &x`. Positive: read / writable-mutate / re-borrow /
# has_drop referent, JIT+AOT+memcheck 0/0/0.
# @subsystem checker/borrow
# @guards Phase 1 named non-escaping local borrows
# @sources checker_borrow.c:checker_place_root_symbol
add_test(
    NAME test_borrow_local
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_local_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=borrow_local
        -DMARKER=LB
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)
# Moving out of a borrow must be rejected.
#
# A `&T` names something it does not own, so a move through it would hand out an
# ownership it never had and leave the real owner holding freed memory. This is
# the counterpart to the escape rejections in this family: those stop a borrow
# from outliving its referent, this stops it from destroying it.
#
# @subsystem checker/borrow
# @guards borrow-escape Phase 0
# @sources checker_borrow.c:checker_place_root_symbol
add_test(
    NAME test_borrow_local_move_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_local_move_reject.lls
        "-DEXPECT=is borrowed by a live local borrow"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# test_borrow_local_copyout_reject
#
# @subsystem checker/borrow
# @guards copy-out of a borrow rejected
# @sources checker_borrow.c:checker_place_root_symbol
add_test(
    NAME test_borrow_local_copyout_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_local_copyout_reject.lls
        "-DEXPECT=value cannot be copied out"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# test_borrow_local_capture_reject
#
# @subsystem checker/borrow
# @guards borrow captured by a closure rejected
# @sources checker_borrow.c:checker_place_root_symbol
add_test(
    NAME test_borrow_local_capture_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_local_capture_reject.lls
        "-DEXPECT=is borrowed by a live local borrow"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)

# Phase 2 borrow-extension spike (docs/plan_borrow_extension.md §3): return
# borrows under single-input lifetime elision (`fn(&self) -> &field`). Positive:
# immediate use / bind to a Phase-1 local / writable-borrow return + mutate /
# has_drop field, JIT+AOT+memcheck 0/0/0.
# @subsystem checker/borrow
# @guards Phase 2 single-input return-borrow elision
# @sources checker_borrow.c:checker_place_root_symbol
add_test(
    NAME test_borrow_return
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_return_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=borrow_return
        -DMARKER=BR
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)
# Negative: multi-borrow-input method (ambiguous elision) + moving the pinned
# receiver of a bound borrow return (would dangle). (Returning a borrow of a
# local is covered by test_borrow_method_return_reject above.)
# @subsystem checker/borrow
# @guards multi-borrow-input elision ambiguity rejected
# @sources checker_borrow.c:checker_reject_borrow_return
add_test(
    NAME test_borrow_return_multiinput_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_return_multiinput_reject.lls
        "-DEXPECT=exactly one borrow input"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# test_borrow_return_pin_reject
#
# @subsystem checker/borrow
# @guards moving a pinned borrow source rejected
# @sources checker_borrow.c:checker_try_mark_moved
add_test(
    NAME test_borrow_return_pin_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_return_pin_reject.lls
        "-DEXPECT=is borrowed by a live local borrow"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# test_borrow_return_temp_reject
#
# @subsystem checker/borrow
# @guards borrow of a temporary returned, rejected
# @sources checker_borrow.c:checker_reject_borrow_return
add_test(
    NAME test_borrow_return_temp_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_return_temp_reject.lls
        "-DEXPECT=source is a temporary"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# Phase 2 breadth: free-function `fn(&T) -> &T` returning a local (dangling) +
# transitively-chained borrow return through a temporary receiver.
# @subsystem checker/borrow
# @guards free function returning a local borrow (dangling) rejected
# @sources checker_borrow.c:checker_reject_borrow_return
add_test(
    NAME test_borrow_return_freelocal_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_return_freelocal_reject.lls
        "-DEXPECT=must derive from"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# test_borrow_return_chain_temp_reject
#
# @subsystem checker/borrow
# @guards borrow of a chained temporary rejected
# @sources checker_borrow.c:checker_reject_borrow_return
add_test(
    NAME test_borrow_return_chain_temp_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/borrow_return_chain_temp_reject.lls
        "-DEXPECT=must derive from"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)

# Borrowed slices `&[T]` — {ptr,len} views over a Vec(T) range. Positive:
# creation / index / len / for-in / slice-as-param / sub-slice / has_drop elems,
# JIT+AOT+memcheck 0/0/0. Negative: return / struct-field would dangle → reject.
# @subsystem checker/borrow
# @guards borrowed slices &[T] as {ptr,len} views
# @sources codegen_expr.c:cg_expr_index
add_test(
    NAME test_slice
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/slice_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=slice
        -DMARKER=SL
        -P ${CMAKE_SOURCE_DIR}/tests/test_plotfmt.cmake
)
# test_slice_return_reject
#
# @subsystem checker/borrow
# @guards slice return rejected
# @sources checker_borrow.c:checker_reject_borrow_return
add_test(
    NAME test_slice_return_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/slice_return_reject.lls
        "-DEXPECT=cannot return a slice"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# test_slice_field_reject
#
# @subsystem checker/borrow
# @guards slice as a struct field rejected
# @sources checker_decl.c:check_struct_decl
add_test(
    NAME test_slice_field_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/slice_field_reject.lls
        "-DEXPECT=cannot be slices"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# Slice return under single-input elision: a view of a LOCAL still dangles → reject.
# @subsystem checker/borrow
# @guards returning a view of a local rejected
# @sources checker_borrow.c:checker_reject_borrow_return
add_test(
    NAME test_slice_return_local_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/slice_return_local_reject.lls
        "-DEXPECT=must derive from"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)
# Storing through a read-only slice must be rejected (only &!array(T) is writable).
# @subsystem checker/borrow
# @guards store through a read-only slice rejected
# @sources codegen_stmt.c:cg_stmt_assign
add_test(
    NAME test_slice_readonly_store_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/slice_readonly_store_reject.lls
        "-DEXPECT=read-only slice"
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_escape_reject.cmake
)

# M-6: the permanent regression baseline for the whole M-1..M-5 overhaul.
#
# One corpus of 30+ extreme scenarios, each mapped to a fix: dynamic string
# arguments to print (M-1's double-registration), borrowed strings across
# function boundaries (M-2's `cap == 0` split), element swap through index
# assignment (M-4), enum/struct construction from a borrowed string, string
# allocation inside a loop with `break`, match binders returned, `try` early-exit
# paths, and closures capturing strings and structs.
#
# It is deliberately one big program rather than 30 small ones: several of the
# original bugs only appeared when allocations from different features
# interleaved in the same scope.
#
# @subsystem runtime/memcheck
# @guards M-6 memory-safety regression baseline (JIT)
# @sources runtime/memcheck.c:ls_mc_report
add_test(
    NAME test_mem_overhaul_jit
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/memcheck_overhaul.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=test_mem_overhaul_jit
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_jit.cmake
)
set_tests_properties(test_mem_overhaul_jit PROPERTIES DEPENDS "test_mem_m4_5_jit")

# M-6 baseline on the AOT path. See the JIT twin for what the corpus covers;
# the reason for running it twice is that JIT and AOT link different runtimes.
#
# @subsystem runtime/memcheck
# @guards M-6 memory-safety regression baseline (AOT)
# @sources runtime/memcheck.c:ls_mc_report
add_test(
    NAME test_mem_overhaul_aot
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/memcheck_overhaul.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_aot.cmake
)
set_tests_properties(test_mem_overhaul_aot PROPERTIES DEPENDS "test_mem_overhaul_jit")

# Phase E.1 end-to-end test: extern struct + extern { } block + extern fn without 'from'
add_test(
    NAME test_extern_struct
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/extern_struct_test.lls
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
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/extern_struct_byval.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_extern_struct_byval.cmake
)
set_tests_properties(test_extern_struct_byval PROPERTIES DEPENDS "test_extern_struct")
endif()

# Phase E.3.1: errno() builtin
# @subsystem codegen/ffi
# @guards Phase E.3.1 errno() builtin
# @sources codegen_call.c:cg_expr_call
add_test(
    NAME test_e3_errno
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/errno_test.lls
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
# @subsystem frontend/parser
# @guards Phase E.3.2 conditional compilation #if WINDOWS/LINUX/MACOS
# @sources scanner.c:skip_whitespace
add_test(
    NAME test_e3_condcomp
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/condcomp_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=condcomp
        -P ${CMAKE_SOURCE_DIR}/tests/test_e3_glue.cmake
)
set_tests_properties(test_e3_condcomp PROPERTIES DEPENDS "test_e3_errno")

# Phase E.3.4: stdlib path resolution (LS_HOME/stdlib lookup)
# @subsystem modules
# @guards Phase E.3.4 stdlib path resolution (LS_HOME)
# @sources module.c:module_load
add_test(
    NAME test_e3_stdlib_path
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/stdlib_path_test/main.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=stdlib_path
        -P ${CMAKE_SOURCE_DIR}/tests/test_e3_glue.cmake
)
set_tests_properties(test_e3_stdlib_path PROPERTIES DEPENDS "test_e3_condcomp")

# Phase E.4: pure-LS io stdlib (replaces builtins_io.c). Same .lls test files
# the built-in version used to pass; output must include "ALL PASS".
# @subsystem stdlib/sys
# @guards Phase E.4 pure-LS io stdlib
# @sources lib/std/sys/io.lls
add_test(
    NAME test_e4_io_basic
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/io_basic_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DTEST_NAME=io_basic
        -P ${CMAKE_SOURCE_DIR}/tests/test_e3_glue.cmake
)
set_tests_properties(test_e4_io_basic PROPERTIES DEPENDS "test_e3_stdlib_path")

# test_e4_io_seek
#
# @subsystem stdlib/sys
# @guards Phase E.4 io seek/tell
# @sources lib/std/sys/io.lls
add_test(
    NAME test_e4_io_seek
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/io_seek_test.lls
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
# @subsystem modules
# @guards cross-module generic container (Step 0)
# @sources checker_generics.c:instantiate_template
add_test(
    NAME test_stack
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/stack_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_stack.cmake
)
set_tests_properties(test_stack PROPERTIES DEPENDS "test_generics_g2")

# Cross-module / transitive generic container: a module that imports std.stack
# and uses Stack(int) internally, imported alongside a direct Stack(string) use.
# Regression for idempotent template registration + on-demand generic-method
# forward-declaration. Reuses test_stack.cmake (STACK PASS sentinel).
# @subsystem modules
# @guards transitive cross-module generic container
# @sources checker_generics.c:instantiate_template
add_test(
    NAME test_stack_xmod
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/stack_xmod/main.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -DTEST_NAME=stack_xmod
        -P ${CMAKE_SOURCE_DIR}/tests/test_stack.cmake
)
set_tests_properties(test_stack_xmod PROPERTIES DEPENDS "test_stack")

# Module-qualified generic type `st.Stack(int)` (single-owner). JIT+AOT+memcheck.
# @subsystem modules
# @guards module-qualified generic type st.Stack(int)
# @sources checker_generics.c:resolve_type_node
add_test(
    NAME test_stack_qual
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/stack_qual.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -DTEST_NAME=stack_qual
        -P ${CMAKE_SOURCE_DIR}/tests/test_stack.cmake
)
set_tests_properties(test_stack_qual PROPERTIES DEPENDS "test_stack")

# std.core.set — pure-LS hash set built on Map(T, bool). Exercises Set(int) /
# Set(Str) (POD + has_drop), `[..]` list literals (de-dup), for-in iteration,
# to_vec, set algebra (union/intersect/difference + `+`/`-` operators) and
# predicates. JIT + AOT + memcheck (probe: generic struct + Map(T,_) field drop).
add_test(
    NAME test_set
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/set_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_set.cmake
)
set_tests_properties(test_set PROPERTIES DEPENDS "test_stack")

# Generic free-function called with an ABSTRACT type param from inside another
# generic body (method / free fn) — regression for the call-site mangling gap
# (docs/plan_generic_freefn_mangle.md): codegen used to emit `make(T)` instead of
# the instantiated `make(int)`. JIT + AOT + memcheck.
add_test(
    NAME test_generic_freefn
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/generic_freefn_in_body.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_generic_freefn.cmake
)
set_tests_properties(test_generic_freefn PROPERTIES DEPENDS "test_set")

# Vec/Map/Set method-completion batch: Vec retain/dedup/swap_remove/min/max/sum/
# product/is_sorted, Map get_or/merge, Set retain. POD + has_drop (Str) elements.
# JIT + AOT + memcheck.
add_test(
    NAME test_container_methods
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/container_methods.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_container_methods.cmake
)
set_tests_properties(test_container_methods PROPERTIES DEPENDS "test_set")

# std.core.heap — BinaryHeap(T: Order) max-heap priority queue over Vec(T). The
# trait-bound container probe: a generic container whose methods call `<` on T
# under monomorphization. BinaryHeap(int) ordering + literal heapsort +
# BinaryHeap(Str) has_drop. JIT + AOT + memcheck.
add_test(
    NAME test_heap
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/heap_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_heap.cmake
)
set_tests_properties(test_heap PROPERTIES DEPENDS "test_set")

# std.core.deque — Deque(T) growable ring buffer (double-ended queue). Both-ends
# O(1), growth + wraparound stress, sliding-window pattern, Deque(Str) has_drop.
# JIT + AOT + memcheck.
add_test(
    NAME test_deque
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/deque_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_deque.cmake
)
set_tests_properties(test_deque PROPERTIES DEPENDS "test_set")

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
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/ring_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_ring.cmake
)
set_tests_properties(test_ring PROPERTIES DEPENDS "test_generic_ambig")

# std.ring Phase 1 — SPSC lock-free cross-thread. Producer thread + main-thread
# consumer over Atomic(i64) cursors (no mutex). POD count+sum + has_drop Str move
# across threads. JIT + 6x AOT, no memcheck (thread tracker not safe).
add_test(
    NAME test_ring_spsc
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/ring_spsc_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_ring_spsc.cmake
)
set_tests_properties(test_ring_spsc PROPERTIES DEPENDS "test_ring")

# std.ring Phase 3 — MPMC lock-free (CAS slot reservation). Many producers +
# many consumers over new_mpmc_ring. POD conservation + has_drop Str (a double-
# reserved slot would double-free). JIT + 6x AOT, no memcheck.
add_test(
    NAME test_ring_mpmc
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/ring_mpmc_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_ring_mpmc.cmake
)
set_tests_properties(test_ring_mpmc PROPERTIES DEPENDS "test_ring_spsc")

# std.chan Phase 2 — blocking bounded channel (mutex + 2 condvars). Single-
# threaded correctness + memcheck (incl. __drop residual on owned Str).
add_test(
    NAME test_chan
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/chan_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_chan.cmake
)
set_tests_properties(test_chan PROPERTIES DEPENDS "test_ring_spsc")

# std.chan Phase 2 — MPMC blocking: N producers + M consumers drive __cond_wait
# (full/empty blocking) through a small-cap channel. JIT + 6x AOT, no memcheck.
add_test(
    NAME test_chan_mpmc
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/chan_mpmc_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_chan_mpmc.cmake
)
set_tests_properties(test_chan_mpmc PROPERTIES DEPENDS "test_chan")

# codegen_addr_of deref-receiver fix — (*ptr).method() on a &!self/&self method
# operates on the pointee, not a spilled copy (enables ChanIter for for-in).
add_test(
    NAME test_deref_method
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/deref_method_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_deref_method.cmake
)
set_tests_properties(test_deref_method PROPERTIES DEPENDS "test_chan_mpmc")

# std.chan Phase 4 — `for x in ch` Iterator(T) protocol (ChanIter holds *Chan,
# next() = blocking recv). Producer thread streams then closes; main drains.
add_test(
    NAME test_chan_forin
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/chan_forin_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_chan_forin.cmake
)
set_tests_properties(test_chan_forin PROPERTIES DEPENDS "test_deref_method")

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
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/vec_global_drop_test.lls
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

# Simd(T, N) — portable SIMD vector type (Phase 1a, docs/plan_simd.md). The type
# lowers to LLVM <N x T>; __simd_splat/zero/lane/fma/reduce_add + elementwise
# + - * / lower to single vector IR instructions. f32/f64/i32 element types.
# JIT + memcheck (POD, 0/0/0) + AOT.
add_test(
    NAME test_simd
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_simd.cmake
)

# ls emit-c — C (Intel intrinsics) emitter for the numeric/SIMD kernel subset.
# Emits the coverage kernel, asserts the AVX-512 intrinsics, compiles the
# generated C with clang, and checks the out-of-subset reject path.
add_test(
    NAME test_emit_c
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_emit_c.cmake
)

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

# std.chan Phase 0 — condition-variable intrinsics (__cond_*). Infrastructure for
# the blocking Chan (Phase 2): new runtime primitive (CONDITION_VARIABLE /
# pthread_cond) + the first 2-arg sync intrinsic (__cond_wait) through checker /
# codegen / jit. Single-threaded smoke (JIT + memcheck + AOT).
add_test(
    NAME test_cond_smoke
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_cond_smoke.cmake
)
set_tests_properties(test_cond_smoke PROPERTIES DEPENDS "test_sync")

# std.sync Guard(T) — DATA GUARD: compile-time data-race prevention without a
# lifetime system. The protected value is `priv`, reachable only through a
# closure that runs under the lock; LS borrows are arg-only/non-escaping so the
# lent &!value cannot leave the critical section. New compiler pieces: &!field
# borrow (mutable borrow of a struct field as a call arg) + `priv` fields.
# Single-thread lock/get + memcheck 0/0/0; 8-worker shared-global exact count
# (JIT + AOT x8, no memcheck); priv-access + struct-literal-bypass rejected.
add_test(
    NAME test_guard
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_guard.cmake
)
set_tests_properties(test_guard PROPERTIES DEPENDS "test_sync")

# std.sync RwLock(T) + SpinGuard(T) — Guard variants. RwLock distinguishes
# readers (shared &T, parallel) from writers (exclusive &!T); read-only is
# compiler-enforced (a reader mutating through &T is rejected). SpinGuard is a
# Guard backed by the bare adaptive SpinLock (busy-wait) for very short critical
# sections. New runtime: __rwlock_* (SRWLOCK shared/exclusive). Single-thread
# read/write/lock/get + memcheck 0/0/0; 8-worker shared-global exact 40000
# (JIT + AOT x8); reader-mutation rejected.
add_test(
    NAME test_rwlock_spinguard
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_rwlock_spinguard.cmake
)
set_tests_properties(test_rwlock_spinguard PROPERTIES DEPENDS "test_guard")

# Parser statement-boundary regressions (L-003/L-004). L-003: a line-leading
# `*K p` generic-pointer var decl after a value-ending statement is split off
# correctly (not eaten as `value * K`) — fixed by a newline + same-line-name
# guard in the Pratt loop's `*` handling. L-004: an if/while condition starting
# with `(` keeps its trailing infix op (fixed earlier; locked in here).
add_test(
    NAME test_stmt_boundary
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_stmt_boundary.cmake
)

# `ls fmt` formatter: --check flags messy input, formatting preserves behavior
# (parse-equivalence), and is idempotent.
add_test(
    NAME test_fmt
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_fmt.cmake
)

# `ls test` runner: all-pass file exits 0; a failing assertion exits non-zero
# and prints FAIL (anti-"假绿" guarantee).
add_test(
    NAME test_lstest
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_lstest.cmake
)

# `ls doc` API generator: signatures extracted from source, doc-comments
# attached, methods grouped, internals excluded.
add_test(
    NAME test_doc
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_doc.cmake
)

# 去 Rust 化关键字换皮（de-rust phase 1）：def/methods/interface/public/private
# 取代 fn/impl/trait/pub/priv，合并 trait-impl 形态 `methods Type: Interface`。
# 旧 Rust 关键字已退役（不再被 scanner 识别）。正向 JIT+AOT + 负向 `fn` 拒绝。
add_test(
    NAME test_derust_keywords
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_derust_keywords.cmake
)

# `Destroy` interface + C++-style `~` destructor (取代魔法 __drop)：
# methods X: Destroy { def ~(&!self) } 在 parser 折叠成固有 __drop，复用全部 RAII
# 机制；where T: Destroy 经 satisfies 特判。正向 JIT+AOT+memcheck + 负向拒绝。
add_test(
    NAME test_destroy
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_destroy.cmake
)

# C1 §3.5 has_drop fixpoint worklist：深层传播链 + 嵌套泛型容器，memcheck 保证
# 漏翻 has_drop（漏析构）会以泄漏暴露。（legacy 全表扫 oracle +
# LS_HASDROP_VERIFY parity harness 已退役，worklist 现为唯一实现，见 git 历史。）
add_test(
    NAME test_hasdrop_worklist
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_hasdrop_worklist.cmake
)

# Read-only `&field` / `&element` borrow — the twin of the `&!field` writable
# field borrow. `&obj.field` / `&arr[i]` to a read-only `&T` param (fn or
# Block(&T)) lends a zero-copy read-only borrow of a has_drop field/element
# (Block path uses codegen_lvalue_ptr, no clone); source stays alive. Enables
# zero-clone container iteration (L-006). JIT + AOT + memcheck 0/0/0.
add_test(
    NAME test_field_borrow
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_field_borrow.cmake
)

# owned-local field-borrow leak/double-free regression — `&local.field` (a
# struct/enum-typed field of an OWNED local) passed to a read-only `&T` free-fn
# param used to clone the field (struct: leaked at loop scope; enum: double-freed
# the shared payload). The fix borrows in place via GEP. JIT + AOT + memcheck 0/0/0.
add_test(
    NAME test_field_borrow_owned_local
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_field_borrow_owned_local.cmake
)

# type_name static-buffer self-clobber regression — a nested-Block type mismatch
# must print distinct expected/got names (the rotating pool used to wrap onto the
# half-built outer slot; fixed by building names on a stack-local buffer).
add_test(
    NAME test_type_name_distinct
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_type_name_distinct.cmake
)

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

# L-002: interface same-name method disambiguation — inherent priority on bare
# dispatch, qualified `Iface.method(recv)` selecting the overload (contended ones
# emitted as `T.<Iface>.m`), ambiguity rejected with a hint. JIT+AOT+memcheck +
# negatives (ambiguous / no-receiver / bad-receiver / generic v1 limit).
add_test(
    NAME test_iface_method_disambig
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_iface_method_disambig.cmake
)
set_tests_properties(test_iface_method_disambig PROPERTIES DEPENDS "test_trait_call")

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

# Operator lowering must not leak the temporaries it creates.
#
# `a + b` on a user type becomes a call whose result is an owned rvalue; chained
# expressions produce one per operator. Each needs registering for drop at the
# end of the statement, and the lowering path is separate from ordinary call
# codegen -- which is exactly why it could (and did) miss the registration while
# normal calls were fine.
#
# @subsystem codegen/ownership
# @guards operator lowering must not leak owned temporaries
# @sources codegen_own.c:cg_push_temp_drop
add_test(
    NAME test_operator_overload_memcheck
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/operator_overload_memcheck.lls
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

# ---- B-5: dedup imports by resolved file, not import-path spelling ----
add_test(
    NAME test_module_dedup_spelling
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_module_dedup_spelling.cmake
)

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

# Deep-nesting regressions (stdfuzz-found): recursive-descent parsers overflowed
# the native stack on deeply-nested input; json additionally hung on legal deep-
# balanced arrays via the exponential Vec.copy clone. Both timeout-guarded.
# @subsystem stdlib/text
# @guards stdfuzz-found recursive-descent stack overflow
# @sources lib/std/text/json.lls
add_test(
    NAME test_json_deep_nesting
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/json_deep_nesting.lls
        -DLS_HOME=${CMAKE_SOURCE_DIR}
        -DTEST_NAME=test_json_deep_nesting
        -P ${CMAKE_SOURCE_DIR}/tests/test_deep_nesting.cmake
)
set_tests_properties(test_json_deep_nesting PROPERTIES
    DEPENDS "test_std_json"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
# test_html_deep_nesting
#
# @subsystem stdlib/text
# @guards stdfuzz-found recursive-descent stack overflow
# @sources lib/std/text/html.lls
add_test(
    NAME test_html_deep_nesting
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/html_deep_nesting.lls
        -DLS_HOME=${CMAKE_SOURCE_DIR}
        -DTEST_NAME=test_html_deep_nesting
        -P ${CMAKE_SOURCE_DIR}/tests/test_deep_nesting.cmake
)
set_tests_properties(test_html_deep_nesting PROPERTIES
    DEPENDS "test_std_html_parse"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
# test_regex_deep_nesting
#
# @subsystem stdlib/text
# @guards stdfuzz-found recursive-descent stack overflow
# @sources lib/std/text/regex.lls
add_test(
    NAME test_regex_deep_nesting
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/regex_deep_nesting.lls
        -DLS_HOME=${CMAKE_SOURCE_DIR}
        -DTEST_NAME=test_regex_deep_nesting
        -P ${CMAKE_SOURCE_DIR}/tests/test_deep_nesting.cmake
)
set_tests_properties(test_regex_deep_nesting PROPERTIES
    DEPENDS "test_regex"
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

# ---- L-022 half 1: cross-module inherent methods (JIT + AOT + memcheck) ----
add_test(
    NAME test_l022_crossmod
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_l022_crossmod.cmake
)
set_tests_properties(test_l022_crossmod PROPERTIES
    DEPENDS "test_l0091_modgen"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- L-022 half 2: facade transitive inherent-method visibility ----
add_test(
    NAME test_l022_facade
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_l022_facade.cmake
)
set_tests_properties(test_l022_facade PROPERTIES
    DEPENDS "test_l022_crossmod"
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

# ---- std.regex prefilter (literal / first-byte fast paths) — JIT + AOT ----
# Guards against the prefilter silently skipping a valid start position.
add_test(
    NAME test_regex_prefilter
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_regex_prefilter.cmake
)
set_tests_properties(test_regex_prefilter PROPERTIES
    DEPENDS "test_regex"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- std.regex allocation-free group access (span / slice / Caps) ----
add_test(
    NAME test_regex_group_span
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_regex_group_span.cmake
)
set_tests_properties(test_regex_group_span PROPERTIES
    DEPENDS "test_regex"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- std.regex "pass the same text" misuse detection ----
add_test(
    NAME test_regex_text_fingerprint
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_regex_text_fingerprint.cmake
)
set_tests_properties(test_regex_text_fingerprint PROPERTIES
    DEPENDS "test_regex"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- std.regex compiled Regex object — JIT + AOT ----
add_test(
    NAME test_regex_object
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_regex_object.cmake
)
set_tests_properties(test_regex_object PROPERTIES
    DEPENDS "test_regex"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- std.regex D1 lazy DFA (match-only fast path) — JIT + AOT ----
add_test(
    NAME test_regex_dfa
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_regex_dfa.cmake
)
set_tests_properties(test_regex_dfa PROPERTIES
    DEPENDS "test_regex"
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

# ---- F6b-enum: generic ENUM instances keyed by module-prefixed type-arg names ----
# (mod_a.Node vs mod_b.Node into Option(T) used to collide on "Option(Node)")
add_test(
    NAME test_modtype_generic_enum
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_modtype_generic_enum.cmake
)
set_tests_properties(test_modtype_generic_enum PROPERTIES
    DEPENDS "test_modtype_memcheck"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- numeric literal overflow rejected at parse time (boundary values keep working) ----
add_test(
    NAME test_literal_overflow
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_literal_overflow.cmake
)
set_tests_properties(test_literal_overflow PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- ast_clone_deep exhaustiveness: @time/@bench in generic method bodies ----
add_test(
    NAME test_generic_clone_attime
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_generic_clone_attime.cmake
)
set_tests_properties(test_generic_clone_attime PROPERTIES
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

# ---- borrowing for-in: `for x in &v` zero-copy element read (non-escaping &T) ----
add_test(
    NAME test_borrow_for_in
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_borrow_for_in.cmake
)
set_tests_properties(test_borrow_for_in PROPERTIES
    DEPENDS "test_map_upsert"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- Map.upsert: update-or-insert, single hash+probe (count/group-by/memoize) ----
add_test(
    NAME test_map_upsert
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_map_upsert.cmake
)
set_tests_properties(test_map_upsert PROPERTIES
    DEPENDS "test_arena_pod"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- std.arena Phase 1: typed POD bump arena (where T: Pod), reset+reuse ----
add_test(
    NAME test_arena_pod
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_arena_pod.cmake
)
set_tests_properties(test_arena_pod PROPERTIES
    DEPENDS "test_vec_oob"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- std.arena: Region.str_substr zero-malloc slice interning (parser tokens) ----
add_test(
    NAME test_region_intern
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_region_intern.cmake
)
set_tests_properties(test_region_intern PROPERTIES
    DEPENDS "test_arena_pod"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- StrSlice: zero-copy fat view (&str-equivalent) — reads / map key / split_view ----
add_test(
    NAME test_strslice
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_strslice.cmake
)
set_tests_properties(test_strslice PROPERTIES
    DEPENDS "test_region_intern"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- oran.cus: O-RAN CUS-plane parse/build/filter/stats/render (lib/oran/) ----
add_test(
    NAME test_oran
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DREPO_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_oran.cmake
)
set_tests_properties(test_oran PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- sim: instruction-level microarch sim + advisor (lib/sim/) ----
add_test(
    NAME test_sim
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DREPO_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_sim.cmake
)
set_tests_properties(test_sim PROPERTIES
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

# ---- Generic return-borrow elision: Box(T)/Vec.get_ref zero-copy `&T`, scalar reject ----
add_test(
    NAME test_generic_borrow_return
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_generic_borrow_return.cmake
)
set_tests_properties(test_generic_borrow_return PROPERTIES
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

# ---- C2-1 diagnostics: source snippet + caret rendering (type/move/parse) ----
add_test(
    NAME test_diag_render
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_diag_render.cmake
)

# ---- C2-3 diagnostics: `lls check --json` schema-v1 structured output ----
add_test(
    NAME test_diag_json
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_diag_json.cmake
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

# ---- method call on a borrow-returning call result (`v.get_ref(i).m(args)`) ----
# Regression: receiver of reference type was not recognized as an instance
# method dispatch, miscounting self → "wrong number of arguments".
add_test(
    NAME test_method_on_borrow_call
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_method_on_borrow_call.cmake
)
set_tests_properties(test_method_on_borrow_call PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- A4 noalias-recovery safety gold standard (plan_opt_noalias_recovery.md) ----
# Cross-thread &!self sharing patterns (SpinGuard spin / Guard mutex / Chan
# blocking) that deadlock if noalias is ever emitted on their borrow params.
# LS_FORCE_NOALIAS=1 hangs this sample (manual positive control); the default
# whitelist build must keep passing. JIT + 6x AOT, no memcheck (threads).
add_test(
    NAME test_noalias_guard
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/noalias_guard.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_noalias_guard.cmake
)
set_tests_properties(test_noalias_guard PROPERTIES DEPENDS "test_chan_mpmc")

# ---- D1 -g line-table debug info regression (plan_debug_info.md phase 1) ----
# DI skeleton + statement locations in emit-ir -g, DI-free default IR, and a
# PDB next to the -g AOT exe. Unique work files: debug_info_aot.exe/.pdb.
add_test(
    NAME test_debug_info
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/debug_info_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_debug_info.cmake
)

# ---- A2 llvm.lifetime.start/end markers regression (plan_opt_lifetime_markers.md) ----
# Paired markers + verifier clean in emit-ir, kill-switch strips them, output is
# identical on both settings, and the O2 frame coalesces two disjoint aggregate
# locals. Unique work files: lifetime_markers_on/off.exe.
add_test(
    NAME test_lifetime_markers
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/lifetime_markers.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_lifetime_markers.cmake
)

# ---- Task 7.2: qualified method-symbol exactness (mangle_method_symbol) ----
# Two 70-deep generic chains with a >255-char shared name prefix: def-site
# char[256] truncation used to collapse their .__drop symbols into one
# (silent cross-type drop binding). Asserts both full-length symbols exist
# in emit-ir and the JIT output is correct.
add_test(
    NAME test_mangle_deep_collision
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/mangle_deep_collision.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -DSTDLIB=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_mangle_deep_collision.cmake
)

# ---- interface-side array(T,N) by-value param rejection ----
# The inherent-methods twin has always rejected by-value array params; the
# interface declaration and `methods T: Iface` param loops lacked the check
# (found during Batch 7 Task 7.8), letting a by-value array of has_drop
# elements bit-copy into the callee frame and double-free. Negative corpus
# expects the diagnostic at BOTH decl sites; positive corpus pins borrowed
# slices (&array / &!array) staying legal in interface + impl signatures.
add_test(
    NAME test_impl_array_param
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_impl_array_param.cmake
)

# ---- uniform by-value array param rejection: the four remaining paths ----
# Policy A (2026-07-19): array(T,N) by-value params are rejected everywhere.
# The interface-side fix left four sibling gaps: free fns (check pass inlines
# AST_FN_DECL and bypassed check_fn_decl -> dead code), generic free fns, and
# the two generic method instantiation paths. Negative corpus expects the
# diagnostic at ALL FOUR sites in one run; positive corpus pins borrowed
# slices (&array / &!array) staying legal on the free-fn and generic paths.
add_test(
    NAME test_fn_array_param
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_fn_array_param.cmake
)

# ---- array(T,N) through a non-identifier place (2026-07-25) ----
# Fixed arrays are represented by the ADDRESS of their storage. Five sites
# (print / for-in / element store / element read / codegen_addr_of) each
# hand-rolled that address from an IDENT symbol, so struct fields, nested field
# chains, array elements, borrowed receivers, nested arrays and rvalues all
# failed — half with "cannot get address of array", half SILENTLY (blank print,
# skipped loop body, dropped store, rc=0). The corpus value-checks every read
# and pins every whole-array print; the negative half pins that a store with no
# addressable target is a diagnostic instead of a silent no-op.
add_test(
    NAME test_struct_array_field
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_struct_array_field.cmake
)

# ---- L-023: array(T,N) with has_drop ELEMENTS ----
# The write side of a fixed array had no ownership protocol while the read side
# and the by-value return already cloned: a named owned source was bit-copied
# into a literal element / indexed slot / whole-array bind (double free), an
# indexed assignment never dropped the value it overwrote (leak), a struct
# literal with an inline array-literal field stored NOTHING (stack garbage ->
# garbage values, invalid free, segfault when printing an element), a struct with
# an owning array field was not has_drop at all (leak), and an unbound rvalue
# array temp was never released (leak). `Vec(Str) v = [x]` had the same bit-copy
# through __from_list. Four of the six were silent with rc=0, so the corpus
# value-checks every read and requires memcheck 0/0/0.
add_test(
    NAME test_array_owned_elem
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_array_owned_elem.cmake
)

# ---- ast_clone_deep must not drop a parser-owned [move v] list ----
# closure.move_names is the `[move v]` source syntax and ast_free frees it, but
# ast_clone_deep nulled it alongside the checker-filled captures[]. Every cloned
# subtree (generic method body, comptime block, operator lowering) therefore lost
# the list and with it the "not referenced inside the closure body" validation,
# which kept firing in ordinary functions: two identical closures produced one
# diagnostic instead of two.
add_test(
    NAME test_closure_move_clone
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_closure_move_clone.cmake
)

# A closure's braced body must yield its tail expression, like a function's.
# codegen intercepts the last statement for functions but not for closures, so
# `|| { 7 }` fell through to `ret zeroinitializer` and silently returned 0.
# Pins the values (JIT + AOT) and the ownership path (an owned Str leaves
# through the tail), which is where a codegen-side fix would double-free.
add_test(
    NAME test_closure_tail_expr
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/closure_tail_expr_test.lls
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_closure_tail_expr.cmake
)

# Cross-module writes to a module global silently stored nothing, in two
# shapes: `mod.VAR = v` (codegen_lvalue_ptr bailed on non-struct objects) and
# calling a &!self mutating method on `mod.VAR` (codegen_addr_of had the same
# gap and the method ran on a private rvalue-spill copy). Reads and
# same-module writes/mutations always worked. Pins values through the
# module's own accessors, JIT + AOT, plus memcheck.
add_test(
    NAME test_modglobal_assign
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/modglobal_assign/main.lls
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_modglobal_assign.cmake
)

# An unterminated block comment used to silently swallow the rest of the file
# with zero diagnostic (`lls check` reported "Type check passed" on a program
# whose real body was eaten as comment text). Pins a clean scanner error for
# both single-level and nested unterminated comments.
add_test(
    NAME test_unterminated_comment_reject
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_unterminated_comment_reject.cmake
)

# @derive on a generic struct wrongly required its own inherent methods(T)
# block to be textually adjacent -- any unrelated declaration in between
# (an interface, a free function, another struct) made the derive-synthesized
# trait impls land before that block and fail with a spurious "requires an
# inherent methods block" error. Pins the fix plus a sibling-combination smoke
# test (static generic method + operator overload + derive + L-002
# disambiguation, all together).
add_test(
    NAME test_derive_declaration_gap
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -P ${CMAKE_SOURCE_DIR}/tests/test_derive_declaration_gap.cmake
)

# B4: >255-variant enum -- locks in the drop-sentinel/!range fallback path
# (whole-slot zeroing, since an i8 tag has no spare value left when every one
# of its 256 possible values is a live variant) that existed since 2026-07-05
# but had zero test coverage before this.
add_test(
    NAME test_enum_300_variants
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/enum_300_variants_test.lls
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_enum_300_variants.cmake
)

# B5: \xHH hex escapes + correctly-closed multi-level nested block comments.
# Both confirmed working by probe during the boundary-testing investigation,
# neither previously covered by any test.
add_test(
    NAME test_lex_boundary
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/lex_boundary_test.lls
        -P ${CMAKE_SOURCE_DIR}/tests/test_lex_boundary.cmake
)

# ---- P0: parser recursion depth guard (docs/plan_arch_cleanup.md W1) ----
# Deep-nesting corpora (parens / prefix stars / blocks) must fail fast with a
# clean "nesting too deep" diagnostic instead of a stack-overflow crash, the
# error-flood cap must bound diagnostic output, and 64-level nesting stays legal.
add_test(
    NAME test_parse_depth
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_parse_depth.cmake
)
set_tests_properties(test_parse_depth PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- Task 2.2 (docs/plan_arch_round2_backlog.md Batch 2): deep generic
# type-arg nesting + cross-module type argument through the growable
# MangleBuf instance-name builder (src/mangle.c), replacing the old fixed
# 256/512-byte snprintf buffers in instantiate_template /
# checker_instantiate_struct / resolve_type_node's pre-check. ----
add_test(
    NAME test_mangle_deep_nest
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_mangle_deep_nest.cmake
)
set_tests_properties(test_mangle_deep_nest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---------------------------------------------------------------------------
# IR snapshot tests (Task 0.1, docs/plan_arch_cleanup_round2.md Batch 0) —
# golden-file byte comparison of `lls emit-ir` output. This is the shared
# verification gate for "zero behavior change" refactors: emit-ir byte/
# substring comparisons used to be hand-rolled per-feature (see
# test_lifetime_markers.cmake, test_enum_drop_sentinel.cmake,
# test_memcpy_prim.cmake, test_debug_info.cmake) — this collapses that into
# one comparator (tests/ir_snapshot.cmake) + one convenience registration
# function, following the golden-file precedent already established by
# tests/mca_oracle/ (regen_golden.sh + kernels/*.golden).
#
# Golden files live in tests/ir_golden/<name>.ll (committed) and are
# regenerated — ONLY when an IR change is intentional — via
# tests/regen_ir_golden.sh (bash/Git Bash only; see that script's header for
# why PowerShell `2>` redirection must never be used to produce them).
#
# Scope: pins only the default (unoptimized) `emit-ir` dump. JIT and
# optimization-level (-O1/-O2/...) codegen paths are out of scope.
function(ls_ir_snapshot)
    cmake_parse_arguments(ARG "" "NAME;SAMPLE" "" ${ARGN})
    if(NOT ARG_NAME)
        message(FATAL_ERROR "ls_ir_snapshot(): NAME is required")
    endif()
    if(NOT ARG_SAMPLE)
        message(FATAL_ERROR "ls_ir_snapshot(): SAMPLE is required")
    endif()
    add_test(
        NAME test_ir_snapshot_${ARG_NAME}
        COMMAND ${CMAKE_COMMAND}
            -DLS_EXE=$<TARGET_FILE:ls>
            -DSAMPLE=${ARG_SAMPLE}
            -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/ir_golden/${ARG_NAME}.ll
            -DNAME=${ARG_NAME}
            -DWORK_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/tests/ir_snapshot.cmake
    )
    set_tests_properties(test_ir_snapshot_${ARG_NAME} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endfunction()

# Windows-only: the golden .ll files embed the generating host's
# `target datalayout` / `target triple` lines (x86_64-pc-windows-msvc), so
# emit-ir on Linux CI would produce a different header and turn every
# snapshot red. This facility is a local zero-behavior-change refactoring
# gate for the Windows/MSVC primary platform; Linux CI must not run it.
if(WIN32)
# test_ir_snapshot_enum_basic_test
#
# @subsystem codegen/optimization
# @guards zero-behaviour-change refactoring gate (golden IR)
# @sources codegen.c:codegen_compile
ls_ir_snapshot(NAME enum_basic_test SAMPLE ${CMAKE_SOURCE_DIR}/tests/samples/enum_basic_test.lls)
# test_ir_snapshot_closure_g
#
# @subsystem codegen/optimization
# @guards zero-behaviour-change refactoring gate (golden IR)
# @sources codegen.c:codegen_compile
ls_ir_snapshot(NAME closure_g SAMPLE ${CMAKE_SOURCE_DIR}/tests/samples/closure_g.lls)
# test_ir_snapshot_match_own_stress_test
#
# @subsystem codegen/optimization
# @guards zero-behaviour-change refactoring gate (golden IR)
# @sources codegen.c:codegen_compile
ls_ir_snapshot(NAME match_own_stress_test SAMPLE ${CMAKE_SOURCE_DIR}/tests/samples/match_own_stress_test.lls)
endif()

# test_ls_opt_env
#
# @subsystem driver/optimization
# @guards LS_OPT reaching the JIT; CLI -O beating it; O0 staying the default
# @sources main.c:handle_run optpipe.c:ls_opt_env_level
#
# LS_OPT was parsed by ls_opt_default_jit() but then discarded: `lls run`
# hard-coded LS_OPT_O0 and jit_run_file_impl overwrote .level unconditionally,
# so exporting it changed nothing for JIT while working normally for AOT. This
# pins the fix in both directions -- the env var must take effect, and it must
# NOT be able to move the default off O0 when unset or malformed (optpipe's own
# fallback for AOT is O2, which is the wrong default here: the B1 tiering spike
# measured +80~150% end-to-end cost for optimizing `lls run`).
add_test(
    NAME test_ls_opt_env
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/ls_opt_env_test.lls
        -P ${CMAKE_SOURCE_DIR}/tests/ls_opt_env.cmake
)
set_tests_properties(test_ls_opt_env PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- interface signature validation (2026-07-30) ----
# Three defects, one family. (1) FromList/FromPairs are marker protocol
# interfaces registered with param_count 0 on purpose, so comparing a real
# impl's arity against it rejected every non-generic impl. (2) Generic
# `methods X(T): Iface` skipped signature validation ENTIRELY (folded into
# the inherent impl_node then early-returned) -- wrong arity, wrong types,
# wrong static-ness, wrong self-borrow-kind and missing methods were all
# silently accepted; a bogus generic Clone reached codegen and produced
# invalid IR caught only by the LLVM verifier. (3) covered by
# test_generic_depth.
add_test(
    NAME test_iface_generic_sig
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_iface_generic_sig.cmake
)

# ---- generic instantiation depth guard (2026-07-30) ----
# checker_instantiate_struct <-> resolve_type_node_with_substitution <->
# instantiate_template recursed with no limit, so a self-referential template
# (struct Rec(T) { Rec(Rec(T)) inner }) overflowed the stack: 0xC00000FD, no
# diagnostic, no output. The parser's own depth guard cannot help -- the
# nesting is generated, not written.
add_test(
    NAME test_generic_depth
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_generic_depth.cmake
)

# ---- previously-orphaned drivers, registered 2026-08-02 --------------------
# These three drivers existed on disk but no add_test() ever referenced them,
# so they had never run. The doc generator's self-audit surfaced them; all
# three were then executed by hand and passed unchanged, which means the
# features below had ZERO coverage the whole time rather than broken coverage.
# (A fourth, test_generics_g15.cmake, was deleted instead: its corpus was never
# written, it was never registered, and generic `methods` blocks are covered by
# test_generics_g1.)

# std.sys.path: pure-LS path utilities (join / dirname / basename / extension).
#
# String manipulation with a long tail of edge cases -- trailing separators,
# empty components, a path that is only a separator, drive letters -- exactly
# the shape that returns a slightly wrong string instead of failing.
#
# This driver sat on disk unregistered until 2026-08-02: it had never run once.
# It passed unchanged when finally executed, so the module was never broken --
# it was simply unguarded.
#
# @subsystem stdlib/sys
# @guards std.sys.path batch-2 utilities (unregistered until 2026-08-02)
# @sources lib/std/sys/path.lls
add_test(
    NAME test_path
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/path_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_path.cmake
)

# `--profile` instrumentation on both the JIT and the AOT path. The flag injects
# per-function counters, so it edits the emitted module -- a regression here is
# a compile failure or a corrupted program under a flag nothing else exercises.
#
# @subsystem tooling/cli
# @guards --profile instrumentation (unregistered until 2026-08-02)
# @sources main.c
add_test(
    NAME test_profile
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/profile_test.lls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_profile.cmake
)

# std.sys.proc and std.sys.env: process arguments/exit and environment access.
# Both are thin wrappers over platform C, so the JIT and AOT legs matter -- the
# JIT resolves the runtime symbol in-process while AOT links it, and only one of
# those breaking is a realistic failure.
#
# @subsystem stdlib/sys
# @guards std.sys.proc + std.sys.env (unregistered until 2026-08-02)
# @sources lib/std/sys/proc.lls, lib/std/sys/env.lls
add_test(
    NAME test_proc_env
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE_DIR=${CMAKE_SOURCE_DIR}/tests/samples
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_proc_env.cmake
)
