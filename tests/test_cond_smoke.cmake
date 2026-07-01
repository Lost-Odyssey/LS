# Phase 0 (std.chan foundation) — condition-variable intrinsics.
#
#   cond_smoke.lls   single-threaded: __cond_init / signal / broadcast / destroy
#                   resolve and run (the new runtime primitive + intrinsic +
#                   jit wiring). __cond_wait is exercised later by the blocking
#                   Chan tests (Phase 2), where a peer thread signals.
#
# This is infrastructure validation: it proves the 2-arg sync intrinsic plumbing
# (checker arity, codegen lowering, jit REG) is sound before Ring/Chan land.

cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(SDIR "${CMAKE_CURRENT_LIST_DIR}/samples")
set(ST "${SDIR}/cond_smoke.lls")

# JIT
execute_process(COMMAND "${LS}" run "${ST}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(NOT sr EQUAL 0)
    message(FATAL_ERROR "cond smoke JIT failed (rc=${sr}):\n${se}\n${so}")
endif()
if(NOT so MATCHES "cond smoke ok")
    message(FATAL_ERROR "cond smoke JIT: bad output:\n${so}")
endif()

# memcheck (no heap of our own → 0/0/0)
execute_process(COMMAND "${LS}" run --memcheck "${ST}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0)
    message(FATAL_ERROR "cond smoke memcheck run failed (rc=${mr}):\n${me}")
endif()
if(NOT "${me}" MATCHES "OK clean")
    message(FATAL_ERROR "cond smoke --memcheck not clean:\n${me}")
endif()

# AOT
set(ST_EXE "${CMAKE_BINARY_DIR}/cond_smoke.exe")
execute_process(COMMAND "${LS}" compile "${ST}" -o "${ST_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "cond smoke AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${ST_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "cond smoke ok")
    message(FATAL_ERROR "cond smoke AOT run: rc=${ar} output:\n${ao}")
endif()

message(STATUS "cond smoke: JIT + memcheck + AOT all OK")
