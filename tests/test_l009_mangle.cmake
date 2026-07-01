# test_l009_mangle.cmake — L-009 cross-module function name mangling (JIT + AOT + memcheck)
#
# Regression for two pre-L-009 failure modes:
#   ① user-local `read_file` + imported module `read_file` -> IR verification crash
#   ② two modules each defining `helper` -> silent wrong result (both hit module A)
#
# Fixture: tests/samples/l009_mangle/{main,mod_a,mod_b}.lls
#   main imports mod_a + mod_b, defines its own local read_file, and asserts:
#     mod_a.helper()==1, mod_b.helper()==2          (distinct module-qualified)
#     mod_a.combined()==11, mod_b.combined()==22     (bare intra-module calls)
#     mod_a/mod_b/local read_file all distinct strings
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/l009_mangle/main.lls")

set(_expected
    "helper a=1 b=2"
    "combined a=11 b=22"
    "read a:x b:x local:x"
    "L009 PASS"
)

# ---- 1. JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "l009 JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "l009 JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "l009 JIT: OK")

# ---- 2. AOT ----
set(aot_bin "${WORK_DIR}/l009_mangle_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "l009 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "l009 AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "l009 AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "l009 AOT: OK")
file(REMOVE "${aot_bin}")

# ---- 3. memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "l009 memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "l009 --memcheck FAILED (leaks/dfree detected)\nstderr:\n${mc_err}")
endif()
message(STATUS "l009 memcheck: OK clean")

message(STATUS "test_l009_mangle: ALL PASSED")
