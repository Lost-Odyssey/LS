# test_l0091_modgen.cmake — L-009.1 module generics (A1 + A2): JIT + AOT + memcheck
#
# A1: generic functions used INSIDE an imported module are now instantiated
#     (previously the module's pending-generic queue was discarded → call site
#      failed with "undefined function 'box(int)'").
# A2: same-named generic functions with different bodies in two modules get
#     distinct module-prefixed symbols (previously both mangled to tag(int) →
#     silent-wrong, second module's body lost).
#
# Fixture: tests/samples/l0091_modgen/{main,mod_a,mod_b}.ls
#   main asserts: mod_a.use_int()==7 (A1), mod_a.run_tag()==1 & mod_b.run_tag()==2
#   (A2), and a root-module generic id(int)(42)==42 (unprefixed, unaffected).
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/l0091_modgen/main.ls")
set(_expected "u=7 ta=1 tb=2 r=42" "L0091 PASS")

# ---- 1. JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "l0091 JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "l0091 JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "l0091 JIT: OK")

# ---- 2. AOT ----
set(aot_bin "${WORK_DIR}/l0091_modgen_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "l0091 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "l0091 AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "l0091 AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "l0091 AOT: OK")
file(REMOVE "${aot_bin}")

# ---- 3. memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "l0091 memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "l0091 --memcheck FAILED (leaks/dfree)\nstderr:\n${mc_err}")
endif()
message(STATUS "l0091 memcheck: OK clean")

message(STATUS "test_l0091_modgen: ALL PASSED")
