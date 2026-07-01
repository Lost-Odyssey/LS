# test_e3_glue.cmake — Phase E.3 generic JIT+AOT runner
# Runs $SAMPLE through both `ls run` and `ls compile && exec`, asserts
# "ALL PASS" appears in stdout.
# Variables injected by CMakeLists.txt:
#   LS_EXE     — path to ls.exe
#   SAMPLE     — path to .lls test file
#   WORK_DIR   — build directory (for AOT output)
#   TEST_NAME  — test name (used for AOT binary filename)

# Point LS_HOME at the project root so stdlib imports (e.g. `import io`)
# resolve to std/io.lls under the source tree (ls.exe lives in build/Release/
# which has no std/ sub-directory of its own).
if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_ls_stdlib_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_ls_stdlib_root}")

# ---- JIT path ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${jit_out}")
endif()
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
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
)
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "reported a FAIL:\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "ALL PASS")
    message(FATAL_ERROR
        "${TEST_NAME} AOT run FAILED (exit ${aot_rc})\n"
        "stdout:\n${aot_out}\n"
        "stderr:\n${aot_err}")
endif()
message(STATUS "${TEST_NAME} AOT: OK")
