# test_sink_fstring_show.cmake — Stage D: f-string interpolation honors Show.
# f"...{showStruct}..." renders the struct/enum via Show (was a compile error
# before). Exact output + memcheck (interpolated owned Str rvalues dropped).
# docs/plan_print_sink.md Stage D.
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SRC "${SAMPLE_DIR}/sink_fstring_show_test.lls")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sink_fstring_show FAILED (rc=${rc})\n${err}\n${out}")
endif()
if(NOT "${out}" MATCHES "point is P { x: 3, y: 4 } done")
    message(FATAL_ERROR "f-string Show struct interp wrong\n${out}")
endif()
if(NOT "${out}" MATCHES "\\[B\\(9\\)\\]")
    message(FATAL_ERROR "f-string Show enum interp wrong\n${out}")
endif()
if(NOT "${out}" MATCHES "P { x: 3, y: 4 } and B\\(9\\) and 42")
    message(FATAL_ERROR "f-string mixed interp wrong\n${out}")
endif()
if(NOT "${err}" MATCHES "OK clean")
    message(FATAL_ERROR "sink_fstring_show memcheck leak/double-free\n${err}")
endif()
message(STATUS "sink_fstring_show: f-string honors Show + memcheck clean")
