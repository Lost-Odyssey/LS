# test_bf044_shortcircuit.cmake — BF-044: has_drop struct vec[i].field on the RHS
# of a short-circuit && / || (JIT + AOT + memcheck).
# Pre-fix: "Instruction does not dominate all uses" (codegen crash). The regression
# assertion is: compiles + runs, all three branch markers print, memcheck clean.
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/bf044_shortcircuit/main.ls")
set(_expected "AND_OK" "OR_OK" "CHAIN_OK" "BF044 PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "bf044 JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "bf044 JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "bf044 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/bf044_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "bf044 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "bf044 AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "bf044 AOT missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "bf044 AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck (spill clones must be balanced — 0 leak / 0 dfree) ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "bf044 memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "bf044 --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "bf044 memcheck: OK clean")

message(STATUS "test_bf044_shortcircuit: ALL PASSED")
