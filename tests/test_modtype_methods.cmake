# test_modtype_methods.cmake — B-4.1: same-named struct/enum WITH methods in two
# imported modules coexist (impl_registry keyed by type unique name). Instance +
# static methods + enum methods, disambiguated via qualified types. JIT+AOT+memcheck.
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/modtype_methods/main.ls")
set(_expected "va=30 vb=105 ra=2 rb=40" "MODTYPE_METHODS PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "modtype_methods JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "modtype_methods JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "modtype_methods JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/modtype_methods_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "modtype_methods AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "modtype_methods AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "modtype_methods AOT missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "modtype_methods AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "modtype_methods memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "modtype_methods --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "modtype_methods memcheck: OK clean")

message(STATUS "test_modtype_methods: ALL PASSED")
