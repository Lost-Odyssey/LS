# test_vec_get_option.cmake — `Vec.get(i) -> Option(T)` recoverable read.
# Ownership sweep: POD / owned Str / has_drop struct / nested Vec payloads through
# match, combinators (unwrap_or / is_none?), force-unwrap and try+ok_or.
# JIT + AOT + memcheck; FAIL anywhere in output vetoes.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/vec_get_option_test.lls")
set(_expected "VEC_GET_OPTION PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "vec_get_option JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "vec_get_option JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "vec_get_option JIT has FAIL lines\n${jit_out}")
endif()
message(STATUS "vec_get_option JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/vec_get_option_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "vec_get_option AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "vec_get_option AOT run FAILED (rc=${aot_run_rc})")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "vec_get_option AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "vec_get_option AOT has FAIL lines\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "vec_get_option AOT: OK")

# ---- memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "vec_get_option memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "vec_get_option memcheck leak\n${mc_err}")
endif()
message(STATUS "vec_get_option memcheck: OK clean")

message(STATUS "test_vec_get_option: ALL PASSED")
