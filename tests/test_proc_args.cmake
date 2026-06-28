# test_proc_args.cmake — bug #22: proc.args() must work in AOT, not just JIT.
# Before the fix, the AOT entry main() ignored argc/argv and never called
# __ls_set_args, so proc.args() returned empty. JIT already worked.
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/proc_args_test.ls")
set(_expected "argc_extra=3" "arg\\[0\\]=foo" "arg\\[1\\]=bar" "arg\\[2\\]=baz")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}" foo bar baz
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "proc_args JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "proc_args JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "proc_args JIT: OK")

# ---- AOT (the point of bug #22) ----
set(aot_bin "${WORK_DIR}/proc_args_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "proc_args AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}" foo bar baz
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "proc_args AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "proc_args AOT missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "proc_args AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}" foo bar
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "proc_args memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "proc_args --memcheck FAILED (leak)\nstderr:\n${mc_err}")
endif()
message(STATUS "proc_args memcheck: OK clean")

message(STATUS "test_proc_args: ALL PASSED")
