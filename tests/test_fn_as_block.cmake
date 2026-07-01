cmake_minimum_required(VERSION 3.20)

set(SAMPLE "${SAMPLE_DIR}/fn_as_block_test.lls")
set(aot_bin "${WORK_DIR}/fn_as_block_aot")
set(aot_mc_bin "${WORK_DIR}/fn_as_block_mc_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
    set(aot_mc_bin "${aot_mc_bin}.exe")
endif()

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "fn_as_block JIT failed (rc=${jit_rc}):\n${jit_err}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "ALL PASS")
    message(FATAL_ERROR "fn_as_block JIT missing ALL PASS:\n${jit_out}")
endif()

execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE jit_mc_out
    ERROR_VARIABLE jit_mc_err
    RESULT_VARIABLE jit_mc_rc
)
if(NOT jit_mc_rc EQUAL 0)
    message(FATAL_ERROR "fn_as_block JIT memcheck failed (rc=${jit_mc_rc}):\n${jit_mc_err}")
endif()
if("${jit_mc_out}" MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${jit_mc_out}")
endif()
if(NOT "${jit_mc_out}" MATCHES "ALL PASS")
    message(FATAL_ERROR "fn_as_block JIT memcheck missing ALL PASS:\n${jit_mc_out}")
endif()
if(NOT "${jit_mc_err}" MATCHES "0 leak")
    message(FATAL_ERROR "fn_as_block JIT memcheck expected 0 leaks:\n${jit_mc_err}")
endif()

execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    ERROR_VARIABLE aot_cerr
    RESULT_VARIABLE aot_crc
)
if(NOT aot_crc EQUAL 0)
    message(FATAL_ERROR "fn_as_block AOT compile failed:\n${aot_cerr}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE aot_err
    RESULT_VARIABLE aot_rc
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "fn_as_block AOT run failed:\n${aot_err}")
endif()
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "ALL PASS")
    message(FATAL_ERROR "fn_as_block AOT missing ALL PASS:\n${aot_out}")
endif()

execute_process(
    COMMAND "${LS_EXE}" compile --memcheck "${SAMPLE}" -o "${aot_mc_bin}"
    ERROR_VARIABLE aot_mc_cerr
    RESULT_VARIABLE aot_mc_crc
)
if(NOT aot_mc_crc EQUAL 0)
    message(FATAL_ERROR "fn_as_block AOT memcheck compile failed:\n${aot_mc_cerr}")
endif()
execute_process(
    COMMAND "${aot_mc_bin}"
    OUTPUT_VARIABLE aot_mc_out
    ERROR_VARIABLE aot_mc_err
    RESULT_VARIABLE aot_mc_rc
)
if(NOT aot_mc_rc EQUAL 0)
    message(FATAL_ERROR "fn_as_block AOT memcheck run failed:\n${aot_mc_err}")
endif()
if("${aot_mc_out}" MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${aot_mc_out}")
endif()
if(NOT "${aot_mc_out}" MATCHES "ALL PASS")
    message(FATAL_ERROR "fn_as_block AOT memcheck missing ALL PASS:\n${aot_mc_out}")
endif()
if(NOT "${aot_mc_err}" MATCHES "0 leak")
    message(FATAL_ERROR "fn_as_block AOT memcheck expected 0 leaks:\n${aot_mc_err}")
endif()

message(STATUS "fn_as_block: ALL OK")
