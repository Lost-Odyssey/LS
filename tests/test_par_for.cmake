# Closure-foundation Phase A — capture a Block by-clone (and par_for on top).
#
#   closure_capture_block.lls  single-threaded: a closure captures another Block
#                             (POD-env and has_drop/Str-env) and calls it
#                             repeatedly; the source Block stays live. Run under
#                             --memcheck (0/0/0) to prove env clone/drop balance.
#   par_for_test.lls           threaded: std.thread.parallel_for runs a Block(int) body in
#                             parallel; each chunk closure captures the body
#                             by-clone. Workers write disjoint slots; join then
#                             verify. NO --memcheck (tracker not thread-safe —
#                             same as task/sync); soundness via repeated AOT runs.

cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(SDIR "${CMAKE_CURRENT_LIST_DIR}/samples")

# ===================== single-threaded capture-Block (memcheck) =====================
set(CB "${SDIR}/closure_capture_block.lls")

# JIT
execute_process(COMMAND "${LS}" run "${CB}"
    OUTPUT_VARIABLE co ERROR_VARIABLE ce RESULT_VARIABLE cr TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "capture-block JIT failed (rc=${cr}):\n${ce}\n${co}")
endif()
if(NOT co MATCHES "A PASS" OR co MATCHES "A FAIL")
    message(FATAL_ERROR "capture-block JIT: bad output:\n${co}")
endif()

# --memcheck (env clone/drop balance)
execute_process(COMMAND "${LS}" run --memcheck "${CB}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0)
    message(FATAL_ERROR "capture-block memcheck run failed (rc=${mr}):\n${me}")
endif()
if(NOT "${me}" MATCHES "OK clean")
    message(FATAL_ERROR "capture-block --memcheck not clean:\n${me}")
endif()

# AOT
set(CB_EXE "${CMAKE_BINARY_DIR}/closure_capture_block.exe")
execute_process(COMMAND "${LS}" compile "${CB}" -o "${CB_EXE}"
    RESULT_VARIABLE ccr ERROR_VARIABLE cce TIMEOUT 30)
if(NOT ccr EQUAL 0)
    message(FATAL_ERROR "capture-block AOT compile failed:\n${cce}")
endif()
execute_process(COMMAND "${CB_EXE}" OUTPUT_VARIABLE cao RESULT_VARIABLE car TIMEOUT 30)
if(NOT car EQUAL 0 OR NOT cao MATCHES "A PASS" OR cao MATCHES "A FAIL")
    message(FATAL_ERROR "capture-block AOT run: rc=${car} output:\n${cao}")
endif()

# ============================== par_for (threaded) ==============================
set(PF "${SDIR}/par_for_test.lls")

# JIT
execute_process(COMMAND "${LS}" run "${PF}"
    OUTPUT_VARIABLE po ERROR_VARIABLE pe RESULT_VARIABLE pr TIMEOUT 60)
if(NOT pr EQUAL 0)
    message(FATAL_ERROR "par_for JIT failed (rc=${pr}):\n${pe}\n${po}")
endif()
if(NOT po MATCHES "PAR PASS" OR po MATCHES "PAR FAIL")
    message(FATAL_ERROR "par_for JIT: bad output:\n${po}")
endif()

# AOT compile
set(PF_EXE "${CMAKE_BINARY_DIR}/par_for_test.exe")
execute_process(COMMAND "${LS}" compile "${PF}" -o "${PF_EXE}"
    RESULT_VARIABLE pcr ERROR_VARIABLE pce TIMEOUT 30)
if(NOT pcr EQUAL 0)
    message(FATAL_ERROR "par_for AOT compile failed:\n${pce}")
endif()

# Run AOT several times — a cross-thread double-free of a worker's closure env
# (the by-clone Block) would surface as an intermittent crash (rc!=0).
foreach(i RANGE 1 6)
    execute_process(COMMAND "${PF_EXE}" OUTPUT_VARIABLE pao RESULT_VARIABLE par TIMEOUT 60)
    if(NOT par EQUAL 0 OR NOT pao MATCHES "PAR PASS" OR pao MATCHES "PAR FAIL")
        message(FATAL_ERROR "par_for AOT run ${i}: rc=${par} output:\n${pao}")
    endif()
endforeach()

message(STATUS "test_par_for: ALL PASSED")
