# test_sink_redirect.cmake — Stage C-1: set_sink redirects print() itself.
# print()'s output primitive (emit_printf -> __ls_printf) writes to the current
# sink stream, so set_sink(file/stderr) captures ALL print output; reset returns
# to stdout. Verifies byte-exact captured content + close-on-switch + memcheck.
# docs/plan_print_sink.md Stage C.
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SRC "${SAMPLE_DIR}/sink_redirect_test.lls")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "sink_redirect FAILED (rc=${mc_rc})\n${mc_err}\n${mc_out}")
endif()
if(NOT "${mc_out}" MATCHES "ALL PASS")
    message(FATAL_ERROR "sink_redirect: print() did not redirect correctly\n${mc_out}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "sink_redirect memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "sink_redirect: ALL PASS + memcheck OK clean")
