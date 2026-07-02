# test_debug_info.cmake — D1 -g line-table debug info regression
# (docs/plan_debug_info.md phase 1).
#
# debug_info_test.lls covers the three subprogram sources (root free fn,
# generic method instance from an imported module, closure literal) plus
# f-string lowering (synthesised line-0 statements — the sticky-location
# discipline's regression spot). Asserts:
#   1. emit-ir -g produces DICompileUnit + subprograms + !dbg locations and
#      the module DIFile maps the generic instance into vec.lls, with the
#      verifier clean (its "inlinable call needs !dbg" check is a hard error).
#   2. emit-ir without -g stays DI-free (default path zero-disturbance).
#   3. compile -g links a PDB next to the exe (Windows) and the exe runs
#      with correct output.
#
# Required: LS_EXE, SAMPLE, WORK_DIR, STDLIB (repo root → LS_HOME).
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_debug_info.cmake requires LS_EXE and SAMPLE")
endif()
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(TN "debug_info")

# ---- 1. emit-ir -g: DI skeleton + locations present, verifier clean ----
# (emit-ir prints the IR on stderr)
execute_process(COMMAND "${LS_EXE}" emit-ir -g "${SAMPLE}"
    OUTPUT_VARIABLE go ERROR_VARIABLE gir RESULT_VARIABLE gr TIMEOUT 60)
if(NOT gr EQUAL 0)
    message(FATAL_ERROR "${TN} emit-ir -g failed (rc=${gr})\n${gir}")
endif()
if(gir MATCHES "verification failed" OR gir MATCHES "invalid debug info")
    message(FATAL_ERROR "${TN} emit-ir -g broke the verifier:\n${gir}")
endif()
foreach(want "DICompileUnit" "emissionKind: LineTablesOnly"
             "DISubprogram\\(name: \"main\"" "DISubprogram\\(name: \"add_all\""
             "DISubprogram\\(name: \"__closure_" "!dbg" "vec.lls")
    if(NOT gir MATCHES "${want}")
        message(FATAL_ERROR "${TN} emit-ir -g IR is missing '${want}'")
    endif()
endforeach()
if(WIN32 AND NOT gir MATCHES "\"CodeView\"")
    message(FATAL_ERROR "${TN} emit-ir -g IR is missing the CodeView module flag")
endif()

# ---- 2. emit-ir without -g: default pipeline must stay DI-free ----
execute_process(COMMAND "${LS_EXE}" emit-ir "${SAMPLE}"
    OUTPUT_VARIABLE no ERROR_VARIABLE nir RESULT_VARIABLE nr TIMEOUT 60)
if(NOT nr EQUAL 0)
    message(FATAL_ERROR "${TN} emit-ir (no -g) failed (rc=${nr})")
endif()
if(nir MATCHES "DICompileUnit" OR nir MATCHES "!dbg")
    message(FATAL_ERROR "${TN} default (no -g) IR unexpectedly contains debug info")
endif()

# ---- 3. compile -g: PDB emitted, exe runs correctly ----
set(BIN "${WORK_DIR}/${TN}_aot")
if(WIN32)
    set(BIN "${BIN}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile -g "${SAMPLE}" -o "${BIN}"
    OUTPUT_VARIABLE co ERROR_VARIABLE ce RESULT_VARIABLE cr TIMEOUT 120)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "${TN} compile -g failed:\n${ce}\n${co}")
endif()
if(WIN32)
    string(REGEX REPLACE "\\.exe$" ".pdb" PDB "${BIN}")
    if(NOT EXISTS "${PDB}")
        message(FATAL_ERROR "${TN} compile -g produced no PDB at ${PDB}")
    endif()
endif()
execute_process(COMMAND "${BIN}"
    OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 60)
if(NOT ar EQUAL 0)
    message(FATAL_ERROR "${TN} -g exe failed (rc=${ar})\n${ao}")
endif()
if(NOT ao MATCHES "sum=6 dbl=42")
    message(FATAL_ERROR "${TN} -g exe output wrong:\n${ao}")
endif()
file(REMOVE "${BIN}")
if(WIN32)
    file(REMOVE "${PDB}" "${BIN}.obj")
endif()

message(STATUS "${TN}: -g DI skeleton + line locations + PDB link PASS")
