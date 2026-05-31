# test_decl_init_mul.cmake — declaration-initializer `Type name = a * b`
# Pre-fix bug: a bare `ident * ident` in a var-decl initializer was misparsed as a
# pointer declaration (`* b`), yielding "[type error] unknown type 'b'". The decl/mul
# disambiguation now only splits `*Ident Ident` at a real statement boundary, so
# multiplication (POD and operator-overloaded) parses without parentheses.
# Regression assertion: compiles + runs (JIT + AOT), markers print, memcheck clean.
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/decl_init_mul.ls")
set(_expected "42" "84" "DECL_MUL PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "decl_init_mul JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "decl_init_mul JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "decl_init_mul JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/decl_init_mul_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "decl_init_mul AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "decl_init_mul AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "decl_init_mul AOT missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "decl_init_mul AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "decl_init_mul memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "decl_init_mul --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "decl_init_mul memcheck: OK clean")

message(STATUS "test_decl_init_mul: ALL PASSED")
