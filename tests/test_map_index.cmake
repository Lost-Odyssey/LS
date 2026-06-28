# test_map_index.cmake — Map(K,V) index protocol.
#  * Positive: m[k]=v insert/update + m[k] read (clone, chains) — JIT+AOT+memcheck.
#  * Negative: reading a missing key via m[k] aborts with a "key not found"
#    diagnostic and a non-zero exit; the post-access line must NOT run.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/map_index_test.ls")
set(_expected "MAP_INDEX PASS")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "map_index positive JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "map_index positive JIT missing '${_expected}'\n${jit_out}")
endif()
message(STATUS "map_index positive JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/map_index_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "map_index positive AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "map_index positive AOT run FAILED (rc=${aot_run_rc})")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "map_index positive AOT missing '${_expected}'\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "map_index positive AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "map_index memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "map_index memcheck leak\n${mc_err}")
endif()
message(STATUS "map_index positive memcheck: OK clean")

# ---- negative: reading a missing key via m[k] must abort ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/map_index_panic.ls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "map_index_panic: expected non-zero exit (abort)\n${n_out}")
endif()
if(NOT "${n_out}" MATCHES "key not found")
    message(FATAL_ERROR "map_index_panic: missing 'key not found' diagnostic\n${n_out}")
endif()
if("${n_out}" MATCHES "AFTER")
    message(FATAL_ERROR "map_index_panic: ran past the missing-key access\n${n_out}")
endif()
message(STATUS "map_index_panic: aborted as expected (rc=${n_rc})")

message(STATUS "test_map_index: ALL PASSED")
