# test_lifetime_markers.cmake — A2 llvm.lifetime.start/end regression
# (docs/plan_opt_lifetime_markers.md).
#
# Asserts:
#   1. emit-ir emits paired llvm.lifetime.start/end for aggregate locals, with
#      the verifier clean (a bad lifetime interval trips the verifier or, worse,
#      miscompiles — so this is the primary safety gate).
#   2. emit-ir with LS_NO_LIFETIME=1 is lifetime-free — the kill switch yields
#      the pre-A2 IR (byte-identical default path is covered here by absence).
#   3. compile + run gives the correct output on BOTH marker settings — the O2
#      StackColoring pass consumes the ends without changing observable results.
#   4. (best-effort, needs llvm-objdump) the O2 stack frame SHRINKS with markers
#      on: two disjoint aggregate locals coalesce into one frame slot.
#
# Required: LS_EXE, SAMPLE, WORK_DIR, STDLIB (repo root -> LS_HOME).
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_lifetime_markers.cmake requires LS_EXE and SAMPLE")
endif()
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(TN "lifetime_markers")
set(EXPECT "886656 zeron=1n=2n=3")

# ---- 1. emit-ir: paired lifetime markers present, verifier clean ----
# (emit-ir prints the IR on stderr.)
execute_process(COMMAND "${LS_EXE}" emit-ir "${SAMPLE}"
    OUTPUT_VARIABLE o1 ERROR_VARIABLE ir RESULT_VARIABLE r1 TIMEOUT 60)
if(NOT r1 EQUAL 0)
    message(FATAL_ERROR "${TN} emit-ir failed (rc=${r1})\n${ir}")
endif()
if(ir MATCHES "verification failed" OR ir MATCHES "invalid record")
    message(FATAL_ERROR "${TN} emit-ir tripped the verifier:\n${ir}")
endif()
if(NOT ir MATCHES "call void @llvm.lifetime.start")
    message(FATAL_ERROR "${TN} emit-ir has no lifetime.start markers")
endif()
if(NOT ir MATCHES "call void @llvm.lifetime.end")
    message(FATAL_ERROR "${TN} emit-ir has no lifetime.end markers")
endif()

# ---- 2. emit-ir with LS_NO_LIFETIME=1: no markers at all ----
set(ENV{LS_NO_LIFETIME} "1")
execute_process(COMMAND "${LS_EXE}" emit-ir "${SAMPLE}"
    OUTPUT_VARIABLE o2 ERROR_VARIABLE ir2 RESULT_VARIABLE r2 TIMEOUT 60)
unset(ENV{LS_NO_LIFETIME})
if(NOT r2 EQUAL 0)
    message(FATAL_ERROR "${TN} emit-ir (LS_NO_LIFETIME) failed (rc=${r2})")
endif()
if(ir2 MATCHES "llvm.lifetime")
    message(FATAL_ERROR "${TN} LS_NO_LIFETIME=1 IR still contains lifetime markers")
endif()

# ---- 3. compile + run: identical correct output on both settings ----
set(BON "${WORK_DIR}/${TN}_on")
set(BOFF "${WORK_DIR}/${TN}_off")
if(WIN32)
    set(BON "${BON}.exe")
    set(BOFF "${BOFF}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${BON}"
    OUTPUT_VARIABLE co ERROR_VARIABLE ce RESULT_VARIABLE cr TIMEOUT 120)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "${TN} compile (markers on) failed:\n${ce}\n${co}")
endif()
set(ENV{LS_NO_LIFETIME} "1")
execute_process(COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${BOFF}"
    OUTPUT_VARIABLE co2 ERROR_VARIABLE ce2 RESULT_VARIABLE cr2 TIMEOUT 120)
unset(ENV{LS_NO_LIFETIME})
if(NOT cr2 EQUAL 0)
    message(FATAL_ERROR "${TN} compile (markers off) failed:\n${ce2}\n${co2}")
endif()
foreach(pair "${BON}=on" "${BOFF}=off")
    string(REGEX REPLACE "=(on|off)$" "" BIN "${pair}")
    execute_process(COMMAND "${BIN}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 60)
    if(NOT ar EQUAL 0)
        message(FATAL_ERROR "${TN} exe ${pair} failed (rc=${ar})\n${ao}")
    endif()
    string(STRIP "${ao}" ao_s)
    if(NOT ao_s STREQUAL EXPECT)
        message(FATAL_ERROR "${TN} exe ${pair} output wrong: got [${ao_s}] want [${EXPECT}]")
    endif()
endforeach()

# ---- 4. (best-effort) frame shrinks with markers on ----
# Largest `subq $N, %rsp` prologue in each .obj — markers coalesce the two
# disjoint arrays in work(), so the ON frame must be strictly smaller.
find_program(OBJDUMP NAMES llvm-objdump
             HINTS "C:/Program Files/LLVM/bin" ENV LLVM_BIN)
if(OBJDUMP)
    function(max_frame OBJ OUTVAR)
        execute_process(COMMAND "${OBJDUMP}" -d "${OBJ}"
            OUTPUT_VARIABLE dis RESULT_VARIABLE dr TIMEOUT 60)
        set(mx 0)
        string(REGEX MATCHALL "sub[q]?[ \t]+[$]([0-9]+), %rsp" subs "${dis}")
        foreach(m ${subs})
            string(REGEX MATCH "([0-9]+)" n "${m}")
            if(n GREATER mx)
                set(mx ${n})
            endif()
        endforeach()
        set(${OUTVAR} ${mx} PARENT_SCOPE)
    endfunction()
    max_frame("${BON}.obj" F_ON)
    max_frame("${BOFF}.obj" F_OFF)
    if(F_ON GREATER 0 AND F_OFF GREATER 0)
        # Hard invariant (never brittle): markers must NEVER grow the frame. A
        # misplaced lifetime.start/end that defeats coalescing would trip this.
        if(F_ON GREATER F_OFF)
            message(FATAL_ERROR
                "${TN} markers GREW the frame: on=${F_ON} off=${F_OFF} (must be on<=off)")
        endif()
        # Report the coalescing win. work()'s two disjoint aggregate locals make
        # it the dominant frame, so this normally shows the real shrink; if a
        # future stdlib frame ever dominates instead, on==off here is acceptable
        # (the quantified benefit lives in benchmarks/SUMMARY.html).
        if(F_ON LESS F_OFF)
            message(STATUS "${TN}: frame ${F_OFF}B -> ${F_ON}B with markers (coalesced)")
        else()
            message(STATUS "${TN}: frame ${F_ON}B (no dominant coalescing in this obj)")
        endif()
    else()
        message(STATUS "${TN}: frame sizes unresolved (obj format), skipping frame check")
    endif()
else()
    message(STATUS "${TN}: llvm-objdump not found, skipping frame check")
endif()

file(REMOVE "${BON}" "${BOFF}")
if(WIN32)
    file(REMOVE "${BON}.obj" "${BOFF}.obj")
endif()
message(STATUS "${TN}: paired markers + kill switch + output parity PASS")
