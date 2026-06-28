# test_strslice.cmake — StrSlice zero-copy fat view (the &str-equivalent).
# See docs/plan_strslice_view.md.  JIT + AOT + memcheck (0/0/0).
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/strslice_test.ls")
set(_expected "STRSLICE PASS")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "strslice JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "strslice JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL ")
    message(FATAL_ERROR "strslice JIT had a FAIL line\n${jit_out}")
endif()
message(STATUS "strslice JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/strslice_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "strslice AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "strslice AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "strslice AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL ")
    message(FATAL_ERROR "strslice AOT had a FAIL line\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "strslice AOT: OK")

# ---- positive: memcheck (0 leak / 0 double-free / 0 invalid free) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "strslice memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "strslice memcheck not clean\n${mc_err}")
endif()
message(STATUS "strslice memcheck: OK clean")

message(STATUS "test_strslice: ALL PASSED")
