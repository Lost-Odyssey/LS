# test_hasdrop_worklist.cmake — C1 §3.5 has_drop fixpoint worklist.
#   ① parity: LS_HASDROP_VERIFY=1 runs the worklist AND the legacy full-scan
#      oracle from an identical seed and aborts on any per-type disagreement.
#   ② correctness: --memcheck must be 0 leaks (a missed has_drop flip would
#      skip a destructor and leak the Str deep in the A/D/Leaf chain).
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(F "${CMAKE_CURRENT_LIST_DIR}/samples/hasdrop_worklist_stress.lls")

# --- parity: worklist vs legacy oracle (abort on mismatch) ---
set(ENV{LS_HASDROP_VERIFY} "1")
execute_process(COMMAND "${LS}" run "${F}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 60)
if(NOT sr EQUAL 0 OR NOT so MATCHES "HASDROP OK")
    message(FATAL_ERROR "hasdrop parity/run bad (rc=${sr}):\n${se}\n${so}")
endif()
unset(ENV{LS_HASDROP_VERIFY})

# --- correctness: memcheck must be clean (has_drop actually set) ---
execute_process(COMMAND "${LS}" run --memcheck "${F}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 60)
set(mc "${mo}${me}")
if(NOT mc MATCHES "0 leak" OR NOT mc MATCHES "0 double-free" OR NOT mc MATCHES "0 invalid free")
    message(FATAL_ERROR "hasdrop memcheck not clean:\n${mc}")
endif()

message(STATUS "test_hasdrop_worklist: ALL PASSED")
