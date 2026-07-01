# Step 1 — realloc() exposed to LS surface + memcheck tracks the realloc chain.
# Verifies: JIT run / JIT memcheck 0 leaks / AOT run / AOT memcheck 0 leaks.

cmake_minimum_required(VERSION 3.20)

set(LS      "${LS_EXE}")
set(SRC     "${CMAKE_CURRENT_LIST_DIR}/samples/rawvec_realloc_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/rawvec_realloc.exe")
set(MC_EXE  "${CMAKE_BINARY_DIR}/rawvec_realloc_mc.exe")

# ── Step 1: JIT run ──────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "rawvec_realloc JIT run failed (rc=${jit_rc}):\n${jit_err}")
endif()
if(NOT jit_out MATCHES "RAW1 PASS")
    message(FATAL_ERROR "rawvec_realloc JIT: expected 'RAW1 PASS', got:\n${jit_out}")
endif()
message(STATUS "rawvec_realloc JIT: OK")

# ── Step 2: JIT memcheck ─────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "rawvec_realloc JIT memcheck failed (rc=${mc_rc}):\n${mc_err}")
endif()
if(NOT mc_err MATCHES "0 leak")
    message(FATAL_ERROR "rawvec_realloc JIT memcheck: expected 0 leaks:\n${mc_err}")
endif()
if(NOT mc_err MATCHES "0 double-free")
    message(FATAL_ERROR "rawvec_realloc JIT memcheck: expected 0 double-free:\n${mc_err}")
endif()
if(NOT mc_err MATCHES "0 invalid free")
    message(FATAL_ERROR "rawvec_realloc JIT memcheck: expected 0 invalid free:\n${mc_err}")
endif()
message(STATUS "rawvec_realloc JIT memcheck: OK (0/0/0)")

# ── Step 3: AOT compile + run ────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    OUTPUT_VARIABLE aot_cout
    ERROR_VARIABLE  aot_cerr
    RESULT_VARIABLE aot_crc
)
if(NOT aot_crc EQUAL 0)
    message(FATAL_ERROR "rawvec_realloc AOT compile failed (rc=${aot_crc}):\n${aot_cerr}")
endif()
execute_process(
    COMMAND "${OUT_EXE}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
)
if(NOT aot_out MATCHES "RAW1 PASS")
    message(FATAL_ERROR "rawvec_realloc AOT: expected 'RAW1 PASS', got (rc=${aot_rc}):\n${aot_out}")
endif()
message(STATUS "rawvec_realloc AOT: OK")

# ── Step 4: AOT memcheck ─────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile --memcheck "${SRC}" -o "${MC_EXE}"
    OUTPUT_VARIABLE mc_cout
    ERROR_VARIABLE  mc_cerr
    RESULT_VARIABLE mc_crc
)
if(NOT mc_crc EQUAL 0)
    message(FATAL_ERROR "rawvec_realloc AOT memcheck compile failed (rc=${mc_crc}):\n${mc_cerr}")
endif()
execute_process(
    COMMAND "${MC_EXE}"
    OUTPUT_VARIABLE mc_aot_out
    ERROR_VARIABLE  mc_aot_err
    RESULT_VARIABLE mc_aot_rc
)
if(NOT mc_aot_out MATCHES "RAW1 PASS")
    message(FATAL_ERROR "rawvec_realloc AOT memcheck: expected 'RAW1 PASS', got:\n${mc_aot_out}")
endif()
if(NOT mc_aot_err MATCHES "0 leak")
    message(FATAL_ERROR "rawvec_realloc AOT memcheck: expected 0 leaks:\n${mc_aot_err}\n${mc_aot_out}")
endif()
message(STATUS "rawvec_realloc AOT memcheck: OK (0 leaks)")

message(STATUS "rawvec_realloc all passed")
