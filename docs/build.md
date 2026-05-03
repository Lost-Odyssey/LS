# LS 构建配置说明

## CMakeLists.txt 要点

```cmake
cmake_minimum_required(VERSION 3.20)
project(ls C)
set(CMAKE_C_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# LLVM 静态链接
find_package(LLVM REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(LLVM_LIBS
    core support irreader executionengine orcjit native passes target mc
)

# MSVC: 静态 CRT，ls.exe 完全独立
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /WX /utf-8)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/fsanitize=address)
    endif()
else()
    add_compile_options(-Wall -Wextra -Werror -pedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-fsanitize=address -g -O0)
        add_link_options(-fsanitize=address)
    else()
        add_compile_options(-O2)
    endif()
endif()

file(GLOB_RECURSE SOURCES src/*.c runtime/*.c)
add_executable(ls ${SOURCES})
target_include_directories(ls PRIVATE ${LLVM_INCLUDE_DIRS} src/ include/)
target_link_libraries(ls ${LLVM_LIBS})

# Windows 额外系统库（静态链接 LLVM 需要）
if(WIN32)
    target_link_libraries(ls Shlwapi Version Ole32 Uuid Advapi32 Shell32 Ws2_32)
endif()

enable_testing()
# 各 test_*.c 注册为 CTest target
```

## 产出说明

- `ls.exe`：静态链接 LLVM + 静态 CRT（/MT），约 50-150MB，可独立分发
- AOT 产出的用户可执行文件：完全独立，不含 LLVM 代码

## 启用 CG_DEBUG

```powershell
cmake -B build -G Ninja -DCG_DEBUG=1 -DCMAKE_BUILD_TYPE=Debug ...
```

或在 `src/common.h` 中临时修改 `#define CG_DEBUG 1`。
