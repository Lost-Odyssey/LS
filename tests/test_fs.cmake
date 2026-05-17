# test_fs.cmake — Verifies std.fs batch-1 (directory/path operations).
#
# Required variables (passed by add_test):
#   LS_EXE   — path to ls.exe
#   SAMPLE   — absolute path to fs_test.ls
#   WORK_DIR — scratch directory for AOT output

if(NOT LS_EXE OR NOT SAMPLE OR NOT WORK_DIR)
    message(FATAL_ERROR "test_fs.cmake requires LS_EXE, SAMPLE, WORK_DIR")
endif()

get_filename_component(_ls_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_root}")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_out MATCHES "ALL PASS")
    message(FATAL_ERROR
        "fs JIT: program did not print ALL PASS (rc=${jit_rc})\n"
        "stdout: ${jit_out}\nstderr: ${jit_err}")
endif()
message(STATUS "fs JIT: OK")

# ---- AOT ----
if(WIN32 OR CMAKE_HOST_WIN32)
    set(aot_bin "${WORK_DIR}/fs_test_aot.exe")
else()
    set(aot_bin "${WORK_DIR}/fs_test_aot")
endif()
file(REMOVE "${aot_bin}")

execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    OUTPUT_VARIABLE aot_compile_out
    ERROR_VARIABLE  aot_compile_err
    RESULT_VARIABLE aot_compile_rc
)
if(NOT aot_compile_rc EQUAL 0)
    message(FATAL_ERROR
        "fs AOT compile failed (rc=${aot_compile_rc})\n"
        "stderr: ${aot_compile_err}")
endif()

execute_process(
    COMMAND "${aot_bin}"
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
)
if(NOT aot_out MATCHES "ALL PASS")
    message(FATAL_ERROR
        "fs AOT: program did not print ALL PASS (rc=${aot_rc})\n"
        "stdout: ${aot_out}\nstderr: ${aot_err}")
endif()

file(REMOVE "${aot_bin}")
message(STATUS "fs AOT: OK")
