# test_borrow_for_in.cmake — `for x in &v` borrowing for-in (zero-copy read).
# JIT + AOT + memcheck 0/0/0. x binds as non-escaping &T; source survives loop.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/borrow_for_in_test.ls")
set(_expected "BORROW FORIN PASS")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "borrow_for_in JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "borrow_for_in JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL ")
    message(FATAL_ERROR "borrow_for_in JIT had a FAIL line\n${jit_out}")
endif()
message(STATUS "borrow_for_in JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/borrow_for_in_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "borrow_for_in AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "borrow_for_in AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "borrow_for_in AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL ")
    message(FATAL_ERROR "borrow_for_in AOT had a FAIL line\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "borrow_for_in AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "borrow_for_in memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "borrow_for_in memcheck not clean\n${mc_err}")
endif()
message(STATUS "borrow_for_in memcheck: OK clean")

message(STATUS "test_borrow_for_in: ALL PASSED")
