# std/perf.lls: import std.perf as p — now / rdtsc / rdtscp / elapsed_*
# Verifies: JIT output / AOT output / JIT memcheck 0 leaks
# (std.perf uses no heap allocation; 0-leak check confirms no hidden allocs)

cmake_minimum_required(VERSION 3.20)

if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_ls_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_ls_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_ls_root}")

set(LS      "${LS_EXE}")
set(SRC     "${CMAKE_CURRENT_LIST_DIR}/samples/std_perf_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/std_perf_test.exe")

# ── Step 1: JIT run ──────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "std.perf JIT run failed (rc=${jit_rc}):\n${jit_err}")
endif()
if(jit_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${jit_out}")
endif()
if(NOT jit_out MATCHES "ALL PASS")
    message(FATAL_ERROR "std.perf JIT: expected 'ALL PASS', got:\n${jit_out}")
endif()
message(STATUS "std.perf JIT: OK")

# ── Step 2: JIT memcheck ─────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "std.perf JIT memcheck failed (rc=${mc_rc}):\n${mc_err}")
endif()
if(mc_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${mc_out}")
endif()
if(NOT mc_out MATCHES "ALL PASS")
    message(FATAL_ERROR "std.perf JIT memcheck: expected 'ALL PASS', got:\n${mc_out}")
endif()
if(NOT mc_err MATCHES "0 leak")
    message(FATAL_ERROR "std.perf JIT memcheck: expected 0 leaks:\n${mc_err}")
endif()
message(STATUS "std.perf JIT memcheck: OK (0 leaks)")

# ── Step 3: AOT compile + run ────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    OUTPUT_VARIABLE aot_cout
    ERROR_VARIABLE  aot_cerr
    RESULT_VARIABLE aot_crc
)
if(NOT aot_crc EQUAL 0)
    message(FATAL_ERROR "std.perf AOT compile failed (rc=${aot_crc}):\n${aot_cerr}")
endif()
execute_process(
    COMMAND "${OUT_EXE}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
)
if(aot_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${aot_out}")
endif()
if(NOT aot_out MATCHES "ALL PASS")
    message(FATAL_ERROR "std.perf AOT: expected 'ALL PASS', got (rc=${aot_rc}):\n${aot_out}")
endif()
message(STATUS "std.perf AOT: OK")

message(STATUS "std.perf all passed")
