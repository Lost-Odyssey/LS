# test_phase_e2_e4_closure.cmake — Phase E.2 + E.4.
#
# E.2: closure parameters of type vec(int) / map(K,V) are treated as borrowed;
#      caller's heap is not freed by the closure body (no double-free).
# E.4: array(POD, N) can be captured by value; outer remains live.
#
# Expected output (in order): 15, 15, 2, 3, 10, 30, 50, 6, 5, 20

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/closure_e2_e4_test.ls")

set(_expected "15" "2" "3" "10" "30" "50" "6" "5" "20")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
)
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "Phase E.2/E.4 JIT FAILED: missing line '${_line}'\n"
            "stdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "phase_e2_e4 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/closure_e2_e4_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "Phase E.2/E.4 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
)
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "Phase E.2/E.4 AOT FAILED: missing line '${_line}'\n"
            "stdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "phase_e2_e4 AOT: OK")

# ---- Memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    ERROR_VARIABLE  mc_err
)
if(NOT "${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\)")
    message(FATAL_ERROR "Phase E.2/E.4 memcheck FAILED: leaks\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "0 double-free")
    message(FATAL_ERROR "Phase E.2/E.4 memcheck FAILED: double-free\n${mc_err}")
endif()
message(STATUS "phase_e2_e4 memcheck: 0 leaks / 0 double-free OK")

message(STATUS "Phase E.2 (closure param borrowed) + E.4 (array capture): ALL OK")
