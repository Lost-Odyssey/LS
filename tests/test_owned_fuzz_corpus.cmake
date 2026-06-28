# test_owned_fuzz_corpus.cmake — ownership/drop regression guard.
# Curated corpus of grammar-fuzzer-generated programs (tests/fuzz/genfuzz.py),
# each exercising owned-type paths: match yielding owned payloads into vars,
# Option/Result combinators, nested containers (Vec(Vec)/Map(Str,Vec)),
# has_drop structs, recursive has_drop enums (Tree). Every program must run AND
# be memcheck-clean. Catches regressions in the historically buggy codegen
# ownership machinery (L-012 / L-013 / M5-004 …). Corpus is python-free at test
# time — only the generator (genfuzz.py) needs python. See docs/plan_fuzzing.md.
#
# Required: LS_EXE, CORPUS_DIR, LS_HOME
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT CORPUS_DIR OR NOT LS_HOME)
    message(FATAL_ERROR "requires LS_EXE, CORPUS_DIR, LS_HOME")
endif()
set(ENV{LS_HOME} "${LS_HOME}")

file(GLOB _cases "${CORPUS_DIR}/*.ls")
if(_cases STREQUAL "")
    message(FATAL_ERROR "no corpus in ${CORPUS_DIR}")
endif()

set(_fail 0)
foreach(_f IN LISTS _cases)
    get_filename_component(_name "${_f}" NAME)
    execute_process(
        COMMAND "${LS_EXE}" run --memcheck "${_f}"
        OUTPUT_QUIET ERROR_VARIABLE _err RESULT_VARIABLE _rc
        TIMEOUT 30
    )
    if(NOT "${_rc}" MATCHES "^-?[0-9]+$")
        message(WARNING "HANG: ${_name}")
        set(_fail 1)
    elseif(NOT _rc EQUAL 0)
        message(WARNING "FAIL: ${_name} (exit ${_rc} — crash or runtime error)")
        set(_fail 1)
    elseif(NOT "${_err}" MATCHES "OK clean")
        message(WARNING "MEMCHECK: ${_name} (no 'OK clean' in report)\n${_err}")
        set(_fail 1)
    else()
        message(STATUS "ok: ${_name}")
    endif()
endforeach()

if(_fail)
    message(FATAL_ERROR "test_owned_fuzz_corpus: FAILED (see warnings above)")
endif()
message(STATUS "test_owned_fuzz_corpus: ALL PASSED")
