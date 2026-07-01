# test_phase_c_closure.cmake — Phase C closure POD captures + heap env + RAII.
#
# Verifies the full make_adder pattern (closure outlives the function that
# built it) plus mixed-POD captures and shadowing semantics. Runs JIT, AOT,
# and memcheck (must be 0 leaks since the env is freed at scope cleanup).

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/closure_phase_c_test.lls")

set(_expected "107" "8" "13" "45" "-15.000000" "5" "12")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "Phase C JIT FAILED: missing line '${_line}' in output\n"
            "stdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "phase_c JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/closure_phase_c_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "Phase C AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
)
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "Phase C AOT FAILED: missing line '${_line}' in output\n"
            "stdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "phase_c AOT: OK")

# ---- Memcheck (heap env must be cleaned up via RAII) ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
)
if(NOT "${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\)")
    message(FATAL_ERROR
        "Phase C memcheck FAILED: expected 0 leaks\n"
        "stderr:\n${mc_err}")
endif()
message(STATUS "phase_c memcheck: 0 leaks OK")

message(STATUS "Phase C closure captures + RAII: ALL OK")
