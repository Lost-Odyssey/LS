# M-H (std.map prereq): the `Hash` trait + FxHash hasher (std/hash.ls).
# Verifies stable/distinct hashes, high-bit distribution under Fibonacci scatter,
# and `where T: Hash` trait-bound dispatch. JIT + memcheck 0/0/0 + AOT, plus a
# negative case (a type without `impl Hash` must be rejected at compile time).
# See docs/plan_std_map.md §3. Self-verifying sample prints "HASH PASS".

cmake_minimum_required(VERSION 3.20)
set(LS  "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/hash_test.ls")
set(NEG "${CMAKE_CURRENT_LIST_DIR}/samples/hash_neg_test.ls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/hash.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "hash JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "HASH PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "hash JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "hash memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "hash AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ao MATCHES "HASH PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "hash AOT: bad output (rc=${ar}):\n${ao}")
endif()

# Negative: `where T: Hash` with a type lacking `impl Hash` must fail to compile.
execute_process(COMMAND "${LS}" run "${NEG}"
    OUTPUT_VARIABLE no ERROR_VARIABLE ne RESULT_VARIABLE nr TIMEOUT 30)
if(nr EQUAL 0)
    message(FATAL_ERROR "hash_neg: expected compile rejection but succeeded\nstdout:\n${no}")
endif()
if(NOT "${ne}" MATCHES "does not implement Hash")
    message(FATAL_ERROR "hash_neg: expected 'does not implement Hash' in stderr:\n${ne}")
endif()

message(STATUS "hash all passed")
