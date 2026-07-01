# test_map_upsert.cmake — Map.upsert (update-or-insert, single hash+probe).
# JIT + AOT + memcheck 0/0/0. Covers POD counter, Str key (has_drop key drop on
# present path), Vec value (has_drop group-by), equivalence with get-then-set.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/map_upsert_test.lls")
set(_expected "MAP UPSERT PASS")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "map_upsert JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "map_upsert JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL ")
    message(FATAL_ERROR "map_upsert JIT had a FAIL line\n${jit_out}")
endif()
message(STATUS "map_upsert JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/map_upsert_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "map_upsert AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "map_upsert AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "map_upsert AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL ")
    message(FATAL_ERROR "map_upsert AOT had a FAIL line\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "map_upsert AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "map_upsert memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "map_upsert memcheck not clean\n${mc_err}")
endif()
message(STATUS "map_upsert memcheck: OK clean")

message(STATUS "test_map_upsert: ALL PASSED")
