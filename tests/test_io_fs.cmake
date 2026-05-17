# test_io_fs.cmake — Tests for io.read_line() and fs.list_dir()
# Variables injected by CMakeLists.txt:
#   LS_EXE, SAMPLE, STDIN_FILE, WORK_DIR, TEST_NAME

# Point LS_HOME at source root so std/*.ls files are found.
if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_root}")

# ---- JIT path ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    INPUT_FILE  "${STDIN_FILE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT "${jit_out}" MATCHES "ALL PASS")
    message(FATAL_ERROR
        "${TEST_NAME} JIT FAILED (exit ${jit_rc})\n"
        "stdout:\n${jit_out}\n"
        "stderr:\n${jit_err}")
endif()
message(STATUS "${TEST_NAME} JIT: OK")

# ---- AOT path ----
set(aot_bin "${WORK_DIR}/${TEST_NAME}_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()

execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    OUTPUT_VARIABLE aot_compile_out
    ERROR_VARIABLE  aot_compile_err
    RESULT_VARIABLE aot_compile_rc
)
if(NOT aot_compile_rc EQUAL 0)
    message(FATAL_ERROR
        "${TEST_NAME} AOT compile FAILED (exit ${aot_compile_rc})\n"
        "stderr:\n${aot_compile_err}")
endif()

execute_process(
    COMMAND "${aot_bin}"
    INPUT_FILE  "${STDIN_FILE}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
    WORKING_DIRECTORY "${_root}"
)
if(NOT "${aot_out}" MATCHES "ALL PASS")
    message(FATAL_ERROR
        "${TEST_NAME} AOT run FAILED (exit ${aot_rc})\n"
        "stdout:\n${aot_out}\n"
        "stderr:\n${aot_err}")
endif()
message(STATUS "${TEST_NAME} AOT: OK")
