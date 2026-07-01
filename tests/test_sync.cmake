# std.sync — Mutex(T) + SpinLock(T).
#
#   sync_test.lls         single-threaded with/raw/trylock + clean teardown,
#                        run under --memcheck (0/0/0).
#   sync_thread_test.lls  N workers do a non-atomic RMW on a shared guarded
#                        counter; the lock serialises them → exact final count.
#                        NO --memcheck (tracker not thread-safe — like task);
#                        correctness via repeated AOT runs.

cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(SDIR "${CMAKE_CURRENT_LIST_DIR}/samples")

# ============================ single-threaded ============================
set(ST "${SDIR}/sync_test.lls")

execute_process(COMMAND "${LS}" run "${ST}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(NOT sr EQUAL 0)
    message(FATAL_ERROR "sync JIT failed (rc=${sr}):\n${se}\n${so}")
endif()
if(NOT so MATCHES "SYNC OK" OR so MATCHES "SYNC FAIL")
    message(FATAL_ERROR "sync JIT: bad output:\n${so}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${ST}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0)
    message(FATAL_ERROR "sync memcheck run failed (rc=${mr}):\n${me}")
endif()
if(NOT "${me}" MATCHES "OK clean")
    message(FATAL_ERROR "sync --memcheck not clean:\n${me}")
endif()

set(ST_EXE "${CMAKE_BINARY_DIR}/sync_test.exe")
execute_process(COMMAND "${LS}" compile "${ST}" -o "${ST_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "sync AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${ST_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "SYNC OK" OR ao MATCHES "SYNC FAIL")
    message(FATAL_ERROR "sync AOT run: rc=${ar} output:\n${ao}")
endif()

# ============================== threaded ================================
set(TT "${SDIR}/sync_thread_test.lls")

execute_process(COMMAND "${LS}" run "${TT}"
    OUTPUT_VARIABLE to ERROR_VARIABLE te RESULT_VARIABLE tr TIMEOUT 60)
if(NOT tr EQUAL 0)
    message(FATAL_ERROR "sync thread JIT failed (rc=${tr}):\n${te}\n${to}")
endif()
if(NOT to MATCHES "SYNC OK" OR to MATCHES "SYNC FAIL")
    message(FATAL_ERROR "sync thread JIT: bad output (lost updates? lock broken?):\n${to}")
endif()

set(TT_EXE "${CMAKE_BINARY_DIR}/sync_thread_test.exe")
execute_process(COMMAND "${LS}" compile "${TT}" -o "${TT_EXE}"
    RESULT_VARIABLE tcr ERROR_VARIABLE tce TIMEOUT 30)
if(NOT tcr EQUAL 0)
    message(FATAL_ERROR "sync thread AOT compile failed:\n${tce}")
endif()
foreach(i RANGE 1 8)
    execute_process(COMMAND "${TT_EXE}" OUTPUT_VARIABLE tao RESULT_VARIABLE tar TIMEOUT 60)
    if(NOT tar EQUAL 0 OR NOT tao MATCHES "SYNC OK" OR tao MATCHES "SYNC FAIL")
        message(FATAL_ERROR "sync thread AOT run ${i}: rc=${tar} output:\n${tao}")
    endif()
endforeach()

# ===================== SpinLock heavy contention (yield path) =====================
# More workers than cores + a long critical section → waiters spin past the
# pause-only threshold and fall through to __cpu_yield. Must stay exact AND
# terminate (no priority-inversion deadlock under the adaptive backoff).
set(SC "${SDIR}/spin_contend_test.lls")

execute_process(COMMAND "${LS}" run "${SC}"
    OUTPUT_VARIABLE co ERROR_VARIABLE ce2 RESULT_VARIABLE cr2 TIMEOUT 60)
if(NOT cr2 EQUAL 0)
    message(FATAL_ERROR "spin-contend JIT failed (rc=${cr2}):\n${ce2}\n${co}")
endif()
if(NOT co MATCHES "SPIN OK" OR co MATCHES "SPIN FAIL")
    message(FATAL_ERROR "spin-contend JIT: bad output (lost updates? deadlock?):\n${co}")
endif()

set(SC_EXE "${CMAKE_BINARY_DIR}/spin_contend_test.exe")
execute_process(COMMAND "${LS}" compile "${SC}" -o "${SC_EXE}"
    RESULT_VARIABLE scr ERROR_VARIABLE sce TIMEOUT 30)
if(NOT scr EQUAL 0)
    message(FATAL_ERROR "spin-contend AOT compile failed:\n${sce}")
endif()
foreach(i RANGE 1 6)
    execute_process(COMMAND "${SC_EXE}" OUTPUT_VARIABLE sco RESULT_VARIABLE scar TIMEOUT 60)
    if(NOT scar EQUAL 0 OR NOT sco MATCHES "SPIN OK" OR sco MATCHES "SPIN FAIL")
        message(FATAL_ERROR "spin-contend AOT run ${i}: rc=${scar} output:\n${sco}")
    endif()
endforeach()

message(STATUS "test_sync: ALL PASSED")
