# test_enum_drop_sentinel.cmake — dead-tag sentinel contract
# (docs/plan_enum_drop_sentinel.md): legal re-drop paths stay silent,
# slot reuse across iterations is clean, values check out.
# JIT + AOT + memcheck (0 leak / 0 double-free).
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/enum_drop_sentinel_test.lls")
set(_expected "SENTINEL PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "enum_drop_sentinel JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "enum_drop_sentinel JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL:")
    message(FATAL_ERROR "enum_drop_sentinel JIT had a failed check\n${jit_out}")
endif()
message(STATUS "enum_drop_sentinel JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/enum_drop_sentinel_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "enum_drop_sentinel AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "enum_drop_sentinel AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "enum_drop_sentinel AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL:")
    message(FATAL_ERROR "enum_drop_sentinel AOT had a failed check\n${aot_out}")
endif()
message(STATUS "enum_drop_sentinel AOT: OK")

# ---- memcheck (0 leak / 0 double-free; legal re-drops must be silent) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "enum_drop_sentinel memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "enum_drop_sentinel memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "enum_drop_sentinel memcheck: OK clean")
