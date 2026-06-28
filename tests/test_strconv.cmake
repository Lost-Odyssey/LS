# Phase S.P3 — std/strconv.ls: format / int_to_hex / int_to_oct / int_to_bin /
#               float_fixed / to_string / to_string_f
# Verifies: JIT output / JIT memcheck 0 leaks / AOT output / AOT memcheck 0 leaks

cmake_minimum_required(VERSION 3.20)

# Point LS_HOME at the project root so `import std.strconv` resolves to
# std/strconv.ls under the source tree.
if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_ls_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_ls_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_ls_root}")

set(LS      "${LS_EXE}")
set(SRC     "${CMAKE_CURRENT_LIST_DIR}/samples/strconv_test.ls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/strconv_test.exe")
set(MC_EXE  "${CMAKE_BINARY_DIR}/strconv_mc_test.exe")

# ── Step 1: JIT run ──────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "S.P3 JIT run failed (rc=${jit_rc}):\n${jit_err}")
endif()
if(jit_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${jit_out}")
endif()
if(NOT jit_out MATCHES "ALL PASS")
    message(FATAL_ERROR "S.P3 JIT: expected 'ALL PASS', got:\n${jit_out}")
endif()
message(STATUS "S.P3 JIT: OK")

# ── Step 2: JIT memcheck ─────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "S.P3 JIT memcheck failed (rc=${mc_rc}):\n${mc_err}")
endif()
if(mc_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${mc_out}")
endif()
if(NOT mc_out MATCHES "ALL PASS")
    message(FATAL_ERROR "S.P3 JIT memcheck: expected 'ALL PASS', got:\n${mc_out}")
endif()
if(NOT mc_err MATCHES "0 leak")
    message(FATAL_ERROR "S.P3 JIT memcheck: expected 0 leaks:\n${mc_err}")
endif()
message(STATUS "S.P3 JIT memcheck: OK (0 leaks)")

# ── Step 3: AOT compile + run ────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    OUTPUT_VARIABLE aot_cout
    ERROR_VARIABLE  aot_cerr
    RESULT_VARIABLE aot_crc
)
if(NOT aot_crc EQUAL 0)
    message(FATAL_ERROR "S.P3 AOT compile failed (rc=${aot_crc}):\n${aot_cerr}")
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
    message(FATAL_ERROR "S.P3 AOT: expected 'ALL PASS', got (rc=${aot_rc}):\n${aot_out}")
endif()
message(STATUS "S.P3 AOT: OK")

# ── Step 4: AOT memcheck ─────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS}" compile --memcheck "${SRC}" -o "${MC_EXE}"
    OUTPUT_VARIABLE mc_cout
    ERROR_VARIABLE  mc_cerr
    RESULT_VARIABLE mc_crc
)
if(NOT mc_crc EQUAL 0)
    message(FATAL_ERROR "S.P3 AOT memcheck compile failed (rc=${mc_crc}):\n${mc_cerr}")
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
    message(FATAL_ERROR "S.P3 AOT memcheck: expected 'ALL PASS', got:\n${mc_aot_out}")
endif()
if(NOT mc_aot_err MATCHES "0 leak")
    message(FATAL_ERROR "S.P3 AOT memcheck: expected 0 leaks:\n${mc_aot_err}\n${mc_aot_out}")
endif()
message(STATUS "S.P3 AOT memcheck: OK (0 leaks)")

message(STATUS "S.P3 all passed")
