# test_profile.cmake — Verifies --profile instrumentation works for JIT + AOT.
#
# Required variables (passed by add_test):
#   LS_EXE    — path to ls.exe
#   SAMPLE    — absolute path to the .ls sample
#   WORK_DIR  — directory for AOT output

if(NOT LS_EXE OR NOT SAMPLE OR NOT WORK_DIR)
    message(FATAL_ERROR "test_profile.cmake requires LS_EXE, SAMPLE, WORK_DIR")
endif()

# Set LS_HOME so stdlib imports work.
get_filename_component(_ls_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_root}")

# ---- JIT --profile ----
execute_process(
    COMMAND "${LS_EXE}" run --profile "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(jit_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${jit_out}")
endif()
if(NOT jit_out MATCHES "ALL PASS")
    message(FATAL_ERROR
        "profile JIT: program did not print ALL PASS (rc=${jit_rc})\n"
        "stdout: ${jit_out}\nsrderr: ${jit_err}")
endif()
if(NOT jit_err MATCHES "LS Profile Report")
    message(FATAL_ERROR
        "profile JIT: no '=== LS Profile Report ===' in stderr\n"
        "stderr: ${jit_err}")
endif()
if(NOT jit_err MATCHES "factorial")
    message(FATAL_ERROR
        "profile JIT: expected 'factorial' in report\n"
        "stderr: ${jit_err}")
endif()
message(STATUS "profile JIT: OK")

# ---- AOT --profile ----
if(WIN32 OR CMAKE_HOST_WIN32)
    set(aot_bin "${WORK_DIR}/profile_test_aot.exe")
else()
    set(aot_bin "${WORK_DIR}/profile_test_aot")
endif()
file(REMOVE "${aot_bin}")

execute_process(
    COMMAND "${LS_EXE}" compile --profile "${SAMPLE}" -o "${aot_bin}"
    OUTPUT_VARIABLE aot_compile_out
    ERROR_VARIABLE  aot_compile_err
    RESULT_VARIABLE aot_compile_rc
)
if(NOT aot_compile_rc EQUAL 0)
    message(FATAL_ERROR
        "profile AOT compile failed (rc=${aot_compile_rc})\n"
        "stderr: ${aot_compile_err}")
endif()
if(NOT EXISTS "${aot_bin}")
    message(FATAL_ERROR "AOT compile reported success but ${aot_bin} not produced")
endif()

execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
)
if(aot_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${aot_out}")
endif()
if(NOT aot_out MATCHES "ALL PASS")
    message(FATAL_ERROR
        "profile AOT: program did not print ALL PASS (rc=${aot_rc})\n"
        "stdout: ${aot_out}\nstderr: ${aot_err}")
endif()
if(NOT aot_err MATCHES "LS Profile Report")
    message(FATAL_ERROR
        "profile AOT: no '=== LS Profile Report ===' in stderr\n"
        "stderr: ${aot_err}")
endif()
if(NOT aot_err MATCHES "factorial")
    message(FATAL_ERROR
        "profile AOT: expected 'factorial' in report\n"
        "stderr: ${aot_err}")
endif()

file(REMOVE "${aot_bin}")
message(STATUS "profile AOT: OK")
