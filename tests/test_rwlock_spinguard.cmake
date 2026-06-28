# test_rwlock_spinguard.cmake — std.sync RwLock(T) + SpinGuard(T) data guards.
#
#   rwlock_test.ls / spinguard_test.ls    single-threaded read/write (RwLock) and
#                   lock/get (SpinGuard) over Vec/Str/Map (+POD for SpinGuard);
#                   JIT + AOT + memcheck 0/0/0.
#   rwlock_thread_test / spinguard_thread_test  8 workers on a shared global →
#                   exact 40000. NO --memcheck (tracker not thread-safe); AOT x8.
#   rwlock_reader_reject.ls   a reader mutating through &T → compile error.
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(SDIR "${CMAKE_CURRENT_LIST_DIR}/samples")

# ---- helper: single-threaded JIT + memcheck + AOT, expecting a marker ----
function(run_single name marker)
    set(F "${SDIR}/${name}.ls")
    execute_process(COMMAND "${LS}" run "${F}"
        OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
    if(NOT sr EQUAL 0 OR NOT so MATCHES "${marker} OK" OR so MATCHES "${marker} FAIL")
        message(FATAL_ERROR "${name} JIT bad (rc=${sr}):\n${se}\n${so}")
    endif()
    execute_process(COMMAND "${LS}" run --memcheck "${F}"
        OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
    if(NOT mr EQUAL 0 OR NOT "${me}" MATCHES "OK clean")
        message(FATAL_ERROR "${name} --memcheck not clean:\n${me}")
    endif()
    set(EXE "${CMAKE_BINARY_DIR}/${name}.exe")
    execute_process(COMMAND "${LS}" compile "${F}" -o "${EXE}"
        RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
    if(NOT cr EQUAL 0)
        message(FATAL_ERROR "${name} AOT compile failed:\n${ce}")
    endif()
    execute_process(COMMAND "${EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
    if(NOT ar EQUAL 0 OR NOT ao MATCHES "${marker} OK" OR ao MATCHES "${marker} FAIL")
        message(FATAL_ERROR "${name} AOT run: rc=${ar}\n${ao}")
    endif()
endfunction()

# ---- helper: threaded JIT + AOT x8, expecting a marker ----
function(run_threaded name marker)
    set(F "${SDIR}/${name}.ls")
    execute_process(COMMAND "${LS}" run "${F}"
        OUTPUT_VARIABLE to ERROR_VARIABLE te RESULT_VARIABLE tr TIMEOUT 60)
    if(NOT tr EQUAL 0 OR NOT to MATCHES "${marker} OK" OR to MATCHES "${marker} FAIL")
        message(FATAL_ERROR "${name} thread JIT bad (lost updates? lock broken?):\n${te}\n${to}")
    endif()
    set(EXE "${CMAKE_BINARY_DIR}/${name}.exe")
    execute_process(COMMAND "${LS}" compile "${F}" -o "${EXE}"
        RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
    if(NOT cr EQUAL 0)
        message(FATAL_ERROR "${name} thread AOT compile failed:\n${ce}")
    endif()
    foreach(i RANGE 1 8)
        execute_process(COMMAND "${EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 60)
        if(NOT ar EQUAL 0 OR NOT ao MATCHES "${marker} OK" OR ao MATCHES "${marker} FAIL")
            message(FATAL_ERROR "${name} thread AOT run ${i}: rc=${ar}\n${ao}")
        endif()
    endforeach()
endfunction()

run_single(rwlock_test RWLOCK)
run_single(spinguard_test SPINGUARD)
run_threaded(rwlock_thread_test RWLOCK)
run_threaded(spinguard_thread_test SPINGUARD)

# ---- negative: reader mutating through &T must be rejected ----
set(NR "${SDIR}/rwlock_reader_reject.ls")
execute_process(COMMAND "${LS}" run "${NR}"
    OUTPUT_VARIABLE nro ERROR_VARIABLE nre RESULT_VARIABLE nrr TIMEOUT 30)
if(nrr EQUAL 0)
    message(FATAL_ERROR "rwlock_reader_reject should FAIL but succeeded:\n${nro}")
endif()
if(NOT "${nre}${nro}" MATCHES "read-only borrow")
    message(FATAL_ERROR "rwlock_reader_reject wrong error:\n${nre}\n${nro}")
endif()

message(STATUS "test_rwlock_spinguard: ALL PASSED")
