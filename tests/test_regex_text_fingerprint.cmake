# test_regex_text_fingerprint.cmake — misuse detection for "pass the same text"
#
# The offsets a match produces live in the handle, so every accessor that turns
# them back into bytes must be handed the text again -- and nothing ties that
# argument to the text the match ran against. The old net was only
# `s + l > text.len()`, so a wrong text of the SAME length returned a
# plausible-looking wrong substring with rc=0:
#     matches?("abc12"); group("99xyz", 1)  ->  Some("99x")
#
# This pins the (pointer, length) fingerprint that turns both realistic misuses
# -- wrong text, and a Caps left stale by re-matching the same Regex -- into
# None, while keeping correct usage and the text-free group_span() unaffected.
#
# A detector, not a proof: a freed text whose allocation is reused at the same
# address and length is out of reach without lifetimes, and is not attempted.
#
# @subsystem stdlib/text
# @guards wrong-text and stale-Caps detection; group_span deliberately exempt
# @sources lib/std/text/regex.lls runtime/ls_regex.c

if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_root}")

set(SAMPLE "${_root}/tests/samples/regex_text_fingerprint.lls")

# ---- JIT path ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT "${jit_out}" MATCHES "ALL PASS" OR "${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_regex_text_fingerprint JIT FAILED (exit ${jit_rc})\n"
        "stdout:\n${jit_out}\n"
        "stderr:\n${jit_err}")
endif()
message(STATUS "test_regex_text_fingerprint JIT: OK")

# ---- memcheck path ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT "${mc_out}${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR
        "test_regex_text_fingerprint memcheck FAILED (exit ${mc_rc})\n"
        "stdout:\n${mc_out}\n"
        "stderr:\n${mc_err}")
endif()
message(STATUS "test_regex_text_fingerprint memcheck: OK")

# ---- AOT path ----
set(EXE "${WORK_DIR}/regex_text_fingerprint_aot.exe")
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${EXE}"
    OUTPUT_VARIABLE c_out
    ERROR_VARIABLE  c_err
    RESULT_VARIABLE c_rc
)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR
        "test_regex_text_fingerprint AOT compile FAILED (exit ${c_rc})\n"
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
        "test_regex_text_fingerprint AOT FAILED (exit ${a_rc})\n"
        "stdout:\n${a_out}\nstderr:\n${a_err}")
endif()
message(STATUS "test_regex_text_fingerprint AOT: OK")

message(STATUS "test_regex_text_fingerprint: ALL PASSED")
