# test_repl_pipe.cmake — drive `ls repl` with piped stdin and assert output.
#
# Verifies the REPL import-persistence fix: `import math` followed by a call to
# math.sqrt must compute, proving the import took effect for a later statement.
# Piped stdin is non-TTY → the editor uses its fgets fallback (no raw mode).
#
# Required -D vars: LS_EXE, SCRIPT, EXPECT

if(NOT LS_EXE OR NOT SCRIPT OR NOT DEFINED EXPECT)
    message(FATAL_ERROR "usage: -DLS_EXE= -DSCRIPT= -DEXPECT=")
endif()

execute_process(
    COMMAND "${LS_EXE}" repl
    INPUT_FILE "${SCRIPT}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE  err
    RESULT_VARIABLE rc
)

message(STATUS "ls repl exit code: ${rc}")
message(STATUS "stdout:\n${out}")
if(err)
    message(STATUS "stderr:\n${err}")
endif()

string(FIND "${out}" "${EXPECT}" idx)
if(idx EQUAL -1)
    message(FATAL_ERROR "expected output to contain '${EXPECT}', but it did not")
endif()

message(STATUS "test_repl_pipe: found expected '${EXPECT}' — PASS")
