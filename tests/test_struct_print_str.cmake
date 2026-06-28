# test_struct_print_str.cmake — D-1 (docs/bugs_deferred_p5_4.md §D-1):
# struct auto-print renders Str fields as quoted text. Verifies JIT + AOT
# correctness (exact format), then memcheck 0/0/0.
#
# Required: LS_EXE, SAMPLE, WORK_DIR
cmake_minimum_required(VERSION 3.20)

function(_check_out where out)
    if("${out}" MATCHES "FAIL")
        message(FATAL_ERROR "struct_print_str ${where} reported FAIL:\n${out}")
    endif()
    if(NOT "${out}" MATCHES "DCOPY PASS")
        message(FATAL_ERROR "struct_print_str ${where} missing 'DCOPY PASS':\n${out}")
    endif()
    # The whole point of D-1: Str field printed as quoted text, NOT raw layout.
    if("${out}" MATCHES "Str{data=")
        message(FATAL_ERROR "struct_print_str ${where} still dumps Str{data=...}:\n${out}")
    endif()
    if(NOT "${out}" MATCHES "Person{age=10, name=\"ALICE\", inner=Inner{tag=\"x\"}}")
        message(FATAL_ERROR "struct_print_str ${where} wrong format:\n${out}")
    endif()
    if(NOT "${out}" MATCHES "Person{age=0, name=\"\", inner=Inner{tag=\"\"}}")
        message(FATAL_ERROR "struct_print_str ${where} empty-Str format wrong:\n${out}")
    endif()
endfunction()

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "struct_print_str JIT failed (rc=${jit_rc})\n${jit_err}")
endif()
_check_out("JIT" "${jit_out}")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/struct_print_str_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "struct_print_str AOT compile failed:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
file(REMOVE "${aot_bin}")
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "struct_print_str AOT run failed (rc=${aot_run_rc})\n${aot_out}")
endif()
_check_out("AOT" "${aot_out}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "struct_print_str memcheck failed (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT mc_err MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "struct_print_str memcheck SUMMARY mismatch\n${mc_err}")
endif()
message(STATUS "test_struct_print_str: JIT + AOT format + memcheck PASS")
