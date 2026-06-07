# 设计：`Vec`（用户 has_drop struct）作为 enum payload

> 状态：起草 2026-06-07（分支 `feat/rawvec`）。
> 来源：[plan_vec_replacement.md](plan_vec_replacement.md) §6.1 VR-LIM-011。
> 关联：[plan_vec_ownership_drop.md](plan_vec_ownership_drop.md)（同属值语义 drop/clone 正确性）。
> 规模：codegen（enum auto-drop/clone 对用户 has_drop struct payload 的派发），中等。

---

## 1. 问题
`enum Numbers { Nums(Vec(int)) }` / `enum Mixed { M(string, Vec(string)) }` 把 `Vec` 作为
payload 后，JIT 直接访问冲突（崩溃/错值）。`enum_vec_payload_test` 用内建 `vec(int)` payload
正常，换成 `Vec` payload 即坏。

## 2. 根因分析（带 file:line）

enum 的自动 drop / clone 对各类 payload 分派见 `emit_auto_enum_drop_fn`
（`codegen.c:17371`，payload 分支 17451-17506）与 `emit_auto_enum_clone_fn`（`codegen.c:17584`）。
gating 谓词 `cg_type_owns_heap_for_enum`（`codegen.c:17312`）**已正确识别** has_drop struct
（`TYPE_STRUCT → has_drop`，17320），所以 `Vec` payload 会进入需要 drop/clone 的 case。问题
在分派的**具体处理**：

### 2.1 drop 侧：drop_fn 为 NULL 时的 inline fallback 漏 free 裸 buffer ★
payload 为 `TYPE_STRUCT && has_drop` 时调 `emit_struct_drop(field_ptr, pt)`（`codegen.c:17468`）。
`emit_struct_drop`（`codegen.c:3845`）：drop_fn 非 NULL → 调用之（正确，`Vec.__drop` 会 free
裸 buffer）；**drop_fn == NULL → 走 inline fallback**（`codegen.c:3862+`），而该 fallback：
- `if (field_type->kind == TYPE_POINTER) continue;`（`codegen.c:3871`）——把 `Vec` 的
  `*T data` 字段**当普通指针跳过**，**不 free 裸 buffer** → 泄漏；
- 且不调用 `Vec` 的用户 `__drop`（裸 buffer 的释放逻辑只在用户 `__drop` 里）。

在 enum auto-drop 的发射时机，generic `Vec(int)` 的 `drop_fn` 可能尚未在**本 LLVM 模块**
绑定（跨模块/泛型实例化的 Type* 持有的是别模块的句柄或 NULL，参见 17527 的 lazy-gen 注释
与 clone 侧 17588 的"stale LLVMValueRef across modules"注释）→ 落入 fallback → 漏 buffer。

### 2.2 clone 侧：对 has_drop struct payload 未派发用户 `__clone` ★崩溃源
`emit_auto_enum_clone_fn`（`codegen.c:17584`）若对 `Vec` payload 做**字段级浅拷**
（直接拷 `{data,len,cap}`），两个 enum 副本会**共享同一 `data` 裸 buffer** → 各自 `__drop`
free 一次 → **double-free / JIT 崩溃**（即"直接访问冲突"现象）。`Vec` 的深拷只能靠用户
`__clone`（`std/vec.ls:386`，内部 `copy()` 逐元素重建 buffer）。须确认 clone 分支对
has_drop struct payload 调 `emit_struct_clone_val`（它会派发用户 `__clone`），而非字段浅拷。
对比 CLAUDE.md「has_drop struct 字段所有权」BF：按值传 struct 时 `emit_struct_clone_val`
已被要求深拷 vec/map/has_drop-enum 字段——但 `Vec` 的裸 `*T` 字段唯有用户 `__clone` 能深拷。

## 3. 设计
让 enum auto-drop / auto-clone 对 **has_drop struct payload 一律派发该 struct 的用户钩子**
（`__drop` / `__clone`），并保证这些钩子在 enum 所在 LLVM 模块**已发射/可解析**：

1. **drop**：payload 为 has_drop struct 时，确保 `emit_struct_drop` 拿到非 NULL drop_fn——
   在 enum auto-drop 内，若 `pt->as.strukt.drop_fn` 为 NULL，**先按需惰性发射**
   `emit_auto_drop_fn(ctx, pt)`（与模块函数内 struct 局部 drop 的惰性生成同源，CLAUDE.md
   「模块函数内 struct 局部 drop 泄漏」修复），**绝不**落到会跳过 `*T` 的 inline fallback。
   对有用户 `__drop` 的 struct（如 `Vec`），drop_fn 必须是用户 `__drop` 的包装。
2. **clone**：payload 为 has_drop struct 时，调 `emit_struct_clone_val`（派发用户 `__clone`，
   见其对 has_drop 的 user-`__clone` dispatch），**禁止**字段级浅拷 `Vec`。同样保证
   `Vec.__clone` 在本模块已发射（NULL 则惰性生成）。
3. **跨模块/泛型**：`Vec(int)` 是导入方实例化的 generic struct（见
   [plan_xmodule_generics]）。其 `__drop`/`__clone` 须随 enum payload 的使用在本模块
   **被实例化并发射**——把"enum payload 含 generic struct"纳入 generic 方法的 pending 发射
   触发条件（确保 `Vec(int).__drop`/`.__clone` 进入本模块的 pending-gm 队列）。

## 4. 实现步骤
1. **复现**：`tests/samples/enum_vec_payload_test.ls`（已存在，§6.1 暂留内建 vec）回迁
   `Numbers(Vec(int))` / `Mixed(string, Vec(string))`，`run`（JIT）当前崩溃/错值，
   `--memcheck` 泄漏。`emit-ir`（`LS_DUMP_ON_FAIL` 思路）看 enum 的 `__drop`/`__clone` 是否
   走了 fallback / 浅拷。
2. **drop 修复**：`emit_auto_enum_drop_fn` 的 struct payload 分支（`codegen.c:17468`）——调
   `emit_struct_drop` 前，若 `pt->as.strukt.drop_fn==NULL` 则 `emit_auto_drop_fn(ctx, pt)`；
   同时审 `emit_struct_drop` fallback：对**含裸 `*T` + 用户 `__drop`** 的 struct 永不应落
   fallback（断言/兜底惰性生成）。
3. **clone 修复**：`emit_auto_enum_clone_fn` 的 struct payload 分支——确保走
   `emit_struct_clone_val`（派发用户 `__clone`），NULL 则惰性生成 `Vec.__clone`。
4. **pending 触发**：确认 enum payload 中的 `Vec(int)` 触发其 `__drop`/`__clone` 进入本模块
   pending-gm 发射（否则符号在本模块缺失 → 解析失败/崩溃）。
5. memcheck 0/0/0 + JIT/AOT 一致；覆盖：单 payload `Nums(Vec(int))`、多 payload
   `M(string, Vec(string))`、嵌套 `Vec` payload 的 `match` 解构 + 借用主体（enum-borrow）。

## 5. 工作量 / 风险
中（定位 + drop/clone 两分支的惰性生成 + pending 触发）。风险中-高：
- enum drop/clone 是 RAII 核心，改错会大面积 double-free——**逐步**：先 drop（漏 buffer，
  泄漏可控），再 clone（双释放，崩溃，最危险），每步全量 memcheck + JIT/AOT。
- 跨模块泛型符号发射顺序敏感（pending-gm 时机）——参照 [plan_xmodule_generics] 既有机制。
- `match` 借用主体（enum-borrow Phase A/B）下 payload 不应被 drop/clone（零拷贝路径），勿
  误对借用主体调 `Vec.__drop`。

## 6. 验收
- `enum_vec_payload_test` 回迁 `Vec` payload 后 JIT+AOT+memcheck 全绿。
- 回归：现有 `std.json`（`JsonValue` 含 `vec(JsonValue)` payload）等 enum-容器组合不破。
  > 注：std.json 当前用内建 `vec`，本特性修好后是否把其 payload 也迁 `Vec` 属 Phase 2 桶 F，
  > 不在本文范围；但本修复必须保证 enum + has_drop struct payload 的通用正确性。
