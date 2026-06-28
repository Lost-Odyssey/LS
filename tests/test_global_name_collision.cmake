# test_global_name_collision.cmake — regression for the compiler-internal-global
# vs user-global NAME collision (a real heap-corruption / "invalid free" soundness
# bug). A user global named `fmt` / `Strlit` / `rawstr` squatted the same LLVM name
# as an internal private constant; the name-based global init-store + cleanup then
# wrote a Str into / destroyed a .rodata format string. Fix: internal hints moved
# to the dotted ".ls.*" namespace (disjoint from user identifiers).
#
# Verifies JIT + AOT exact output, then memcheck 0/0/0.
# Required: LS_EXE, SAMPLE, WORK_DIR
cmake_minimum_required(VERSION 3.20)

function(_check_out where out)
    if("${out}" MATCHES "FAIL")
        message(FATAL_ERROR "global_name_collision ${where} reported FAIL:\n${out}")
    endif()
    # Exact rendered lines: F / S / R / OK in order.
    if(NOT "${out}" MATCHES "F\nS\nR\nOK")
        message(FATAL_ERROR "global_name_collision ${where} wrong output:\n${out}")
    endif()
endfunction()

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "global_name_collision JIT failed (rc=${jit_rc})\n${jit_err}")
endif()
_check_out("JIT" "${jit_out}")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/global_name_collision_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "global_name_collision AOT compile failed:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
file(REMOVE "${aot_bin}")
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "global_name_collision AOT run failed (rc=${aot_run_rc})\n${aot_out}")
endif()
_check_out("AOT" "${aot_out}")

# ---- memcheck (0 leak / 0 double-free / 0 invalid free) ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "global_name_collision memcheck failed (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT mc_err MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "global_name_collision memcheck SUMMARY mismatch\n${mc_err}")
endif()
message(STATUS "test_global_name_collision: JIT + AOT + memcheck PASS")
