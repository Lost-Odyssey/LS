# test_io_raii.cmake — io.File is an RAII resource handle: it auto-closes at
# scope exit (no leak on a forgotten close) and nils its handle so an explicit
# close followed by the destructor is a safe no-op (no double fclose). io.File
# is move-only (object handle + Destroy, no Clone), so it is matched out of the
# owned `io.open(...)` rvalue by move. Asserts memcheck is clean (the auto-close
# frees the handle; the explicit-then-auto close does not double-free).
# Runs io_basic_test.ls (which now includes a no-explicit-close RAII case).
cmake_minimum_required(VERSION 3.20)

# Resolve stdlib (the updated lib/std/sys/io.ls) from the source tree, not the
# build copy — mirrors tests/test_e3_glue.cmake.
get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SRC "${SAMPLE_DIR}/io_basic_test.ls")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "io_raii memcheck FAILED (rc=${mc_rc})\n${mc_err}\n${mc_out}")
endif()
if(NOT "${mc_out}" MATCHES "ALL PASS")
    message(FATAL_ERROR "io_raii: io_basic did not reach ALL PASS\n${mc_out}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "io_raii memcheck leak/double-free — File RAII broken\n${mc_err}")
endif()
message(STATUS "io_raii memcheck: OK clean (File auto-close, no double fclose)")
