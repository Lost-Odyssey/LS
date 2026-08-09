# test_rawstring.cmake -- r"..." / r'...' raw string literals
#
# Normal string literals process escapes and the scanner rejects unknown ones,
# so a regex had to be written with every backslash doubled ("\d+" for \d+).
# Noisy, and a hazard in one direction: "\\d" quietly means "backslash then d".
#
# Raw strings process nothing. Two quote flavours because a raw string cannot
# contain its own delimiter; together they cover everything except content with
# BOTH quote kinds, which falls back to an ordinary escaped string -- pinned in
# the sample so that limit stays documented rather than surprising.
#
# Also pins that `r` is still an ordinary identifier (it is only special
# immediately before a quote) and that both unterminated forms name which quote
# they were opened with.
#
# @subsystem frontend/scanner
# @guards raw string bytes verbatim, two quote flavours, r still an identifier
# @sources scanner.c:scan_raw_string parser_expr.c:prefix_rawstring_lit

if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_root}")

set(SAMPLE "${_root}/tests/samples/rawstring_test.lls")

# ---- JIT path ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT "${jit_out}" MATCHES "ALL PASS" OR "${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_rawstring_test JIT FAILED (exit ${jit_rc})\n"
        "stdout:\n${jit_out}\n"
        "stderr:\n${jit_err}")
endif()
message(STATUS "test_rawstring_test JIT: OK")

# ---- memcheck path ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT "${mc_out}${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR
        "test_rawstring_test memcheck FAILED (exit ${mc_rc})\n"
        "stdout:\n${mc_out}\n"
        "stderr:\n${mc_err}")
endif()
message(STATUS "test_rawstring_test memcheck: OK")

# ---- AOT path ----
set(EXE "${WORK_DIR}/rawstring_test_aot.exe")
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${EXE}"
    OUTPUT_VARIABLE c_out
    ERROR_VARIABLE  c_err
    RESULT_VARIABLE c_rc
)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR
        "test_rawstring_test AOT compile FAILED (exit ${c_rc})\n"
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
        "test_rawstring_test AOT FAILED (exit ${a_rc})\n"
        "stdout:\n${a_out}\nstderr:\n${a_err}")
endif()
message(STATUS "test_rawstring_test AOT: OK")

message(STATUS "test_rawstring_test: ALL PASSED")
