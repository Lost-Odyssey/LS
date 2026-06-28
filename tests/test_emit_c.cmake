# ls emit-c — C (Intel intrinsics) emitter for the numeric/SIMD kernel subset.
# Emits the coverage kernel, asserts the expected AVX-512 intrinsics appear,
# compiles the generated C with clang (AVX-512 feature flags) to prove it is
# valid immediately-compilable C, and verifies the out-of-subset reject path.

cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(SDIR "${CMAKE_CURRENT_LIST_DIR}/samples")
set(KERNEL "${SDIR}/emit_c_kernel.ls")
set(OUT "${CMAKE_BINARY_DIR}/emit_c_kernel.c")

# --- emit ---
execute_process(COMMAND "${LS}" emit-c "${KERNEL}" -o "${OUT}"
    OUTPUT_VARIABLE eo ERROR_VARIABLE ee RESULT_VARIABLE er TIMEOUT 30)
if(NOT er EQUAL 0)
    message(FATAL_ERROR "emit-c failed (rc=${er}):\n${ee}\n${eo}")
endif()
if(NOT EXISTS "${OUT}")
    message(FATAL_ERROR "emit-c produced no output file")
endif()

# --- substring assertions on the generated C ---
file(READ "${OUT}" CSRC)
foreach(needle
        "#include <immintrin.h>"
        "_mm512_fmadd_ps"
        "_mm512_maskz_loadu_ps"
        "_mm512_mask_storeu_ps"
        "_mm512_max_ps"
        "_mm512_reduce_add_ps"
        "_mm512_reduce_max_ps"
        "__m512 ")
    string(FIND "${CSRC}" "${needle}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "generated C missing expected text: ${needle}")
    endif()
endforeach()

# --- compile the generated C with clang (AVX-512), proving it is valid C ---
find_program(CLANG_EXE NAMES clang clang.exe)
if(CLANG_EXE)
    execute_process(COMMAND "${CLANG_EXE}"
        -mavx512f -mavx512vl -mavx512bw -mavx512dq -mfma -O3 -ffp-contract=fast
        -c "${OUT}" -o "${CMAKE_BINARY_DIR}/emit_c_kernel.o"
        OUTPUT_VARIABLE co ERROR_VARIABLE cce RESULT_VARIABLE ccr TIMEOUT 60)
    if(NOT ccr EQUAL 0)
        message(FATAL_ERROR "clang failed to compile generated C:\n${cce}\n${co}")
    endif()
    # confirm the compiler turned it into the peak AVX-512 kernel
    execute_process(COMMAND "${CLANG_EXE}"
        -mavx512f -mavx512vl -mavx512bw -mavx512dq -mfma -O3 -ffp-contract=fast
        -S "${OUT}" -o "${CMAKE_BINARY_DIR}/emit_c_kernel.s"
        RESULT_VARIABLE sr TIMEOUT 60)
    if(sr EQUAL 0)
        file(READ "${CMAKE_BINARY_DIR}/emit_c_kernel.s" ASM)
        string(FIND "${ASM}" "vfmadd231ps" fpos)
        if(fpos EQUAL -1)
            message(FATAL_ERROR "generated-then-compiled C has no vfmadd231ps (zmm FMA)")
        endif()
    endif()
else()
    message(STATUS "test_emit_c: clang not found, skipping compile-check")
endif()

# --- reject path: out-of-subset function must fail, write nothing ---
set(BAD "${SDIR}/emit_c_reject.ls")
set(BADOUT "${CMAKE_BINARY_DIR}/emit_c_reject_should_not_exist.c")
file(REMOVE "${BADOUT}")
execute_process(COMMAND "${LS}" emit-c "${BAD}" -o "${BADOUT}"
    OUTPUT_VARIABLE ro ERROR_VARIABLE re RESULT_VARIABLE rr TIMEOUT 30)
if(rr EQUAL 0)
    message(FATAL_ERROR "emit-c should have rejected the out-of-subset sample, but succeeded")
endif()
if(EXISTS "${BADOUT}")
    message(FATAL_ERROR "emit-c wrote an output file on the reject path")
endif()
if(NOT "${re}" MATCHES "emit-c")
    message(FATAL_ERROR "emit-c reject: missing diagnostic:\n${re}")
endif()

# --- selective emission: --skip-unsupported emits the kernel, skips the Str fn ---
set(SKIPOUT "${CMAKE_BINARY_DIR}/emit_c_skip.c")
execute_process(COMMAND "${LS}" emit-c "${BAD}" --skip-unsupported -o "${SKIPOUT}"
    OUTPUT_VARIABLE so2 ERROR_VARIABLE se2 RESULT_VARIABLE sr2 TIMEOUT 30)
if(NOT sr2 EQUAL 0)
    message(FATAL_ERROR "emit-c --skip-unsupported should succeed:\n${se2}")
endif()
file(READ "${SKIPOUT}" SKIPSRC)
string(FIND "${SKIPSRC}" "kernel(" kpos)
if(kpos EQUAL -1)
    message(FATAL_ERROR "--skip-unsupported did not emit the in-subset kernel")
endif()
string(FIND "${SKIPSRC}" "greet" gpos)
if(NOT gpos EQUAL -1)
    message(FATAL_ERROR "--skip-unsupported wrongly emitted the out-of-subset fn")
endif()

# --- --only kernel succeeds; --only on an out-of-subset fn errors ---
execute_process(COMMAND "${LS}" emit-c "${BAD}" --only kernel -o "${CMAKE_BINARY_DIR}/emit_c_only.c"
    RESULT_VARIABLE or1 TIMEOUT 30)
if(NOT or1 EQUAL 0)
    message(FATAL_ERROR "emit-c --only kernel should succeed (rc=${or1})")
endif()
execute_process(COMMAND "${LS}" emit-c "${BAD}" --only greet -o "${CMAKE_BINARY_DIR}/emit_c_only_bad.c"
    RESULT_VARIABLE or2 TIMEOUT 30)
if(or2 EQUAL 0)
    message(FATAL_ERROR "emit-c --only greet (out of subset) should fail")
endif()

# --- subset extension: POD structs + fixed arrays + pointer-based forward ---
set(STRUCTSRC "${SDIR}/emit_c_struct.ls")
set(STRUCTOUT "${CMAKE_BINARY_DIR}/emit_c_struct.c")
execute_process(COMMAND "${LS}" emit-c "${STRUCTSRC}" -o "${STRUCTOUT}"
    OUTPUT_VARIABLE sto ERROR_VARIABLE ste RESULT_VARIABLE str TIMEOUT 30)
if(NOT str EQUAL 0)
    message(FATAL_ERROR "emit-c struct sample failed (rc=${str}):\n${ste}\n${sto}")
endif()
file(READ "${STRUCTOUT}" STRUCTC)
foreach(needle
        "typedef struct Dense"
        "typedef struct Pt"
        "Dense* layer"
        "(layer)->nout"
        "float tmp[16]"
        "(Pt){")
    string(FIND "${STRUCTC}" "${needle}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "struct emit missing expected text: ${needle}")
    endif()
endforeach()
if(CLANG_EXE)
    execute_process(COMMAND "${CLANG_EXE}" -mavx512f -mfma -O2
        -c "${STRUCTOUT}" -o "${CMAKE_BINARY_DIR}/emit_c_struct.o"
        ERROR_VARIABLE sce RESULT_VARIABLE scr TIMEOUT 60)
    if(NOT scr EQUAL 0)
        message(FATAL_ERROR "clang failed to compile generated struct C:\n${sce}")
    endif()
endif()

message(STATUS "test_emit_c: ALL PASSED")
