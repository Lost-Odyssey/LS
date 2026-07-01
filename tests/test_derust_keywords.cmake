# test_derust_keywords.cmake — 去 Rust 化关键字换皮回归守卫。
#   正向：def / methods / interface / private + 合并 trait-impl `methods T: I`
#         （含泛型）跑通 JIT + AOT，输出 DERUST OK。
#   负向：退役的旧关键字 `fn` 现被当普通标识符 → 编译期拒绝。
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(F "${CMAKE_CURRENT_LIST_DIR}/samples/derust_keywords_smoke.lls")

# --- positive: JIT ---
execute_process(COMMAND "${LS}" run "${F}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(NOT sr EQUAL 0 OR NOT so MATCHES "DERUST OK" OR so MATCHES "DERUST FAIL")
    message(FATAL_ERROR "derust_keywords JIT bad (rc=${sr}):\n${se}\n${so}")
endif()

# --- positive: AOT ---
set(EXE "${CMAKE_BINARY_DIR}/derust_keywords_smoke.exe")
execute_process(COMMAND "${LS}" compile "${F}" -o "${EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 60)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "derust_keywords AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "DERUST OK" OR ao MATCHES "DERUST FAIL")
    message(FATAL_ERROR "derust_keywords AOT run: rc=${ar}\n${ao}")
endif()

# --- negative: retired `fn` must be rejected ---
set(R "${CMAKE_CURRENT_LIST_DIR}/samples/derust_keywords_reject.lls")
execute_process(COMMAND "${LS}" run "${R}"
    OUTPUT_VARIABLE ro ERROR_VARIABLE re RESULT_VARIABLE rr TIMEOUT 30)
if(rr EQUAL 0)
    message(FATAL_ERROR "derust_keywords: retired `fn` was NOT rejected (rc=0)\n${ro}")
endif()

message(STATUS "test_derust_keywords: ALL PASSED")
