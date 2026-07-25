# test_array_owned_elem.cmake — array(T,N) whose ELEMENTS own heap data.
#
# Companion to test_struct_array_field (array PLACE resolution, POD-only on
# purpose). Pre-fix (L-023, 2026-07-25) the write side of a fixed array had no
# ownership protocol while the read side and the by-value return already cloned:
#   ① `array(Str,2) a = [x, "s"]`   named source bit-copied      -> double free
#   ② `a[0] = n`                    source bit-copied, old value never dropped
#   ③ `array(Str,2) b = a`          bind shares element buffers  -> double free
#   ④ `S { d: [mk(), mk()] }`       stored NOTHING (stack garbage) -> garbage
#                                   values, invalid free, segfault on @print
#   ⑤ `build()[0]`                  rvalue array temp never released -> leak
#   ⑥ `struct { array(Str,2) }`     not has_drop at all          -> leak
#   Vec twin of ①: `Vec(Str) v = [x]` had the same bit-copy via __from_list.
#
# Four of the six were silent with rc=0, so this driver checks VALUES
# (RESULT bad=0), not just the exit code, and requires memcheck 0/0/0.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/array_owned_elem_test.lls")

function(check_corpus_output label out)
    if(NOT "${out}" MATCHES "RESULT bad=0")
        message(FATAL_ERROR "${label}: value checks failed (want 'RESULT bad=0')\n${out}")
    endif()
    # Printing a heap Str element reached through a struct's array field used to
    # segfault (rc=139, all output lost). Pin the rendered element text so a
    # regression cannot hide behind a passing bad-counter.
    if(NOT "${out}" MATCHES "T-fieldprint[ \t\r\n]+a1")
        message(FATAL_ERROR "${label}: missing 'a1' after T-fieldprint\n${out}")
    endif()
endfunction()

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "array_owned_elem JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
check_corpus_output("array_owned_elem JIT" "${jit_out}")
message(STATUS "array_owned_elem JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/array_owned_elem_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "array_owned_elem AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "array_owned_elem AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
check_corpus_output("array_owned_elem AOT" "${aot_out}")
file(REMOVE "${aot_bin}")
message(STATUS "array_owned_elem AOT: OK")

# ---- memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "array_owned_elem memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
check_corpus_output("array_owned_elem memcheck" "${mc_out}")
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "array_owned_elem memcheck not clean\n${mc_err}")
endif()
message(STATUS "array_owned_elem memcheck: OK clean")

message(STATUS "test_array_owned_elem: ALL PASSED")
