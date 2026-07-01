# test_move_only.cmake — move-only resource types (Destroy + raw ptr/object
# field, no Clone) matched out of an OWNED enum subject (inline `match make()`).
# Guards the codegen_match.c move-out (vs clone) for move-only binders that makes
# RAII handles like io.File work with the idiomatic inline match.
# JIT + AOT + memcheck (single owner — each resource freed exactly once).
cmake_minimum_required(VERSION 3.20)

# Resolve stdlib from the source tree (not the build copy) — mirrors test_e3_glue.
get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SRC "${SAMPLE_DIR}/move_only_test.lls")
set(_expected "MOVEONLY PASS")

execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "move_only JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "move_only JIT missing '${_expected}'\n${jit_out}")
endif()
message(STATUS "move_only JIT: OK")

set(aot_bin "${WORK_DIR}/move_only_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "move_only AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0 OR NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "move_only AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
message(STATUS "move_only AOT: OK")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "move_only memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "move_only memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "move_only memcheck: OK clean")
