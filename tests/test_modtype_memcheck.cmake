# test_modtype_memcheck.cmake — B-6: has_drop same-named struct/enum across two
# modules, stressed through methods, vecs (+ Phase H deep copy), enum payloads and
# cross-module returns. JIT + AOT + memcheck (0 leak / 0 double-free).
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/modtype_memcheck/main.ls")
set(_expected "na=A nb=B" "va0=A1 va1=A2 vb0=B1" "ba=AX bb=BX" "MODTYPE_MEMCHECK PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "modtype_memcheck JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "modtype_memcheck JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "modtype_memcheck JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/modtype_memcheck_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "modtype_memcheck AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "modtype_memcheck AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "modtype_memcheck AOT missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "modtype_memcheck AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck (the point of B-6) ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "modtype_memcheck memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "modtype_memcheck --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "modtype_memcheck memcheck: OK clean")

message(STATUS "test_modtype_memcheck: ALL PASSED")
