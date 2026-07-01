# test_fmt.cmake — `ls fmt` formatter regression.
#   Invariants: (1) --check flags a messy file (exit 1); (2) formatting in place
#   preserves behavior (parse-equivalence: formatted file runs identically);
#   (3) idempotence (--check on a formatted file is clean, exit 0).
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/fmt_fixture.lls")
set(COPY "${CMAKE_BINARY_DIR}/fmt_fixture_copy.lls")

# baseline: messy fixture runs and prints the marker
execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(NOT sr EQUAL 0 OR NOT so MATCHES "FMT_FIXTURE OK" OR so MATCHES "FMT_FIXTURE FAIL")
    message(FATAL_ERROR "fixture baseline run bad (rc=${sr}):\n${se}\n${so}")
endif()

# (1) --check must report the messy fixture as needing reformat (exit non-zero)
execute_process(COMMAND "${LS}" fmt --check "${SRC}"
    RESULT_VARIABLE cr OUTPUT_VARIABLE co TIMEOUT 30)
if(cr EQUAL 0)
    message(FATAL_ERROR "fmt --check should flag the messy fixture, but exited 0")
endif()

# format a private copy in place
configure_file("${SRC}" "${COPY}" COPYONLY)
execute_process(COMMAND "${LS}" fmt "${COPY}"
    RESULT_VARIABLE fr ERROR_VARIABLE fe TIMEOUT 30)
if(NOT fr EQUAL 0)
    message(FATAL_ERROR "fmt in-place failed (rc=${fr}):\n${fe}")
endif()

# (2) parse-equivalence: the formatted copy must still run identically
execute_process(COMMAND "${LS}" run "${COPY}"
    OUTPUT_VARIABLE fo ERROR_VARIABLE foe RESULT_VARIABLE frr TIMEOUT 30)
if(NOT frr EQUAL 0 OR NOT fo MATCHES "FMT_FIXTURE OK" OR fo MATCHES "FMT_FIXTURE FAIL")
    message(FATAL_ERROR "formatted copy run bad (rc=${frr}):\n${foe}\n${fo}")
endif()

# (3) idempotence: --check on the formatted copy must be clean (exit 0)
execute_process(COMMAND "${LS}" fmt --check "${COPY}"
    RESULT_VARIABLE ir OUTPUT_VARIABLE io TIMEOUT 30)
if(NOT ir EQUAL 0)
    message(FATAL_ERROR "fmt not idempotent: --check on formatted copy exited ${ir}\n${io}")
endif()

message(STATUS "test_fmt: ALL PASSED")
