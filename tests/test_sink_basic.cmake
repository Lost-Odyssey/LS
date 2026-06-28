# test_sink_basic.cmake — Stage A smoke for std.core.sink (docs/plan_print_sink.md).
# Verifies the Sink write helpers + __sink_flush redirect (stdout/stderr/file) +
# the io.file(&!File) ownership-transfer bridge + close-on-switch, and that the
# whole path is memcheck-clean (the redirect File handle is closed exactly once).
cmake_minimum_required(VERSION 3.20)

# Resolve stdlib (new lib/std/core/sink.ls + updated io.ls) from the SOURCE tree,
# not the build copy — mirrors tests/test_io_raii.cmake.
get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SRC "${SAMPLE_DIR}/sink_basic_test.ls")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "sink_basic FAILED (rc=${mc_rc})\n${mc_err}\n${mc_out}")
endif()
if(NOT "${mc_out}" MATCHES "ALL PASS")
    message(FATAL_ERROR "sink_basic: assertions did not reach ALL PASS\n${mc_out}")
endif()
if(NOT "${mc_out}" MATCHES "STDOUT_OK")
    message(FATAL_ERROR "sink_basic: stdout flush missing STDOUT_OK\n${mc_out}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "sink_basic memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "sink_basic: ALL PASS + memcheck OK clean")
