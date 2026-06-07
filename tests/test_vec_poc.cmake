# Step 4 / Gate M0 — hand-written RawVecI (POD) over raw malloc/realloc/free +
# sizeof + p[i]. __drop frees the buffer once; memcheck 0/0/0 despite the
# realloc migration chain. JIT + AOT + memcheck.

cmake_minimum_required(VERSION 3.20)

set(LS      "${LS_EXE}")
set(SRC     "${CMAKE_CURRENT_LIST_DIR}/samples/rawvec_poc_test.ls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/rawvec_poc.exe")
set(MC_EXE  "${CMAKE_BINARY_DIR}/rawvec_poc_mc.exe")

# ── JIT run ──────────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "rawvec_poc JIT run failed (rc=${jit_rc}):\n${jit_err}")
endif()
if(NOT jit_out MATCHES "RAWPOC PASS")
    message(FATAL_ERROR "rawvec_poc JIT: expected 'RAWPOC PASS', got:\n${jit_out}")
endif()
if(jit_out MATCHES "FAIL ")
    message(FATAL_ERROR "rawvec_poc JIT: a check FAILed:\n${jit_out}")
endif()
message(STATUS "rawvec_poc JIT: OK")

# ── JIT memcheck ─────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "rawvec_poc JIT memcheck failed (rc=${mc_rc}):\n${mc_err}")
endif()
if(NOT mc_err MATCHES "0 leak")
    message(FATAL_ERROR "rawvec_poc JIT memcheck: expected 0 leaks:\n${mc_err}")
endif()
if(NOT mc_err MATCHES "0 double-free")
    message(FATAL_ERROR "rawvec_poc JIT memcheck: expected 0 double-free:\n${mc_err}")
endif()
if(NOT mc_err MATCHES "0 invalid free")
    message(FATAL_ERROR "rawvec_poc JIT memcheck: expected 0 invalid free:\n${mc_err}")
endif()
message(STATUS "rawvec_poc JIT memcheck: OK (0/0/0)")

# ── AOT compile + run ────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    OUTPUT_VARIABLE aot_cout
    ERROR_VARIABLE  aot_cerr
    RESULT_VARIABLE aot_crc
)
if(NOT aot_crc EQUAL 0)
    message(FATAL_ERROR "rawvec_poc AOT compile failed (rc=${aot_crc}):\n${aot_cerr}")
endif()
execute_process(
    COMMAND "${OUT_EXE}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
)
if(NOT aot_out MATCHES "RAWPOC PASS")
    message(FATAL_ERROR "rawvec_poc AOT: expected 'RAWPOC PASS', got (rc=${aot_rc}):\n${aot_out}")
endif()
if(aot_out MATCHES "FAIL ")
    message(FATAL_ERROR "rawvec_poc AOT: a check FAILed:\n${aot_out}")
endif()
message(STATUS "rawvec_poc AOT: OK")

# ── AOT memcheck ─────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile --memcheck "${SRC}" -o "${MC_EXE}"
    OUTPUT_VARIABLE mc_cout
    ERROR_VARIABLE  mc_cerr
    RESULT_VARIABLE mc_crc
)
if(NOT mc_crc EQUAL 0)
    message(FATAL_ERROR "rawvec_poc AOT memcheck compile failed (rc=${mc_crc}):\n${mc_cerr}")
endif()
execute_process(
    COMMAND "${MC_EXE}"
    OUTPUT_VARIABLE mc_aot_out
    ERROR_VARIABLE  mc_aot_err
    RESULT_VARIABLE mc_aot_rc
)
if(NOT mc_aot_out MATCHES "RAWPOC PASS")
    message(FATAL_ERROR "rawvec_poc AOT memcheck: expected 'RAWPOC PASS', got:\n${mc_aot_out}")
endif()
if(NOT mc_aot_err MATCHES "0 leak")
    message(FATAL_ERROR "rawvec_poc AOT memcheck: expected 0 leaks:\n${mc_aot_err}\n${mc_aot_out}")
endif()
message(STATUS "rawvec_poc AOT memcheck: OK (0 leaks)")

message(STATUS "rawvec_poc all passed (Gate M0 green)")
