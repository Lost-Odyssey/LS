cmake_minimum_required(VERSION 3.20)
project(ls C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# LLVM — static linking
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(LLVM_LIBS
    core support irreader
    executionengine orcjit native
    passes target mc
    x86asmparser x86codegen x86desc x86info
)

# Compiler options
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /utf-8)
    # Don't treat warnings as errors for now (LLVM headers may trigger warnings)
    # add_compile_options(/WX)
    add_compile_definitions(CG_DEBUG=1)
else()
    add_compile_options(-Wall -Wextra -pedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-fsanitize=address -g -O0)
        add_link_options(-fsanitize=address)
    else()
        add_compile_options(-O2)
    endif()
endif()

# Main executable sources
set(LS_SOURCES
    src/main.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/jit.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)

add_executable(ls ${LS_SOURCES})
target_include_directories(ls PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(ls ${LLVM_LIBS})

# Windows: extra system libs needed for static LLVM linking
if(WIN32)
    target_link_libraries(ls
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    # Linux/macOS: dlopen/dlsym for FFI
    target_link_libraries(ls dl)
endif()

# Tests (Phase 1 & 2: no LLVM dependency)
enable_testing()

add_executable(test_scanner tests/test_scanner.c src/scanner.c)
target_include_directories(test_scanner PRIVATE src/ include/)
add_test(NAME test_scanner COMMAND test_scanner)

add_executable(test_parser
    tests/test_parser.c
    src/scanner.c
    src/ast.c
    src/parser.c
)
target_include_directories(test_parser PRIVATE src/ include/)
add_test(NAME test_parser COMMAND test_parser)

add_executable(test_types
    tests/test_types.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/module.c
    src/builtins_math.c
    src/builtins_io.c
)
target_include_directories(test_types PRIVATE src/ include/)
add_test(NAME test_types COMMAND test_types)

# Phase 4 test: codegen (needs LLVM)
add_executable(test_codegen
    tests/test_codegen.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)
target_include_directories(test_codegen PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_codegen ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_codegen
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_codegen dl)
endif()
add_test(NAME test_codegen COMMAND test_codegen)

# Phase 5 test: JIT (needs LLVM)
add_executable(test_jit
    tests/test_jit.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/jit.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)
target_include_directories(test_jit PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_jit ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_jit
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_jit dl)
endif()
add_test(NAME test_jit COMMAND test_jit)

# Phase 6 test: FFI (needs LLVM)
add_executable(test_ffi
    tests/test_ffi.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/jit.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)
target_include_directories(test_ffi PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_ffi ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_ffi
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_ffi dl)
endif()
add_test(NAME test_ffi COMMAND test_ffi)

# Phase 7 test: Module system (needs LLVM)
add_executable(test_module
    tests/test_module.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/jit.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)
target_include_directories(test_module PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_module ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_module
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_module dl)
endif()
add_test(NAME test_module COMMAND test_module)
set_tests_properties(test_module PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Memory management E2E tests (needs LLVM + JIT)
add_executable(test_memory
    tests/test_memory.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/types.c
    src/symtable.c
    src/checker.c
    src/codegen.c
    src/debug.c
    src/jit.c
    src/ffi.c
    src/module.c
    src/builtins_math.c
    src/builtins_math_cg.c
    src/builtins_io.c
    src/builtins_io_cg.c
    runtime/memcheck.c
)
target_include_directories(test_memory PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(test_memory ${LLVM_LIBS})
if(WIN32)
    target_link_libraries(test_memory
        Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32
    )
else()
    target_link_libraries(test_memory dl)
endif()
add_test(NAME test_memory COMMAND test_memory)
