# test_regex_dfa.cmake — D1 lazy DFA (match-only fast path)
#
# Pins: eligible patterns actually route through the DFA (proven via the
# debug counter, not just is_dfa_eligible()); ineligible patterns (for five
# different reasons) do not; a pattern too large for the state-budget cap
# falls back correctly; group(i>0) after a DFA-routed matches?() still
# works (lazy capture fallback); (a+)+$ stays linear-time.
#
# @subsystem stdlib/text
# @guards std.regex lazy DFA match-only fast path (__ls_regex_exec_dfa)
# @sources lib/std/text/regex.lls runtime/ls_regex.c

if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_root}")

set(SAMPLE "${_root}/tests/samples/regex_dfa.lls")

# ---- JIT path ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT "${jit_out}" MATCHES "ALL PASS" OR "${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_regex_dfa JIT FAILED (exit ${jit_rc})\n"
        "stdout:\n${jit_out}\n"
        "stderr:\n${jit_err}")
endif()
message(STATUS "test_regex_dfa JIT: OK")

# ---- memcheck path ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT "${mc_out}${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR
        "test_regex_dfa memcheck FAILED (exit ${mc_rc})\n"
        "stdout:\n${mc_out}\n"
        "stderr:\n${mc_err}")
endif()
message(STATUS "test_regex_dfa memcheck: OK")

# ---- AOT path ----
set(aot_bin "${WORK_DIR}/test_regex_dfa_aot")
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
        "test_regex_dfa AOT compile FAILED (exit ${aot_compile_rc})\n"
        "stderr:\n${aot_compile_err}")
endif()

execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    ERROR_VARIABLE  aot_err
    RESULT_VARIABLE aot_rc
    WORKING_DIRECTORY "${_root}"
)
if(NOT "${aot_out}" MATCHES "ALL PASS" OR "${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_regex_dfa AOT FAILED (exit ${aot_rc})\n"
        "stdout:\n${aot_out}\n"
        "stderr:\n${aot_err}")
endif()
message(STATUS "test_regex_dfa AOT: OK")
