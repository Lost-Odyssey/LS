# test_generic_free_fn_borrow.cmake — regression for the two generic free-function
# call gaps (independent of comptime):
#   Gap 1 — value-arg type inference: `f(v)` works without explicit `f(Type)(v)`.
#   Gap 2 — `&T` borrow params: auto-borrow a value + accept explicit `&v`,
#           zero-copy (memcheck 0/0/0). by-value `T` still works.
#   negative: a type param appearing in no value-arg position is a clean compile
#             error (not a crash, not a silent miscompile).
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/generic_free_fn_borrow.lls")
set(_expected "GFFB DONE")

# Expected output lines, in order:
#   30 / 7 / 99 (sum_ints inferred over 3 types), 10 / 99 (source still usable),
#   7 (explicit &r), 30 (explicit type args), hi (Str through borrow),
#   hi (source Str field intact), 30 (by-value T).
set(_needles "${_expected}" "30" "7" "99" "10" "hi")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "gffb JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
foreach(needle ${_needles})
    if(NOT "${jit_out}" MATCHES "${needle}")
        message(FATAL_ERROR "gffb JIT missing '${needle}'\n${jit_out}")
    endif()
endforeach()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "gffb JIT had a FAIL line\n${jit_out}")
endif()
message(STATUS "gffb JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/gffb_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "gffb AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0 OR NOT "${aot_out}" MATCHES "${_expected}"
   OR NOT "${aot_out}" MATCHES "hi" OR "${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "gffb AOT FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "gffb AOT: OK")

# ---- positive: memcheck (borrow = no clone of the whole struct → 0/0/0) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0 OR NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "gffb memcheck not clean\n${mc_err}")
endif()
message(STATUS "gffb memcheck: OK clean")

# ---- negative: type param in no value-arg position → clean compile error ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/generic_free_fn_borrow_reject.lls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "gffb_reject: expected compile error, got success\n${n_out}")
endif()
set(n_all "${n_out}${n_err}")
if(NOT "${n_all}" MATCHES "cannot infer type parameter")
    message(FATAL_ERROR "gffb_reject: missing diagnostic\n${n_all}")
endif()
message(STATUS "gffb_reject: rejected as expected")

message(STATUS "test_generic_free_fn_borrow: ALL PASSED")
