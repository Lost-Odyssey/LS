# test_hasdrop_worklist.cmake — C1 §3.5 has_drop fixpoint worklist.
#   correctness: --memcheck must be 0 leaks (a missed has_drop flip would
#   skip a destructor and leak the Str deep in the A/D/Leaf chain).
#   (The legacy full-scan oracle + LS_HASDROP_VERIFY parity harness this test
#   used to also exercise were retired once the worklist was the sole
#   implementation; see git history.)
#
# @subsystem checker/types
# @guards C1 has_drop fixpoint worklist
# @sources checker.c:checker_propagate_has_drop_worklist
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(F "${CMAKE_CURRENT_LIST_DIR}/samples/hasdrop_worklist_stress.lls")

# --- sanity: plain run must reach the "HASDROP OK" marker ---
execute_process(COMMAND "${LS}" run "${F}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 60)
if(NOT sr EQUAL 0 OR NOT so MATCHES "HASDROP OK")
    message(FATAL_ERROR "hasdrop run bad (rc=${sr}):\n${se}\n${so}")
endif()

# --- correctness: memcheck must be clean (has_drop actually set) ---
execute_process(COMMAND "${LS}" run --memcheck "${F}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 60)
set(mc "${mo}${me}")
if(NOT mc MATCHES "0 leak" OR NOT mc MATCHES "0 double-free" OR NOT mc MATCHES "0 invalid free")
    message(FATAL_ERROR "hasdrop memcheck not clean:\n${mc}")
endif()

message(STATUS "test_hasdrop_worklist: ALL PASSED")
