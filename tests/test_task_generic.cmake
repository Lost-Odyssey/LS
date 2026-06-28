# std.task — generic structured concurrency. `spawn.task(T)(|| body)` runs the
# body on an OS worker thread; `t.join()` waits and MOVEs the T result back. The
# body MOVE-captures owned heap (Vec/Str), so each task is single-owner and
# sound across the thread boundary (no auto-drop double-free).
#
# NO --memcheck here: the memcheck tracker is process-global and not yet
# thread-safe (the worker frees its closure env concurrently with the main
# thread), so it would race. Double-free soundness is covered instead by
# repeated AOT runs — a cross-thread double-free surfaces as a crash (~rc!=0).

cmake_minimum_required(VERSION 3.20)
set(LS  "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/task_test.ls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/task_test.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

# JIT
execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "task JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "TASK OK" OR jo MATCHES "TASK FAIL")
    message(FATAL_ERROR "task JIT: bad output:\n${jo}")
endif()

# AOT compile
execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "task AOT compile failed:\n${ce}")
endif()

# Run the AOT exe several times — move-capture single-ownership soundness: a
# cross-thread double-free of a worker's closure env would crash intermittently.
foreach(i RANGE 1 8)
    execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
    if(NOT ar EQUAL 0)
        message(FATAL_ERROR "task AOT run ${i} crashed (rc=${ar}) — possible cross-thread double-free:\n${ao}")
    endif()
    if(NOT ao MATCHES "TASK OK" OR ao MATCHES "TASK FAIL")
        message(FATAL_ERROR "task AOT run ${i}: bad output:\n${ao}")
    endif()
endforeach()

message(STATUS "task all passed")
