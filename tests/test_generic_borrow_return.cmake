# test_generic_borrow_return.cmake — generic return-borrow elision: extend
# single-input `&self` borrow-return to GENERIC methods so a generic container
# hands out a zero-copy `&T` instead of cloning. Covers:
#   (1) generic_borrow_return_test.ls — Box(T) aggregate borrow (struct element),
#       read + bind + writable &!T (JIT + AOT + memcheck 0/0/0).
#   (2) vec_get_ref_test.ls — Vec.get_ref(&self,i) -> &T on the real container,
#       validating the `*T data[i]` pointer-index borrow-return path.
#   (3) generic_borrow_scalar_reject.ls (NEGATIVE) — a POD-scalar element borrow
#       (`Box(int).get_ref -> &int`) must be a clean compile error (aggregate-only).
# FAIL anywhere in positive output vetoes; the negative must rc!=0 with a message.
cmake_minimum_required(VERSION 3.20)
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

# ---- positive samples: JIT marker, no FAIL; AOT; memcheck OK clean ----
function(run_positive name sample marker)
    execute_process(COMMAND "${LS_EXE}" run "${sample}"
        OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr)
    if(NOT jr EQUAL 0 OR NOT "${jo}" MATCHES "${marker}" OR "${jo}" MATCHES "FAIL")
        message(FATAL_ERROR "${name} JIT bad (rc=${jr}):\n${je}\n${jo}")
    endif()

    set(exe "${WORK_DIR}/${name}_aot")
    if(WIN32)
        set(exe "${exe}.exe")
    endif()
    execute_process(COMMAND "${LS_EXE}" compile "${sample}" -o "${exe}"
        RESULT_VARIABLE cr ERROR_VARIABLE ce)
    if(NOT cr EQUAL 0)
        message(FATAL_ERROR "${name} AOT compile failed:\n${ce}")
    endif()
    execute_process(COMMAND "${exe}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar)
    if(NOT ar EQUAL 0 OR NOT "${ao}" MATCHES "${marker}" OR "${ao}" MATCHES "FAIL")
        message(FATAL_ERROR "${name} AOT bad (rc=${ar}):\n${ao}")
    endif()
    file(REMOVE "${exe}")

    execute_process(COMMAND "${LS_EXE}" run --memcheck "${sample}"
        OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr)
    if(NOT mr EQUAL 0 OR NOT "${me}" MATCHES "OK clean")
        message(FATAL_ERROR "${name} memcheck not clean:\n${me}")
    endif()
    message(STATUS "${name}: OK (JIT + AOT + memcheck)")
endfunction()

run_positive("generic_borrow_return" "${SAMPLE_DIR}/generic_borrow_return_test.ls" "GBR PASS")
run_positive("vec_get_ref"           "${SAMPLE_DIR}/vec_get_ref_test.ls"           "VGR PASS")

# ---- negative: POD-scalar generic borrow return must be rejected ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/generic_borrow_scalar_reject.ls"
    OUTPUT_VARIABLE no ERROR_VARIABLE ne RESULT_VARIABLE nr)
if(nr EQUAL 0)
    message(FATAL_ERROR "scalar-reject should have failed to compile but rc=0:\n${no}")
endif()
if(NOT "${ne}${no}" MATCHES "POD scalar")
    message(FATAL_ERROR "scalar-reject wrong error (expected 'POD scalar'):\n${ne}\n${no}")
endif()
message(STATUS "generic_borrow_scalar_reject: OK (clean compile error)")

message(STATUS "test_generic_borrow_return: ALL PASSED")
