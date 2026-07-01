# Phase F.7 Stress Test
# Tests all closure capture patterns in a 1000-iteration loop:
#   S1. POD by-copy, S2. string by-move factory, S3. struct(has_drop) with Block field,
#   S4. vec(Block) push/call/drop, S5. has_drop enum capture (emit_enum_clone_val),
#   S6. inline [move] vec capture
# Verifies: JIT output / JIT memcheck 0 leaks / AOT output / AOT memcheck 0 leaks

cmake_minimum_required(VERSION 3.20)

set(LS      "${LS_EXE}")
set(SRC     "${CMAKE_CURRENT_LIST_DIR}/samples/closure_f7_stress_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/f7_stress_test.exe")
set(MC_EXE  "${CMAKE_BINARY_DIR}/f7_stress_mc_test.exe")

# ── Step 1: JIT run ───────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "F.7 JIT run failed (rc=${jit_rc}):\n${jit_err}")
endif()
if(NOT jit_out MATCHES "stress ok")
    message(FATAL_ERROR "F.7 JIT: expected 'stress ok', got:\n${jit_out}")
endif()
message(STATUS "F.7 JIT: OK")

# ── Step 2: JIT memcheck ──────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "F.7 JIT memcheck failed (rc=${mc_rc}):\n${mc_err}")
endif()
if(NOT mc_out MATCHES "stress ok")
    message(FATAL_ERROR "F.7 JIT memcheck: expected 'stress ok', got:\n${mc_out}")
endif()
if(mc_err MATCHES "\\[memcheck\\] LEAK" OR mc_err MATCHES "\\[memcheck\\] DOUBLE FREE" OR mc_err MATCHES "\\[memcheck\\] INVALID FREE")
    message(FATAL_ERROR "F.7 JIT memcheck violations:\n${mc_err}")
endif()
if(NOT mc_err MATCHES "OK clean")
    message(FATAL_ERROR "F.7 JIT memcheck: expected 'OK clean', got:\n${mc_err}")
endif()
message(STATUS "F.7 JIT memcheck: OK (0 leaks, 0 double-free)")

# ── Step 3: AOT compile ───────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    OUTPUT_VARIABLE aot_compile_out
    ERROR_VARIABLE  aot_compile_err
    RESULT_VARIABLE aot_compile_rc
)
if(NOT aot_compile_rc EQUAL 0)
    message(FATAL_ERROR "F.7 AOT compile failed (rc=${aot_compile_rc}):\n${aot_compile_err}")
endif()

execute_process(
    COMMAND "${OUT_EXE}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "F.7 AOT run failed (rc=${aot_rc}):\n${aot_err}")
endif()
if(NOT aot_out MATCHES "stress ok")
    message(FATAL_ERROR "F.7 AOT: expected 'stress ok', got:\n${aot_out}")
endif()
message(STATUS "F.7 AOT: OK")

# ── Step 4: AOT memcheck ──────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile --memcheck "${SRC}" -o "${MC_EXE}"
    OUTPUT_VARIABLE mc_compile_out
    ERROR_VARIABLE  mc_compile_err
    RESULT_VARIABLE mc_compile_rc
)
if(NOT mc_compile_rc EQUAL 0)
    message(FATAL_ERROR "F.7 AOT memcheck compile failed (rc=${mc_compile_rc}):\n${mc_compile_err}")
endif()

execute_process(
    COMMAND "${MC_EXE}"
    OUTPUT_VARIABLE mc_aot_out
    ERROR_VARIABLE  mc_aot_err
    RESULT_VARIABLE mc_aot_rc
)
if(NOT mc_aot_rc EQUAL 0)
    message(FATAL_ERROR "F.7 AOT memcheck run failed (rc=${mc_aot_rc}):\n${mc_aot_err}")
endif()
if(NOT mc_aot_out MATCHES "stress ok")
    message(FATAL_ERROR "F.7 AOT memcheck: expected 'stress ok', got:\n${mc_aot_out}")
endif()
if(mc_aot_err MATCHES "\\[memcheck\\] LEAK" OR mc_aot_err MATCHES "\\[memcheck\\] DOUBLE FREE" OR mc_aot_err MATCHES "\\[memcheck\\] INVALID FREE")
    message(FATAL_ERROR "F.7 AOT memcheck violations:\n${mc_aot_err}")
endif()
if(NOT mc_aot_err MATCHES "OK clean")
    message(FATAL_ERROR "F.7 AOT memcheck: expected 'OK clean', got:\n${mc_aot_err}")
endif()
message(STATUS "F.7 AOT memcheck: OK (0 leaks, 0 double-free)")
