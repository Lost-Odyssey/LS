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

## AOT Defender flake 与缓解（Windows）

全量 `ctest -j` 偶发**随机某个 AOT 测试空 stdout 失败、单独重跑即过**。判据：
失败集**每轮不同**、隔离重跑全绿 = flake，**非回归**。

- **真因（与输出丢失无关，那个是另一桩已根治的 CRT flush 问题，见 CLAUDE.md §3）**：
  Windows Defender 实时监控在 AOT 刚落盘 `.exe` 时短暂持锁，使紧随其后的
  compile/run/delete 偶发失败。
- **治本**：把构建目录加入 Defender 排除项（管理员 PowerShell，一次性）：

  ```powershell
  Add-MpPreference -ExclusionPath "C:\YANG\10003_language\LS\build"
  # 可选：连源树一起排除，覆盖 AOT 写到源树临时目录的少数测试
  Add-MpPreference -ExclusionPath "C:\YANG\10003_language\LS"
  ```

  查看/移除：

  ```powershell
  (Get-MpPreference).ExclusionPath
  Remove-MpPreference -ExclusionPath "C:\YANG\10003_language\LS\build"
  ```

- **兜底（无管理员权限时）**：对全量跑加 `--repeat until-pass:2`，让单测失败自动
  重跑一次：

  ```powershell
  cd build; ctest -j 5 -C Release --repeat until-pass:2
  ```

  注意 `until-pass` 会把**真回归**也重试一次——确认失败稳定（每轮同一测试）
  再判定为真 bug，勿用重试掩盖回归。
