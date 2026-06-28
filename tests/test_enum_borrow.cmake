# Phase 9 — &Enum read-only borrow + zero-copy match destructuring
# Verifies: JIT output / JIT memcheck 0 leaks / AOT output / AOT memcheck 0 leaks

cmake_minimum_required(VERSION 3.20)

set(LS      "${LS_EXE}")
set(SRC     "${CMAKE_CURRENT_LIST_DIR}/samples/enum_borrow_test.ls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/enum_borrow_test.exe")
set(MC_EXE  "${CMAKE_BINARY_DIR}/enum_borrow_mc.exe")

# ── Step 1: JIT run ──────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "enum_borrow JIT run failed (rc=${jit_rc}):\n${jit_err}")
endif()
if(jit_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${jit_out}")
endif()
if(NOT jit_out MATCHES "ALL PASS")
    message(FATAL_ERROR "enum_borrow JIT: expected 'ALL PASS', got:\n${jit_out}")
endif()
message(STATUS "enum_borrow JIT: OK")

# ── Step 2: JIT memcheck ─────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "enum_borrow JIT memcheck failed (rc=${mc_rc}):\n${mc_err}")
endif()
if(NOT mc_err MATCHES "0 leak")
    message(FATAL_ERROR "enum_borrow JIT memcheck: expected 0 leaks:\n${mc_err}")
endif()
message(STATUS "enum_borrow JIT memcheck: OK (0 leaks)")

# ── Step 3: AOT compile + run ────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    OUTPUT_VARIABLE aot_cout
    ERROR_VARIABLE  aot_cerr
    RESULT_VARIABLE aot_crc
)
if(NOT aot_crc EQUAL 0)
    message(FATAL_ERROR "enum_borrow AOT compile failed (rc=${aot_crc}):\n${aot_cerr}")
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
    message(FATAL_ERROR "enum_borrow AOT: expected 'ALL PASS', got (rc=${aot_rc}):\n${aot_out}")
endif()
message(STATUS "enum_borrow AOT: OK")

# ── Step 4: AOT memcheck ─────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile --memcheck "${SRC}" -o "${MC_EXE}"
    OUTPUT_VARIABLE mc_cout
    ERROR_VARIABLE  mc_cerr
    RESULT_VARIABLE mc_crc
)
if(NOT mc_crc EQUAL 0)
    message(FATAL_ERROR "enum_borrow AOT memcheck compile failed (rc=${mc_crc}):\n${mc_cerr}")
endif()
execute_process(
    COMMAND "${MC_EXE}"
    OUTPUT_VARIABLE mc_aot_out
    ERROR_VARIABLE  mc_aot_err
    RESULT_VARIABLE mc_aot_rc
)
if(mc_aot_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${mc_aot_out}")
endif()
if(NOT mc_aot_out MATCHES "ALL PASS")
    message(FATAL_ERROR "enum_borrow AOT memcheck: expected 'ALL PASS', got:\n${mc_aot_out}")
endif()
if(NOT mc_aot_err MATCHES "0 leak")
    message(FATAL_ERROR "enum_borrow AOT memcheck: expected 0 leaks:\n${mc_aot_err}\n${mc_aot_out}")
endif()
message(STATUS "enum_borrow AOT memcheck: OK (0 leaks)")

message(STATUS "enum_borrow all passed")
