# test_path.cmake — Verifies std.path batch-2 (pure LS path utilities).

if(NOT LS_EXE OR NOT SAMPLE OR NOT WORK_DIR)
    message(FATAL_ERROR "test_path.cmake requires LS_EXE, SAMPLE, WORK_DIR")
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
if(jit_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${jit_out}")
endif()
if(NOT jit_out MATCHES "ALL PASS")
    message(FATAL_ERROR
        "path JIT: program did not print ALL PASS (rc=${jit_rc})\n"
        "stdout: ${jit_out}\nstderr: ${jit_err}")
endif()
message(STATUS "path JIT: OK")

# ---- AOT ----
if(WIN32 OR CMAKE_HOST_WIN32)
    set(aot_bin "${WORK_DIR}/path_test_aot.exe")
else()
    set(aot_bin "${WORK_DIR}/path_test_aot")
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
        "path AOT compile failed (rc=${aot_compile_rc})\n"
        "stderr: ${aot_compile_err}")
endif()

execute_process(
    COMMAND "${aot_bin}"
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
)
if(aot_out MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${aot_out}")
endif()
if(NOT aot_out MATCHES "ALL PASS")
    message(FATAL_ERROR
        "path AOT: program did not print ALL PASS (rc=${aot_rc})\n"
        "stdout: ${aot_out}\nstderr: ${aot_err}")
endif()

file(REMOVE "${aot_bin}")
message(STATUS "path AOT: OK")
