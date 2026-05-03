# LS 已完成特性实现记录

本文档记录各特性的实现细节，供日后参考。

---

## C 风格 for 循环

语法：`for (init; cond; update) { body }`，init 支持变量声明或赋值，三个子句均可为空（`for(;;)` 无限循环）。`continue` 跳转到 update。AST 节点：`AST_FOR_C`。涉及：ast.h/c, parser.c, checker.c, codegen.c。

## foreach / for-in 循环

语法：`for i in 0..10 { }`（range 迭代）、`for i in n { }`（等价 `for i in 0..n`）。`AST_RANGE` 节点，`TOKEN_DOTDOT`。循环变量自动推导为 int，作用域限于循环体。Codegen 基本块：`foreach.cond/body/update/end`。

## break / continue

在 while、for-c、foreach 三种循环中全部支持。`CodegenContext.loop_scope` 记录循环入口作用域，break/continue 前调用 `emit_cleanup_to()` 释放循环体内层 string。

## 固定大小数组 array(T, N)

语法：`array(int, 3) nums = [10, 20, 30]`。LLVM 映射：`[N x T]`。支持：索引读写（GEP2）、`.length`（编译期常量）、for-in 迭代、函数参数传递（按值）、`print(arr)` 输出 `[e0,e1,...]`。类型检查：元素类型一致性、大小匹配、索引必须为整数。

## 全局变量

顶层变量声明，Pass 1 用 `LLVMAddGlobal` 注册，`__ls_global_init()` 生成运行时初始化，main 入口注入调用。跨模块：导入模块的全局变量 forward-declare 为 LLVM external global，在 `__ls_global_init` 中先初始化导入模块。`math.MAGIC` 通过 `AST_FIELD + TYPE_MODULE` 解析。

## LsString 结构体

`LsString = { i8* data, i32 len, i32 cap }`，LLVM named struct `%LsString`。静态字面量 `cap=0`，动态字符串 `cap >= LS_MIN_STR_CAP(16)`，分配策略 `cap = max(16, next_pow2(len+1))`。`+` 拼接展开为 malloc+memcpy，`==`/`!=` 展开为 strcmp，`.length` O(1) 读 struct 第 1 字段。C FFI 调用自动提取 `.data`。

## String Batch 1（查询方法）

`empty()`, `at(i)`, `find(sub)`, `contains(sub)`, `starts_with(prefix)`, `ends_with(suffix)`, `compare(other)`。方法调度：`check_string_method()` + `codegen_string_method()`。依赖 C runtime：`strstr`, `strncmp`。

## String Batch 2（变换方法）

`upper()`, `lower()`, `substr(start, len)`, `trim()`, `replace(old, new)`。均返回新 malloc 字符串。`replace` 通过内嵌 `__ls_str_replace` helper 实现（两趟算法：先计数，再 malloc+拷贝替换）。

## String RAII 自动释放

`emit_scope_cleanup(ctx)`：block 退出前遍历作用域，对 `cap > 0` 的 string 条件分支 free data。赋值时先 free 旧值。`return` 前全量 cleanup（跳过返回值本身避免 use-after-free）。`break/continue` 前清理循环体内层 string。

## impl 隐式 self + static 方法

实例方法：codegen 自动注入首参 `*StructType`，绑定 `self` alloca。调用时编译器自动插入 `&obj` 作为首参。`self.field` 自动解引用。Static 方法：`static fn` 声明，无 self，通过类型名或实例调用（后者忽略 obj）。`TOKEN_STATIC` 关键字。Breaking change：旧语法 `fn method(Struct self, ...)` 不再支持。

## vec(T) 动态数组

内部：`LsVec { ptr data, i32 len, i32 cap }`。方法：`push(v)`, `pop()`, `get(i)`, `set(i,v)`, `len()`, `cap()`, `is_empty()`。自动扩容（cap×2）。RAII 自动 drop。String 元素 push 时触发 move（mark cap=-1）；static string push 不 move。Codegen 生成 `__ls_vec_T_*` helper 函数族。

## map(K, V) 哈希映射

内部：链式哈希表 `LsMap { ptr buckets, i32 len, i32 cap }`，节点 `LsMapNode_KK_VV { i64 hash, K key, V val, ptr next }`。方法 8 个：`set`, `get`, `contains_key`, `remove`, `clear`, `is_empty`, `keys`, `values`。FNV-1a 哈希（string key）。负载因子 >75% 自动 rehash。RAII drop 释放所有节点。String key/value 深拷贝。JIT 模式：`__ls_map_hash_s` 前向声明，从 builtins 模块解析。

## C FFI

架构：`ffi.h/c` 跨平台封装 + codegen 直接生成平台 API 调用（AOT 独立）。`__ls_ffi_init()` 在 main 入口注入。`extern fn` 做类型检查，支持变参 `...`。`lib.call` unsafe，默认返回 i32。自动后缀补全（`.dll`/`.so`/`.dylib`）。JIT 通过 `LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess` 注册进程符号。

## 模块系统

`module name` 声明；`import path` 映射到文件系统；循环导入检测（导入栈）；所有顶层符号默认公开。跨模块变量通过 external global forward-declare。`jit_run_file` 传递 `ModuleRegistry`。
