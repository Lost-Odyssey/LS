# test_derive_fuzz_corpus.cmake — @derive / static-reflection robustness guard.
# Edge & malformed @derive programs (tests/fuzz/derive_corpus) must never crash
# the compiler: `ls emit-ir` (the full parse -> check -> codegen "emit" path) must
# exit 0 (compiled) or 1 (clean error), never a crash or hang. Stresses the new
# @derive parser + checker + source-synthesis paths. See docs/plan_fuzzing.md.
cmake_minimum_required(VERSION 3.20)
if(NOT LS_EXE OR NOT CORPUS_DIR)
    message(FATAL_ERROR "requires LS_EXE, CORPUS_DIR")
endif()
file(GLOB _cases "${CORPUS_DIR}/*.ls")
if(_cases STREQUAL "")
    message(FATAL_ERROR "no corpus in ${CORPUS_DIR}")
endif()
set(_fail 0)
foreach(_f IN LISTS _cases)
    get_filename_component(_name "${_f}" NAME)
    execute_process(
        COMMAND "${LS_EXE}" emit-ir "${_f}"
        OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _rc TIMEOUT 30
    )
    if(NOT "${_rc}" MATCHES "^-?[0-9]+$")
        message(WARNING "HANG: ${_name}")
        set(_fail 1)
    elseif(_rc EQUAL 0 OR _rc EQUAL 1)
        message(STATUS "ok (exit ${_rc}): ${_name}")
    else()
        message(WARNING "CRASH: ${_name} (exit ${_rc})")
        set(_fail 1)
    endif()
endforeach()
if(_fail)
    message(FATAL_ERROR "test_derive_fuzz_corpus: FAILED (compiler crashed on a @derive edge case)")
endif()
message(STATUS "test_derive_fuzz_corpus: ALL PASSED")
