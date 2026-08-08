# test_closure_move_clone.cmake — a `[move v]` capture list must survive
# ast_clone_deep.
#
# ast_clone_deep nulled closure.move_names/move_count under a comment claiming
# captures are checker-filled. captures[] is; move_names is PARSER-owned source
# syntax that ast_free frees, so dropping it silently discarded the whole list
# inside every cloned subtree (generic method bodies, comptime blocks, operator
# lowering) — and with it the checker's "not referenced inside the closure body"
# validation, which kept firing in ordinary functions. Two identical closures,
# one in a generic method and one in a plain function, used to produce ONE
# diagnostic instead of two.
#
# Negative: closure_move_clone_reject.lls — BOTH names must be reported.
# Positive: closure_move_clone_test.lls  — a valid list still compiles, runs and
#           stays memcheck-clean across two monomorphisations (the clone path
#           runs once per instantiation).
#
# @subsystem frontend/ast
# @guards ast_clone_deep drops [move v] (5fb973a)
# @sources ast.c:ast_clone_deep
cmake_minimum_required(VERSION 3.20)

# ---- negative: both diagnostics, not just the un-cloned one ----
execute_process(COMMAND "${LS_EXE}" check "${SAMPLE_DIR}/closure_move_clone_reject.lls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "closure_move_clone_reject: expected type errors, got success\n${n_out}${n_err}")
endif()
set(n_all "${n_out}${n_err}")
foreach(want "unused_plain" "unused_generic")
    if(NOT "${n_all}" MATCHES "'${want}' in \\[move \\.\\.\\.\\] list is not referenced")
        message(FATAL_ERROR
            "closure_move_clone_reject: missing the diagnostic for '${want}'.\n"
            "A missing 'unused_generic' means ast_clone_deep dropped move_names again.\n${n_all}")
    endif()
endforeach()
message(STATUS "closure_move_clone_reject: both [move] lists diagnosed")

# ---- positive: JIT ----
set(POS "${SAMPLE_DIR}/closure_move_clone_test.lls")
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "closure_move_clone JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "RESULT bad=0")
    message(FATAL_ERROR "closure_move_clone JIT: value checks failed\n${jit_out}")
endif()
message(STATUS "closure_move_clone JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/closure_move_clone_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "closure_move_clone AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "closure_move_clone AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "RESULT bad=0")
    message(FATAL_ERROR "closure_move_clone AOT: value checks failed\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "closure_move_clone AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "closure_move_clone memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "closure_move_clone memcheck not clean\n${mc_err}")
endif()
message(STATUS "closure_move_clone memcheck: OK clean")

message(STATUS "test_closure_move_clone: ALL PASSED")
