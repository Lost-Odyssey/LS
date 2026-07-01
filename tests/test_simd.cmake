# Simd(T, N) — portable SIMD vector type (Phase 1a): the type + core intrinsics
# (__simd_splat/zero/lane/fma/reduce_add) + elementwise operators (+ - * /),
# lowering to <N x T> LLVM IR. Single-threaded correctness; Simd is POD so
# --memcheck is 0/0/0. JIT + memcheck + AOT, like test_atomic.

cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(SDIR "${CMAKE_CURRENT_LIST_DIR}/samples")
set(ST "${SDIR}/simd_test.lls")

# JIT
execute_process(COMMAND "${LS}" run "${ST}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(NOT sr EQUAL 0)
    message(FATAL_ERROR "simd JIT failed (rc=${sr}):\n${se}\n${so}")
endif()
if(NOT so MATCHES "SIMD OK" OR so MATCHES "SIMD FAIL")
    message(FATAL_ERROR "simd JIT: bad output:\n${so}")
endif()

# memcheck (POD, must be 0/0/0)
execute_process(COMMAND "${LS}" run --memcheck "${ST}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0)
    message(FATAL_ERROR "simd memcheck run failed (rc=${mr}):\n${me}")
endif()
if(NOT "${me}" MATCHES "OK clean")
    message(FATAL_ERROR "simd --memcheck not clean:\n${me}")
endif()

# AOT
set(ST_EXE "${CMAKE_BINARY_DIR}/simd_test.exe")
execute_process(COMMAND "${LS}" compile "${ST}" -o "${ST_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "simd AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${ST_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "SIMD OK" OR ao MATCHES "SIMD FAIL")
    message(FATAL_ERROR "simd AOT run: rc=${ar} output:\n${ao}")
endif()

# ===== Phase 2: f16 element type + __simd_cast (mixed precision) =====
set(FT "${SDIR}/simd_f16_test.lls")

execute_process(COMMAND "${LS}" run "${FT}"
    OUTPUT_VARIABLE fo ERROR_VARIABLE fe RESULT_VARIABLE fr TIMEOUT 30)
if(NOT fr EQUAL 0 OR NOT fo MATCHES "F16 OK" OR fo MATCHES "F16 FAIL")
    message(FATAL_ERROR "simd f16 JIT: rc=${fr}\n${fe}\n${fo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${FT}"
    OUTPUT_VARIABLE fmo ERROR_VARIABLE fme RESULT_VARIABLE fmr TIMEOUT 30)
if(NOT fmr EQUAL 0 OR NOT "${fme}" MATCHES "OK clean")
    message(FATAL_ERROR "simd f16 --memcheck not clean:\n${fme}")
endif()

set(FT_EXE "${CMAKE_BINARY_DIR}/simd_f16_test.exe")
execute_process(COMMAND "${LS}" compile "${FT}" -o "${FT_EXE}"
    RESULT_VARIABLE fcr ERROR_VARIABLE fce TIMEOUT 30)
if(NOT fcr EQUAL 0)
    message(FATAL_ERROR "simd f16 AOT compile failed:\n${fce}")
endif()
execute_process(COMMAND "${FT_EXE}" OUTPUT_VARIABLE fao RESULT_VARIABLE far TIMEOUT 30)
if(NOT far EQUAL 0 OR NOT fao MATCHES "F16 OK" OR fao MATCHES "F16 FAIL")
    message(FATAL_ERROR "simd f16 AOT run: rc=${far} output:\n${fao}")
endif()

# ===== std.sci.simd: vectorized activations (tanh/sigmoid/silu/relu) =====
set(AT "${SDIR}/simd_activation_test.lls")

execute_process(COMMAND "${LS}" run "${AT}"
    OUTPUT_VARIABLE aco ERROR_VARIABLE ace RESULT_VARIABLE acr TIMEOUT 30)
if(NOT acr EQUAL 0 OR NOT aco MATCHES "ACT OK" OR aco MATCHES "ACT FAIL")
    message(FATAL_ERROR "simd activation JIT: rc=${acr}\n${ace}\n${aco}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${AT}"
    OUTPUT_VARIABLE acmo ERROR_VARIABLE acme RESULT_VARIABLE acmr TIMEOUT 30)
if(NOT acmr EQUAL 0 OR NOT "${acme}" MATCHES "OK clean")
    message(FATAL_ERROR "simd activation --memcheck not clean:\n${acme}")
endif()

set(AT_EXE "${CMAKE_BINARY_DIR}/simd_activation_test.exe")
execute_process(COMMAND "${LS}" compile "${AT}" -o "${AT_EXE}"
    RESULT_VARIABLE accr ERROR_VARIABLE acce TIMEOUT 30)
if(NOT accr EQUAL 0)
    message(FATAL_ERROR "simd activation AOT compile failed:\n${acce}")
endif()
execute_process(COMMAND "${AT_EXE}" OUTPUT_VARIABLE acao RESULT_VARIABLE acar TIMEOUT 30)
if(NOT acar EQUAL 0 OR NOT acao MATCHES "ACT OK" OR acao MATCHES "ACT FAIL")
    message(FATAL_ERROR "simd activation AOT run: rc=${acar} output:\n${acao}")
endif()

# ===== std.sci.simd: vectorized exp (__simd_floor/__simd_bitcast) + atan =====
set(ET "${SDIR}/simd_exp_atan_test.lls")

execute_process(COMMAND "${LS}" run "${ET}"
    OUTPUT_VARIABLE eco ERROR_VARIABLE ece RESULT_VARIABLE ecr TIMEOUT 30)
if(NOT ecr EQUAL 0 OR NOT eco MATCHES "EXPATAN OK" OR eco MATCHES "EXPATAN FAIL")
    message(FATAL_ERROR "simd exp/atan JIT: rc=${ecr}\n${ece}\n${eco}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${ET}"
    OUTPUT_VARIABLE ecmo ERROR_VARIABLE ecme RESULT_VARIABLE ecmr TIMEOUT 30)
if(NOT ecmr EQUAL 0 OR NOT "${ecme}" MATCHES "OK clean")
    message(FATAL_ERROR "simd exp/atan --memcheck not clean:\n${ecme}")
endif()

set(ET_EXE "${CMAKE_BINARY_DIR}/simd_exp_atan_test.exe")
execute_process(COMMAND "${LS}" compile "${ET}" -o "${ET_EXE}"
    RESULT_VARIABLE eccr ERROR_VARIABLE ecce TIMEOUT 30)
if(NOT eccr EQUAL 0)
    message(FATAL_ERROR "simd exp/atan AOT compile failed:\n${ecce}")
endif()
execute_process(COMMAND "${ET_EXE}" OUTPUT_VARIABLE ecao RESULT_VARIABLE ecar TIMEOUT 30)
if(NOT ecar EQUAL 0 OR NOT ecao MATCHES "EXPATAN OK" OR ecao MATCHES "EXPATAN FAIL")
    message(FATAL_ERROR "simd exp/atan AOT run: rc=${ecar} output:\n${ecao}")
endif()

# ===== std.sci.nn: vectorized softmax_rows (smd.exp) =====
set(ST "${SDIR}/nn_softmax_test.lls")

execute_process(COMMAND "${LS}" run "${ST}"
    OUTPUT_VARIABLE sco ERROR_VARIABLE sce RESULT_VARIABLE scr TIMEOUT 30)
if(NOT scr EQUAL 0 OR NOT sco MATCHES "SOFTMAX OK" OR sco MATCHES "SOFTMAX FAIL")
    message(FATAL_ERROR "nn softmax JIT: rc=${scr}\n${sce}\n${sco}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${ST}"
    OUTPUT_VARIABLE scmo ERROR_VARIABLE scme RESULT_VARIABLE scmr TIMEOUT 30)
if(NOT scmr EQUAL 0 OR NOT "${scme}" MATCHES "OK clean")
    message(FATAL_ERROR "nn softmax --memcheck not clean:\n${scme}")
endif()

set(ST_EXE "${CMAKE_BINARY_DIR}/nn_softmax_test.exe")
execute_process(COMMAND "${LS}" compile "${ST}" -o "${ST_EXE}"
    RESULT_VARIABLE sccr ERROR_VARIABLE scce TIMEOUT 30)
if(NOT sccr EQUAL 0)
    message(FATAL_ERROR "nn softmax AOT compile failed:\n${scce}")
endif()
execute_process(COMMAND "${ST_EXE}" OUTPUT_VARIABLE scao RESULT_VARIABLE scar TIMEOUT 30)
if(NOT scar EQUAL 0 OR NOT scao MATCHES "SOFTMAX OK" OR scao MATCHES "SOFTMAX FAIL")
    message(FATAL_ERROR "nn softmax AOT run: rc=${scar} output:\n${scao}")
endif()

# ===== std.sci.nn: blocked FP32 GEMM (register-resident 6x16 micro-kernel) =====
set(GT "${SDIR}/simd_gemm_test.lls")

execute_process(COMMAND "${LS}" run "${GT}"
    OUTPUT_VARIABLE gco ERROR_VARIABLE gce RESULT_VARIABLE gcr TIMEOUT 60)
if(NOT gcr EQUAL 0 OR NOT gco MATCHES "GEMM OK" OR gco MATCHES "GEMM FAIL")
    message(FATAL_ERROR "simd gemm JIT: rc=${gcr}\n${gce}\n${gco}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${GT}"
    OUTPUT_VARIABLE gmo ERROR_VARIABLE gme RESULT_VARIABLE gmr TIMEOUT 60)
if(NOT gmr EQUAL 0 OR NOT "${gme}" MATCHES "OK clean")
    message(FATAL_ERROR "simd gemm --memcheck not clean:\n${gme}")
endif()

set(GT_EXE "${CMAKE_BINARY_DIR}/simd_gemm_test.exe")
execute_process(COMMAND "${LS}" compile "${GT}" -o "${GT_EXE}"
    RESULT_VARIABLE gccr ERROR_VARIABLE gcce TIMEOUT 60)
if(NOT gccr EQUAL 0)
    message(FATAL_ERROR "simd gemm AOT compile failed:\n${gcce}")
endif()
execute_process(COMMAND "${GT_EXE}" OUTPUT_VARIABLE gao RESULT_VARIABLE gar TIMEOUT 60)
if(NOT gar EQUAL 0 OR NOT gao MATCHES "GEMM OK" OR gao MATCHES "GEMM FAIL")
    message(FATAL_ERROR "simd gemm AOT run: rc=${gar} output:\n${gao}")
endif()

# ===== std.sci.nn.Pool: static f32 activation arena (64-byte aligned) =====
set(PT "${SDIR}/simd_pool_test.lls")

execute_process(COMMAND "${LS}" run "${PT}"
    OUTPUT_VARIABLE po ERROR_VARIABLE pe RESULT_VARIABLE pr TIMEOUT 30)
if(NOT pr EQUAL 0 OR NOT po MATCHES "POOL OK" OR po MATCHES "POOL FAIL")
    message(FATAL_ERROR "simd pool JIT: rc=${pr}\n${pe}\n${po}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${PT}"
    OUTPUT_VARIABLE pmo ERROR_VARIABLE pme RESULT_VARIABLE pmr TIMEOUT 30)
if(NOT pmr EQUAL 0 OR NOT "${pme}" MATCHES "OK clean")
    message(FATAL_ERROR "simd pool --memcheck not clean:\n${pme}")
endif()

set(PT_EXE "${CMAKE_BINARY_DIR}/simd_pool_test.exe")
execute_process(COMMAND "${LS}" compile "${PT}" -o "${PT_EXE}"
    RESULT_VARIABLE pcr ERROR_VARIABLE pce TIMEOUT 30)
if(NOT pcr EQUAL 0)
    message(FATAL_ERROR "simd pool AOT compile failed:\n${pce}")
endif()
execute_process(COMMAND "${PT_EXE}" OUTPUT_VARIABLE pao RESULT_VARIABLE par TIMEOUT 30)
if(NOT par EQUAL 0 OR NOT pao MATCHES "POOL OK" OR pao MATCHES "POOL FAIL")
    message(FATAL_ERROR "simd pool AOT run: rc=${par} output:\n${pao}")
endif()

# overflow must abort (non-zero, never reaches the trailing print)
execute_process(COMMAND "${LS}" run "${SDIR}/simd_pool_oob.lls"
    OUTPUT_VARIABLE oo ERROR_VARIABLE oe RESULT_VARIABLE oor TIMEOUT 30)
if(oor EQUAL 0 OR oo MATCHES "UNREACHABLE")
    message(FATAL_ERROR "simd pool overflow should abort, got rc=${oor}:\n${oo}")
endif()

# ===== std.sci.nn.conv1d (im2col + sgemm) + end-to-end conv->relu->conv =====
set(CT "${SDIR}/simd_conv_test.lls")

execute_process(COMMAND "${LS}" run "${CT}"
    OUTPUT_VARIABLE cvo ERROR_VARIABLE cve RESULT_VARIABLE cvr TIMEOUT 30)
if(NOT cvr EQUAL 0 OR NOT cvo MATCHES "CONV OK" OR cvo MATCHES "CONV FAIL")
    message(FATAL_ERROR "simd conv JIT: rc=${cvr}\n${cve}\n${cvo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${CT}"
    OUTPUT_VARIABLE cvmo ERROR_VARIABLE cvme RESULT_VARIABLE cvmr TIMEOUT 30)
if(NOT cvmr EQUAL 0 OR NOT "${cvme}" MATCHES "OK clean")
    message(FATAL_ERROR "simd conv --memcheck not clean:\n${cvme}")
endif()

set(CT_EXE "${CMAKE_BINARY_DIR}/simd_conv_test.exe")
execute_process(COMMAND "${LS}" compile "${CT}" -o "${CT_EXE}"
    RESULT_VARIABLE cvcr ERROR_VARIABLE cvce TIMEOUT 30)
if(NOT cvcr EQUAL 0)
    message(FATAL_ERROR "simd conv AOT compile failed:\n${cvce}")
endif()
execute_process(COMMAND "${CT_EXE}" OUTPUT_VARIABLE cvao RESULT_VARIABLE cvar TIMEOUT 30)
if(NOT cvar EQUAL 0 OR NOT cvao MATCHES "CONV OK" OR cvao MATCHES "CONV FAIL")
    message(FATAL_ERROR "simd conv AOT run: rc=${cvar} output:\n${cvao}")
endif()

message(STATUS "test_simd: ALL PASSED")
