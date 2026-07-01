# Owned-param / move-into-container optimization — RawVec string push.
# Verifies the rvalue/__move-args-move ABI: rvalue & __move args are MOVED into the
# container (no clone), while a named-variable arg is borrowed (cloned, caller's
# var stays valid). JIT + AOT + memcheck 0/0/0.

cmake_minimum_required(VERSION 3.20)

set(LS      "${LS_EXE}")
set(SRC     "${CMAKE_CURRENT_LIST_DIR}/samples/rawvec_move_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/rawvec_move.exe")
set(MC_EXE  "${CMAKE_BINARY_DIR}/rawvec_move_mc.exe")

# ── JIT run ──────────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "rawvec_move JIT run failed (rc=${jit_rc}):\n${jit_err}")
endif()
if(NOT jit_out MATCHES "ALL PASS")
    message(FATAL_ERROR "rawvec_move JIT: expected 'ALL PASS', got:\n${jit_out}")
endif()
if(jit_out MATCHES "FAIL ")
    message(FATAL_ERROR "rawvec_move JIT: a check FAILed:\n${jit_out}")
endif()
message(STATUS "rawvec_move JIT: OK")

# ── JIT memcheck ─────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "rawvec_move JIT memcheck failed (rc=${mc_rc}):\n${mc_err}")
endif()
if(NOT mc_err MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "rawvec_move JIT memcheck SUMMARY mismatch:\n${mc_err}")
endif()
message(STATUS "rawvec_move JIT memcheck: OK (0/0/0)")

# ── AOT compile + run ────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE aot_crc ERROR_VARIABLE aot_cerr)
if(NOT aot_crc EQUAL 0)
    message(FATAL_ERROR "rawvec_move AOT compile failed (rc=${aot_crc}):\n${aot_cerr}")
endif()
execute_process(
    COMMAND "${OUT_EXE}"
    OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_rc)
if(NOT aot_out MATCHES "ALL PASS")
    message(FATAL_ERROR "rawvec_move AOT: expected 'ALL PASS', got (rc=${aot_rc}):\n${aot_out}")
endif()
if(aot_out MATCHES "FAIL ")
    message(FATAL_ERROR "rawvec_move AOT: a check FAILed:\n${aot_out}")
endif()
message(STATUS "rawvec_move AOT: OK")

message(STATUS "rawvec_move all passed")
