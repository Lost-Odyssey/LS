# test_modtype_generic_enum.cmake — generic enum instances keyed by module-prefixed
# type-arg names: mod_a.Node vs mod_b.Node fed to Option(T) must yield two distinct
# instantiations (used to collide on "Option(Node)" → payload read through the wrong
# layout). JIT + AOT + memcheck.
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/modtype_generic_enum/main.lls")
set(_expected "a=42 sum=7.5" "MODTYPE_GENERIC_ENUM PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "modtype_generic_enum JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "modtype_generic_enum JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "modtype_generic_enum JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/modtype_generic_enum_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "modtype_generic_enum AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "modtype_generic_enum AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "modtype_generic_enum AOT missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "modtype_generic_enum AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "modtype_generic_enum memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "modtype_generic_enum --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "modtype_generic_enum memcheck: OK clean")

message(STATUS "test_modtype_generic_enum: ALL PASSED")
