# test_memcheck_jit.cmake — JIT memcheck 通用驱动脚本
#
# 用 `ls run --memcheck $SAMPLE` 运行测试，断言 stderr 包含 "OK clean"。
#
# Required cache variables (passed by add_test):
#   LS_EXE    — path to the ls.exe / ls binary
#   SAMPLE    — absolute path to the .lls sample to run
#   WORK_DIR  — build directory (unused here but kept for consistency)
#   TEST_NAME — test name (for error messages)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_memcheck_jit.cmake requires LS_EXE and SAMPLE")
endif()

if(NOT TEST_NAME)
    set(TEST_NAME "memcheck_jit")
endif()

# Point LS_HOME at the project source so stdlib imports resolve.
if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_ls_stdlib_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_ls_stdlib_root}")

execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    RESULT_VARIABLE run_rc
    OUTPUT_VARIABLE run_out
    ERROR_VARIABLE  run_err
)

if(NOT run_rc EQUAL 0)
    message(FATAL_ERROR
        "${TEST_NAME} JIT --memcheck failed (rc=${run_rc})\n"
        "stdout: ${run_out}\n"
        "stderr: ${run_err}\n")
endif()

if(NOT run_err MATCHES "\\[memcheck\\] OK clean")
    message(FATAL_ERROR
        "${TEST_NAME} JIT --memcheck did not report 'OK clean'\n"
        "stdout: ${run_out}\n"
        "stderr: ${run_err}\n")
endif()

if(NOT run_err MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR
        "${TEST_NAME} JIT memcheck SUMMARY mismatch\n"
        "stderr: ${run_err}\n")
endif()

message(STATUS "${TEST_NAME} JIT memcheck PASS: ${SAMPLE}")
