# test_phase_c5_closure.cmake — Phase C.5: by-move string captures + per-
# closure env_drop + Block-param borrowing.

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/closure_phase_c5_test.lls")

set(_expected "INFO: alice" "TAG: bob" "7" "STATIC: carol" "X: y" "X: z")

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
            "Phase C.5 JIT FAILED: missing line '${_line}' in output\n"
            "stdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "phase_c5 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/closure_phase_c5_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "Phase C.5 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
)
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "Phase C.5 AOT FAILED: missing line '${_line}' in output\n"
            "stdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "phase_c5 AOT: OK")

# ---- Memcheck (string captures must be freed via env_drop) ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
)
if(NOT "${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\)")
    message(FATAL_ERROR
        "Phase C.5 memcheck FAILED: expected 0 leaks\n"
        "stderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "0 double-free")
    message(FATAL_ERROR
        "Phase C.5 memcheck FAILED: double-free reported\n"
        "stderr:\n${mc_err}")
endif()
message(STATUS "phase_c5 memcheck: 0 leaks / 0 double-free OK")

message(STATUS "Phase C.5 string captures: ALL OK")
