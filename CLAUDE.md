# LS (LLVM Script) — Claude Code 实现说明书

> **用途**：将此文档作为 Claude Code 的 CLAUDE.md 项目上下文。
> 每个 Phase 是一个独立开发迭代，按顺序执行，每个 Phase 结束后必须通过验收标准。

## 0. 重要交互规则

- **永远用中文回答用户**，无论用户用什么语言提问。

---

## 1. 项目概述

用 C 语言实现一门通用编程语言 **LS** (LLVM Script)，语法为 C + Ruby 混合风格，
使用 LLVM 作为后端，支持 AOT 编译和 JIT 增量编译两种执行模式。

### 1.1 核心设计决策（不可更改）

| 维度 | 决策 |
|------|------|
| 实现语言 | C17 (MSVC on Windows，也兼容 GCC/Clang) |
| 构建系统 | CMake >= 3.20 + Visual Studio 17 2022 生成器 (或 Ninja) |
| 目标平台 | Windows 10 x64（首要），Linux/macOS 兼容为次要目标 |
| 后端 | LLVM 18 C API（不用自己写 VM） |
| LLVM 链接方式 | **静态链接**（ls.exe 为单文件独立可执行，不依赖 LLVM DLL） |
| 类型系统 | 静态类型 + 显式标注 |
| 内存管理 | 手动 malloc/free，无 GC |
| JIT 引擎 | 第一阶段 LLJIT，后期迁移 ORC v2 |
| JIT 用途 | 增量编译（只重编译改动的函数） |
| C FFI | Ruby 风格动态加载 shared library (Windows: LoadLibrary/GetProcAddress) |
| 外部依赖 | 仅 LLVM（不引入其他第三方库） |

### 1.2 语法风格概述

LS 的语法是 **C 的结构 + 现代语言的表达力**：
- 大括号 `{}` 界定块，分号 `;` **可选**（写或不写均可）
- 变量声明采用 C 风格，类型前置：`int x = 10;`（无 `let` 关键字）
- 函数用 `fn` + C 风格参数列表：`fn add(int a, int b) -> int { ... }`，无返回值时省略 `-> type`（默认 void）
- `struct` 定义复合类型，C 风格字段声明，支持方法绑定（`impl` 块，`self` 隐式传递，`static fn` 声明静态方法）
- `match` 表达式做模式匹配（同 Rust 的 match）
- `module` / `import` 做模块组织
- 闭包用匿名 `fn` 语法：`fn(int x) -> int { x * 2 }`（与函数定义风格一致）
- `print()` 是编译器内建函数，支持任意可打印类型（int, float, bool, string, pointer, object），多参数以空格分隔
- 格式化字符串 `f"text {expr} text"`：类似 Python/Rust 的字符串插值，`{}` 中可放任意表达式
- `object` 类型：类型擦除指针（等价于 C 的 `void*`），用于通用容器、FFI、回调上下文等场景
- `array(T, N)` 固定大小数组：支持索引读写、`.length`、`for-in` 迭代、函数参数、`print` 输出
- 全局变量：顶层声明的变量可在所有函数中读写，跨模块可通过 `module.var` 访问
- 方法链和尾随块在后续阶段考虑

### 1.3 语法速览示例

```
// 变量声明（C 风格，类型前置，分号可选）
int x = 42
string name = "hello"
*int ptr = malloc(sizeof(int))

// 函数定义（fn 关键字 + C 风格参数，无返回值省略 -> type）
fn factorial(int n) -> int {
    match n {
        0 => 1,
        _ => n * factorial(n - 1),
    }
}

// 无返回值函数（默认 void）
fn greet(string name) {
    print(f"Hello, {name}!")
}

// print 支持任意类型和多参数
print(42)                           // 42
print(3.14)                         // 3.140000
print(true)                         // true
print("hello", 42, 3.14)           // hello 42 3.140000

// 格式化字符串（f-string）
int age = 30
print(f"Age: {age}, double: {age * 2}")  // Age: 30, double: 60
string msg = f"result = {1 + 2}"         // 可赋值给变量

// string 操作（内部为 LsString struct { data, len, cap }）
string s = "hello"
int len = s.length                      // 5（O(1) 访问）
string full = "hello" + " world"        // 拼接（malloc 新串）
bool eq = ("abc" == "abc")              // true（值比较，非指针比较）
bool empty = s.empty()                  // false
int ch = s.at(0)                        // 104 (ASCII 'h')
int pos = s.find("llo")                 // 2（未找到返回 -1）
bool has = s.contains("ell")            // true
bool sw = s.starts_with("hel")         // true
bool ew = s.ends_with("llo")           // true
int cmp = s.compare("world")           // < 0（字典序比较）

// struct（C 风格字段声明）+ 方法
struct Point {
    f64 x;
    f64 y;
}

impl Point {
    // 实例方法 — self 隐式注入为 *Point 指针，无需声明
    fn distance(Point other) -> f64 {
        f64 dx = self.x - other.x
        f64 dy = self.y - other.y
        sqrt(dx * dx + dy * dy)
    }

    // 静态方法 — 显式 static 关键字，无 self
    static fn origin() -> Point {
        Point p
        p.x = 0.0
        p.y = 0.0
        return p
    }
}

// 方法调用
f64 d = p.distance(p2)          // 实例方法：自动传 &p 作为 self
Point o = Point.origin()         // 静态方法：通过类型名调用
Point o2 = p.origin()            // 静态方法：也可通过实例调用（忽略 obj）

// C 风格 for 循环
int sum = 0
for (int i = 0; i < 10; i = i + 1) {
    sum = sum + i
}
// 无限循环
for (;;) { break }

// foreach 循环 — 范围迭代
for i in 0..10 {
    print(i)            // 0, 1, 2, ..., 9
}
// 整数简写: for i in n 等价于 for i in 0..n
for i in 5 { print(i) }

// 固定大小数组（array 关键字）
array(int, 3) nums = [10, 20, 30]
int first = nums[0]                // 索引访问
nums[1] = 99                       // 索引写入
int len = nums.length              // 3（编译期常量）
for x in nums { print(x) }        // 数组迭代
print(nums)                        // [10, 99, 30]

// 全局变量（顶层声明，可在函数中读写）
int counter = 0
fn increment() { counter = counter + 1 }

// 闭包（匿名 fn）
fn(int) -> int double = fn(int x) -> int { x * 2 }

// 模块（支持导出函数和变量）
module math
import std.io

// C FFI — 动态库加载
lib msvcrt = load("msvcrt.dll")           // Windows
// lib libc = load("libc.so.6")           // Linux

// 方式 1：extern fn 声明（有类型检查）
extern fn puts(string s) -> int from msvcrt
puts("hello from FFI")

// 方式 2：动态调用（unsafe，不检查类型）
msvcrt.call("puts", "hello dynamic")

// object — 类型擦除指针（等价于 void*）
*int p = malloc(sizeof(int))
object obj = p              // *T → object 隐式转换
*int q = obj as *int        // object → *T 需要显式 as 转换

// 手动内存管理
*u8 buf = malloc(1024)
// ... use buf ...
free(buf)

// struct 的自动内存管理（析构函数）
struct Person {
    string name;
    int age;
}

// 自动生成 __drop：如果 struct 含有 string 字段或嵌套的有析构函数的 struct，
// 编译器会自动生成析构函数，在变量退出作用域时释放资源
Person p
p.name = "Alice"
p.name = p.name.upper()  // 旧值自动释放
// p 退出作用域时，p.name 的内存会自动释放

// 用户自定义 __drop：可以在 impl 块中定义析构函数
impl Person {
    fn __drop() {
        print("Person is being dropped")
        // 注意：string 字段的释放由编译器自动处理，无需手动释放
    }
}
```

---

## 2. 项目目录结构

```
ls/
├── CMakeLists.txt              # 顶层构建配置
├── CLAUDE.md                   # ← 本文件，Claude Code 的项目上下文
├── src/
│   ├── main.c                  # 入口：CLI 解析、REPL、文件编译
│   ├── common.h                # 公共类型、宏、错误码
│   │
│   ├── scanner.h / scanner.c   # 词法分析器
│   ├── token.h                 # Token 类型枚举 + Token 结构体
│   │
│   ├── ast.h / ast.c           # AST 节点定义 + 构造/销毁函数
│   ├── parser.h / parser.c     # Pratt Parser，产出 AST
│   │
│   ├── types.h / types.c       # 类型表示 + 类型检查器
│   ├── symtable.h / symtable.c # 符号表（作用域链）
│   │
│   ├── codegen.h / codegen.c   # AST → LLVM IR 生成
│   ├── jit.h / jit.c           # LLJIT 封装（增量编译）
│   │
│   ├── ffi.h / ffi.c           # C FFI：dlopen/dlsym 封装
│   ├── module.h / module.c     # 模块系统：路径解析、import
│   │
│   └── debug.h / debug.c       # LLVM IR 打印、诊断工具
│
├── include/
│   └── ls.h                    # 公共头文件（如作为库使用）
│
├── runtime/
│   └── builtins.c              # 内建函数：sizeof、类型转换（print 已改为编译器内建 intrinsic）
│
├── tests/
│   ├── test_scanner.c          # Scanner 单元测试
│   ├── test_parser.c           # Parser 单元测试
│   ├── test_types.c            # 类型系统 + 数组/全局变量类型检查 单元测试
│   ├── test_codegen.c          # Codegen 单元测试（含数组、全局变量 IR 验证）
│   ├── test_jit.c              # JIT 集成测试
│   ├── test_ffi.c              # FFI 集成测试（运行时加载 + codegen 验证）
│   ├── test_module.c           # 模块系统测试（含跨模块变量导入导出）
│   └── samples/                # .ls 端到端测试文件
│       ├── hello.ls
│       ├── print_types.ls          # 多类型 print 测试
│       ├── fstring_test.ls         # 格式化字符串测试
│       ├── fstring_var.ls          # f-string 赋值给变量
│       ├── for_loop_test.ls        # C 风格 for 循环测试
│       ├── foreach_test.ls         # foreach (for-in + range) 测试
│       ├── object_test.ls          # object 类型全面测试
│       ├── array_test.ls           # fixed-size array 全面测试
│       ├── global_var_test.ls      # 全局可变变量跨函数读写测试
│       ├── ffi_test.ls             # FFI 基础测试（extern fn + lib.call）
│       ├── ffi_win32.ls            # Windows DLL 调用测试（kernel32/msvcrt）
│       └── module_test/
│           ├── main.ls
│           ├── math.ls
│           └── constants.ls        # 导出全局变量 + 函数的模块
│
└── docs/
    ├── grammar.ebnf            # 形式化语法定义
    └── types.md                # 类型系统文档
```

---

## 3. 构建配置

### CMakeLists.txt 要求

```cmake
cmake_minimum_required(VERSION 3.20)
project(ls C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# LLVM — 静态链接，ls.exe 为独立可执行文件
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(LLVM_LIBS
    core support irreader executionengine orcjit native
    passes target mc
)

# 关键：静态链接 LLVM，ls.exe 不依赖任何 LLVM DLL
# vovkos/llvm-package-windows 的 libcmt 版本提供 .lib 静态库
# 链接后 ls.exe 约 50-150MB，但可以独立分发运行

# 编译选项（跨平台）
if(MSVC)
    # MSVC: 静态 CRT 链接（/MT），确保 ls.exe 完全独立
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

# 主可执行文件
file(GLOB_RECURSE SOURCES src/*.c runtime/*.c)
add_executable(ls ${SOURCES})
target_include_directories(ls PRIVATE
    ${LLVM_INCLUDE_DIRS} src/ include/
)
target_link_libraries(ls ${LLVM_LIBS})

# Windows 特有：静态链接 LLVM 时需要额外链接这些系统库
if(WIN32)
    target_link_libraries(ls
        Shlwapi     # 路径处理
        Version     # LLVM 内部使用
        Ole32       # COM (LLVM 内部)
        Uuid        # COM GUIDs
        Advapi32    # 注册表/安全 API
        Shell32     # Shell API
        Ws2_32      # Winsock (LLVM 网络相关)
    )
endif()

# 产出说明：
# - ls.exe 静态链接 LLVM + 静态 CRT（/MT），约 50-150MB
# - 可以拷贝到任何 Windows 10 x64 机器上直接运行，无需安装任何依赖
# - AOT 编译产出的用户程序（如 hello.exe）更小，完全不含 LLVM 代码

# 测试
enable_testing()
# ... 每个 test_*.c 注册为 CTest
```

### 编译 & 运行命令

```powershell
# ===== Windows (Developer PowerShell for VS 2022) =====

# 构建（Visual Studio 生成器）
cmake -B build -G "Visual Studio 17 2022" -A x64 -DLLVM_DIR="C:\llvm\lib\cmake\llvm"
cmake --build build --config Release

# 构建（Ninja，更快）
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR="C:\llvm\lib\cmake\llvm"
cmake --build build

# AOT 编译
.\build\Release\ls.exe compile input.ls -o output.exe

# JIT / REPL
.\build\Release\ls.exe run input.ls
.\build\Release\ls.exe repl

# 运行测试
cd build; ctest --output-on-failure -C Release
```

```bash
# ===== Linux / macOS =====

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/ls compile input.ls -o output
./build/ls run input.ls
./build/ls repl

cd build && ctest --output-on-failure
```

---

## 4. Phase 实现计划

---

### Phase 1: 项目骨架 + Scanner

**目标**：搭建完整构建系统，实现词法分析器，能将源码切分为 Token 流。

#### 4.1.1 Token 类型定义 (token.h)

```c
typedef enum {
    // 字面量
    TOKEN_INT_LIT,          // 42, 0xFF, 0b1010
    TOKEN_FLOAT_LIT,        // 3.14, 1.0e-5
    TOKEN_STRING_LIT,       // "hello"
    TOKEN_CHAR_LIT,         // 'a'
    TOKEN_TRUE,             // true
    TOKEN_FALSE,            // false
    TOKEN_NIL,              // nil

    // 关键字
    TOKEN_FN,               // fn
    TOKEN_RETURN,           // return
    TOKEN_IF,               // if
    TOKEN_ELSE,             // else
    TOKEN_WHILE,            // while
    TOKEN_FOR,              // for
    TOKEN_IN,               // in
    TOKEN_MATCH,            // match
    TOKEN_STRUCT,           // struct
    TOKEN_IMPL,             // impl
    TOKEN_MODULE,           // module
    TOKEN_IMPORT,           // import
    TOKEN_LOAD,             // load  (FFI)
    TOKEN_SELF,             // self
    TOKEN_DO,               // do    (预留)
    TOKEN_END,              // end   (预留)
    TOKEN_BREAK,            // break
    TOKEN_CONTINUE,         // continue

    // 类型关键字
    TOKEN_TYPE_INT,         // int
    TOKEN_TYPE_I8,          // i8
    TOKEN_TYPE_I16,         // i16
    TOKEN_TYPE_I32,         // i32
    TOKEN_TYPE_I64,         // i64
    TOKEN_TYPE_U8,          // u8
    TOKEN_TYPE_U16,         // u16
    TOKEN_TYPE_U32,         // u32
    TOKEN_TYPE_U64,         // u64
    TOKEN_TYPE_F32,         // f32
    TOKEN_TYPE_F64,         // f64
    TOKEN_TYPE_BOOL,        // bool
    TOKEN_TYPE_STRING,      // string
    TOKEN_TYPE_VOID,        // void
    TOKEN_TYPE_OBJECT,      // object (类型擦除指针, 等价于 void*)

    // 标识符
    TOKEN_IDENTIFIER,       // foo, bar_baz

    // 运算符
    TOKEN_PLUS,             // +
    TOKEN_MINUS,            // -
    TOKEN_STAR,             // *
    TOKEN_SLASH,            // /
    TOKEN_PERCENT,          // %
    TOKEN_AMP,              // &
    TOKEN_PIPE,             // |
    TOKEN_CARET,            // ^
    TOKEN_TILDE,            // ~
    TOKEN_LSHIFT,           // <<
    TOKEN_RSHIFT,           // >>
    TOKEN_AND,              // &&
    TOKEN_OR,               // ||
    TOKEN_BANG,             // !
    TOKEN_EQ,               // ==
    TOKEN_NEQ,              // !=
    TOKEN_LT,               // <
    TOKEN_GT,               // >
    TOKEN_LEQ,              // <=
    TOKEN_GEQ,              // >=

    // 赋值
    TOKEN_ASSIGN,           // =
    TOKEN_PLUS_ASSIGN,      // +=
    TOKEN_MINUS_ASSIGN,     // -=
    TOKEN_STAR_ASSIGN,      // *=
    TOKEN_SLASH_ASSIGN,     // /=

    // 界定符
    TOKEN_LPAREN,           // (
    TOKEN_RPAREN,           // )
    TOKEN_LBRACE,           // {
    TOKEN_RBRACE,           // }
    TOKEN_LBRACKET,         // [
    TOKEN_RBRACKET,         // ]
    TOKEN_SEMICOLON,        // ;
    TOKEN_COLON,            // :
    TOKEN_COMMA,            // ,
    TOKEN_DOT,              // .
    TOKEN_ARROW,            // ->
    TOKEN_FAT_ARROW,        // =>
    TOKEN_UNDERSCORE,       // _  (wildcard in match)

    // 格式化字符串 f"...{expr}..."
    TOKEN_FSTRING_START,    // f" — 格式化字符串开始
    TOKEN_FSTRING_TEXT,     // {} 之间的文本段
    TOKEN_FSTRING_END,      // 格式化字符串的结尾 "

    // 特殊
    TOKEN_EOF,
    TOKEN_ERROR,
} TokenType;

typedef struct {
    TokenType type;
    const char *start;     // 指向源码中的起始位置
    int length;
    int line;
    int column;
} Token;
```

#### 4.1.2 Scanner 接口 (scanner.h)

```c
typedef struct {
    const char *source;    // 完整源码
    const char *start;     // 当前 token 起始
    const char *current;   // 当前扫描位置
    int line;
    int column;
    int start_column;      // Token 起始列号
    bool in_fstring;       // 是否在 f"..." 内部扫描
    int fstring_brace_depth; // f-string 表达式中 {} 嵌套深度
} Scanner;

void scanner_init(Scanner *s, const char *source);
Token scanner_next(Scanner *s);      // 返回下一个 token
Token scanner_peek(Scanner *s);      // 预看不消费
```

#### 4.1.3 Scanner 实现要点

- 跳过空白和注释（`//` 单行，`/* */` 多行）
- 数字字面量支持十进制、十六进制 `0x`、二进制 `0b`、浮点 `1.5e-3`
- 字符串支持转义：`\n \t \\ \" \0 \xHH`
- 关键字用完美哈希或简单 trie 查找，不要 `strcmp` 链
- Token 不复制源码，只存 `(start, length)` 切片
- 遇到非法字符返回 `TOKEN_ERROR`，附带行号列号
- 格式化字符串 `f"..."` 扫描：遇到 `f"` 时进入 f-string 模式，交替产出 `TOKEN_FSTRING_TEXT`（文本段）和普通表达式 token（`{expr}` 中的内容），用 `fstring_brace_depth` 追踪嵌套

#### 4.1.4 验收标准

- [ ] `cmake --build build` 零警告通过
- [ ] `test_scanner` 覆盖：所有关键字、所有运算符、数字字面量（含 hex/bin/float）、字符串转义、注释跳过、错误 token
- [ ] 能扫描 `samples/hello.ls` 并打印完整 token 流
- [ ] AddressSanitizer 无报错

---

### Phase 2: AST + Parser

**目标**：实现 Pratt Parser，将 Token 流解析为 AST。

#### 4.2.1 AST 节点类型 (ast.h)

使用 tagged union 风格。每个节点是 `AstNode` 结构体，包含 `AstNodeType` 枚举 + union。

核心节点类型：

```c
typedef enum {
    // 字面量
    AST_INT_LIT, AST_FLOAT_LIT, AST_STRING_LIT,
    AST_BOOL_LIT, AST_NIL_LIT,
    AST_FORMAT_STRING,  // f"text {expr} text" — 格式化字符串

    // 表达式
    AST_IDENT,          // 变量引用
    AST_UNARY,          // !x, -x, *x, &x
    AST_BINARY,         // a + b, a && b
    AST_CALL,           // foo(a, b)
    AST_INDEX,          // arr[i]
    AST_FIELD,          // obj.field
    AST_CLOSURE,        // fn(int x) -> int { body }
    AST_MATCH,          // match expr { ... }
    AST_CAST,           // expr as type

    // 语句
    AST_VAR_DECL,       // int x = expr
    AST_ASSIGN,         // x = expr;
    AST_RETURN,         // return expr;
    AST_IF,             // if (cond) { } else { }
    AST_WHILE,          // while (cond) { }
    AST_FOR,            // for (x in iter) { }
    AST_BLOCK,          // { stmts... }
    AST_EXPR_STMT,      // expr;

    // 声明
    AST_FN_DECL,        // fn name(type param, ...) -> ret { body }
    AST_STRUCT_DECL,    // struct Name { fields }
    AST_IMPL_DECL,      // impl Name { methods }
    AST_MODULE_DECL,    // module name;
    AST_IMPORT,         // import path;

    // FFI
    AST_LOAD_LIB,       // load("lib.so")
    AST_FFI_CALL,       // lib.call(:fn, args...)
} AstNodeType;
```

#### 4.2.2 Parser 接口 (parser.h)

```c
typedef struct {
    Scanner *scanner;
    Token current;
    Token previous;
    bool had_error;
    bool panic_mode;       // 错误恢复用
} Parser;

AstNode *parse(const char *source);  // 入口：源码 → AST
void ast_free(AstNode *node);        // 递归释放 AST
void ast_print(AstNode *node, int indent);  // debug 打印
```

#### 4.2.3 Pratt Parser 实现要点

- 核心是一张 `ParseRule` 表，映射 `TokenType` → `{prefix_fn, infix_fn, precedence}`
- 优先级从低到高：ASSIGNMENT < OR < AND < EQUALITY < COMPARISON < BITWISE < SHIFT < TERM < FACTOR < UNARY < CALL < PRIMARY
- `prefix_fn` 处理前缀表达式（字面量、一元运算符、分组 `(`、匿名 `fn` 闭包）
- `infix_fn` 处理中缀表达式（二元运算符、函数调用 `(`、字段访问 `.`、下标 `[`）
- 语句解析不走 Pratt 表，用常规递归下降
- **分号可选**：语句结尾的 `;` 可以写也可以省略，Parser 遇到 `;` 就消费，遇到换行/`}` 等边界则自动结束当前语句
- **变量声明 vs 函数定义**：都以类型关键字或标识符开头，Parser 需要前瞻 — 若类型后跟标识符再跟 `(`，识别为函数调用表达式；若类型后跟标识符再跟 `=` 或语句结束，识别为变量声明
- 错误恢复：进入 panic 模式后同步到下一个语句边界（`;` 或 `}` 或关键字）

#### 4.2.4 类型标注语法解析

类型出现在变量声明、函数参数、返回类型中：

```
type := "int" | "i8" | ... | "f64" | "bool" | "string" | "void" | "object"
      | "*" type              // 指针
      | "[" type "]"          // 数组/切片
      | "fn" "(" types ")" "->" type   // 函数类型
      | IDENTIFIER            // 用户定义的 struct 类型
```

变量声明语法（C 风格类型前置）：
```
var_decl := type IDENTIFIER [ "=" expr ]
// 例：int x = 42
// 例：*f64 ptr = null
```

函数声明语法：
```
fn_decl := "fn" IDENTIFIER "(" param_list ")" [ "->" type ] block
param   := type IDENTIFIER
// 例：fn add(int a, int b) -> int { ... }
// 例：fn greet(string name) { ... }   // 默认 void 返回
```

闭包（匿名 fn）语法：
```
closure := "fn" "(" param_list ")" [ "->" type ] block
// 例：fn(int x) -> int { x * 2 }
// 例：fn() { print("hello") }
```

解析类型时递归处理指针和函数类型，存入 `TypeNode` 结构。

#### 4.2.5 验收标准

- [ ] 能解析 `samples/` 下所有测试文件为 AST
- [ ] `ast_print` 输出可读的缩进树
- [ ] `test_parser` 覆盖：表达式优先级、变量声明/fn/struct/impl/match/import、闭包（匿名 fn）、FFI 语法、分号可选
- [ ] 语法错误有清晰的行号列号报告，格式：`error [line:col]: message`
- [ ] 错误恢复后能继续解析后续语句（不在第一个错误就终止）

---

### Phase 3: 类型系统 + 符号表

**目标**：实现类型检查器，遍历 AST 做类型校验和标注。

#### 4.3.1 类型表示 (types.h)

```c
typedef enum {
    TYPE_INT, TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64,
    TYPE_F32, TYPE_F64,
    TYPE_BOOL, TYPE_STRING, TYPE_VOID, TYPE_NIL,
    TYPE_POINTER,       // *T
    TYPE_ARRAY,         // [T]
    TYPE_FUNCTION,      // fn(A, B) -> R
    TYPE_STRUCT,        // struct { ... }
    TYPE_OBJECT,        // object — 类型擦除指针 (void*)
    TYPE_MODULE,        // module reference
    TYPE_LIB,           // FFI library handle
} TypeKind;

typedef struct Type {
    TypeKind kind;
    union {
        struct Type *pointer_to;                  // TYPE_POINTER
        struct { struct Type *elem; } array;      // TYPE_ARRAY
        struct {                                  // TYPE_FUNCTION
            struct Type **params;
            int param_count;
            struct Type *return_type;
        } function;
        struct {                                  // TYPE_STRUCT
            const char *name;
            struct { const char *name; struct Type *type; } *fields;
            int field_count;
        } strukt;
    };
} Type;
```

#### 4.3.2 符号表 (symtable.h)

```c
typedef struct Symbol {
    const char *name;
    Type *type;
    bool is_mutable;      // 暂时所有变量都可变，后续可加 const
    int scope_depth;
    LLVMValueRef llvm_value;  // codegen 阶段回填
} Symbol;

typedef struct Scope {
    Symbol *symbols;       // 动态数组
    int count;
    int capacity;
    struct Scope *parent;  // 父作用域
} Scope;

Scope *scope_new(Scope *parent);
void scope_free(Scope *scope);
Symbol *scope_define(Scope *s, const char *name, Type *type);
Symbol *scope_resolve(Scope *s, const char *name);  // 沿链向上查找
```

#### 4.3.3 类型检查器要点

- 遍历 AST，对每个节点推导并检查类型
- 二元运算符检查：两侧类型必须兼容，不允许隐式转换（需要显式 `as`）
- 函数调用检查：参数数量和类型必须精确匹配（`print` 特殊处理：接受任意可打印类型，至少 1 个参数）
- 格式化字符串 `AST_FORMAT_STRING`：对每个 `{expr}` 中的表达式进行类型检查，要求是可打印类型（numeric、bool、string、pointer），整体类型推导为 `string`
- struct 字段访问检查：字段必须存在，类型正确
- match 表达式：所有分支返回类型必须一致
- 指针运算：只有 `*ptr` 解引用和 `&val` 取地址合法
- **object 类型转换规则**：
  - `*T` → `object`：隐式转换（赋值、传参时自动允许）
  - `object` → `*T`：必须显式 `as` 转换（编译器不检查运行时安全性，用户负责）
  - 值类型（int, bool, f64 等）**不能**直接赋给 `object`（需先取地址 `&val`）
  - `object` 可与 `nil` 比较（`==` / `!=`）
  - `object` 可传给 `print()`，输出地址
- 检查通过后，在 AST 节点上标注推导出的 `Type *`

#### 4.3.4 验收标准

- [ ] `test_codegen` 中的类型错误用例全部被正确拒绝
- [ ] 类型错误信息清晰：`type error [line:col]: expected 'int', got 'f64'`
- [ ] 所有合法程序通过检查后，每个表达式节点都有 `.resolved_type != NULL`
- [ ] 嵌套作用域正确遮蔽外层变量

---

### Phase 4: LLVM IR 代码生成 (AOT)

**目标**：遍历类型检查后的 AST，生成 LLVM IR，通过 LLVM 编译为原生可执行文件。

#### 4.4.1 Codegen 上下文 (codegen.h)

```c
typedef struct {
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMTargetMachineRef target_machine;

    Scope *current_scope;         // 当前符号作用域
    LLVMValueRef current_fn;      // 当前正在编译的函数
    LLVMBasicBlockRef break_bb;   // break 跳转目标
    LLVMBasicBlockRef continue_bb; // continue 跳转目标
} CodegenContext;

void codegen_init(CodegenContext *ctx);
void codegen_destroy(CodegenContext *ctx);
int codegen_compile(CodegenContext *ctx, AstNode *ast);  // AST → IR
int codegen_emit_object(CodegenContext *ctx, const char *output_path);  // IR → .o
int codegen_emit_executable(CodegenContext *ctx, const char *output_path); // link
void codegen_dump_ir(CodegenContext *ctx);  // 打印 LLVM IR（调试用）
```

#### 4.4.2 类型映射 LS → LLVM

```
int, i32   → LLVMInt32Type()
i8         → LLVMInt8Type()
i16        → LLVMInt16Type()
i64        → LLVMInt64Type()
u8..u64    → 同上（LLVM 不区分有无符号，在指令层面区分）
f32        → LLVMFloatType()
f64        → LLVMDoubleType()
bool       → LLVMInt1Type()
void       → LLVMVoidType()
*T         → LLVMPointerType(map(T), 0)
string     → LLVMStructType({ i8*, i32, i32 })     // LsString { data, len, cap }
object     → LLVMPointerType(0)                 // opaque ptr (等同于 void*)
struct     → LLVMStructType(fields...)
fn(A)->R   → LLVMFunctionType(R, [A...])
```

#### 4.4.3 各节点 Codegen 策略

- **变量声明**：`LLVMBuildAlloca` 在栈上分配，`LLVMBuildStore` 初始化
- **变量引用**：`LLVMBuildLoad` 从 alloca 加载
- **二元运算**：整数用 `LLVMBuildAdd/Sub/Mul/SDiv`，浮点用 `LLVMBuildFAdd/FSub/FMul/FDiv`，比较用 `LLVMBuildICmp/FCmp`
- **函数定义**：`LLVMAddFunction` + 为每个参数创建 alloca 并 store
- **函数调用**：`LLVMBuildCall2`（`print` 除外，见下方 intrinsic 说明）
- **if/else**：创建 `then_bb / else_bb / merge_bb`，`LLVMBuildCondBr`
- **while**：创建 `cond_bb / body_bb / end_bb`，`LLVMBuildBr` 循环跳转
- **match**：展开为级联 `if/else`（第一阶段不做跳转表优化）
- **struct**：`LLVMStructType` + `LLVMBuildStructGEP` 访问字段
- **闭包**：匿名 `fn` 的捕获变量打包到一个匿名 struct，函数额外接收该 struct 指针作为首参数
- **malloc/free**：直接调用 C runtime 的 `malloc`/`free`（声明为 external function）
- **string**：内部表示为 `LsString` struct `{ i8* data, i32 len, i32 cap }`。字面量为静态（cap=0, data 指向全局常量），动态创建的字符串 cap >= `LS_MIN_STR_CAP`(16)。传给 C FFI 函数时自动提取 `.data` 指针。支持 `+` 拼接（malloc + memcpy）、`==`/`!=` 值比较（strcmp）、`.length` 属性（O(1)）
- **print()**：编译器内建函数（intrinsic），不生成独立的 `print` 函数定义。在 codegen 的 `AST_CALL` 阶段拦截，根据每个参数的 `resolved_type` 选择 printf 格式符（int→`%d`、f64→`%f`、bool→`%s` "true"/"false"、string→`%s`、pointer→`%p`），展开为一次 `printf` 调用。多参数以空格分隔，自动追加 `\n`
- **f-string（格式化字符串）**：`AST_FORMAT_STRING` 节点包含文本段 `parts[]` 和表达式 `exprs[]`。在 `print(f"...")` 中直接展开为 printf 调用；赋值给变量时使用 `sprintf` + 栈缓冲区生成字符串。Windows AOT 链接时需 `-llegacy_stdio_definitions`

#### 4.4.4 AOT 编译流程

```
源码 → Scanner → Parser → AST → TypeChecker → Codegen → LLVM IR
  → LLVMRunPassManager (优化 O2)
  → LLVMTargetMachineEmitToFile (.obj 目标文件)
  → 调用系统链接器 (Windows: link.exe / Linux: cc) → 可执行文件

产出的可执行文件完全独立，不依赖 LLVM，不依赖 ls。
```

#### 4.4.5 验收标准

- [ ] Windows: `ls compile hello.ls -o hello.exe && hello.exe` 输出 "Hello, World!"
- [ ] Linux: `ls compile hello.ls -o hello && ./hello` 输出 "Hello, World!"
- [ ] `factorial.ls` 正确计算阶乘
- [ ] `struct_test.ls` 正确创建 struct、访问字段、调用方法
- [ ] `match_test.ls` 正确执行模式匹配
- [ ] `codegen_dump_ir` 输出合法 LLVM IR（可通过 `llvm-as` 验证）
- [ ] AOT 产出的 exe 可在未安装 LLVM 和 ls 的机器上独立运行

---

### Phase 5: LLJIT 增量编译

**目标**：集成 LLVM LLJIT，支持增量编译——只重编译修改过的函数。

#### 4.5.1 JIT 引擎接口 (jit.h)

```c
typedef struct {
    LLVMOrcLLJITRef jit;
    LLVMOrcJITDylibRef main_dylib;
    LLVMOrcThreadSafeContextRef ts_context;

    // 函数版本追踪（用于增量编译）
    struct { const char *name; uint64_t hash; } *fn_registry;
    int fn_count;
} JitEngine;

int jit_init(JitEngine *engine);
void jit_destroy(JitEngine *engine);

// 增量编译一个模块（新增/更新的函数）
int jit_add_module(JitEngine *engine, LLVMModuleRef module);

// 查找函数地址并执行
void *jit_lookup(JitEngine *engine, const char *name);

// 检查函数是否需要重编译（基于 AST 哈希）
bool jit_needs_recompile(JitEngine *engine, const char *name, uint64_t new_hash);
```

#### 4.5.2 增量编译策略

1. **函数级粒度**：每个函数独立生成一个 LLVM Module
2. **变更检测**：对每个函数的 AST 子树计算哈希值，与上次编译的哈希对比
3. **热替换**：变更的函数重新 codegen 到新 Module，通过 `jit_add_module` 注入
4. **符号覆盖**：LLJIT 的 JITDylib 支持符号替换，新版本自动覆盖旧版本
5. **依赖传播**：如果函数签名改变（不只是实现），所有调用者也需要重编译

#### 4.5.3 执行流程

```
文件变更 → 重新 scan + parse 变更的文件
  → 对每个函数计算 AST hash
  → 对比 fn_registry，找出变更的函数
  → 只对变更函数执行 typecheck + codegen
  → jit_add_module 注入新 Module
  → jit_lookup("main") 执行
```

#### 4.5.4 REPL 模式（基于 JIT）

```
ls> int x = 42
ls> fn double(int n) -> int { n * 2 }
ls> print(double(x))
84
```

REPL 每行/每块作为一个增量 Module 注入 JIT，维护全局状态。

#### 4.5.5 验收标准

- [ ] `ls run hello.ls` 通过 JIT 正确执行
- [ ] 修改一个函数后重新 run，只该函数被重编译（通过日志验证）
- [ ] REPL 能定义变量、函数，跨行引用
- [ ] JIT 执行结果与 AOT 编译结果一致
- [ ] 压力测试：连续修改同一函数 100 次不泄漏内存

---

### Phase 6: C FFI — 动态库加载 ✅ 已完成

**目标**：实现 C FFI，支持运行时加载 shared library（Windows `.dll` / Linux `.so` / macOS `.dylib`）并调用 C 函数。

#### 4.6.1 架构设计

FFI 分为两层：

1. **`ffi.h` / `ffi.c`** — C 层跨平台动态库加载封装，供 ls 编译器自身使用（测试、REPL 等）
2. **Codegen 直接生成平台 API 调用** — 编译产出的可执行文件不依赖 `ffi.c`，直接调用 `LoadLibraryA`/`GetProcAddress`（Windows）或 `dlopen`/`dlsym`（Linux），AOT 产物完全独立

#### 4.6.2 FFI 接口 (ffi.h)

```c
// 跨平台动态库句柄（opaque pointer，避免在头文件中引入 windows.h）
typedef void *ffi_handle_t;

typedef struct {
    ffi_handle_t handle;
    char *path;          // 加载路径（owned）
} FfiLibrary;

// 高层 API — 供编译器内部使用
FfiLibrary *ffi_load(const char *path);     // 加载，失败返回 NULL
void ffi_unload(FfiLibrary *lib);           // 卸载并释放
void *ffi_symbol(FfiLibrary *lib, const char *name); // 查找符号
const char *ffi_error(void);                // 最近一次错误信息

// 运行时 helper — 简化的 C 函数，可供 JIT 调用
void *ls_ffi_load(const char *path);
void ls_ffi_unload(void *handle);
void *ls_ffi_symbol(void *handle, const char *name);
```

#### 4.6.3 FFI 在语言层面的使用

两种调用方式：

```
// ===== 方式 1：extern fn（有类型检查，推荐）=====
lib msvcrt = load("msvcrt.dll")          // Windows
lib kernel32 = load("kernel32.dll")
// lib libc = load("libc.so.6")          // Linux

// 声明外部函数签名 — 编译器做类型检查
extern fn puts(string s) -> int from msvcrt
extern fn GetCurrentProcessId() -> int from kernel32
extern fn printf(string fmt, ...) -> int from msvcrt   // 支持变参

puts("Hello from FFI!")
int pid = GetCurrentProcessId()
printf("PID = %d\n", pid)

// ===== 方式 2：lib.call（动态调用，unsafe）=====
// 不检查类型，第一个参数为函数名字符串
msvcrt.call("puts", "hello dynamic")
kernel32.call("Sleep", 0)
```

#### 4.6.4 Codegen 实现要点

- **`lib X = load("path")`**：
  - 创建全局变量 `@X`（ptr 类型，初始 null）
  - 生成 `__ls_ffi_init()` 函数，调用 `LoadLibraryA`/`dlopen` 并将句柄存入全局
  - 在 `main()` 入口处注入 `call void @__ls_ffi_init()` 初始化

- **`extern fn name(params) -> ret from lib`**：
  - 生成 LLVM 外部函数声明（`LLVMExternalLinkage`）
  - 编译器做参数/返回值类型检查
  - 变参函数用 `...` 标记 → `LLVMFunctionType(..., is_vararg=1)`

- **`lib.call("name", args...)`**：
  - 从全局变量加载库句柄
  - 调用 `GetProcAddress`/`dlsym` 运行时查找函数指针
  - 基于参数的 `resolved_type` 构建 LLVM function type，通过函数指针调用
  - 不做类型检查（unsafe），默认返回 `i32`

- **平台 API 声明**：`declare_builtins()` 中声明以下外部函数：
  - Windows: `LoadLibraryA`, `GetProcAddress`, `FreeLibrary`
  - Linux: `dlopen`, `dlsym`, `dlclose`

- **自动后缀补全**：`ffi_load()` 在路径无后缀时自动追加 `.dll`/`.so`/`.dylib`

#### 4.6.5 JIT 模式支持

JIT 通过 `LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess` 注册进程符号搜索，自动找到 `LoadLibraryA`/`GetProcAddress` 等系统函数。无需额外配置，JIT 和 AOT 使用完全相同的 codegen 路径。

#### 4.6.6 测试覆盖

`test_ffi.c` 包含 32 个断言，覆盖：
- 运行时加载有效/无效库
- 符号查找成功/失败
- 自动后缀补全
- Parse + typecheck FFI 语法（`lib`/`extern fn`/`lib.call`）
- Codegen 验证（IR 包含 `LoadLibraryA`、`GetProcAddress`）
- 变参函数 extern fn

端到端测试：
- `ffi_test.ls`：`extern fn` + `lib.call` 基础测试
- `ffi_win32.ls`：调用 `kernel32.dll`（GetCurrentProcessId）+ `msvcrt.dll`（puts）+ 动态调用（Sleep）

#### 4.6.7 验收标准

- [x] `ffi_test.ls` 在 Windows 上成功加载 `msvcrt.dll` 并调用 `puts`（JIT + AOT）
- [x] `ffi_win32.ls` 成功加载 `kernel32.dll` 并调用 `GetCurrentProcessId`、`Sleep`
- [x] `extern fn` 声明正确做类型检查（参数类型不匹配时报错）
- [x] `lib.call` 动态调用工作正常（unsafe，不检查类型）
- [x] 变参函数（`printf`）支持 `...` 标记
- [x] 库不存在时返回清晰错误信息（含平台特定错误码）
- [x] AOT 产出的可执行文件完全独立（直接调用平台 API，不依赖 ffi.c）
- [x] `test_ffi` 32 个断言全部通过
- [x] 所有已有测试（test_scanner、test_parser、test_types、test_codegen、test_jit）无回归

---

### Phase 7: 模块系统

**目标**：实现 `module` / `import` 机制，支持多文件项目。

#### 4.7.1 模块机制设计

```
// math.ls
module math

fn add(int a, int b) -> int { a + b }
fn pi() -> f64 { 3.14159265 }

// main.ls
import math

int result = math.add(1, 2)
```

#### 4.7.2 模块解析规则

- `module name;` 声明当前文件的模块名
- `import path;` 中 `path` 映射到文件系统：`math` → `./math.ls`，`std.io` → `<stdlib>/std/io.ls`
- 支持相对路径：`import ./utils;`
- 每个模块编译为一个 LLVM Module
- 跨模块引用通过 LLVM 的 external linkage 链接
- 循环导入检测：维护一个导入栈，发现环就报错

#### 4.7.3 可见性

- 默认所有顶层 `fn`、`struct` 和全局变量都是公开的（第一阶段简化）
- 后续可加 `pub` / 私有关键字

#### 4.7.4 跨模块变量

模块可导出全局变量，其他模块通过 `module.var` 语法访问：

```
// constants.ls
module constants
int ANSWER = 42

// main.ls
import constants
print(constants.ANSWER)   // 42
```

Codegen 将导入模块的全局变量 forward-declare 为 LLVM external global，
并在 `__ls_global_init()` 中统一初始化（先导入模块，后主模块）。

#### 4.7.5 验收标准

- [x] `module_test/` 下的多文件项目正确编译和运行
- [x] 跨模块函数调用、struct 引用正常工作
- [x] 跨模块变量导入导出正常工作（`module.var` 语法）
- [x] 循环导入被检测并报错
- [x] 未导入的模块成员访问被拒绝
- [x] JIT 模式正确传递 ModuleRegistry，模块系统在 `ls run` 下可用
- [x] `test_module` 20+ 个断言全部通过（含跨模块变量测试）

---

## 5. 编码规范（Claude Code 必须遵守）

### 5.1 C 代码风格

- 函数命名：`module_verb_noun`，如 `scanner_next_token`，`codegen_emit_binary`
- 结构体命名：`PascalCase`，如 `AstNode`，`CodegenContext`
- 枚举值：`UPPER_SNAKE_CASE`，如 `TOKEN_INT_LIT`，`AST_BINARY`
- 宏：`UPPER_SNAKE_CASE`，如 `GROW_ARRAY`
- 局部变量：`snake_case`
- 指针星号靠近类型：`int *ptr` 不是 `int* ptr`
- 每个 `.c` 文件开头注释说明其职责（一行即可）
- 每个公开函数有一行注释说明用途

### 5.2 内存管理规范

- 所有 `malloc` 必须检查返回值
- 谁分配谁释放，在头文件注释中标注所有权
- AST 节点用统一的 `ast_free` 递归释放
- LLVM 对象用 `LLVMDispose*` 系列函数释放
- Debug 模式下用 AddressSanitizer 跑所有测试

### 5.3 错误处理

- 不使用 `setjmp/longjmp`
- Scanner 错误返回 `TOKEN_ERROR`
- Parser 错误设置 `parser->had_error = true`，同步后继续
- 类型错误收集到错误列表，一次性报告（最多 20 条）
- Codegen 错误通过返回值 + stderr 报告
- 所有错误信息格式：`[error_type] file:line:col: message`

### 5.4 测试要求

- 每个 Phase 必须有对应的单元测试
- 使用简单的断言宏（自己写，不引入测试框架）：

```c
#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " != " #b)
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp(a, b) == 0, #a " != " #b)
```

- 每个 `samples/*.ls` 同时用 AOT 和 JIT 执行，对比输出一致

---

## 6. Claude Code 使用指南

### 6.1 每个 Phase 的工作流

```
1. 阅读当前 Phase 的需求
2. 实现所有 .h 接口定义
3. 实现对应 .c 文件
4. 编写并通过单元测试
5. 编写对应的 .ls 测试样例
6. 用 ASan 跑一遍确认无内存问题
7. `ctest --output-on-failure` 全部通过后，进入下一 Phase
```

### 6.2 每个 Phase 的首条指令模板

**Phase 1**：
> 请根据 CLAUDE.md 的 Phase 1 要求，实现项目骨架和 Scanner。创建 CMakeLists.txt、所有头文件、scanner.c、test_scanner.c、main.c（打印 token 流模式）。确保 cmake build 零警告，所有测试通过。

**Phase 2**：
> 请根据 CLAUDE.md 的 Phase 2 要求，实现 AST 定义和 Pratt Parser。创建 ast.h/c、parser.h/c、test_parser.c，以及所有 samples/*.ls 测试文件。

**Phase 3**：
> 请根据 CLAUDE.md 的 Phase 3 要求，实现类型系统和符号表。创建 types.h/c、symtable.h/c，并在 parser 的基础上添加类型检查 pass。

**Phase 4**：
> 请根据 CLAUDE.md 的 Phase 4 要求，实现 LLVM IR 代码生成。创建 codegen.h/c、debug.h/c、runtime/builtins.c。确保 hello.ls 能 AOT 编译为可执行文件并正确运行。

**Phase 5**：
> 请根据 CLAUDE.md 的 Phase 5 要求，实现 LLJIT 增量编译引擎。创建 jit.h/c、test_jit.c。实现 REPL 模式和增量编译变更检测。

**Phase 6**：
> 请根据 CLAUDE.md 的 Phase 6 要求，实现 C FFI。创建 ffi.h/c、test_ffi.c、ffi_test.ls。

**Phase 7**：
> 请根据 CLAUDE.md 的 Phase 7 要求，实现模块系统。创建 module.h/c，修改 parser 和 codegen 支持跨模块编译。

---

## 7. 计划新增的语言特性

### ~~增加类似C语言的for循环~~ ✅ 已完成

C 风格 for 循环已实现，语法：`for (init; cond; update) { body }`
- init 子句支持变量声明（`int i = 0`）或赋值表达式，可为空
- cond 子句为布尔表达式，可为空（`for(;;)` 为无限循环）
- update 子句支持赋值/表达式，可为空
- 支持 break / continue（continue 跳转到 update 而非 cond）
- 支持嵌套 for 循环
- AST 节点类型：`AST_FOR_C`（与 for-in 的 `AST_FOR` 独立）
- 涉及文件：ast.h/c, parser.c, checker.c, codegen.c
- 测试：test_parser.c（2 个用例）、test_codegen.c（1 个用例）、samples/for_loop_test.ls

### ~~增加类似现代C++ 的 foreach 循环~~ ✅ 已完成

foreach 循环已实现，使用 `for var in iterable { body }` 语法，支持以下迭代形式：
- **范围迭代**：`for i in 0..10 { }` — 遍历 [0, 10)，新增 `AST_RANGE` 表达式节点（`a..b`，`TOKEN_DOTDOT` 作为中缀运算符）
- **整数简写**：`for i in n { }` — 等价于 `for i in 0..n { }`
- 范围两端支持任意整数表达式：`for i in lo..hi { }`
- 支持可选括号：`for (i in 0..5) { }`
- 支持 break / continue（continue 跳转到自增步骤）
- 支持嵌套 foreach
- 循环变量自动推导为 `int` 类型，作用域限定在循环体内
- Codegen 基本块：`foreach.cond`/`foreach.body`/`foreach.update`/`foreach.end`
- 涉及文件：ast.h/c, parser.c, checker.c, codegen.c, token.h（TOKEN_DOTDOT 已存在）
- 测试：test_parser.c（2 个用例）、test_codegen.c（1 个用例）、samples/foreach_test.ls

### ~~循环处理中增加break关键字以及相关的处理~~ ✅ 已完成

break 和 continue 已在所有三种循环中正确实现：
- **while**：break → `while.end`，continue → `while.cond`
- **C-style for**：break → `for.end`，continue → `for.update`
- **foreach (for-in)**：break → `foreach.end`，continue → `foreach.update`

### ~~增加内置 fixed size 数组的支持, 关键字 array~~ ✅ 已完成

固定大小数组已实现，使用 `array(T, N)` 语法声明，全链路支持：
- **声明语法**：`array(int, 3) nums = [10, 20, 30]`
- **Scanner**：`TOKEN_ARRAY` 关键字
- **AST**：`AST_ARRAY_LIT`（数组字面量）、`AST_INDEX`（索引访问）、`TYPE_NODE_ARRAY`（类型节点）
- **类型系统**：`TYPE_ARRAY` 含 `elem` 和 `size`，`type_equals` 检查元素类型和大小均一致
- **类型检查**：
  - 数组字面量元素类型一致性检查
  - 声明大小与字面量大小不匹配时报错（如 `array(int, 3) x = [1, 2]`）
  - 索引必须为整数类型
  - `.length` 属性返回 `int`，仅支持 `length`，其他字段报错
  - 函数参数传递时大小必须精确匹配
- **Codegen**：
  - `LLVMArrayType2(elem, size)` 映射
  - 局部数组：alloca + 逐元素 store 或常量初始化
  - 全局数组：LLVM global + `__ls_global_init` 初始化
  - 索引读写：`LLVMBuildGEP2` 生成 GEP 指令
  - `.length`：编译期常量
  - `for x in arr`：生成隐藏索引变量 + 循环结构
  - `print(arr)`：输出 `[e0, e1, ...]` 格式
  - 数组作为函数参数（按值传递）
- 涉及文件：token.h, scanner.c, ast.h/c, parser.c, checker.c, codegen.c
- 测试：test_types.c（8 个用例）、test_codegen.c（5 个用例）、samples/array_test.ls

### ~~增加全局变量和跨模块变量支持~~ ✅ 已完成

全局变量（模块级变量）已完整实现，包含跨模块导入导出：
- **语法**：顶层 `int x = 42`、`array(int, 3) data = [1, 2, 3]` 等（与局部变量声明语法一致）
- **Parser**：`parse_statement()` 在顶层识别变量声明并加入 AST_PROGRAM 的 decl 列表
- **类型检查**：
  - forward_pass 跳过全局变量（无前向引用需求）
  - check_pass 中正常类型检查 + 注册到全局作用域
  - 函数体内可读写全局变量
  - 模块导出：`type_module_add_export` 收集 `AST_VAR_DECL` 的类型
- **Codegen**：
  - Pass 1：`LLVMAddGlobal` 创建全局变量，初始化为 null/zero
  - `__ls_global_init()` 函数：为所有有初始化器的全局变量生成初始化代码
  - 支持常量初始化（直接 `LLVMSetInitializer`）和运行时初始化（`LLVMBuildStore`）
  - 导入模块的全局变量也在 `__ls_global_init` 中初始化（先导入模块，后主模块）
  - `__ls_global_init` 在 `main()` 入口处被调用（与 `__ls_ffi_init` 同层注入）
  - 全局变量读取：`LLVMBuildLoad2` from `LLVMGetNamedGlobal`
  - 全局变量写入：`LLVMBuildStore` to global
- **跨模块**：
  - 导入模块的全局变量 forward-declare 为 LLVM external global
  - 导入模块的函数体和全局初始化器一并编译进主模块
  - `math.MAGIC` 语法通过 `AST_FIELD` + `TYPE_MODULE` 解析
- **JIT**：`jit_run_file` 正确传递 `ModuleRegistry`，模块系统在 JIT 模式下可用
- 涉及文件：parser.c, checker.c, codegen.c, jit.c, module.c
- 测试：test_types.c（4 个用例）、test_codegen.c（4 个用例）、test_module.c（5 个用例）、samples/global_var_test.ls、samples/module_test/constants.ls

### ~~String 内部表示升级为 LsString struct~~ ✅ 已完成

String 类型从裸 `i8*` 升级为带 metadata 的 struct，全链路支持：
- **内部表示**：`LsString = { i8* data, i32 len, i32 cap }`，LLVM named struct `%LsString`
- **静态字面量**：`cap = 0`，data 指向全局常量（`.rodata` 段），无需 `free`
- **动态字符串**：`cap >= LS_MIN_STR_CAP(16)`，通过 `malloc` 分配，调用者负责 `free`
- **分配策略**：`cap = max(16, next_power_of_2(len + 1))`（增长策略留待 builtin 方法实现时加入）
- **`.length` 属性**：O(1) 读取，直接从 struct 第 1 字段提取（checker + codegen 均支持）
- **`+` 拼接运算符**：codegen 展开为 `malloc` + `memcpy`，生成新 LsString
- **`==` / `!=` 比较**：值比较，codegen 展开为 `strcmp(a.data, b.data)`
- **`match` 表达式**：string subject 的模式匹配也使用 `strcmp`
- **print / f-string**：自动提取 `.data` 传给 `printf`/`sprintf`
- **C FFI 兼容**：`extern fn` 声明使用 C ABI 映射（string → `i8*`），调用时自动提取 `.data`
- **全局变量**：支持全局 string 变量声明和跨模块导出
- 涉及文件：common.h（`LS_MIN_STR_CAP`）、codegen.c（`ls_string_type/make/data/len/const` 等 helpers + 全面适配）、checker.c（string `.length` 支持）
- 测试：所有 7 个测试套件通过，所有 20+ 个 .ls 样例文件通过（JIT + AOT）

### ~~增加对string的更多的builtin函数 — Batch 1: 查询方法~~ ✅ 已完成

String Batch 1（查询方法，不分配内存）已完整实现，包含 7 个 builtin 方法：
- **`s.empty()`** → `bool`：判断字符串是否为空（`len == 0`）
- **`s.at(int i)`** → `int`：返回第 i 个字节的 ASCII 值（GEP + load + zext）
- **`s.find(string sub)`** → `int`：返回子串首次出现的索引，未找到返回 -1（`strstr` + 指针减法）
- **`s.contains(string sub)`** → `bool`：判断是否包含子串（`strstr != NULL`）
- **`s.starts_with(string prefix)`** → `bool`：判断是否以指定前缀开头（`strncmp`）
- **`s.ends_with(string suffix)`** → `bool`：判断是否以指定后缀结尾（长度检查 + `strcmp` 尾部比较）
- **`s.compare(string other)`** → `int`：字典序比较，返回 <0 / 0 / >0（`strcmp`）
- **方法调度**：checker.c 中 `check_string_method()` 做类型检查，codegen.c 中 `codegen_string_method()` 生成 LLVM IR
- **builtins 声明**：`declare_builtins()` 中新增 `strstr`、`strncmp` 声明
- 涉及文件：checker.c, codegen.c
- 测试：test_types.c（8 个新用例）、test_codegen.c（7 个新用例）、samples/string_test.ls（7 个方法的端到端测试）

### ~~增加对string的更多的builtin函数 — Batch 2: 变换方法~~ ✅ 已完成

String Batch 2（变换方法，分配新字符串）已完整实现，包含 5 个 builtin 方法：
- **`s.upper()`** → `string`：返回大写副本（malloc + 逐字节 ASCII 转换循环）
- **`s.lower()`** → `string`：返回小写副本（malloc + 逐字节 ASCII 转换循环）
- **`s.substr(int start, int len)`** → `string`：返回子串（malloc + memcpy + null-terminate）
- **`s.trim()`** → `string`：去除首尾空白 space/tab/newline/cr（双向扫描循环 + malloc + memcpy）
- **`s.replace(string old, string new)`** → `string`：替换所有匹配项（`__ls_str_replace` 运行时辅助函数，支持多次替换、替换为空串等）
- **`__ls_str_replace`** 辅助函数：作为 LLVM IR 函数定义嵌入模块（两趟算法：先 strstr 计数，再 malloc + 逐段拷贝替换），AOT/JIT 均可用
- **方法链**：支持如 `"  HELLO  ".trim().lower()` → `"hello"`
- 涉及文件：checker.c, codegen.c
- 测试：test_types.c（6 个新用例）、test_codegen.c（5 个新用例）、samples/string_test.ls（6 组端到端测试）

下一步 Batch 3 计划：实现 `.split()` 方法（需要先实现动态数组/vector 类型）。

### ~~impl 隐式 self + static 方法~~ ✅ 已完成

impl 块中的方法调用已支持隐式 self 传递和 static 方法，语义类似 C++：
- **实例方法**：不写 self 参数，`self` 自动以 `*Struct` 指针注入方法体作用域
  - 调用语法：`obj.method(args)` — 编译器自动将 `&obj` 作为首参传入
  - `self.field` 自动解引用（等同于 C++ 的 `this->field`）
  - `self.field = expr` 赋值也自动解引用，修改会反映到调用方
- **static 方法**：用 `static fn` 声明，无 self
  - 调用语法：`StructName.method(args)` ��� `obj.method(args)`（后者忽略 obj）
  - 通过类型名调用实例方法会报错
- **`TOKEN_STATIC`** 关键字新增到 scanner
- **AST**：`fn_decl` 新增 `is_static` 和 `impl_struct_name` 字段
- **Checker**：
  - `check_impl_decl` 为实例方法自动注入 `self: *Struct` 到函数类型和作用域
  - `AST_CALL` 检测 struct 方法调用，实例方法 user_expected = param_count - 1
  - `AST_FIELD` 自动解引用 `*Struct → Struct` 用于字段/方法访问
  - 方法注册表增加 `is_static` 标记
- **Codegen**：
  - `codegen_fn_decl` 为实例方法生成额外首参 `*StructType`，绑定 `self` alloca
  - `AST_CALL` 为实例方法调用前插 obj 的 alloca 地址作为首参
  - `AST_FIELD` 和 `AST_ASSIGN` 支持 pointer-to-struct 自动解引用
- ���及文件：token.h, scanner.c, ast.h, parser.c, checker.h, checker.c, codegen.c
- 测试：test_parser.c（1 个更新用例）、test_types.c（4 个新用例）、test_codegen.c（2 个新用例）、samples/struct_test.ls、e2e_test.ls
- **旧语法 `fn method(Struct self, ...)` 不再支持**（breaking change）

**下一步计��**：修改参数传递规则，复杂结构传递指针、简单变量传递值（具体实现时讨论）。

### ~~String 作用域自动释放（RAII-style auto-free）~~ ✅ 已完成

动态分配的 string 在离开作用域时自动释放，无需手动 `free`：
- **LsString 内存分类**：
  - `cap == 0`：静态字面量（data 指向 `.rodata`，不需要 free）
  - `cap > 0`：动态分配（`malloc` 产生，如 `+` 拼接、`.upper()`、`.replace()` 等）
- **作用域退出自动 free**：
  - `emit_scope_cleanup(ctx)` 在每个 `AST_BLOCK`（语句块和表达式块）的 `pop_scope` 前调用
  - 遍历当前作用域所有 `TYPE_STRING` 局部变量，对每个检查 `cap > 0`，是则 `free(data)`
  - 生成 `sf.dyn` / `sf.free` / `sf.cont` 基本块（条件分支避免 free 静态字面量）
- **赋值自动 free 旧值**：
  - `s = s.upper()` 时，在 `LLVMBuildStore` 前先 `emit_string_free` 释放旧的动态 string
- **`return` 前自动 cleanup**：
  - 显式 `return val` 前遍历所有作用域，free 全部 string 局部变量
  - 若返回值是 string 标识符（`return s`），跳过该变量避免 use-after-free（所有权转移给调用方）
  - `void` 函数的 `return` 也做全量 cleanup
  - 函数体隐式 return（编译器自动插入的 `ret`）前也做 cleanup
- **`break` / `continue` 前自动 cleanup**：
  - `CodegenContext` 新增 `loop_scope` 字段，记录循环入口时的作用域
  - `break`/`continue` 前调用 `emit_cleanup_to(ctx, ctx->loop_scope)` 释放循环体内层的 string 局部变量
  - 三种循环（while / for-c / foreach）均已支持
- **已知限制**（后续可改进）：
  - 临时 string 表达式（如 `print("hello".upper())`）的中间结果无变量名，不会被 free
  - `return a + b`（返回值为 string 表达式）中间临时 string 不会被 free
- 涉及文件：codegen.h（`loop_scope` 字段）、codegen.c（`emit_string_free`、`emit_scope_cleanup`、`emit_cleanup_to`）
- 测试：test_codegen.c（3 个新用例）、全部 7 个测试套件 148 个断言通过

### 新增builtin的正则表达式功能

### 增加原生支持半精度浮点数f16的能力

### 增加文件读写的能力

### 增加内置的vector支持

### 增加内置的map支持