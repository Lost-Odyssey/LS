# test_sim.cmake — sim (instruction-level microarch sim + advisor, lib/sim/).
# Runs each sim_* sample through JIT + AOT + memcheck (0/0/0).
# LS_HOME points at the source tree so ls.exe resolves lib/sim/*.lls.
cmake_minimum_required(VERSION 3.20)

set(ENV{LS_HOME} "${REPO_DIR}")

# sim_bfp8_html writes tmp/sim_bfp8_viz.html relative to cwd (= REPO_DIR via the
# add_test WORKING_DIRECTORY) — make sure the dir exists on a fresh checkout.
file(MAKE_DIRECTORY "${REPO_DIR}/tmp")

set(SAMPLES
    "sim_viz_test.lls|SIM VIZ PASS"
    "sim_engine_test.lls|SIM ENGINE PASS"
    "sim_advisor_test.lls|SIM ADVISOR PASS"
    "sim_uarch_test.lls|SIM UARCH PASS"
    "sim_pipeline_test.lls|SIM PIPELINE PASS"
    "sim_engine2_test.lls|SIM ENGINE2 PASS"
    "sim_catalog_test.lls|SIM CATALOG PASS"
    "sim_analysis_test.lls|SIM ANALYSIS PASS"
    "sim_frontend_test.lls|SIM FRONTEND PASS"
    "sim_cmul_advice_test.lls|SIM CMUL ADVICE PASS"
    "sim_report_test.lls|SIM REPORT PASS"
    "sim_licm_test.lls|SIM LICM PASS"
    "sim_earlyexit_test.lls|SIM EARLYEXIT PASS"
    "sim_mca_oracle_test.lls|SIM MCA ORACLE PASS"
    "sim_asmfile_test.lls|SIM ASMFILE PASS"
    "sim_branch_test.lls|SIM BRANCH PASS"
    "sim_batch_compare_test.lls|SIM BATCH COMPARE PASS"
    "sim_movement_test.lls|SIM MOVEMENT PASS"
    "sim_isa_table_test.lls|SIM ISA TABLE PASS"
    "sim_intrinsics_test.lls|SIM INTRINSICS PASS"
    "sim_movement_gallery_test.lls|SIM MOVEMENT GALLERY DONE"
    "sim_mask_track_test.lls|SIM MASK TRACK PASS"
    "sim_bfp8_walk.lls|BFP8 WALK DONE"
    "sim_bfp8_html.lls|SIM BFP8 HTML DONE"
    "sim_full_html.lls|SIM FULL HTML DONE"
)

foreach(entry ${SAMPLES})
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 fname)
    list(GET parts 1 expected)
    set(src "${SAMPLE_DIR}/${fname}")

    # ---- JIT ----
    execute_process(COMMAND "${LS_EXE}" run "${src}"
        OUTPUT_VARIABLE o RESULT_VARIABLE rc ERROR_VARIABLE e)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "sim JIT ${fname} FAILED (rc=${rc})\n${o}\n${e}")
    endif()
    if(NOT "${o}" MATCHES "${expected}")
        message(FATAL_ERROR "sim JIT ${fname} missing '${expected}'\n${o}")
    endif()
    if("${o}" MATCHES "FAIL")
        message(FATAL_ERROR "sim JIT ${fname} had a FAIL line\n${o}")
    endif()

    # ---- AOT ----
    set(bin "${WORK_DIR}/${fname}_aot.exe")
    execute_process(COMMAND "${LS_EXE}" compile "${src}" -o "${bin}"
        RESULT_VARIABLE crc ERROR_VARIABLE ce)
    if(NOT crc EQUAL 0)
        message(FATAL_ERROR "sim AOT compile ${fname} FAILED\n${ce}")
    endif()
    execute_process(COMMAND "${bin}" OUTPUT_VARIABLE ao RESULT_VARIABLE arc)
    if(NOT arc EQUAL 0)
        message(FATAL_ERROR "sim AOT run ${fname} FAILED (rc=${arc})\n${ao}")
    endif()
    if(NOT "${ao}" MATCHES "${expected}")
        message(FATAL_ERROR "sim AOT ${fname} missing '${expected}'\n${ao}")
    endif()
    if("${ao}" MATCHES "FAIL")
        message(FATAL_ERROR "sim AOT ${fname} had a FAIL line\n${ao}")
    endif()
    file(REMOVE "${bin}")

    # ---- memcheck (0 leak / 0 double-free / 0 invalid free) ----
    execute_process(COMMAND "${LS_EXE}" run --memcheck "${src}"
        OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mrc)
    if(NOT mrc EQUAL 0)
        message(FATAL_ERROR "sim memcheck ${fname} FAILED (rc=${mrc})\n${me}")
    endif()
    if(NOT "${me}" MATCHES "OK clean")
        message(FATAL_ERROR "sim memcheck ${fname} not clean\n${me}")
    endif()

    message(STATUS "sim ${fname}: JIT+AOT+memcheck OK")
endforeach()

message(STATUS "test_sim: ALL PASSED")
