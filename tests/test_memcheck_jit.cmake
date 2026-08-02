# test_memcheck_jit.cmake — JIT memcheck 通用驱动脚本
#
# 用 `ls run --memcheck $SAMPLE` 运行测试，断言 stderr 包含 "OK clean"。
#
# ---- why 26 tests share this one driver -----------------------------------
#
# The whole oracle is one line: the program must run AND the allocator report
# must say 0 leak / 0 double-free / 0 invalid free. There is nothing per-test to
# configure, so the corpus IS the test — which is why this driver is the most
# reused one in the suite.
#
# That also makes its blind spot worth stating plainly: a test on this driver
# checks MEMORY behaviour only. It never looks at what the program printed, so a
# change that keeps every allocation balanced while computing the wrong answer
# passes here. Tests whose corpora are self-verifying (they print a PASS marker
# and the driver greps for it) use test_plotfmt.cmake instead. When adding a
# corpus, prefer the self-verifying form; reach for this driver only when the
# property under test really is allocation balance.
#
# The bulk of the users are the M-series memory-model overhaul corpora
# (docs/memory_model_overhaul.md). That project exists because 28 of the first
# 36 recorded bugfixes were memory-safety bugs, all traceable to three
# structural defects: two parallel temp-tracking tables that could register the
# same string twice (double free), an overloaded `cap == 0` that could not tell
# a static literal from a borrowed parameter, and ownership-transfer logic
# hand-written at 5+ sites per container operation, where missing one site was
# a bug. The corpora below are the permanent guard on those fixes.
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
