# std.atomic — lock-free atomic scalars.
#
#   atomic_test.lls         single-threaded correctness of every method, run
#                          under --memcheck (Atomic is POD → 0/0/0).
#   atomic_thread_test.lls  N workers hammer a shared global Atomic; the exact
#                          final count proves real cross-thread atomicity.
#                          NO --memcheck (tracker not thread-safe — like task);
#                          soundness/correctness via repeated AOT runs.

cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(SDIR "${CMAKE_CURRENT_LIST_DIR}/samples")

# ============================ single-threaded ============================
set(ST "${SDIR}/atomic_test.lls")

# JIT
execute_process(COMMAND "${LS}" run "${ST}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(NOT sr EQUAL 0)
    message(FATAL_ERROR "atomic JIT failed (rc=${sr}):\n${se}\n${so}")
endif()
if(NOT so MATCHES "ATOMIC OK" OR so MATCHES "ATOMIC FAIL")
    message(FATAL_ERROR "atomic JIT: bad output:\n${so}")
endif()

# memcheck (POD, must be 0/0/0)
execute_process(COMMAND "${LS}" run --memcheck "${ST}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0)
    message(FATAL_ERROR "atomic memcheck run failed (rc=${mr}):\n${me}")
endif()
if(NOT "${me}" MATCHES "OK clean")
    message(FATAL_ERROR "atomic --memcheck not clean:\n${me}")
endif()

# AOT
set(ST_EXE "${CMAKE_BINARY_DIR}/atomic_test.exe")
execute_process(COMMAND "${LS}" compile "${ST}" -o "${ST_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "atomic AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${ST_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "ATOMIC OK" OR ao MATCHES "ATOMIC FAIL")
    message(FATAL_ERROR "atomic AOT run: rc=${ar} output:\n${ao}")
endif()

# ============================== threaded ================================
set(TT "${SDIR}/atomic_thread_test.lls")

# JIT
execute_process(COMMAND "${LS}" run "${TT}"
    OUTPUT_VARIABLE to ERROR_VARIABLE te RESULT_VARIABLE tr TIMEOUT 60)
if(NOT tr EQUAL 0)
    message(FATAL_ERROR "atomic thread JIT failed (rc=${tr}):\n${te}\n${to}")
endif()
if(NOT to MATCHES "ATOMIC OK" OR to MATCHES "ATOMIC FAIL")
    message(FATAL_ERROR "atomic thread JIT: bad output (lost updates?):\n${to}")
endif()

# AOT compile + repeated runs — every run must hit the exact count.
set(TT_EXE "${CMAKE_BINARY_DIR}/atomic_thread_test.exe")
execute_process(COMMAND "${LS}" compile "${TT}" -o "${TT_EXE}"
    RESULT_VARIABLE tcr ERROR_VARIABLE tce TIMEOUT 30)
if(NOT tcr EQUAL 0)
    message(FATAL_ERROR "atomic thread AOT compile failed:\n${tce}")
endif()
foreach(i RANGE 1 8)
    execute_process(COMMAND "${TT_EXE}" OUTPUT_VARIABLE tao RESULT_VARIABLE tar TIMEOUT 60)
    if(NOT tar EQUAL 0 OR NOT tao MATCHES "ATOMIC OK" OR tao MATCHES "ATOMIC FAIL")
        message(FATAL_ERROR "atomic thread AOT run ${i}: rc=${tar} output:\n${tao}")
    endif()
endforeach()

message(STATUS "test_atomic: ALL PASSED")
