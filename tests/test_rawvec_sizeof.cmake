# Step 2 — sizeof(T) compile-time evaluation (primitive/pointer sizes + generic
# struct method monomorphization + arithmetic). JIT + AOT (no allocation, so no
# memcheck needed).

cmake_minimum_required(VERSION 3.20)

set(LS      "${LS_EXE}")
set(SRC     "${CMAKE_CURRENT_LIST_DIR}/samples/rawvec_sizeof_test.ls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/rawvec_sizeof.exe")

# ── JIT run ──────────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "rawvec_sizeof JIT run failed (rc=${jit_rc}):\n${jit_err}")
endif()
if(NOT jit_out MATCHES "SIZEOF PASS")
    message(FATAL_ERROR "rawvec_sizeof JIT: expected 'SIZEOF PASS', got:\n${jit_out}")
endif()
if(jit_out MATCHES "FAIL ")
    message(FATAL_ERROR "rawvec_sizeof JIT: a check FAILed:\n${jit_out}")
endif()
message(STATUS "rawvec_sizeof JIT: OK")

# ── AOT compile + run ────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    OUTPUT_VARIABLE aot_cout
    ERROR_VARIABLE  aot_cerr
    RESULT_VARIABLE aot_crc
)
if(NOT aot_crc EQUAL 0)
    message(FATAL_ERROR "rawvec_sizeof AOT compile failed (rc=${aot_crc}):\n${aot_cerr}")
endif()
execute_process(
    COMMAND "${OUT_EXE}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
)
if(NOT aot_out MATCHES "SIZEOF PASS")
    message(FATAL_ERROR "rawvec_sizeof AOT: expected 'SIZEOF PASS', got (rc=${aot_rc}):\n${aot_out}")
endif()
if(aot_out MATCHES "FAIL ")
    message(FATAL_ERROR "rawvec_sizeof AOT: a check FAILed:\n${aot_out}")
endif()
message(STATUS "rawvec_sizeof AOT: OK")

message(STATUS "rawvec_sizeof all passed")
