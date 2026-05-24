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
