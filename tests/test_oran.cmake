# test_oran.cmake — oran.cus (O-RAN CUS-plane) library: parse + build + filter +
# stats + render. Runs every oran_* sample through JIT + AOT + memcheck (0/0/0).
# LS_HOME points at the source tree so ls.exe resolves lib/oran/cus.ls.
cmake_minimum_required(VERSION 3.20)

set(ENV{LS_HOME} "${REPO_DIR}")

set(SAMPLES
    "oran_p1_test.ls|ORAN P1 PASS"
    "oran_p2_bitcursor_test.ls|ORAN P2 BITCURSOR PASS"
    "oran_p3_st1_test.ls|ORAN P3 ST1 PASS"
    "oran_p4_st9_test.ls|ORAN P4 ST9 PASS"
    "oran_p4_multi_test.ls|ORAN P4 MULTI PASS"
    "oran_p4_stats_test.ls|ORAN P4 STATS PASS"
    "oran_p5_hexinput_test.ls|ORAN P5 HEXINPUT PASS"
    "oran_p5_render_test.ls|ORAN P5 RENDER PASS"
    "oran_p7_secext_test.ls|ORAN P7 SE PASS"
    "oran_p8_uplane_test.ls|ORAN P8 UPLANE PASS"
    "oran_modcomp_test.ls|ORAN MODCOMP PASS"
    "oran_p9_special_test.ls|ORAN SPECIAL PASS"
    "oran_b1_se_test.ls|ORAN B1 SE PASS"
    "oran_b2_resolve_test.ls|ORAN B2 RESOLVE PASS"
    "oran_b3_se1_test.ls|ORAN B3 SE1 PASS"
    "oran_b4_se11_test.ls|ORAN B4 SE11 PASS"
    "oran_pcap_test.ls|ORAN PCAP PASS"
    "oran_pcap_file_test.ls|ORAN PCAP FILE PASS"
    "oran_o1o2_test.ls|ORAN O1O2 PASS"
    "oran_analyze.ls|ORAN ANALYZE PASS"
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
        message(FATAL_ERROR "oran JIT ${fname} FAILED (rc=${rc})\n${o}\n${e}")
    endif()
    if(NOT "${o}" MATCHES "${expected}")
        message(FATAL_ERROR "oran JIT ${fname} missing '${expected}'\n${o}")
    endif()
    if("${o}" MATCHES "FAIL")
        message(FATAL_ERROR "oran JIT ${fname} had a FAIL line\n${o}")
    endif()

    # ---- AOT ----
    set(bin "${WORK_DIR}/${fname}_aot.exe")
    execute_process(COMMAND "${LS_EXE}" compile "${src}" -o "${bin}"
        RESULT_VARIABLE crc ERROR_VARIABLE ce)
    if(NOT crc EQUAL 0)
        message(FATAL_ERROR "oran AOT compile ${fname} FAILED\n${ce}")
    endif()
    execute_process(COMMAND "${bin}" OUTPUT_VARIABLE ao RESULT_VARIABLE arc)
    if(NOT arc EQUAL 0)
        message(FATAL_ERROR "oran AOT run ${fname} FAILED (rc=${arc})\n${ao}")
    endif()
    if(NOT "${ao}" MATCHES "${expected}")
        message(FATAL_ERROR "oran AOT ${fname} missing '${expected}'\n${ao}")
    endif()
    file(REMOVE "${bin}")

    # ---- memcheck (0 leak / 0 double-free / 0 invalid free) ----
    execute_process(COMMAND "${LS_EXE}" run --memcheck "${src}"
        OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mrc)
    if(NOT mrc EQUAL 0)
        message(FATAL_ERROR "oran memcheck ${fname} FAILED (rc=${mrc})\n${me}")
    endif()
    if(NOT "${me}" MATCHES "OK clean")
        message(FATAL_ERROR "oran memcheck ${fname} not clean\n${me}")
    endif()

    message(STATUS "oran ${fname}: JIT+AOT+memcheck OK")
endforeach()

message(STATUS "test_oran: ALL PASSED")
