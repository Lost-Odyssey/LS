# test_destroy.cmake — `Destroy` interface + C++-style `~` destructor.
#   正向：methods X: Destroy { def ~(&!self) } 折叠成固有 __drop —— 作用域结束
#         自动调用、has_drop 字段自动释放、where T: Destroy 约束。JIT + AOT + memcheck。
#   负向：① 裸 `def ~` 在固有块拒绝；② 手动 .__drop() 拒绝。
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(F "${CMAKE_CURRENT_LIST_DIR}/samples/destroy_smoke.ls")

# --- positive: JIT ---
execute_process(COMMAND "${LS}" run "${F}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(NOT sr EQUAL 0 OR NOT so MATCHES "DESTROY OK" OR so MATCHES "DESTROY FAIL")
    message(FATAL_ERROR "destroy JIT bad (rc=${sr}):\n${se}\n${so}")
endif()

# --- positive: AOT ---
set(EXE "${CMAKE_BINARY_DIR}/destroy_smoke.exe")
execute_process(COMMAND "${LS}" compile "${F}" -o "${EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 60)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "destroy AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "DESTROY OK" OR ao MATCHES "DESTROY FAIL")
    message(FATAL_ERROR "destroy AOT run: rc=${ar}\n${ao}")
endif()

# --- positive: memcheck (has_drop field must be freed after `~` runs) ---
execute_process(COMMAND "${LS}" run --memcheck "${F}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
set(mc "${mo}${me}")
if(NOT mc MATCHES "0 leak" OR NOT mc MATCHES "0 double-free" OR NOT mc MATCHES "0 invalid free")
    message(FATAL_ERROR "destroy memcheck not clean:\n${mc}")
endif()

# --- negatives: must be rejected at compile time ---
foreach(neg destroy_bare_reject destroy_manual_reject destroy_retire_reject destroy_moveonly_reject)
    execute_process(COMMAND "${LS}" run "${CMAKE_CURRENT_LIST_DIR}/samples/${neg}.ls"
        OUTPUT_VARIABLE no ERROR_VARIABLE ne RESULT_VARIABLE nr TIMEOUT 30)
    if(nr EQUAL 0)
        message(FATAL_ERROR "destroy negative ${neg}: expected compile error but got rc=0\n${no}")
    endif()
endforeach()

message(STATUS "test_destroy: ALL PASSED")
