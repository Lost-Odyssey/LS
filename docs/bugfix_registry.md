# LS 疑难 Bug 修复清单

> 本文档记录 LS 编译器开发过程中遇到的**疑难 bug**——根因不明显、跨子系统、涉及底层 ABI/内存语义、或需要非直觉修复方式的问题。
>
> 每条记录包含：症状、根因、修复方式、教训。便于未来遇到类似问题时快速定位。
>
> **约定**：编号格式 `BF-NNN`（BugFix）；严重程度 `CRASH` / `WRONG` / `LEAK` / `PERF`

---

## 总览

| 编号 | 日期 | 严重度 | 标题 | 影响范围 | 详细文档 |
|------|------|--------|------|----------|----------|
| BF-001 | 2026-05-20 | CRASH | enum 含 vec/map payload ctor 未 move 源变量 | codegen | `bugfix_L006_enum_vec_map_payload.md` |
| BF-002 | 2026-05-20 | CRASH | enum clone 缺 vec/map → 参数传递 double-free | codegen | `bugfix_L006_enum_vec_map_payload.md` |
| BF-003 | 2026-05-20 | WRONG | AOT void main 返回垃圾退出码 | codegen / AOT 链接 | `bugfix_L006_enum_vec_map_payload.md` |
| BF-004 | 2026-05-14 | CRASH | has_drop enum 函数参数未深拷贝 | codegen (Phase F.7) | — |
| BF-005 | 2026-05-14 | CRASH | enum match arm string binder 共享 data 指针 | codegen (Phase F.7) | — |
| BF-006 | 2026-05-14 | CRASH | match arm 含 return 导致 "Terminator in middle of BB" | codegen (Phase F.5) | — |
| BF-007 | 2026-05-09 | CRASH | map 形参未标 is_borrowed → callee drop caller 的 buckets | codegen (Phase C.7) | — |
| BF-008 | 2026-05-09 | CRASH | Block-call 返回 string 双注册 temp → double-free | codegen (Phase C.7) | — |
| BF-009 | 2026-05-09 | LEAK | has_drop_n 只数 string → struct(drop) capture env 不生成 drop_fn | codegen (Phase C.7) | — |
| BF-010 | 2026-05-06 | WRONG | parser `starts_var_decl` 跨行误判 | parser (Phase F.4) | — |
| BF-011 | 2026-05 | LEAK | enum.box 泄漏（自递归 has_drop 循环依赖） | codegen (Memcheck B) | — |
| BF-012 | 2026-05 | LEAK | try 早返路径 string 泄漏 | codegen (Memcheck B) | — |
| BF-013 | 2026-05 | CRASH | struct 构造 AST_NEW_EXPR 字段 double-free | codegen (Memcheck B) | — |
| BF-014 | 2026-05 | LEAK | calloc 未被 memcheck 追踪 | runtime/memcheck (Memcheck B) | — |
| BF-015 | 2026-05 | CRASH | sum_tree 自递归 enum match 绑定 box double-free | codegen (Memcheck B) | — |
| BF-016 | 2026-05 | LEAK | struct 字段读 clone 未注册 temp → print(p.name) 泄漏 | codegen (Memcheck B) | — |
| BF-017 | 2026-05 | LEAK | match rvalue subject enum 退出后无人 drop payload | codegen (Memcheck A) | — |
| BF-018 | — | CRASH | 静态/动态 CRT FILE* 不兼容（JIT io 路径） | jit / runtime | `crt_mismatch_bug.md` |
| BF-019 | 2026-05-10 | CRASH | call site arg_type 未声明 → struct clone 跳过 → 泄漏 | codegen (Phase E.1) | — |
| BF-020 | 2026-05-23 | WRONG | map(K, has_drop_enum) set/get 浅拷贝 → 值变 null | codegen (std.json) | — |
| BF-021 | 2026-05-23 | WRONG | emit_enum_ctor 对 has_drop enum payload 浅拷贝 → 悬垂 | codegen (std.json) | — |
| BF-022 | 2026-05-23 | CRASH | vec/arr clone 对 has_drop enum 元素 memcpy → double-free | codegen (std.json) | — |
| BF-023 | 2026-05-23 | CRASH | 自递归 enum clone 内联 IR 无限递归 → 编译器栈溢出 | codegen (std.json) | — |
| BF-024 | 2026-05-23 | WRONG | vec.push borrowed match binder 浅拷贝 → 值变 null | codegen (std.json) | — |
| BF-025 | 2026-05-24 | DOUBLE-FREE | 用户函数返回 string 双重 temp 注册 → double-free | codegen | — |
| BF-026 | 2026-05-24 | LEAK | match arm string binder scope cleanup 缺失 → 泄漏 | codegen | — |
| BF-027 | 2026-05-25 | CRASH | 跨模块 enum drop_fn/clone_fn 持有旧 LLVM module 的 stale 指针 | codegen (模块系统) | — |
| BF-028 | 2026-05-25 | WRONG | import 模块的 impl 方法未注册到导入方 impl_registry | checker (模块系统) | — |
| BF-029 | 2026-05-25 | DOUBLE-FREE | match-arm string binder 作为 arm 返回值时 scope cleanup 误释放 | codegen | — |
| BF-030 | 2026-05-25 | DOUBLE-FREE | `_escape_string` 循环内 `s.substr(i,1)` 临时 string 在 match+while 嵌套中非确定性双释放 | std/json.ls (LS 代码) | — |
| BF-031 | 2026-05-26 | BUILD | `find_fn_template` static 前向声明缺失 → GCC/Clang 编译错误 | checker.c | — |
| BF-032 | 2026-05-26 | WRONG | `emit_string_clone_val` 对 borrowed 参数（cap=0）跳过克隆 → 悬垂指针 | codegen (emit_enum_ctor) | `bugfix_BF032_string_clone_borrowed_param.md` |
| BF-033 | 2026-05-26 | WRONG | `map_type_id` 对 enum 值类型使用默认后缀 `"i"` → 潜在 LLVM 类型名冲突 | codegen (map helpers) | — |
| BF-034 | 2026-05-26 | CRASH | `codegen_print_call` `printf_args` 按 `argc*2` 分配，f-string 展开后 expr 槽溢出 → Linux heap crash | codegen (print) | — |
| BF-036 | 2026-05-26 | WRONG | `proc.args()` 丢弃第一个用户参数：`main.c` 传入的 `g_argv` 不含 script 名，但 `args()` 从 `i=1` 开始 | main.c / std/proc.ls | — |
| BF-039 | 2026-05-28 | LEAK | `map[key]` / `m.get(key)` 读取返回 value 深拷贝，string value 临时使用（如 `print(m[k])`）未注册 temp → 泄漏（与 vec[i] 同源，map 版漏注册）；附带修正 checker 对 `map.set` key/value 的误标 move（clone 语义却标 moved → 误报 + 源 scope drop 被 skip） | codegen (map index/get) / checker (map.set) | — |

---

## BF-001 ~ BF-003：L-006 enum vec/map payload（2026-05-20）

**详见** [`bugfix_L006_enum_vec_map_payload.md`](bugfix_L006_enum_vec_map_payload.md)

三个独立根因：
1. **BF-001**：`emit_enum_ctor` 将 vec 值存入 payload 后，未将源变量 cap 置 0 → scope cleanup 双释放
2. **BF-002**：`emit_enum_clone_val` 的 `needs` 检查缺少 TYPE_VECTOR/TYPE_MAP → 函数参数浅拷贝 → double-free
3. **BF-003**：LS `fn main()` → LLVM `void @main()`，CRT 从 EAX 读垃圾退出码

**教训**：
- 每次新增 payload 类型（vec/map）时，必须审查**三条路径**：构造（ctor move）、克隆（clone）、析构（drop_fn）
- AOT 的 `void main()` 看似工作（多数时候 EAX 恰好为 0），但这是**未定义行为**；测试覆盖率不够时极难发现

---

## BF-004：has_drop enum 函数参数未深拷贝（Phase F.7，2026-05-14）

**症状**：`make_opt_getter(Option(string) opt)` 传参后 caller 和 callee 共享 string data → double-free

**根因**：`AST_CALL` argument codegen 有 `TYPE_STRUCT && has_drop` → `emit_struct_clone_val` 的分支，但没有 `TYPE_ENUM && has_drop` 对应分支

**修复**：新增 `emit_enum_clone_val` 函数（alloca tmp → switch on disc → per-field clone），在 AST_CALL argument 路径加 enum 分支

**教训**：struct 和 enum 的 has_drop 行为应始终对称处理——加一个必须检查另一个

---

## BF-005：enum match arm string binder 共享 data 指针（Phase F.7，2026-05-14）

**症状**：`Some(s) => { return s }` 中 `s` 直接 load 了 enum payload 的 string struct，`return s` 把共享指针传给 caller → env_drop + caller scope cleanup 各 free 一次

**根因**：binder `s` 标 `is_borrowed=true` 防止 arm 内 scope cleanup 释放，但 `return` 把同一指针带出 arm → 两个 owner 同时持有

**修复**：enum match binder 对 TYPE_STRING 字段调 `emit_string_clone_val`，给 binder 独立所有权；`is_borrowed = false`

**教训**：`is_borrowed` 不能解决"值逃逸 arm 作用域"的问题。binder 如果可能被 return / move，必须独立拷贝

---

## BF-006：match arm 含 return 导致 "Terminator in middle of BB"（Phase F.5，2026-05-14）

**症状**：LLVM 验证失败 `"Terminator found in middle of basic block"`

**根因**：enum switch arm 编译完 body 后，如果 body 含 `return` 语句，该 basic block 已有 `ret` 终结符。后续的 `LLVMBuildBr(merge_bb)` 在已终结的 block 上再加一条 br → LLVM 报错

**修复**：所有 `LLVMBuildBr(merge_bb)` 调用前加 `LLVMGetBasicBlockTerminator(...) == NULL` 检查（共 5 处）

**教训**：任何 match/if arm 的 body 编译后，必须检查是否已被 return/break 终结，再 emit 跳转

---

## BF-007：map 形参未标 is_borrowed → callee drop caller 的 buckets（Phase C.7，2026-05-09）

**症状**：传 map 给函数后，caller 的 map 被释放

**根因**：`codegen_fn_decl` 把 `TYPE_VECTOR` 形参标 `is_borrowed=true`（Phase 5.6 加的），但遗漏了 `TYPE_MAP`

**修复**：`is_borrowed` 判断加 `TYPE_MAP`

**教训**：vec 和 map 的 ABI 处理必须始终成对审查

---

## BF-008：Block-call 返回 string 双注册 temp → double-free（Phase C.7，2026-05-09）

**症状**：`print(my_block())` 其中 block 返回 string → double-free

**根因**：`codegen_block_call` 末尾调 `cg_push_temp_string(result)` 注册临时 string；但 `print` 的 `__argtmp` 路径也会注册同一 string → 两次 free

**修复**：`expr_produces_dynamic_string` 对 Block-call 返回 `false`（block call 已由 `cg_push_temp_string` 管理）

**教训**：temp string 跟踪有两条路径（`temp_string_slots` 和 `__argtmp` scope 注册），不能同时命中

---

## BF-009：has_drop_n 只数 string → struct capture env 不生成 drop_fn（Phase C.7，2026-05-09）

**症状**：struct(has_drop) 被闭包捕获后，env_drop 不调 struct.__drop → 字段泄漏

**根因**：计算 env 是否需要 drop_fn 的 `has_drop_n` 计数器只统计了 TYPE_STRING，没有包含 struct/enum 等其他 by-move 类型

**修复**：改为使用 `capture_type_is_by_move_cg` 判断

**教训**：env drop 的"是否需要"判断必须和"怎么 drop"逻辑使用同一组类型条件

---

## BF-010：parser `starts_var_decl` 跨行误判（Phase F.4，2026-05-06）

**症状**：`print(cur(10))` 后接下一行 `i = i + 1`，被 parser 误判为 `print(cur_type) i = ...` 的变量声明

**根因**：`starts_var_decl` 的 `TypeName(Args) varname` 推断路径中，`varname` 可以在下一行，没有同行检查

**修复**：增加 `p->current.line == saved_cur.line` 检查（变量名必须与类型在同一行）

**教训**：LS 分号可选，parser 的前看逻辑必须尊重行边界

---

## BF-011 ~ BF-016：Memcheck Phase B 发现的 5+1 类真实 bug（2026-05 初）

Memcheck 首次上线后，在 `memcheck_edge.ls` 极端测试中发现 19 leaks + 1 double-free + 1 invalid-free，全部修复：

| 编号 | 类型 | 数量 | 根因摘要 |
|------|------|------|----------|
| BF-011 | LEAK | 12 | 自递归 enum box 后 scope cleanup 缺 TYPE_ENUM；`instantiate_enum_template` 循环依赖 |
| BF-012 | LEAK | 5 (4160B) | `try` 早返路径 var_decl auto-clone 丢失 rvalue 函数返回值的堆 |
| BF-013 | CRASH | — | AST_NEW_EXPR 字段初始化先存后 clone → 已释放的源被 scope cleanup 再释放 |
| BF-014 | LEAK | — | map bucket 用 calloc 分配，但 memcheck 只 hook 了 malloc/free |
| BF-015 | CRASH | 34 dfree | `fn sum_tree(Tree t)` 参数未标 borrowed → match subject + scope cleanup 双重递归 free box |
| BF-016 | LEAK | — | `print(p.name)` 读 struct string 字段时 clone 未注册 temp → 泄漏 |

**教训**：自研 memcheck 是发现内存 bug 的最高 ROI 工具。上线后首次运行就发现了 5 个独立根因

---

## BF-017：match rvalue subject enum 退出后 payload 泄漏（Memcheck A，2026-05 初）

**症状**：`match io.read_file(p) { Ok(s) => ... }` 的 `io.read_file` 返回 rvalue enum，match 后无人 drop → payload string 泄漏

**根因**：match subject 是 rvalue（函数调用返回值），不属于任何 scope variable，match 结束后没有 drop 路径

**修复**：merge_bb 之前判断 subject 是否为 scope 中已有 owned 变量；若不是（rvalue temp），调 `emit_enum_drop(ctx, subj_alloca, subj_type)`

**教训**：enum match 的 subject 有两种来源（scope variable / rvalue），必须分别处理生命期

---

## BF-018：静态/动态 CRT FILE* 不兼容（JIT io 路径）

**详见** [`crt_mismatch_bug.md`](crt_mismatch_bug.md)

**症状**：JIT io 测试 crash（STATUS_STACK_BUFFER_OVERRUN），AOT 正常

**根因**：ls.exe 用 `/MT`（静态 CRT）编译，但 LLJIT 从 `ucrtbase.dll`（动态 CRT）解析 `fopen` 等符号。两份 CRT 的 `FILE` 结构体布局不兼容

**教训**：Windows 上静态 CRT + JIT 符号解析 = 双 CRT 陷阱。所有需要 CRT 状态共享的符号必须走 AbsoluteSymbols 注册

---

## BF-019：call site arg_type 未声明 → struct clone 跳过（Phase E.1，2026-05-10）

**症状**：by-ref 重构后 MSVC Release 编译出现 UB → struct 参数未 clone → 泄漏

**根因**：AST_CALL codegen 中 struct-with-drop 克隆路径使用了 `arg_type` 变量，但该变量在条件分支外声明不在作用域内（C 语言允许但 MSVC 在 Release 优化下可能跳过整个路径）

**修复**：在正确位置添加 `Type *arg_type = node->as.call.args[i]->resolved_type;` 声明

**教训**：C17 中变量作用域规则在 MSVC Release 优化下可能导致非直觉行为。codegen 中的临时变量声明必须紧邻使用位置

---

## BF-020 ~ BF-024：std.json 模块 has_drop enum 深层交互 bug 群（2026-05-23）

`std/json.ls` 实现过程中发现的 5 个相互关联的 bug，根因均为 **has_drop enum 通过 vec/map/enum 间接引用时，各子系统缺少对应的深拷贝/释放分支**。

### BF-020：map(K, has_drop_enum) set/get 浅拷贝（WRONG）

**症状**：`entries.set(key, val)` 后，`entries.get(key)` 取回的 JsonValue 值为 Null（堆数据已被释放）

**根因**：`emit_map_helpers_for` 中 `MAP_EMIT_COPY_VAL` 宏只有 string / struct(has_drop) / block 分支，缺少 `TYPE_ENUM && has_drop`。map.set 对 enum 值做了 bitwise copy 而非深拷贝 → 与源共享堆指针。同理 `MAP_EMIT_FREE_VAL` 和 3 处内联 free-val 站点缺少 enum drop

**修复**：
- `MAP_EMIT_COPY_VAL` 加 `val_is_enum_drop` → `emit_enum_clone_val`
- `MAP_EMIT_FREE_VAL` 加 enum drop 调用
- set-update / remove / drop-clear 3 处内联站点加 `val_is_enum_drop` 分支
- `emit_map_helpers_for` 入口处调 `emit_auto_enum_drop_fn` 确保 drop 函数已生成

---

### BF-021：emit_enum_ctor 对 has_drop enum payload 浅拷贝（WRONG）

**症状**：`return Ok(v)` 其中 `v` 是 match binder（borrowed），外层 Object 的 map 值变成 null

**根因**：`emit_enum_ctor` 将 payload 字段 store 到 enum 结构体时，对 string / struct(has_drop) 有 clone 分支，但 `TYPE_ENUM && has_drop` 落入 `else` 分支做了 plain `LLVMBuildStore`（浅拷贝）。borrowed binder 与原始 enum subject 共享堆指针 → subject drop 后 enum ctor 产出的值持有悬垂指针

**修复**：在 struct(has_drop) 分支之后新增 `TYPE_ENUM && has_drop` 分支：
- 非 rvalue 源（binder / 变量）→ `emit_enum_clone_val` 深拷贝
- rvalue 源（AST_CALL / AST_TRY）→ 直接 store（所有权转移）
- IDENT 源如有 moved_flag → 设置为 1

---

### BF-022：vec/arr clone 对 has_drop enum 元素 memcpy（CRASH）

**症状**：`emit_vec_clone_val` 对 `vec(JsonValue)` 做 memcpy 浅拷贝 → 原 vec drop 后克隆 vec 持有悬垂指针 → exit code 116

**根因**：`elem_needs_clone` 检查只有 `TYPE_STRING` 和 `TYPE_STRUCT && has_drop`，缺少 `TYPE_ENUM && has_drop`。has_drop enum 元素的 vec clone 走了 trivial memcpy 路径

**修复**：
- `emit_vec_clone_val` 的 `elem_needs_clone` 加 `TYPE_ENUM && has_drop`
- clone loop body 加 `emit_enum_clone_val` 分支
- `emit_arr_clone_val` 同步修改
- vec filter / vec find 方法同步修改

**教训**：BF-001/BF-004 的教训再次应验——为 struct 加的处理必须同步检查 enum

---

### BF-023：自递归 enum clone 内联 IR 无限递归（CRASH）

**症状**：`import std.json as json` 即导致编译器栈溢出 / exit code 253（仅 import 不调用任何函数）

**根因**：JsonValue 是自递归 enum（`Array(vec(JsonValue))`、`Object(map(string, JsonValue))`）。`emit_enum_clone_val` 内联生成 IR（switch + per-variant clone），当处理 Array 变体时调用 `emit_vec_clone_val`，后者（经 BF-022 修复后）对 has_drop enum 元素调用 `emit_enum_clone_val` → **编译期无限递归**

**修复**：仿照 `emit_auto_enum_drop_fn` 模式，新增 `emit_auto_enum_clone_fn`：
- 生成独立命名的 LLVM 函数 `EnumName.__clone(ptr self) -> enum_t`
- **预注册**到 `enum_type->as.enom.clone_fn`，阻断递归
- 自递归 payload（`pt == enum_type`）通过 box 间接处理：load inner → malloc new box → 递归调用 clone_fn → store
- `types.h` 新增 `enom.clone_fn` 字段
- `emit_enum_clone_val` 改为薄包装：调 `emit_auto_enum_clone_fn` 确保函数存在 → `LLVMBuildCall2` 调用

**教训**：内联 IR 生成不能处理类型图中的环。凡涉及自递归/互递归类型的 codegen helper，必须提取为独立命名函数 + 预注册模式

---

### BF-024：vec.push borrowed match binder 浅拷贝（WRONG）

**症状**：`[{"name": "Bob"}]` 解析后 stringify 得到 `[{"name":null}]`——Object 内的值全部变成 null

**根因**：`_parse_array` 中 `Ok(v) => { items.push(v) }` 的 `v` 是 match binder（codegen 标 `is_borrowed=true`）。`vec.push` 的 codegen 对任何类型都做 `LLVMBuildStore(val, elem_ptr)` 浅拷贝。binder 不拥有堆内存——堆内存由 match subject（`Result(JsonValue, string) first`）拥有。match 退出后 `first` 被 scope drop → 释放了 JsonValue payload 中的 map/vec/string → vec 中的浅拷贝持有悬垂指针

**修复**：在 vec.push 的 codegen 中，store 之前检查 source 是否为 borrowed：
```c
if (arg0->kind == AST_IDENT) {
    CgSymbol *src = cg_scope_resolve(..., arg0->as.ident.name);
    if (src && src->is_borrowed) source_borrowed = true;
}
if (source_borrowed) {
    // 按 elem_type 分派 clone：enum/struct/vec/map
    val = emit_enum_clone_val(ctx, val, elem_type);  // etc.
}
```
同时为 has_drop enum 添加 moved_flag 处理（非 borrowed 路径）

**教训**：
- match binder 的 `is_borrowed=true` 意味着"不拥有堆内存"，任何需要获取所有权的操作（push / set / 赋值）必须先 clone
- string binder 之前独立做了 clone（BF-005），但其他 has_drop 类型的 binder 未同步处理
- vec.push / map.set 的 codegen 对 string / struct / block 各有独立的 ownership transfer 逻辑，新增 has_drop 类型时必须逐一审查

---

## BF-025：用户函数返回 string 双重 temp 注册 → double-free（2026-05-24）

**症状**：`print(my_fn())` 其中 `my_fn` 是用户定义函数，返回 TYPE_STRING → 偶发 double-free

**根因**：`codegen.c:expr_produces_dynamic_string()` 对 `AST_IDENT` callee 的用户函数调用未做特判。`print` 的 `__argtmp` 路径通过 `expr_produces_dynamic_string` 判定为 true → 注册临时 string；而 `AST_CALL` 通用路径（BF-008 修复后）也对用户函数返回值调用 `cg_push_temp_string`。两路径同时注册同一 string → 两次 free。

**修复**（`codegen.c:2235-2243`）：在 `expr_produces_dynamic_string` 的 AST_CALL 分支中，对 `AST_IDENT` callee（用户函数调用）返回 `false`，避免 `__argtmp` 重复注册。用户函数返回值已由 `AST_CALL` 通用路径的 `cg_push_temp_string` 管理。

**教训**：临时 string 的生命期跟踪有两个机制（`temp_string_slots` 和 `__argtmp` scope），任何新增的"函数调用返回 string"路径必须确保只走其中之一。

---

## BF-026：match arm string binder scope cleanup 缺失 → 泄漏（2026-05-24）

**症状**：match arm 内使用了 owned string binder（如 `Some(s) => { print(s); 42 }`），arm 退出后 binder 的堆内存泄漏

**根因**：enum match arm 对 `TYPE_STRING` binder 调 `emit_string_clone_val` 独立克隆（BF-005），克隆后的 binder 拥有堆内存（`is_borrowed=false`）。但 arm 结束处仅 `pop_scope()`，未 emit scope cleanup 代码 → binder 克隆占用的堆内存从未释放。

**修复**（`codegen.c:11255-11257`）：arm body 编译完后，emit_scope_cleanup/pop_scope 之前，对 `is_borrowed=false` 的 string binder 执行正常的 scope cleanup（free cap>0 的字符串）。

**教训**：binder 的 `is_borrowed` 标记直接影响所有权语义——`true` 时 scope cleanup 跳过（不拥有），`false` 时必须正常释放。为 binder 加独立克隆的同时必须确认 cleanup 路径。

---

## BF-027：跨模块 enum drop_fn/clone_fn 持有旧 LLVM module 的 stale 指针（2026-05-25）

**症状**：`import std.json as json` 后使用 has_drop enum 类型 → 偶发 CRASH / 验证器报错 "invalid function pointer"

**根因**：`emit_auto_enum_drop_fn` 和 `emit_auto_enum_clone_fn` 入口处有 `if (drop_fn != NULL) return;` 的提前返回守卫。在跨模块场景下，同一个 `Type *` 结构体可能被多个 LLVM module 共享——前一个 module 编译时已生成 `__drop`/`__clone` 函数并将指针写入 `enum_type->as.enom.drop_fn`。第二个 module 的 codegen 看到 `drop_fn != NULL` 就直接返回，使用了一个**属于不同 LLVM module** 的函数指针 → 验证器拒绝或运行时行为错误。

**修复**（`codegen.c:15007-15011, 15285-15289`）：移除基于 `drop_fn`/`clone_fn` 字段的 early-return，改为无条件通过 `LLVMGetNamedFunction(ctx->module, fn_name)` 检查当前 module 是否已存在同名函数。如果存在则绑定到 Type 字段并返回；否则在当前 module 中创建新函数。

**教训**：跨 module 编译时，Type 是"逻辑类型"而非"每个 module 独有"的。存储 LLVM 函数指针的字段容易在不同 module 间串值。应优先使用 `LLVMGetNamedFunction` 按 module 上下文查询。

---

## BF-028：import 模块的 impl 方法未注册到导入方 impl_registry（2026-05-25）

**症状**：`import std.json as json` 后调用 `JsonValue.null_val()` 或 `x.is_null()` → checker 报 "enum has no method"

**根因**：`forward_pass` 处理 `AST_IMPORT_DECL` 时，遍历被导入模块的顶层声明，但只处理了函数声明和 extern 块的导出注册。`AST_IMPL_DECL`（如 `impl JsonValue { ... }`）被忽略——其方法未通过 `register_method()` 注册到导入方的 `impl_registry`，也未通过 `scope_define()` 暴露为可调用函数。

**修复**（`checker.c:7882-7909`）：在 `forward_pass` 的 import 处理中新增 `AST_IMPL_DECL` 分支——遍历 impl 的所有方法，逐一调用 `register_method(c, impl_name, name, type, is_static, sbk, line, col)` 注册到 impl_registry，同时 `scope_define` 暴露为自由函数以便直接调用。

**教训**：模块系统接入新特性时，import 路径的 `forward_pass` 必须与单文件路径（`check_impl_decl`）保持同步。所有跨模块可访问的声明类型都需在 import 处理中添加对应分支。

---

## BF-029：match-arm string binder 作为 arm 返回值时 scope cleanup 误释放（2026-05-25）

**症状**：`match val { Foo(s) => s }`（arm 体直接返回 binder）→ 返回值持有悬垂指针 / random data

**根因**：BF-026 为 string binder 增加了 scope cleanup（arm 退出时释放克隆的堆内存）。但当 arm 体是 `s`（直接返回 binder）时，body_val 是 binder alloca 中 load 的 LsString 值，其 data 指针与 binder 的堆内存指向同一位置。scope cleanup 释放了 binder 的 cap>0 字符串 → 返回值成为悬垂指针。

**修复**（`codegen.c:11263-11284`）：在 `emit_scope_cleanup` 前检测一个特殊模式——body 是 AST_IDENT 且该 ident 是被拥有的 string binder（`is_borrowed=false`）。如果是，将 binder alloca 中 LsString 的 cap 字段清零（标记为"已移出"），使后续 scope cleanup 跳过 free，同时 body_val 作为 SSA 值保留了原始 cap 被返回给 caller。

**教训**："泄漏"和"双释放"之间只有一线之隔。BF-026 修复泄漏时引入了双释放的新路径。需要在所有权转移点（return / move）始终检查被释放的值是否已被 caller 消费。这个模式与 scope cleanup 对 return 变量的 skip-list 机制本质相同——binder 在函数内表现为变量，需要与普通变量一样的"返回值免清理"保护。

---

## BF-030：`_escape_string` 循环内 `s.substr(i,1)` 临时 string 非确定性双释放（2026-05-25）

**症状**：`json.stringify(obj)` 调用时，约 1/30 概率出现 double-free。`obj_str` 输出为 `{"name":"{\"n","age":30}`（"name" 的值被垃圾覆盖）。memcheck 报告分配点在 `string.substr`，首次释放点为 `0:0 (unknown)`。

**根因**：⚠️ **实际 root cause 未确认**——当前修复仅绕过了问题路径，未触及底层根源。

关键线索：
- `"first freed at 0:0 (unknown)"`：首次释放通过**非 memcheck 插桩路径**发生
- JIT 环境下，LLVM 生成的 helper 函数（map helpers、`JsonValue.__drop/clone`）中的 `cg_emit_free` 在 JIT 链接时可能解析到 **CRT 的 `free`** 而非 memcheck 的 `free`——因为 LLJIT 通过 `orc::CXXRuntimeOverrides` 解析符号，可能绕过 memcheck 的 hook
- 若 map/vec 内部的 realloc/free 走了 CRT 路径，memcheck 无法跟踪这些释放。后续 `cg_flush_temps` 通过 memcheck 的 `free` 释放同一指针时，就产生"双释放"——但实际只释放了一次，memcheck 误报
- `s.substr(i, 1)` 在循环内频繁分配/释放，增加了踩中该路径的概率。换成 `result.append(ch)` 零分配后症状消失，但底层 JIT 符号解析问题仍在

**修复**（`std/json.ls:571`）：将 `result.append(s.substr(i, 1))` 改为 `result.append(ch)`——`ch` 是已通过 `s.at(i)` 读取的字符（`int` 类型），`string.append(int)` 直接追加单字节，**零分配**，完全消除了临时 string 的创建。属于 workaround，非根本修复。

**教训**：
- `s.substr(i, 1)` 看起来很安全，但在热循环中每次调用都产生新分配。优先使用 `s.at(i)` + `append(int)` 替代。
- `"0:0 (unknown)"` 的首次释放点标志着一类特殊问题：**memcheck 盲区**。可能在 JIT 符号解析 / CRT 拦截 / LLVM 内联展开路径中。
- 纯 LS 标准库中的性能/内存问题与编译器 bug 同样影响可靠性——stdlib 代码也应遵循同级别审查。
- **TODO**：调查 JIT 中 `free` 符号解析路径，确认 `cg_emit_free` 是否始终走 memcheck 插桩版本。如确认为 JIT 链接问题，需在 LLJIT 设置中显式注册 memcheck 的 `free`/`malloc` 符号。

---

## BF-031：`find_fn_template` static 前向声明缺失（2026-05-26）

**症状**：MSVC 上无感知（编译通过），但在 GCC/Clang（`-Wall -Wextra -pedantic`）上会因 `-Wimplicit-function-declaration` 产生编译错误。C99+ 标准中隐式函数声明已被移除。

**根因**：`checker.c` 中 `find_fn_template` 定义在 line 6551（`static int find_fn_template(Checker*, const char*)`），但在 line 4306 的 `AST_CALL` 泛型函数分支中被调用，中间相隔 ~2200 行，且无前向声明。MSVC 对 C 标准合规性宽松，不报错；GCC/Clang 严格遵循 C99+ 标准。

**修复**：在 `checker.c` 的 forward declarations 区域（line 1270 附近）添加 `static int find_fn_template(Checker *c, const char *name);`。

**教训**：
- MSVC 的 C17 模式并不真正检查所有 C17 违规。跨平台可移植性需要在 GCC/Clang 上也定期编译验证。
- static 函数在大文件中应保持"先声明后使用"的纪律，尤其当文件超过 5000 行时。

---

## BF-033：`map_type_id` 对 enum 值类型使用默认后缀 → LLVM 类型名冲突（2026-05-26）

**症状**：latent bug——当前测试未触发。若用户在同一文件中同时使用 `map(string, int)` 和 `map(string, SomeEnum)`，两者生成相同的 LLVM struct 名 `LsMapNode_s_i`，第二个 map 会复用第一个的 layout → 字段布局错误 → 数据腐败或 crash。

**根因**：`map_type_id()` 为 `(K, V)` 类型对生成唯一后缀，有 `TYPE_STRING → "s"`、`TYPE_BOOL → "b"`、`TYPE_F32/F64 → "f"`、`TYPE_STRUCT → struct.name` 的分支，但**缺少 `TYPE_ENUM` 分支**。所有 enum 值类型（`JsonValue`、`Option(T)`、用户自定义 enum 等）都落入默认 `"i"`，与 `int` 相同。

**修复**：在 `map_type_id` 中新增 `TYPE_ENUM` 分支，使用 `val_type->as.enom.name` 作为后缀（如 `s_JsonValue`、`s_Option_int_` 等），与 `TYPE_STRUCT` 同一模式。

**当前不触发的原因**：`std.json` 中 `map(string, JsonValue)` 在独立模块编译，与 `map(string, int)` 不在同一 LLVM module。但完全可能在用户代码中触发。

**教训**：
- `map_type_id` 的分支列表必须与 `Type.kind` 枚举同步审查。每次新增类型 kind 时，应检查所有依赖 kind 的 switch/if 链。
- 对称审查法（BF-004 教训）的扩展：为 struct 加了 name 后缀 → 必须同时为 enum 加。

---

## BF-034：`codegen_print_call` `printf_args` 堆缓冲区溢出 → Linux crash（2026-05-26）

**症状**：`print(f">>>{a}{x}{b}{x}{c}{d}")` 这类含多个 f-string 插值的 print 调用在 Linux 上产生 SIGABRT（`free(): invalid next size (fast)`）；Windows 上静默腐败，不 crash。最小复现：6 个变量插值的单条 f-string。

**根因**：`codegen_print_call` 中 `printf_args` 按 `argc * 2` 分配（`argc` 是 `print()` 的参数个数），但每个 f-string 实参实际展开为 `expr_count` 个 LLVMValueRef 槽（格式字符串本身不占槽，每个 `{expr}` 片段占一个槽）。当 f-string 有 6 个插值时，实际需要 6 个槽，但 `1 * 2 = 2` 个槽被分配 → 堆越界写入。

**Linux vs Windows 差异**：glibc ptmalloc2 的 chunk header 紧邻用户数据后面，越界写入立即破坏相邻 chunk 的 size 字段，下次 `free()` 时验证失败 → SIGABRT。Windows CRT heap 有 alignment padding 和 slack 区域，使得小幅越界不立即破坏关键元数据 → 静默腐败，难以察觉。

**修复**（`src/codegen.c` — `codegen_print_call`）：用精确预扫描替代 `argc * 2`：
```c
int max_printf_args = 0;
for (int i = 0; i < argc; i++) {
    AstNode *arg = node->as.call.args[i];
    if (arg->kind == AST_FORMAT_STRING)
        max_printf_args += arg->as.format_string.expr_count;
    else
        max_printf_args += 2;  // format spec + value
}
if (max_printf_args < 1) max_printf_args = 1;
```

**教训**：
- 在使用固定公式估算数组大小时，必须验证公式对所有输入类型的上界是否成立。
- f-string 的"展开后参数数量"与"print 的参数个数"是不同的量，不能混用。
- Linux 堆实现更严格，能更早暴露 Windows 上的隐患。跨平台测试是必要的。

---

## BF-036：`proc.args()` 丢弃第一个用户参数（2026-05-26）

**症状**：`ls run script.ls arg1 arg2`，`proc.args()` 只返回 `["arg2"]`，`arg1` 被丢弃。`proc.program()` 返回 `"arg1"` 而非 `"script.ls"`。

**根因**：`src/main.c` 中调用 `__ls_set_args` 时，将 script 文件名排除在外：

```c
// 错误（修复前）：
int script_argc = (file_idx + 1 < argc) ? argc - file_idx - 1 : 0;
char **script_argv = (script_argc > 0) ? &argv[file_idx + 1] : NULL;
```

以 `ls run script.ls arg1 arg2`（`file_idx=2, argc=5`）为例：
- `script_argc = 5-2-1 = 2`，`g_argv = ["arg1", "arg2"]`
- `g_argv[0]` = "arg1" → `proc.program()` 错误返回 "arg1"
- `proc.args()` 从 `i=1` 开始 → 只返回 `["arg2"]`，**arg1 被丢弃**

**修复**（`src/main.c`）：将 script 文件名作为 `g_argv[0]`，使 layout 与 POSIX `argv` 约定一致：

```c
// 正确（修复后）：
int script_argc = argc - file_idx;
char **script_argv = &argv[file_idx];
```

修复后：`g_argv = ["script.ls", "arg1", "arg2"]`，`proc.program()` 返回 "script.ls"，`proc.args()` 从 `i=1` 返回 `["arg1", "arg2"]`。

**教训**：
- `proc.args()` 的 `i=1` 跳过约定依赖于 `g_argv[0]` 是程序名——调用方必须遵守这个约定。
- `__ls_set_args` 与 POSIX `main(argc, argv)` 共享相同的 layout 约定，违反时应立即发现。
- 新增 CLI 参数传递逻辑时，应同步编写端到端测试验证 `proc.args()` 和 `proc.program()`。

---

## BF-032：`emit_string_clone_val` 对 borrowed 参数（cap=0）跳过克隆 → 悬垂指针（2026-05-26）

**详见** [`bugfix_BF032_string_clone_borrowed_param.md`](bugfix_BF032_string_clone_borrowed_param.md)

**症状**：`json.stringify(obj)` 对包含 Str 值的 Object 产生腐败输出（~15% 概率）。典型表现："Bob" 被替换为 "nam"（"name" key 的前 3 字节）或 "{\"n"（输出 buffer 的前 3 字节）——已释放内存被新分配覆盖的经典模式。

**根因**：`codegen_fn_decl` 对所有 `TYPE_STRING` 参数将 cap 置 0（借用语义），使得 `emit_string_clone_val` 的克隆条件 `cap > 0` 将其误判为静态 .rodata 字面量而跳过克隆。当 borrowed 参数被存入 has_drop enum payload 时（如 `fn str_val(string s) -> JsonValue { return Str(s) }`），enum 内部持有的是 caller 堆 buffer 的裸指针。caller 退出后释放该 buffer → 悬垂指针。

**为什么本地 enum 不触发**：本地 `Str("Bob".copy())` 中 `.copy()` 是 AST_CALL（rvalue transfer），走直接 store 路径，不经过 `emit_string_clone_val`。只有跨函数调用时参数经过 cap=0 借用标记才暴露。

**修复**（`src/codegen.c` — `emit_enum_ctor` string payload 处理）：在非 rvalue 的 else 分支中区分 AST_IDENT 和非 IDENT 源：
- **AST_IDENT**（可能是 borrowed 参数）→ 无条件 malloc+memcpy 深克隆，enum 拥有独立 buffer
- **非 IDENT**（字面量、字段访问等）→ 沿用 `emit_string_clone_val`，cap=0 跳过对 .rodata 仍然正确

**排除的方案**：
- 方案 A（修改 `emit_string_clone_val` 条件为 `cap>0 || len>0`）→ 静态字面量也被克隆 → 新增 leak
- 方案 B（emit_enum_ctor 中对所有 non-rvalue 无条件克隆）→ 同方案 A 的 leak 问题

**验证**：json_obj_one 50/50 PASS（修复前 ~42/50），json_infra memcheck 0 errors，ctest 59/59。

**教训**：
- `cap == 0` 是重载语义（.rodata 静态 vs borrowed 参数），`emit_string_clone_val` 无法区分。在需要获取所有权的场景（enum ctor、struct ctor），不能依赖 cap 判断。
- memcheck 不检测 use-after-free 读取，只有 AddressSanitizer 能可靠捕获。
- "flaky ~15%" 的腐败 = 100% 确定的悬垂指针 + 非确定的 malloc 地址复用。遇到偶发腐败应首先怀疑 UAF。
- 应审查所有调用 `emit_string_clone_val` 后将结果存入 heap-owning 容器的路径。

---

## 附录：排查方法论

以下排查模式在 LS 编译器开发中反复验证有效：

### A. 逐步缩小法（L-006 使用）

```
完整测试 crash
  → 最小复现（同函数内 ctor+match）
  → 分离路径（JIT vs AOT / 本地 vs 参数 / 有 match vs 无 match）
  → 确认内存正确（memcheck clean）后排除内存问题
  → 锁定为 ABI/退出码问题
```

### B. 对称审查法（BF-004/007 使用）

为 struct 加了某个处理 → 检查 enum 是否也需要
为 vec 加了某个处理 → 检查 map 是否也需要

### C. 三路径完备检查法（BF-001/002 使用）

新增 payload 类型后检查：
1. **构造**（ctor）：源变量是否 move？
2. **克隆**（clone）：函数参数传递是否深拷贝？
3. **析构**（drop）：`__drop` 函数是否正确释放？

### D. Memcheck 驱动发现法（BF-011~017 使用）

```
编写极端测试用例（覆盖所有 drop 路径）
  → ls run --memcheck extreme_test.ls
  → 逐个修复 leak/dfree/ifree
  → 直到 "0 leak / 0 double-free / 0 invalid-free"
```

### E. 自递归类型防御法（BF-023 使用）

```
内联 IR 生成 → 发现类型图中有环（A → vec(A) → A → ...）
  → 提取为独立命名 LLVM 函数
  → 预注册函数指针到 Type 结构体（阻断递归）
  → 递归位点改为 call 已注册函数
```

适用场景：drop / clone / eq / hash 等需要按类型递归展开的 codegen helper

### F. 借用语义穿透检查法（BF-024 使用）

```
发现 "值变 null" / "值不正确"（非 crash）
  → 确认值在创建时正确
  → 追踪值经过的每个所有权转移点（push / set / return / 赋值）
  → 检查 source 是否为 borrowed（match binder / &T 参数）
  → 如果 borrowed source 被 move 语义操作消费 → 必须 clone
```

### G. 重载语义穿透检查法（BF-032 使用）

```
发现 "偶发数据腐败"（~N% 概率失败，输出含其他字符串片段）
  → 判定为 use-after-free（已释放内存被新 malloc 覆盖）
  → 追踪 string 在函数边界的 cap 变化（特别是 cap=0 借用标记）
  → 检查所有消费 string 获取所有权的站点（enum ctor / struct ctor / vec.push / map.set）
  → 如果站点依赖 cap>0 判断是否克隆 → 对 borrowed 参数会误判跳过
  → 需要根据 AST 源（IDENT vs literal）区分处理
```

适用场景：任何在 codegen 中使用 `emit_string_clone_val` 的 cap 条件来决定是否克隆的路径
