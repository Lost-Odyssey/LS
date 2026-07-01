# test_phase_b_closure.cmake — Phase B closure codegen end-to-end.
#
# Runs tests/samples/closure_phase_b_test.lls through both JIT and AOT and
# asserts every expected line is produced. Each line corresponds to one of
# the 6 shapes the test exercises (prefix / trailing / multi-arg / no-arg /
# bool-return / multi-stmt body).

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/closure_phase_b_test.lls")

# Expected output lines (in order). Keep in sync with the test file's prints.
set(_expected "11" "40" "12" "42" "true" "107")

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
            "Phase B JIT FAILED: missing line '${_line}' in output\n"
            "stdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "phase_b JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/closure_phase_b_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "Phase B AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    RESULT_VARIABLE aot_run_rc
)
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "Phase B AOT FAILED: missing line '${_line}' in output\n"
            "stdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "phase_b AOT: OK")

message(STATUS "Phase B closure codegen: ALL OK")
