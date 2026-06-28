# test_extern_struct.cmake — Phase E.1 end-to-end test
# Runs extern_struct_test.ls via JIT and AOT, asserts "ALL PASS" in output.
# Variables injected by CMakeLists.txt:
#   LS_EXE    — path to ls.exe
#   SAMPLE    — path to extern_struct_test.ls
#   WORK_DIR  — build directory (for AOT output)

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
        "test_extern_struct JIT FAILED (exit ${jit_rc})\n"
        "stdout:\n${jit_out}\n"
        "stderr:\n${jit_err}")
endif()
message(STATUS "test_extern_struct JIT: OK")

# ---- AOT path ----
set(aot_bin "${WORK_DIR}/extern_struct_test_aot")
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
        "test_extern_struct AOT compile FAILED (exit ${aot_compile_rc})\n"
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
        "test_extern_struct AOT run FAILED (exit ${aot_rc})\n"
        "stdout:\n${aot_out}\n"
        "stderr:\n${aot_err}")
endif()
message(STATUS "test_extern_struct AOT: OK")
