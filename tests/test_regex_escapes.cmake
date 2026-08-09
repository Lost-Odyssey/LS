# test_regex_escapes.cmake -- escapes that used to mean something else, silently
#
# parse_escape's `default` arm turns an unrecognised escape into the literal
# character. Correct for punctuation (`\.` must be a dot) but it also swallowed
# two escapes with a specific meaning everywhere else: \1-\9 (backreference)
# became the literal digit, so `(ab)\1` matched "ab1" rather than "abab"; and
# \xHH became the literal chars "xHH", so `^\x41$` matched "x41" rather than "A".
# Both rc=0, no diagnostic.
#
# Backreferences cannot work in a Pike-VM that never backtracks, so they are
# rejected; \xHH is cheap and expected, so it is implemented. This pins both,
# plus the punctuation pass-through and every recognised class/anchor escape, so
# the fix cannot have narrowed those by accident.
#
# @subsystem stdlib/text
# @guards backreference rejection, \xHH, punctuation pass-through unchanged
# @sources runtime/ls_regex.c:parse_escape

if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_root}")

set(SAMPLE "${_root}/tests/samples/regex_escapes.lls")

# ---- JIT path ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT "${jit_out}" MATCHES "ALL PASS" OR "${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_regex_escapes JIT FAILED (exit ${jit_rc})\n"
        "stdout:\n${jit_out}\n"
        "stderr:\n${jit_err}")
endif()
message(STATUS "test_regex_escapes JIT: OK")

# ---- memcheck path ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT "${mc_out}${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR
        "test_regex_escapes memcheck FAILED (exit ${mc_rc})\n"
        "stdout:\n${mc_out}\n"
        "stderr:\n${mc_err}")
endif()
message(STATUS "test_regex_escapes memcheck: OK")

# ---- AOT path ----
set(EXE "${WORK_DIR}/regex_escapes_aot.exe")
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${EXE}"
    OUTPUT_VARIABLE c_out
    ERROR_VARIABLE  c_err
    RESULT_VARIABLE c_rc
)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR
        "test_regex_escapes AOT compile FAILED (exit ${c_rc})\n"
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
        "test_regex_escapes AOT FAILED (exit ${a_rc})\n"
        "stdout:\n${a_out}\nstderr:\n${a_err}")
endif()
message(STATUS "test_regex_escapes AOT: OK")

message(STATUS "test_regex_escapes: ALL PASSED")
