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

# Retry a few times: rapid back-to-back ls.exe spawns occasionally hit the
# documented ambient Defender/spawn flake (a launch that yields empty output,
# not a wrong result). A real failure misses EXPECT on every attempt; a flake
# clears on retry. This keeps single-run output-contains checks reliable when
# many REPL tests run in sequence.
set(found OFF)
foreach(attempt RANGE 1 3)
    execute_process(
        COMMAND "${LS_EXE}" repl
        INPUT_FILE "${SCRIPT}"
        OUTPUT_VARIABLE out
        ERROR_VARIABLE  err
        RESULT_VARIABLE rc
    )
    message(STATUS "attempt ${attempt}: ls repl exit code ${rc}")
    string(FIND "${out}" "${EXPECT}" idx)
    if(NOT idx EQUAL -1)
        set(found ON)
        break()
    endif()
    message(STATUS "attempt ${attempt}: did not find '${EXPECT}' in:\n${out}")
endforeach()

if(NOT found)
    message(FATAL_ERROR "expected output to contain '${EXPECT}' (3 attempts):\n${out}\nstderr:\n${err}")
endif()

message(STATUS "test_repl_pipe: found expected '${EXPECT}' — PASS")
