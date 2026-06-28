# test_sink_atprint.cmake — Stage F: `@print` is the print intrinsic (dedicated
# @-token like @time/@bench; the only spelling, bare print retired). Composes with
# C-2 Show dispatch + D f-string Show. docs/plan_print_sink.md Stage F.
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SRC "${SAMPLE_DIR}/sink_atprint_test.ls")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sink_atprint FAILED (rc=${rc})\n${err}\n${out}")
endif()
if(NOT "${out}" MATCHES "at-print works")
    message(FATAL_ERROR "@print basic call failed\n${out}")
endif()
if(NOT "${out}" MATCHES "P { x: 3, y: 4 }")
    message(FATAL_ERROR "@print(Show struct) did not dispatch via Show\n${out}")
endif()
if(NOT "${out}" MATCHES "interp P { x: 3, y: 4 }")
    message(FATAL_ERROR "@print(f-string Show) failed\n${out}")
endif()
if(NOT "${out}" MATCHES "ATPRINT DONE")
    message(FATAL_ERROR "sink_atprint did not complete\n${out}")
endif()
if(NOT "${err}" MATCHES "OK clean")
    message(FATAL_ERROR "sink_atprint memcheck leak/double-free\n${err}")
endif()
message(STATUS "sink_atprint: @print works (dual-track) + Show dispatch + memcheck clean")
