# test_proc_env.cmake — Tests for stdlib/proc.ls and stdlib/env.ls
# Verifies JIT run + AOT compile+run for both modules.
# Required: LS_EXE, SAMPLE_DIR, WORK_DIR

cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE_DIR OR NOT WORK_DIR)
    message(FATAL_ERROR "test_proc_env.cmake requires LS_EXE, SAMPLE_DIR, WORK_DIR")
endif()

# Point LS_HOME at the project root so `import env` / `import proc` resolve
# to stdlib/env.ls and stdlib/proc.ls (ls.exe lives in build/Release/, which
# has no stdlib/ sub-directory of its own).
if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_ls_stdlib_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(ENV_SRC  "${SAMPLE_DIR}/env_test.ls")
set(PROC_SRC "${SAMPLE_DIR}/proc_test.ls")

if(WIN32 OR CMAKE_HOST_WIN32)
    set(ENV_EXE  "${WORK_DIR}/env_test.exe")
    set(PROC_EXE "${WORK_DIR}/proc_test.exe")
else()
    set(ENV_EXE  "${WORK_DIR}/env_test")
    set(PROC_EXE "${WORK_DIR}/proc_test")
endif()

# ── env JIT ───────────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS_EXE}" run "${ENV_SRC}"
    OUTPUT_VARIABLE env_jit_out
    ERROR_VARIABLE  env_jit_err
    RESULT_VARIABLE env_jit_rc
)
if(NOT env_jit_rc EQUAL 0)
    message(FATAL_ERROR "env JIT run failed (rc=${env_jit_rc}):\n${env_jit_err}\n${env_jit_out}")
endif()
if(NOT env_jit_out MATCHES "All env tests passed")
    message(FATAL_ERROR "env JIT: expected 'All env tests passed', got:\n${env_jit_out}")
endif()
message(STATUS "env JIT: OK")

# ── env AOT ───────────────────────────────────────────────────────────────────
file(REMOVE "${ENV_EXE}")
execute_process(
    COMMAND "${LS_EXE}" compile "${ENV_SRC}" -o "${ENV_EXE}"
    OUTPUT_VARIABLE env_compile_out
    ERROR_VARIABLE  env_compile_err
    RESULT_VARIABLE env_compile_rc
)
if(NOT env_compile_rc EQUAL 0)
    message(FATAL_ERROR "env AOT compile failed (rc=${env_compile_rc}):\n${env_compile_err}")
endif()
if(NOT EXISTS "${ENV_EXE}")
    message(FATAL_ERROR "env AOT compile reported success but ${ENV_EXE} not produced")
endif()

execute_process(
    COMMAND "${ENV_EXE}"
    OUTPUT_VARIABLE env_aot_out
    ERROR_VARIABLE  env_aot_err
    RESULT_VARIABLE env_aot_rc
)
if(NOT env_aot_rc EQUAL 0)
    message(FATAL_ERROR "env AOT run failed (rc=${env_aot_rc}):\n${env_aot_err}\n${env_aot_out}")
endif()
if(NOT env_aot_out MATCHES "All env tests passed")
    message(FATAL_ERROR "env AOT: expected 'All env tests passed', got:\n${env_aot_out}")
endif()
message(STATUS "env AOT: OK")
file(REMOVE "${ENV_EXE}")

# ── proc JIT ──────────────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS_EXE}" run "${PROC_SRC}"
    OUTPUT_VARIABLE proc_jit_out
    ERROR_VARIABLE  proc_jit_err
    RESULT_VARIABLE proc_jit_rc
)
if(NOT proc_jit_rc EQUAL 0)
    message(FATAL_ERROR "proc JIT run failed (rc=${proc_jit_rc}):\n${proc_jit_err}\n${proc_jit_out}")
endif()
if(NOT proc_jit_out MATCHES "All proc tests passed")
    message(FATAL_ERROR "proc JIT: expected 'All proc tests passed', got:\n${proc_jit_out}")
endif()
message(STATUS "proc JIT: OK")

# ── proc AOT ──────────────────────────────────────────────────────────────────
file(REMOVE "${PROC_EXE}")
execute_process(
    COMMAND "${LS_EXE}" compile "${PROC_SRC}" -o "${PROC_EXE}"
    OUTPUT_VARIABLE proc_compile_out
    ERROR_VARIABLE  proc_compile_err
    RESULT_VARIABLE proc_compile_rc
)
if(NOT proc_compile_rc EQUAL 0)
    message(FATAL_ERROR "proc AOT compile failed (rc=${proc_compile_rc}):\n${proc_compile_err}")
endif()
if(NOT EXISTS "${PROC_EXE}")
    message(FATAL_ERROR "proc AOT compile reported success but ${PROC_EXE} not produced")
endif()

execute_process(
    COMMAND "${PROC_EXE}"
    OUTPUT_VARIABLE proc_aot_out
    ERROR_VARIABLE  proc_aot_err
    RESULT_VARIABLE proc_aot_rc
)
if(NOT proc_aot_rc EQUAL 0)
    message(FATAL_ERROR "proc AOT run failed (rc=${proc_aot_rc}):\n${proc_aot_err}\n${proc_aot_out}")
endif()
if(NOT proc_aot_out MATCHES "All proc tests passed")
    message(FATAL_ERROR "proc AOT: expected 'All proc tests passed', got:\n${proc_aot_out}")
endif()
message(STATUS "proc AOT: OK")
file(REMOVE "${PROC_EXE}")

message(STATUS "All proc/env stdlib tests passed (JIT + AOT)")
