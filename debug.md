# LS 编译器调试指南

> **E2E 测试文件位置**：`tests/samples/*.ls`

## 1. 常见问题分类

### 1.1 编译器挂起/死循环
**症状**：编译命令无响应，进程卡住

**排查步骤**：
1. 使用带超时的命令执行：
   ```powershell
   # PowerShell
   Start-Job -ScriptBlock { & '.\build\Release\ls.exe' compile test.ls } | 
       Wait-Job -Timeout 10 | Receive-Job
   ```
2. 检查是否是 parser 错误恢复死循环
3. 常见原因：
   - Parser panic mode 未能推进 token 流
   - 语法错误导致无限循环

**已知触发场景**：
- `impl` 块内使用关键字作为函数名（如 `fn new()`）
- 嵌套语法错误

### 1.2 编译成功但运行结果错误
**症状**：编译通过，程序运行结果不符合预期

**排查步骤**：
1. **检查 IR 生成**
   ```bash
   ls.exe emit-ir test.ls > test.ll
   ```
2. **分析 IR**
   - 查看函数调用是否正确
   - 检查基本块顺序
   - 验证清理代码（`call void @Struct.__drop`）

3. **常见问题**
   - 析构函数被调用多次
   - 析构函数未被调用
   - 清理顺序错误

## 2. 调试工具和方法

### 2.1 IR 分析
```bash
# 导出 IR 查看
ls.exe emit-ir test.ls

# 或编译时带 IR dump
ls.exe compile test.ls -o test.exe --dump-ir
```

### 2.2 添加临时调试输出
在 `codegen.c` 中添加 fprintf 调试：
```c
fprintf(stderr, "[DEBUG] emit_scope_cleanup: scope->count = %d\n", scope->count);
fflush(stderr);
```

**注意**：调试完成后记得移除！

### 2.3 分步测试
创建最小化测试用例：
```ls
// 从最简单的场景开始
struct Foo { int x; }
impl Foo {
    fn __drop() { print("dropped"); }
}
fn main() -> int {
    Foo f
    return 0
}
```

### 2.4 类型检查
```bash
ls.exe check test.ls
```
如果类型检查通过但运行时错误，问题在 codegen。

## 3. 特定问题排查

### 3.1 Destructor 相关问题

#### 问题：析构函数未被调用
**检查点**：
1. struct 是否有 `impl` 块定义 `fn __drop()`
2. `has_drop` 标志是否正确设置
3. `emit_scope_cleanup` 是否被调用（在 return 之前）
4. 当前基本块是否已有 terminator

#### 问题：析构函数被调用多次
**检查点**：
1. 是否同时在 `emit_struct_drop` 和 `__drop()` 函数体内都调用了成员清理
2. 修复：只有当用户未定义 `__drop()` 时，才由编译器生成递归清理

#### 问题：多个 struct 的 `__drop` 函数冲突
**检查点**：
1. 是否所有 `__drop` 都命名为 `@__drop`
2. 修复：使用 `StructName.__drop` 格式

### 3.2 静态方法名冲突
**症状**：
```
Incorrect number of arguments passed to called function!
call %Point @create(i32 100)
```
或
```
undefined method 'Point.create'
```

**检查点**：
1. 两个 struct 是否有同名的方法
2. 检查 IR 中函数名是否有 `StructName.` 前缀
3. 修复：所有 impl 方法使用 `StructName.method` 格式区分

### 3.2 Parser 死循环
**错误信息**：
```
[error] test.ls:35:9: expected 'fn' in impl block
[error] test.ls:35:9: expected 'fn' in impl block
... (重复)
```

**常见原因**：
- `impl` 块内使用关键字（如 `new`）作为函数名
- 语法错误后 panic mode 无法恢复

**规避方法**：
- 避免在 `impl` 块内使用关键字
- 先用 `ls.exe check` 验证语法

### 3.3 类型检查错误
```bash
# 查看具体类型错误
ls.exe check test.ls
```

## 4. 调试流程 Checklist

- [ ] 1. 确认问题可复现
- [ ] 2. 创建最小化测试用例
- [ ] 3. 使用 `ls.exe check` 排除语法/类型错误
- [ ] 4. 使用 `ls.exe emit-ir` 查看 IR
- [ ] 5. 定位问题阶段（parser/checker/codegen）
- [ ] 6. 添加临时调试输出
- [ ] 7. 修复问题
- [ ] 8. 运行全部测试确认无回归
- [ ] 9. 清理临时调试代码

## 5. 测试命令模板

```powershell
# 编译并运行 (E2E 测试在 tests/samples/)
ls.exe compile tests\samples\test.ls -o test.exe; .\test.exe

# 带超时的编译
Start-Job -ScriptBlock { & '.\build\Release\ls.exe' compile tests\samples\test.ls } | 
    Wait-Job -Timeout 15 | Receive-Job

# 查看 IR
ls.exe emit-ir tests\samples\test.ls

# 类型检查
ls.exe check tests\samples\test.ls

# 运行所有测试
.\build\Release\test_scanner.exe
.\build\Release\test_parser.exe
.\build\Release\test_types.exe
.\build\Release\test_codegen.exe
.\build\Release\test_jit.exe
.\build\Release\test_ffi.exe
.\build\Release\test_module.exe
```

## 6. 已知的 LS 语言限制

### 6.1 关键字冲突
以下关键字不能用作函数名/变量名：
- `new` - 堆分配关键字

### 6.2 impl 块注意事项
- 方法必须用 `fn` 关键字开头
- 不要在 impl 块内使用关键字

## 7. 常见错误修复记录

### 7.1 析构函数双重调用
**问题**：`emit_struct_drop` 既调用 `__drop()` 又递归清理成员
**修复**：只有当用户未定义 `__drop()` 时，才由编译器生成递归清理

### 7.2 多 struct `__drop` 函数名冲突
**问题**：多个 struct 的 `__drop` 都命名为 `@__drop`，LLVM 合并后只有一个生效
**修复**：使用 `StructName.__drop` 格式

### 7.3 cleanup 在 terminator 后调用
**问题**：`emit_scope_cleanup` 在 `LLVMBuildRet` 之后调用
**修复**：cleanup 必须在 return 之前调用

### 7.4 静态方法名冲突导致 Codegen 类型混淆
**问题**：
- 两个不同 struct 定义了同名的静态方法（如 `create`）
- 编译时报错：`Incorrect number of arguments passed to called function`
- IR 中所有同名的静态方法被错误匹配

**原因**：
- Codegen 在处理静态方法调用时，只按函数名查找
- `LLVMGetNamedFunction(ctx->module, "create")` 只能获取第一个同名函数

**修复**：
- `codegen_impl_decl`：给所有 impl 方法（静态方法、实例方法、`__drop`）加类型前缀
- 格式：`StructName.method_name`（如 `@Point.create`、`@Resource.create`）
- `codegen_call`：构建 qualified name 进行查找

**示例**：
```ls
struct Point { int x; int y; }
impl Point {
    static fn create(int x, int y) -> Point { ... }  // @Point.create
}

struct Resource { int id; }
impl Resource {
    static fn create(int id) -> Resource { ... }      // @Resource.create
}

// 调用时正确解析
Point p = Point.create(1, 2)      // 查找 @Point.create
Resource r = Resource.create(100)  // 查找 @Resource.create
```

## 8. String 内存管理

### 8.1 LsString 三态语义
```
┌─────────────────────────────────────────────────────────────┐
│  cap = -1: MOVED  — 已转移，不持有数据，跳过 free          │
│  cap =  0: STATIC — 静态字面量，data 指向 .rodata，不 free│
│  cap >  0: OWNED  — 动态分配，作用域退出时 free          │
└─────────────────────────────────────────────────────────────┘
```

### 8.2 String Move 语义
- **动态字符串（cap>0）赋值**：`a = b` 执行 move，b 被标记为 moved
- **静态字符串（cap=0）赋值**：`a = b` 执行 copy，a 和 b 都指向静态数据
- **返回值**：返回的 string 变量被标记为 moved，跳过清理

### 8.3 临时字符串清理
- 方法调用结果作为语句（如 `s.upper()`）会产生临时字符串
- 编译器在语句末尾自动生成 cleanup 代码
- 通过 `emit_temp_string_cleanup` 函数处理

### 8.3.1 f-string 堆分配
格式化字符串 `f"..."` 现在使用堆分配（malloc）而非栈分配：

**实现**：
```c
// codegen_format_string 函数
LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
LLVMValueRef buf_size = LLVMConstInt(i32_t, 4096, 0);
LLVMValueRef buf = LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn,
                                  &buf_size64, 1, "fstr.buf");
// cap = 4096，表示堆分配
LLVMValueRef cap = LLVMConstInt(i32_t, 4096, 0);
return ls_string_make(ctx, buf, len, cap);
```

**cleanup 机制**：
- `emit_temp_string_cleanup` 检查 `cap > 0`
- 如果 `cap > 0`，调用 `free(data)` 释放堆内存
- 匿名 f-string（语句表达式）会被自动清理

**示例**：
```ls
// 匿名 f-string - 语句结束后自动释放
f"hello {42}"

// 赋值给变量 - 变量离开作用域时释放
string s = f"value = {x}"
print(s)

// 在 struct 方法中返回
impl Point {
    fn to_string() -> string {
        return f"({self.x}, {self.y})"  // 堆分配，调用方负责释放
    }
}
```

### 8.4 Struct 字段 String Move
- 字段赋值时，如果值来自 string 变量，执行 move
- 通过 `emit_string_move` 函数处理

### 8.5 字符串转换内置函数
LS 提供了以下内置函数用于字符串与其他类型之间的转换：

#### to_string(x) -> string
将数值或布尔值转换为字符串。

**支持的类型**：
- `int` → 整数的十进制表示
- `f64` → 浮点数的完整精度表示（使用 `%.17g` 格式）
- `bool` → `"true"` 或 `"false"`

**示例**：
```ls
int n = 42
string s = to_string(n)           // "42"

f64 pi = 3.14159
string sp = to_string(pi)         // "3.1415899999999999"

bool flag = true
string sf = to_string(flag)       // "true"
```

**实现位置**：`src/codegen.c` - `codegen_to_string` 函数

#### from_int(s) -> int
将字符串解析为整数（使用 C 的 `atoi` 函数）。

**示例**：
```ls
int n = from_int("123")           // 123
int neg = from_int("-456")         // -456
```

**实现位置**：`src/codegen.c` - `codegen_from_int` 函数

#### from_float(s) -> f64
将字符串解析为浮点数（使用 C 的 `atof` 函数）。

**示例**：
```ls
f64 f = from_float("3.14")        // 3.14
f64 exp = from_float("2.5e-2")    // 0.025
```

**实现位置**：`src/codegen.c` - `codegen_from_float` 函数

**类型检查**：
- 所有三个函数都在 `src/checker.c` 的 `check_builtin_call` 中进行类型检查
- `to_string` 要求参数是 numeric 或 bool 类型
- `from_int` 和 `from_float` 要求参数是 string 类型

## 9. Phase 0-4 完成的功能

### Phase 0: 临时字符串清理
- ✅ `emit_string_free` 支持 cap=-1/0/>0 三态
- ✅ 临时字符串识别和清理
- ✅ 语句级临时清理

### Phase 1: Move 语义
- ✅ 变量间赋值执行 move（动态字符串）
- ✅ 静态字符串复制（无需 move）
- ✅ 方法返回值赋值

### Phase 2: Return 语句
- ✅ 返回 string 变量标记为 moved

### Phase 3: Struct 字段
- ✅ 字段赋值支持 string move
- ✅ struct 参数传递（值传递）

## 10. Struct Move 语义 (RAII)

### 10.1 moved_flag 机制
带有 `__drop` 的 struct 使用 `moved_flag` 防止双重释放：

```
┌─────────────────────────────────────────────────────────────────────┐
│  moved_flag = false: 有效值，作用域退出时调用 __drop                 │
│  moved_flag = true:  已转移，所有权已移交，跳过 __drop                │
└─────────────────────────────────────────────────────────────────────┘
```

### 10.2 Move 触发场景

| 操作 | 行为 |
|------|------|
| `Foo b = a`（copy init） | checker_error + a.moved_flag = true |
| `b = a`（copy assign） | checker_error + a.moved_flag = true |
| `consume(a)`（函数传参） | 标记 a.moved_flag = true（隐式 move，无 error） |
| `return a` | 标记 a.moved_flag = true，跳过清理 |
| 使用已 moved 的变量 | checker_error: "use of moved value" |

### 10.3 为什么不报错？

函数调用允许隐式 move 的原因：
1. **自然的所有权移交**：函数传参是"移交所有权"的自然方式
2. **双重释放已防止**：moved_flag 确保作用域退出时不会重复调用 `__drop`
3. **错误时机**：如果要在传参时报错，需要在 call site 预测 future usage，不可行

### 10.4 使用已 moved 变量的错误

编译器在**使用点**检测 moved 值，而非传参点：

```ls
struct Resource { int id; }
impl Resource { fn __drop() { print("dropped"); } }

fn consume(Resource r) { }

fn main() {
    Resource r = Resource { id: 1 }
    consume(r)           // OK: 隐式 move，无 error
    print(r.id)         // ERROR: use of moved value 'r'
}
```

### 10.5 已完成的 Bug 修复

| Bug | 修复方式 |
|-----|---------|
| f-string 堆分配 | 使用 malloc 替代栈缓冲区 |
| Heap 指针双重释放 | free() 拦截调用 __drop |
| LIFO 清理顺序 | reverse(scope->count) |
| free(NULL) crash | 添加 NULL 检查 |
| Copy Assignment Warning | checker 报错 |
| User __drop 不递归 | __drop 内部手动调用成员 drop |

## 11. 测试覆盖

### 单元测试
- `test_codegen.c`: 200 个测试
- `test_types.c`: 168 个测试
- `test_scanner.c`: 227 个测试
- `test_parser.c`: 37 个测试
- `test_ffi.c`: 32 个测试
- `test_jit.c`: 12 个测试
- `test_module.c`: 21 个测试

### E2E 测试
- `tests/samples/string_memory_e2e.ls` - 字符串内存管理
- `tests/samples/struct_string_e2e.ls` - Struct 字段字符串
- `tests/samples/to_string_test.ls` - 字符串转换函数（to_string, from_int, from_float）
- `tests/samples/fstring_memory_e2e.ls` - f-string 堆分配和内存清理
- `tests/samples/custom_to_string_test.ls` - 自定义 struct to_string 方法

## 11. 联系方式

如遇到其他问题，可参考：
- `src/codegen.c` - 代码生成
- `src/checker.c` - 类型检查
- `src/parser.c` - 语法解析
