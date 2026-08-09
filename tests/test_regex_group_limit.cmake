# test_regex_group_limit.cmake — the capture-group ceiling is a diagnostic
#
# Capture offsets live in `int saved[MAX_GROUPS * 2]` and group N writes slots
# N*2 / N*2+1, so group 17 already runs off the end. Nothing enforced it: not
# the allocator, not re->n_groups, not the emitted OP_SAVE slot, not the two
# stores that commit it. 17 groups silently corrupted the neighbouring
# ReThread's pc; 18+ killed the process with rc=127 and lost all output.
#
# Rejecting at compile time is the fix (the SAVE stores are the engine's
# hottest writes, and clamping would return another group's offsets). This
# pins both directions: 1..16 groups still work and read back, 17..20 and 17
# named groups give a clean Err, and (?:...) does not count toward the limit.
#
# @subsystem stdlib/text
# @guards capture-group ceiling rejected at compile time, non-capturing exempt
# @sources runtime/ls_regex.c:re_alloc_group lib/std/text/regex.lls

if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_root}")

set(SAMPLE "${_root}/tests/samples/regex_group_limit.lls")

# ---- JIT path ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT "${jit_out}" MATCHES "ALL PASS" OR "${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_regex_group_limit JIT FAILED (exit ${jit_rc})\n"
        "stdout:\n${jit_out}\n"
        "stderr:\n${jit_err}")
endif()
message(STATUS "test_regex_group_limit JIT: OK")

# ---- memcheck path ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT "${mc_out}${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR
        "test_regex_group_limit memcheck FAILED (exit ${mc_rc})\n"
        "stdout:\n${mc_out}\n"
        "stderr:\n${mc_err}")
endif()
message(STATUS "test_regex_group_limit memcheck: OK")

# ---- AOT path ----
set(EXE "${WORK_DIR}/regex_group_limit_aot.exe")
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${EXE}"
    OUTPUT_VARIABLE c_out
    ERROR_VARIABLE  c_err
    RESULT_VARIABLE c_rc
)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR
        "test_regex_group_limit AOT compile FAILED (exit ${c_rc})\n"
        "stdout:\n${c_out}\nstderr:\n${c_err}")
endif()
execute_process(
    COMMAND "${EXE}"
    OUTPUT_VARIABLE a_out
    ERROR_VARIABLE  a_err
    RESULT_VARIABLE a_rc
)
if(NOT "${a_out}" MATCHES "ALL PASS" OR "${a_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_regex_group_limit AOT FAILED (exit ${a_rc})\n"
        "stdout:\n${a_out}\nstderr:\n${a_err}")
endif()
message(STATUS "test_regex_group_limit AOT: OK")

message(STATUS "test_regex_group_limit: ALL PASSED")
