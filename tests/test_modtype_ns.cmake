# test_modtype_ns.cmake — B-2 and B-3 module struct/enum namespace tests
# B-2: struct/enum Type name baked with module prefix; zero behavioral change
# B-3: impl method / drop / clone names follow module prefix

cmake_minimum_required(VERSION 3.20)

set(B2_DIR "${SAMPLE_DIR}/modtype_ns_b2")
set(B3_DIR "${SAMPLE_DIR}/modtype_ns_b3")

# ── B-2 Group: single-module struct/enum, behaviour unchanged ──────────────

# JIT run
execute_process(
    COMMAND "${LS_EXE}" run "${B2_DIR}/main_b2.lls"
    OUTPUT_VARIABLE B2_JIT_OUT
    RESULT_VARIABLE B2_JIT_RC
)
if(NOT B2_JIT_RC EQUAL 0)
    message(FATAL_ERROR "B-2 JIT failed (exit=${B2_JIT_RC}): ${B2_JIT_OUT}")
endif()
if(NOT B2_JIT_OUT MATCHES "sum=10")
    message(FATAL_ERROR "B-2 JIT wrong output: ${B2_JIT_OUT}")
endif()
if(NOT B2_JIT_OUT MATCHES "color=1")
    message(FATAL_ERROR "B-2 JIT enum wrong output: ${B2_JIT_OUT}")
endif()

# Memcheck
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${B2_DIR}/main_b2.lls"
    OUTPUT_VARIABLE B2_MC_OUT
    ERROR_VARIABLE  B2_MC_ERR
    RESULT_VARIABLE B2_MC_RC
)
if(NOT B2_MC_RC EQUAL 0)
    message(FATAL_ERROR "B-2 memcheck failed: ${B2_MC_OUT} ${B2_MC_ERR}")
endif()
if(NOT "${B2_MC_OUT}${B2_MC_ERR}" MATCHES "OK clean")
    message(FATAL_ERROR "B-2 memcheck not clean: out=${B2_MC_OUT} err=${B2_MC_ERR}")
endif()

# AOT compile + run
set(B2_AOT_EXE "${WORK_DIR}/b2_test_tmp.exe")
execute_process(
    COMMAND "${LS_EXE}" compile "${B2_DIR}/main_b2.lls" -o "${B2_AOT_EXE}"
    RESULT_VARIABLE B2_AOT_RC
)
if(NOT B2_AOT_RC EQUAL 0)
    message(FATAL_ERROR "B-2 AOT compile failed")
endif()
execute_process(
    COMMAND "${B2_AOT_EXE}"
    OUTPUT_VARIABLE B2_AOT_OUT
    RESULT_VARIABLE B2_AOT_RUN_RC
)
file(REMOVE "${B2_AOT_EXE}")
if(NOT B2_AOT_RUN_RC EQUAL 0)
    message(FATAL_ERROR "B-2 AOT run failed: ${B2_AOT_OUT}")
endif()
if(NOT B2_AOT_OUT MATCHES "sum=10")
    message(FATAL_ERROR "B-2 AOT wrong output: ${B2_AOT_OUT}")
endif()

# ── B-3 Group: single-module struct + methods + has_drop ──────────────────

# JIT run (Widget with methods)
execute_process(
    COMMAND "${LS_EXE}" run "${B3_DIR}/main_b3.lls"
    OUTPUT_VARIABLE B3_JIT_OUT
    RESULT_VARIABLE B3_JIT_RC
)
if(NOT B3_JIT_RC EQUAL 0)
    message(FATAL_ERROR "B-3 JIT failed: ${B3_JIT_OUT}")
endif()
if(NOT B3_JIT_OUT MATCHES "hello=42")
    message(FATAL_ERROR "B-3 JIT wrong output: ${B3_JIT_OUT}")
endif()
if(NOT B3_JIT_OUT MATCHES "world=200")
    message(FATAL_ERROR "B-3 JIT wrong output: ${B3_JIT_OUT}")
endif()
if(NOT B3_JIT_OUT MATCHES "drops=3")
    message(FATAL_ERROR "B-3 JIT drop count wrong: ${B3_JIT_OUT}")
endif()

# Memcheck (Widget)
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${B3_DIR}/main_b3.lls"
    OUTPUT_VARIABLE B3_MC_OUT
    ERROR_VARIABLE  B3_MC_ERR
    RESULT_VARIABLE B3_MC_RC
)
if(NOT B3_MC_RC EQUAL 0)
    message(FATAL_ERROR "B-3 memcheck failed: ${B3_MC_OUT} ${B3_MC_ERR}")
endif()
if(NOT "${B3_MC_OUT}${B3_MC_ERR}" MATCHES "OK clean")
    message(FATAL_ERROR "B-3 memcheck not clean: out=${B3_MC_OUT} err=${B3_MC_ERR}")
endif()

# AOT compile + run (Widget)
set(B3_AOT_EXE "${WORK_DIR}/b3_test_tmp.exe")
execute_process(
    COMMAND "${LS_EXE}" compile "${B3_DIR}/main_b3.lls" -o "${B3_AOT_EXE}"
    RESULT_VARIABLE B3_AOT_RC
)
if(NOT B3_AOT_RC EQUAL 0)
    message(FATAL_ERROR "B-3 AOT compile failed")
endif()
execute_process(
    COMMAND "${B3_AOT_EXE}"
    OUTPUT_VARIABLE B3_AOT_OUT
    RESULT_VARIABLE B3_AOT_RUN_RC
)
file(REMOVE "${B3_AOT_EXE}")
if(NOT B3_AOT_RUN_RC EQUAL 0)
    message(FATAL_ERROR "B-3 AOT run failed: ${B3_AOT_OUT}")
endif()
if(NOT B3_AOT_OUT MATCHES "hello=42")
    message(FATAL_ERROR "B-3 AOT wrong output: ${B3_AOT_OUT}")
endif()

# JIT run (user-defined __drop counter)
execute_process(
    COMMAND "${LS_EXE}" run "${B3_DIR}/main_b3_drop.lls"
    OUTPUT_VARIABLE B3D_JIT_OUT
    RESULT_VARIABLE B3D_JIT_RC
)
if(NOT B3D_JIT_RC EQUAL 0)
    message(FATAL_ERROR "B-3 drop JIT failed: ${B3D_JIT_OUT}")
endif()
if(NOT B3D_JIT_OUT MATCHES "drops=3")
    message(FATAL_ERROR "B-3 drop counter wrong (JIT): ${B3D_JIT_OUT}")
endif()
if(NOT B3D_JIT_OUT MATCHES "id=99")
    message(FATAL_ERROR "B-3 drop id wrong (JIT): ${B3D_JIT_OUT}")
endif()

# Memcheck (user-defined __drop)
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${B3_DIR}/main_b3_drop.lls"
    OUTPUT_VARIABLE B3D_MC_OUT
    ERROR_VARIABLE  B3D_MC_ERR
    RESULT_VARIABLE B3D_MC_RC
)
if(NOT B3D_MC_RC EQUAL 0)
    message(FATAL_ERROR "B-3 drop memcheck failed: ${B3D_MC_OUT} ${B3D_MC_ERR}")
endif()
if(NOT "${B3D_MC_OUT}${B3D_MC_ERR}" MATCHES "OK clean")
    message(FATAL_ERROR "B-3 drop memcheck not clean: out=${B3D_MC_OUT} err=${B3D_MC_ERR}")
endif()

# AOT compile + run (user-defined __drop)
set(B3D_AOT_EXE "${WORK_DIR}/b3d_test_tmp.exe")
execute_process(
    COMMAND "${LS_EXE}" compile "${B3_DIR}/main_b3_drop.lls" -o "${B3D_AOT_EXE}"
    RESULT_VARIABLE B3D_AOT_RC
)
if(NOT B3D_AOT_RC EQUAL 0)
    message(FATAL_ERROR "B-3 drop AOT compile failed")
endif()
execute_process(
    COMMAND "${B3D_AOT_EXE}"
    OUTPUT_VARIABLE B3D_AOT_OUT
    RESULT_VARIABLE B3D_AOT_RUN_RC
)
file(REMOVE "${B3D_AOT_EXE}")
if(NOT B3D_AOT_RUN_RC EQUAL 0)
    message(FATAL_ERROR "B-3 drop AOT run failed: ${B3D_AOT_OUT}")
endif()
if(NOT B3D_AOT_OUT MATCHES "drops=3")
    message(FATAL_ERROR "B-3 drop AOT counter wrong: ${B3D_AOT_OUT}")
endif()

message(STATUS "test_modtype_ns: B-2 and B-3 all passed")
