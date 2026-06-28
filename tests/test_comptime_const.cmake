# test_comptime_const.cmake — compile-time constant evaluation (Steps 2-4).
# docs/plan_comptime_consteval.md.
#  * Step 2 scalars: folded to literals (int/bool/char/f64; arithmetic/bit/shift/
#    compare/cast/references/math.* via host libm).
#  * Step 3 blocks: `comptime { ... return v }` (loops/if/locals + step budget).
#  * Step 4 arrays: materialized into constant-initialized globals (→ .rodata).
#  * memcheck: clean (constants allocate nothing).
#  * negative: assigning to a comptime constant / array length mismatch are clean
#    compile errors.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/comptime_const_test.ls")
set(_expected "COMPTIME CONST DONE")
set(_needles
    "MASK=511" "BIG=true" "LCODE=81" "SUM=45" "FACT5=120" "SQ=25,81"
    "CRC1=1996959894" "0.707107" "1.000000" "${_expected}")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "comptime_const JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
foreach(needle ${_needles})
    if(NOT "${jit_out}" MATCHES "${needle}")
        message(FATAL_ERROR "comptime_const JIT missing '${needle}'\n${jit_out}")
    endif()
endforeach()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "comptime_const JIT had a FAIL line\n${jit_out}")
endif()
message(STATUS "comptime_const JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/comptime_const_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "comptime_const AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0 OR NOT "${aot_out}" MATCHES "${_expected}"
   OR NOT "${aot_out}" MATCHES "CRC1=1996959894" OR NOT "${aot_out}" MATCHES "SQ=25,81"
   OR NOT "${aot_out}" MATCHES "SUM=45" OR "${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "comptime_const AOT FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "comptime_const AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0 OR NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "comptime_const memcheck not clean\n${mc_err}")
endif()
message(STATUS "comptime_const memcheck: OK clean")

# ---- negative: assignment to a comptime constant is a clean compile error ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/comptime_const_reject.ls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "comptime_const_reject: expected compile error, got success\n${n_out}")
endif()
set(n_all "${n_out}${n_err}")
if(NOT "${n_all}" MATCHES "cannot assign to comptime constant")
    message(FATAL_ERROR "comptime_const_reject: missing diagnostic\n${n_all}")
endif()
if("${n_all}" MATCHES "unreachable")
    message(FATAL_ERROR "comptime_const_reject: ran past the rejected assignment\n${n_all}")
endif()
message(STATUS "comptime_const_reject: rejected as expected")

# ---- negative: array length mismatch is a clean compile error ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/comptime_const_reject2.ls"
    OUTPUT_VARIABLE m_out ERROR_VARIABLE m_err RESULT_VARIABLE m_rc)
if(m_rc EQUAL 0)
    message(FATAL_ERROR "comptime_const_reject2: expected compile error, got success\n${m_out}")
endif()
set(m_all "${m_out}${m_err}")
if(NOT "${m_all}" MATCHES "does not match declared length")
    message(FATAL_ERROR "comptime_const_reject2: missing diagnostic\n${m_all}")
endif()
if("${m_all}" MATCHES "unreachable")
    message(FATAL_ERROR "comptime_const_reject2: ran past the rejected const\n${m_all}")
endif()
message(STATUS "comptime_const_reject2: rejected as expected")

message(STATUS "test_comptime_const: ALL PASSED")
