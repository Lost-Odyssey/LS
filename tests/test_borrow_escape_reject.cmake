# test_borrow_escape_reject.cmake — Phase 0 of the borrow-extension feature
# (docs/plan_borrow_extension.md §3). Borrows escaping the parameter position
# (return type, struct field, enum payload, generic type argument) were either
# a latent IR crash or silently accepted (dangling landmine). They must now be
# a clean compile-time rejection that does NOT crash.
#
# ---- why this family exists (17 tests share this driver) -------------------
#
# LS has NO named lifetimes and deliberately never will (`'a` was evaluated and
# rejected as too much complexity for the value). What keeps `&T` sound instead
# is a much smaller deal: a borrow may live in a PARAMETER, and everywhere else
# it either has an inferable provenance or is refused. Every corpus below is one
# place where "inferable" runs out.
#
# The three shapes that let a borrow outlive its referent, and are refused:
#   * a struct field or enum payload (`struct H { &Foo f }`) — the container can
#     be returned, stored in a Vec, or captured, and nothing ties its lifetime to
#     the referent's. This one was SILENTLY ACCEPTED before Phase 0, with zero
#     checks: a textbook dangling pointer that compiled clean.
#   * a generic type argument (`Vec(&Foo)`) — same escape, reached through the
#     template instantiation path instead of a field declaration, which is why it
#     needs its own corpus rather than being covered by the struct case.
#   * a return type with no borrow input to inherit from (`def g(Foo a) -> &Foo`)
#     — single-input elision needs exactly ONE borrow parameter to name the
#     output's provenance. Zero inputs (returning a local, or a by-value param)
#     dangles; two or more is ambiguous, and guessing would be unsound. Both are
#     refused rather than picked arbitrarily.
#
# The remaining corpora police the borrows that ARE allowed, once a returned
# borrow gets bound to a local (`&Inner r = o.get()`): the receiver `o` becomes
# `r`'s provenance and is pinned, so moving it, copying out of it, or capturing
# the borrow into a closure — each of which would outlive the referent — is
# rejected while the borrow is alive.
#
# What makes the whole family worth its size is the failure mode it replaces.
# These are not "the compiler should be stricter" tests: pre-Phase-0 the shapes
# above either produced an IR-level crash with no source location, or compiled
# to a program that read freed memory and printed plausible-looking garbage. The
# assertion is therefore twofold — the diagnostic text must be present AND the
# compiler must exit cleanly, because "it crashed instead of dangling" is not a
# fix. Each test pins its own EXPECT substring; the shared harness only runs the
# corpus and checks that compilation failed with that text on stderr.
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
