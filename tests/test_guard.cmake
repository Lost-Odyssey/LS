# test_guard.cmake — std.sync Guard(T) data guard (compile-time data-race
# prevention without a lifetime system).
#
#   guard_test.lls          single-threaded lock/get over Vec/Str/Map payloads +
#                          global guard auto-drop; JIT + AOT + memcheck 0/0/0.
#   guard_thread_test.lls   8 workers push into a shared GLOBAL Guard(Vec); the
#                          lock serialises → exact 40000. NO --memcheck (tracker
#                          not thread-safe, same as task/sync); JIT + AOT x8.
#   guard_priv_reject.lls   touching the private value field outside impl → reject.
#   guard_literal_reject.lls  injecting data via a struct literal → reject.
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(SDIR "${CMAKE_CURRENT_LIST_DIR}/samples")

# ============================ single-threaded ============================
set(ST "${SDIR}/guard_test.lls")

execute_process(COMMAND "${LS}" run "${ST}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(NOT sr EQUAL 0)
    message(FATAL_ERROR "guard JIT failed (rc=${sr}):\n${se}\n${so}")
endif()
if(NOT so MATCHES "GUARD OK" OR so MATCHES "GUARD FAIL")
    message(FATAL_ERROR "guard JIT: bad output:\n${so}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${ST}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0)
    message(FATAL_ERROR "guard memcheck run failed (rc=${mr}):\n${me}")
endif()
if(NOT "${me}" MATCHES "OK clean")
    message(FATAL_ERROR "guard --memcheck not clean:\n${me}")
endif()

set(ST_EXE "${CMAKE_BINARY_DIR}/guard_test.exe")
execute_process(COMMAND "${LS}" compile "${ST}" -o "${ST_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "guard AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${ST_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "GUARD OK" OR ao MATCHES "GUARD FAIL")
    message(FATAL_ERROR "guard AOT run: rc=${ar} output:\n${ao}")
endif()

# ============================== threaded ================================
set(TT "${SDIR}/guard_thread_test.lls")

execute_process(COMMAND "${LS}" run "${TT}"
    OUTPUT_VARIABLE to ERROR_VARIABLE te RESULT_VARIABLE tr TIMEOUT 60)
if(NOT tr EQUAL 0)
    message(FATAL_ERROR "guard thread JIT failed (rc=${tr}):\n${te}\n${to}")
endif()
if(NOT to MATCHES "GUARD OK" OR to MATCHES "GUARD FAIL")
    message(FATAL_ERROR "guard thread JIT: bad output (lost updates? guard broken?):\n${to}")
endif()

set(TT_EXE "${CMAKE_BINARY_DIR}/guard_thread_test.exe")
execute_process(COMMAND "${LS}" compile "${TT}" -o "${TT_EXE}"
    RESULT_VARIABLE tcr ERROR_VARIABLE tce TIMEOUT 30)
if(NOT tcr EQUAL 0)
    message(FATAL_ERROR "guard thread AOT compile failed:\n${tce}")
endif()
foreach(i RANGE 1 8)
    execute_process(COMMAND "${TT_EXE}" OUTPUT_VARIABLE tao RESULT_VARIABLE tar TIMEOUT 60)
    if(NOT tar EQUAL 0 OR NOT tao MATCHES "GUARD OK" OR tao MATCHES "GUARD FAIL")
        message(FATAL_ERROR "guard thread AOT run ${i}: rc=${tar} output:\n${tao}")
    endif()
endforeach()

# ============================== negatives ==============================
set(NP "${SDIR}/guard_priv_reject.lls")
execute_process(COMMAND "${LS}" run "${NP}"
    OUTPUT_VARIABLE npo ERROR_VARIABLE npe RESULT_VARIABLE npr TIMEOUT 30)
if(npr EQUAL 0)
    message(FATAL_ERROR "guard_priv_reject should FAIL but succeeded:\n${npo}")
endif()
if(NOT "${npe}${npo}" MATCHES "is private")
    message(FATAL_ERROR "guard_priv_reject wrong error:\n${npe}\n${npo}")
endif()

set(NL "${SDIR}/guard_literal_reject.lls")
execute_process(COMMAND "${LS}" run "${NL}"
    OUTPUT_VARIABLE nlo ERROR_VARIABLE nle RESULT_VARIABLE nlr TIMEOUT 30)
if(nlr EQUAL 0)
    message(FATAL_ERROR "guard_literal_reject should FAIL but succeeded:\n${nlo}")
endif()
if(NOT "${nle}${nlo}" MATCHES "is private")
    message(FATAL_ERROR "guard_literal_reject wrong error:\n${nle}\n${nlo}")
endif()

message(STATUS "test_guard: ALL PASSED")
