# Built-in `perf` module: now / rdtsc / rdtscp / elapsed_ns / elapsed_ms / elapsed_s
# Verifies: JIT output / AOT output
# (memcheck not run — perf functions are inline LLVM intrinsics with no heap allocation)

cmake_minimum_required(VERSION 3.20)

set(LS   "${LS_EXE}")
set(SRC  "${CMAKE_CURRENT_LIST_DIR}/samples/perf_basic_test.ls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/perf_basic_test.exe")

# ── Step 1: JIT run ──────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "perf JIT run failed (rc=${jit_rc}):\n${jit_err}")
endif()
if(jit_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${jit_out}")
endif()
if(NOT jit_out MATCHES "ALL PASS")
    message(FATAL_ERROR "perf JIT: expected 'ALL PASS', got:\n${jit_out}")
endif()
message(STATUS "perf JIT: OK")

# ── Step 2: AOT compile + run ────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    OUTPUT_VARIABLE aot_cout
    ERROR_VARIABLE  aot_cerr
    RESULT_VARIABLE aot_crc
)
if(NOT aot_crc EQUAL 0)
    message(FATAL_ERROR "perf AOT compile failed (rc=${aot_crc}):\n${aot_cerr}")
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
    message(FATAL_ERROR "perf AOT: expected 'ALL PASS', got (rc=${aot_rc}):\n${aot_out}")
endif()
message(STATUS "perf AOT: OK")

message(STATUS "perf all passed")
