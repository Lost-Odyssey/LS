# Step 6 / Gate M2 — generic std.rawvec RawVec(T) across element types
# (int / string / Pt) under monomorphization, matching vec semantics.
# JIT + AOT + memcheck 0/0/0. Imports std.rawvec, so LS_HOME must point at repo.

cmake_minimum_required(VERSION 3.20)

set(LS      "${LS_EXE}")
set(SRC     "${CMAKE_CURRENT_LIST_DIR}/samples/rawvec_m2_test.ls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/rawvec_m2.exe")
set(MC_EXE  "${CMAKE_BINARY_DIR}/rawvec_m2_mc.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

# ── JIT run ──────────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "rawvec_m2 JIT run failed (rc=${jit_rc}):\n${jit_err}")
endif()
if(NOT jit_out MATCHES "M2 PASS")
    message(FATAL_ERROR "rawvec_m2 JIT: expected 'M2 PASS', got:\n${jit_out}")
endif()
if(jit_out MATCHES "FAIL ")
    message(FATAL_ERROR "rawvec_m2 JIT: a check FAILed:\n${jit_out}")
endif()
message(STATUS "rawvec_m2 JIT: OK")

# ── JIT memcheck ─────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "rawvec_m2 JIT memcheck failed (rc=${mc_rc}):\n${mc_err}")
endif()
if(NOT mc_err MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "rawvec_m2 JIT memcheck SUMMARY mismatch:\n${mc_err}")
endif()
message(STATUS "rawvec_m2 JIT memcheck: OK (0/0/0)")

# ── AOT compile + run ────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE aot_crc ERROR_VARIABLE aot_cerr)
if(NOT aot_crc EQUAL 0)
    message(FATAL_ERROR "rawvec_m2 AOT compile failed (rc=${aot_crc}):\n${aot_cerr}")
endif()
execute_process(
    COMMAND "${OUT_EXE}"
    OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_rc)
if(NOT aot_out MATCHES "M2 PASS")
    message(FATAL_ERROR "rawvec_m2 AOT: expected 'M2 PASS', got (rc=${aot_rc}):\n${aot_out}")
endif()
if(aot_out MATCHES "FAIL ")
    message(FATAL_ERROR "rawvec_m2 AOT: a check FAILed:\n${aot_out}")
endif()
message(STATUS "rawvec_m2 AOT: OK")

message(STATUS "rawvec_m2 all passed (Gate M2 green)")
