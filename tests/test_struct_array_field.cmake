# test_struct_array_field.cmake — array(T,N) reached through a place that is not
# a bare identifier (struct field, nested field chain, array element, borrowed
# receiver, nested array, rvalue).
#
# Pre-fix (2026-07-25) the five array-address sites each hand-rolled the address
# from an IDENT symbol lookup, so every other place failed — half of it loudly
# ("cannot get address of array"), half of it SILENTLY: a blank line from
# @print, a for-in body that never ran, a dropped element store with rc=0.
# The positive corpus therefore VALUE-CHECKS every read (RESULT bad=0) and pins
# the exact text of every whole-array print; rc alone would not have caught it.
#  * Positive: struct_array_field_test.lls — JIT + AOT + memcheck 0/0/0.
#  * Negative: struct_array_field_reject.lls — a store into an rvalue array
#    element is a diagnostic, not a silent no-op.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/struct_array_field_test.lls")

# Every whole-array print, in source order. A place that failed to resolve used
# to emit a bare newline, so these lines are the guard against a silent
# regression to that behaviour.
set(_arrays
    "[1, 2, 3]"     # T1  field read, fresh literal
    "[9, 2, 3]"     # T4  after b.d[0] = 9
    "[5, 60, 7]"    # T6  o.inner.d after chain write
    "[20, 2, 2]"    # T7  bs[1].d after element-field write
    "[7, 4]"        # T8  M[1] after nested write
    "[7, 8, 9]"     # T9  struct literal fed from an array variable
    "[4, 50, 6]"    # T10 bare local (no-regression)
    "[11, 20, 30]"  # T11 global (no-regression)
)

function(check_corpus_output label out)
    if(NOT "${out}" MATCHES "RESULT bad=0")
        message(FATAL_ERROR "${label}: value checks failed (want 'RESULT bad=0')\n${out}")
    endif()
    foreach(want IN LISTS _arrays)
        string(REPLACE "[" "\\[" want_re "${want}")
        string(REPLACE "]" "\\]" want_re "${want_re}")
        if(NOT "${out}" MATCHES "${want_re}")
            message(FATAL_ERROR "${label}: missing array print '${want}'\n${out}")
        endif()
    endforeach()
endfunction()

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "struct_array_field JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
check_corpus_output("struct_array_field JIT" "${jit_out}")
message(STATUS "struct_array_field JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/struct_array_field_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "struct_array_field AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "struct_array_field AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
check_corpus_output("struct_array_field AOT" "${aot_out}")
file(REMOVE "${aot_bin}")
message(STATUS "struct_array_field AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "struct_array_field memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
check_corpus_output("struct_array_field memcheck" "${mc_out}")
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "struct_array_field memcheck not clean\n${mc_err}")
endif()
message(STATUS "struct_array_field memcheck: OK clean")

# ---- negative: store into an rvalue array element ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/struct_array_field_reject.lls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "struct_array_field_reject: expected a compile error (a silently dropped write is the bug), got success\n${n_out}")
endif()
if(NOT "${n_out}${n_err}" MATCHES "cannot assign to array element")
    message(FATAL_ERROR "struct_array_field_reject: missing 'cannot assign to array element' diagnostic\n${n_out}${n_err}")
endif()
message(STATUS "struct_array_field_reject: diagnosed (rc=${n_rc})")

message(STATUS "test_struct_array_field: ALL PASSED")
