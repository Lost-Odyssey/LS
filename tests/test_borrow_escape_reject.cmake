# test_borrow_escape_reject.cmake — Phase 0 of the borrow-extension feature
# (docs/plan_borrow_extension.md §3). Borrows escaping the parameter position
# (return type, struct field, enum payload, generic type argument) were either
# a latent IR crash or silently accepted (dangling landmine). They must now be
# a clean compile-time rejection that does NOT crash.
#
# Required: LS_EXE, SAMPLE, EXPECT (substring expected in stderr)
cmake_minimum_required(VERSION 3.20)

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc
)

# Must be a clean non-zero rejection (not a segfault / abnormal termination).
# A segfault on Windows surfaces as a large/abnormal return code; a clean
# checker rejection exits with code 1.
if(_rc EQUAL 0)
    message(FATAL_ERROR
        "borrow-escape-reject: expected compile error but got exit 0\nstdout:\n${_out}\nstderr:\n${_err}")
endif()
if(NOT _rc EQUAL 1)
    message(FATAL_ERROR
        "borrow-escape-reject: expected clean rejection (rc=1) but got rc=${_rc} "
        "(possible crash)\nstdout:\n${_out}\nstderr:\n${_err}")
endif()
if(NOT "${_err}" MATCHES "${EXPECT}")
    message(FATAL_ERROR
        "borrow-escape-reject: expected stderr to contain '${EXPECT}'\nstderr:\n${_err}")
endif()
# Guard against the old latent IR crash leaking through.
if("${_err}" MATCHES "return type does not match|verification failed")
    message(FATAL_ERROR
        "borrow-escape-reject: stderr shows IR verification failure (the landmine "
        "is still live)\nstderr:\n${_err}")
endif()
message(STATUS "test_borrow_escape_reject: got expected rejection for ${SAMPLE} (rc=${_rc})")
