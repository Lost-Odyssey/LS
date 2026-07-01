# test_phase_f1_closure.cmake — Phase F.1: [move v] capture spec.
#
# Verifies that vec/map captured with [move] transfer ownership into the
# closure env (by-move), fixing the dangling-pointer factory pattern.
# Also verifies 0 leak / 0 double-free under memcheck.
#
# Expected output (in order): 6, 99, 4, "hello world"

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/closure_f1_test.lls")

set(_expected "6" "99" "4" "hello world")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
)
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "Phase F.1 JIT FAILED: missing line '${_line}'\n"
            "stdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "phase_f1 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/closure_f1_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "Phase F.1 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
)
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "Phase F.1 AOT FAILED: missing line '${_line}'\n"
            "stdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "phase_f1 AOT: OK")

# ---- Memcheck (JIT) ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    ERROR_VARIABLE  mc_err
)
if(NOT "${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\)")
    message(FATAL_ERROR "Phase F.1 memcheck FAILED: leaks\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "0 double-free")
    message(FATAL_ERROR "Phase F.1 memcheck FAILED: double-free\n${mc_err}")
endif()
message(STATUS "phase_f1 memcheck: 0 leaks / 0 double-free OK")

# ---- Memcheck (AOT) ----
set(aot_mc_bin "${WORK_DIR}/closure_f1_mc_aot")
if(WIN32)
    set(aot_mc_bin "${aot_mc_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile --memcheck "${SAMPLE}" -o "${aot_mc_bin}"
    RESULT_VARIABLE aot_mc_rc
    ERROR_VARIABLE  aot_mc_err
)
if(NOT aot_mc_rc EQUAL 0)
    message(FATAL_ERROR "Phase F.1 AOT+memcheck compile FAILED:\n${aot_mc_err}")
endif()
execute_process(
    COMMAND "${aot_mc_bin}"
    OUTPUT_VARIABLE aot_mc_out
    ERROR_VARIABLE  aot_mc_stderr
)
if(NOT "${aot_mc_stderr}" MATCHES "SUMMARY: 0 leak\\(s\\)")
    message(FATAL_ERROR "Phase F.1 AOT memcheck FAILED: leaks\n${aot_mc_stderr}")
endif()
if(NOT "${aot_mc_stderr}" MATCHES "0 double-free")
    message(FATAL_ERROR "Phase F.1 AOT memcheck FAILED: double-free\n${aot_mc_stderr}")
endif()
message(STATUS "phase_f1 AOT memcheck: 0 leaks / 0 double-free OK")

message(STATUS "Phase F.1 ([move v] vec/map by-move factory pattern): ALL OK")
