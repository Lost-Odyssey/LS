# test_opt_levels.cmake — exercises the optimization-pipeline CLI surface
# (docs/plan_opt_pipeline.md). Runs a self-verifying sample at multiple -O
# levels via both JIT and AOT (incl. --native) and asserts identical correctness
# ("OPT PASS", never "OPT FAIL"). Guards against an -O level / native targeting
# regression changing program behavior.
#
# Required: LS_EXE, SAMPLE, WORK_DIR, TEST_NAME (default "opt_levels").
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_opt_levels.cmake requires LS_EXE and SAMPLE")
endif()
if(NOT TEST_NAME)
    set(TEST_NAME "opt_levels")
endif()

# ---- 1. JIT at -O0 / -O2 / -O3 ----
foreach(_lvl "-O0" "-O2" "-O3")
    execute_process(
        COMMAND "${LS_EXE}" run "${_lvl}" "${SAMPLE}"
        OUTPUT_VARIABLE _out  ERROR_VARIABLE _err  RESULT_VARIABLE _rc
    )
    # -O0 disables the pre-JIT pipeline (== plain run); -O2/-O3 enable it.
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "${TEST_NAME} JIT ${_lvl} failed (rc=${_rc})\nstderr:\n${_err}")
    endif()
    if(_out MATCHES "FAIL" OR NOT _out MATCHES "OPT PASS")
        message(FATAL_ERROR "${TEST_NAME} JIT ${_lvl} wrong output:\n${_out}")
    endif()
endforeach()

# ---- 2. AOT at -O0 (generic) and -O3 --native ----
foreach(_variant "-O0" "-O3;--native")
    set(_aot_bin "${WORK_DIR}/${TEST_NAME}_aot")
    if(WIN32)
        set(_aot_bin "${_aot_bin}.exe")
    endif()
    execute_process(
        COMMAND "${LS_EXE}" compile "${SAMPLE}" ${_variant} -o "${_aot_bin}"
        RESULT_VARIABLE _crc  ERROR_VARIABLE _cerr
    )
    if(NOT _crc EQUAL 0)
        message(FATAL_ERROR "${TEST_NAME} AOT compile ${_variant} failed:\n${_cerr}")
    endif()
    execute_process(
        COMMAND "${_aot_bin}"
        OUTPUT_VARIABLE _rout  RESULT_VARIABLE _rrc
    )
    file(REMOVE "${_aot_bin}")
    if(NOT _rrc EQUAL 0)
        message(FATAL_ERROR "${TEST_NAME} AOT run ${_variant} failed (rc=${_rrc})\nstdout:\n${_rout}")
    endif()
    if(_rout MATCHES "FAIL" OR NOT _rout MATCHES "OPT PASS")
        message(FATAL_ERROR "${TEST_NAME} AOT ${_variant} wrong output:\n${_rout}")
    endif()
endforeach()

message(STATUS "${TEST_NAME}: JIT -O0/-O2/-O3 + AOT -O0/-O3-native PASS")
