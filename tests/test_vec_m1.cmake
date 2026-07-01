# Step 5 / Gate M1 — has_drop element ownership in a self-managed raw-memory
# container: move-in (push), move-out (pop), per-element recursive drop (__drop),
# nested drop (RawVec of has_drop struct / RawVec of RawVec). The new __drop_at
# intrinsic + existing __move provide the ownership primitives. memcheck 0/0/0.

cmake_minimum_required(VERSION 3.20)

set(LS      "${LS_EXE}")
set(SRC     "${CMAKE_CURRENT_LIST_DIR}/samples/rawvec_m1_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/rawvec_m1.exe")
set(MC_EXE  "${CMAKE_BINARY_DIR}/rawvec_m1_mc.exe")

# ── JIT run ──────────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "rawvec_m1 JIT run failed (rc=${jit_rc}):\n${jit_err}")
endif()
if(NOT jit_out MATCHES "M1 PASS")
    message(FATAL_ERROR "rawvec_m1 JIT: expected 'M1 PASS', got:\n${jit_out}")
endif()
if(jit_out MATCHES "FAIL ")
    message(FATAL_ERROR "rawvec_m1 JIT: a check FAILed:\n${jit_out}")
endif()
message(STATUS "rawvec_m1 JIT: OK")

# ── JIT memcheck ─────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "rawvec_m1 JIT memcheck failed (rc=${mc_rc}):\n${mc_err}")
endif()
if(NOT mc_err MATCHES "0 leak")
    message(FATAL_ERROR "rawvec_m1 JIT memcheck: expected 0 leaks:\n${mc_err}")
endif()
if(NOT mc_err MATCHES "0 double-free")
    message(FATAL_ERROR "rawvec_m1 JIT memcheck: expected 0 double-free:\n${mc_err}")
endif()
if(NOT mc_err MATCHES "0 invalid free")
    message(FATAL_ERROR "rawvec_m1 JIT memcheck: expected 0 invalid free:\n${mc_err}")
endif()
message(STATUS "rawvec_m1 JIT memcheck: OK (0/0/0)")

# ── AOT compile + run ────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    OUTPUT_VARIABLE aot_cout
    ERROR_VARIABLE  aot_cerr
    RESULT_VARIABLE aot_crc
)
if(NOT aot_crc EQUAL 0)
    message(FATAL_ERROR "rawvec_m1 AOT compile failed (rc=${aot_crc}):\n${aot_cerr}")
endif()
execute_process(
    COMMAND "${OUT_EXE}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
)
if(NOT aot_out MATCHES "M1 PASS")
    message(FATAL_ERROR "rawvec_m1 AOT: expected 'M1 PASS', got (rc=${aot_rc}):\n${aot_out}")
endif()
if(aot_out MATCHES "FAIL ")
    message(FATAL_ERROR "rawvec_m1 AOT: a check FAILed:\n${aot_out}")
endif()
message(STATUS "rawvec_m1 AOT: OK")

# ── AOT memcheck ─────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile --memcheck "${SRC}" -o "${MC_EXE}"
    OUTPUT_VARIABLE mc_cout
    ERROR_VARIABLE  mc_cerr
    RESULT_VARIABLE mc_crc
)
if(NOT mc_crc EQUAL 0)
    message(FATAL_ERROR "rawvec_m1 AOT memcheck compile failed (rc=${mc_crc}):\n${mc_cerr}")
endif()
execute_process(
    COMMAND "${MC_EXE}"
    OUTPUT_VARIABLE mc_aot_out
    ERROR_VARIABLE  mc_aot_err
    RESULT_VARIABLE mc_aot_rc
)
if(NOT mc_aot_out MATCHES "M1 PASS")
    message(FATAL_ERROR "rawvec_m1 AOT memcheck: expected 'M1 PASS', got:\n${mc_aot_out}")
endif()
if(NOT mc_aot_err MATCHES "0 leak")
    message(FATAL_ERROR "rawvec_m1 AOT memcheck: expected 0 leaks:\n${mc_aot_err}\n${mc_aot_out}")
endif()
message(STATUS "rawvec_m1 AOT memcheck: OK (0 leaks)")

message(STATUS "rawvec_m1 all passed (Gate M1 green)")
