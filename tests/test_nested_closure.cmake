# Closure-foundation Phase B — nested closure literals (transitive capture).
#
#   nested_closure.ls         single-threaded: inner closures reference enclosing-
#                             closure locals/params (not transitive), function-
#                             level POD (transitive by-copy), and Block
#                             (transitive by-clone); 2-level nesting; by-move of an
#                             enclosing local into an inner closure. JIT + AOT +
#                             --memcheck (0/0/0) for env clone/drop balance.
#   nested_closure_thread.ls  threaded: a nested closure inside a Task worker
#                             closure touches a shared global Atomic and captures a
#                             function-scope POD transitively. NO --memcheck
#                             (tracker not thread-safe — same as task/sync);
#                             soundness via repeated AOT runs.
#   nested_closure_reject.ls  negative: transitive by-move capture -> compile error.

cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(SDIR "${CMAKE_CURRENT_LIST_DIR}/samples")

# ===================== single-threaded nested closures (memcheck) =====================
set(NC "${SDIR}/nested_closure.ls")

# JIT
execute_process(COMMAND "${LS}" run "${NC}"
    OUTPUT_VARIABLE co ERROR_VARIABLE ce RESULT_VARIABLE cr TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "nested_closure JIT failed (rc=${cr}):\n${ce}\n${co}")
endif()
if(NOT co MATCHES "NC PASS" OR co MATCHES "NC FAIL")
    message(FATAL_ERROR "nested_closure JIT: bad output:\n${co}")
endif()

# --memcheck (env clone/drop balance across layers)
execute_process(COMMAND "${LS}" run --memcheck "${NC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0)
    message(FATAL_ERROR "nested_closure memcheck run failed (rc=${mr}):\n${me}")
endif()
if(NOT "${me}" MATCHES "OK clean")
    message(FATAL_ERROR "nested_closure --memcheck not clean:\n${me}")
endif()

# AOT
set(NC_EXE "${CMAKE_BINARY_DIR}/nested_closure.exe")
execute_process(COMMAND "${LS}" compile "${NC}" -o "${NC_EXE}"
    RESULT_VARIABLE ncr ERROR_VARIABLE nce TIMEOUT 30)
if(NOT ncr EQUAL 0)
    message(FATAL_ERROR "nested_closure AOT compile failed:\n${nce}")
endif()
execute_process(COMMAND "${NC_EXE}" OUTPUT_VARIABLE nao RESULT_VARIABLE nar TIMEOUT 30)
if(NOT nar EQUAL 0 OR NOT nao MATCHES "NC PASS" OR nao MATCHES "NC FAIL")
    message(FATAL_ERROR "nested_closure AOT run: rc=${nar} output:\n${nao}")
endif()

# ===================== threaded integration (Task + nested closure) =====================
set(NT "${SDIR}/nested_closure_thread.ls")

# JIT
execute_process(COMMAND "${LS}" run "${NT}"
    OUTPUT_VARIABLE to ERROR_VARIABLE te RESULT_VARIABLE tr TIMEOUT 60)
if(NOT tr EQUAL 0)
    message(FATAL_ERROR "nested_closure_thread JIT failed (rc=${tr}):\n${te}\n${to}")
endif()
if(NOT to MATCHES "NCT PASS" OR to MATCHES "NCT FAIL")
    message(FATAL_ERROR "nested_closure_thread JIT: bad output:\n${to}")
endif()

# AOT compile + run several times — a cross-thread double-free of a worker's
# nested-closure env would surface as an intermittent crash (rc!=0).
set(NT_EXE "${CMAKE_BINARY_DIR}/nested_closure_thread.exe")
execute_process(COMMAND "${LS}" compile "${NT}" -o "${NT_EXE}"
    RESULT_VARIABLE tcr ERROR_VARIABLE tce TIMEOUT 30)
if(NOT tcr EQUAL 0)
    message(FATAL_ERROR "nested_closure_thread AOT compile failed:\n${tce}")
endif()
foreach(i RANGE 1 6)
    execute_process(COMMAND "${NT_EXE}" OUTPUT_VARIABLE tao RESULT_VARIABLE tar TIMEOUT 60)
    if(NOT tar EQUAL 0 OR NOT tao MATCHES "NCT PASS" OR tao MATCHES "NCT FAIL")
        message(FATAL_ERROR "nested_closure_thread AOT run ${i}: rc=${tar} output:\n${tao}")
    endif()
endforeach()

# ===================== negative: transitive by-move rejected =====================
set(NR "${SDIR}/nested_closure_reject.ls")
execute_process(COMMAND "${LS}" run "${NR}"
    OUTPUT_VARIABLE ro ERROR_VARIABLE re RESULT_VARIABLE rr TIMEOUT 30)
if(rr EQUAL 0)
    message(FATAL_ERROR "nested_closure_reject: expected compile FAILED but it succeeded:\n${ro}")
endif()
if(NOT "${re}${ro}" MATCHES "transitive by-move")
    message(FATAL_ERROR "nested_closure_reject: wrong diagnostic:\n${re}\n${ro}")
endif()

message(STATUS "test_nested_closure: ALL PASSED")
