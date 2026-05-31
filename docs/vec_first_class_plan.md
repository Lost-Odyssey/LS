# vec/map 一等值语义 — 架构方案与实施计划（解决 D / E / F）

> **日期**：2026-05-31
> **状态**：设计方案，待审核后实施
> **背景**：std.md 想用 `struct MdDoc { vec(MdBlock) }` + 嵌套 `vec(vec(...))` 富结构时，连锁暴露出
> 编译器对「堆拥有型聚合体（vec/map）」的值语义实现不完整。本文从架构层面给出一次性根治方案，
> 并按正确性 / 性能 / 健壮性 / 可维护性四个维度论证。
> **关联**：feature_inventory L-011a/b/c；`docs/plan_std_md.md`

---

## 1. 问题陈述

LS 的内存模型（RAII + move + `has_drop` + string 三态 cap）**概念上是健全的**。缺陷在 codegen 的
**实现架构**：缺两层抽象 →「值操作」与「子对象 place」不统一、不完整，于是 vec/map（尤其嵌套、
作为字段/payload、作为左值）在很多操作点被漏掉。

本轮已修（commit `0dc78df`，92/92 绿）：
- **A** struct 含容器字段自动 drop；
- **B** 嵌套 `vec(vec(...))` 的 drop + 一个 `emit_clone_value` clone dispatcher 雏形；
- **E**（部分）vec rvalue 临时实参登记 drop（**点修，非架构修**）。

仍未解决：
- **D**（最难，设计层）：`&!struct` 的 vec **字段**原地修改（`doc.items.push()`）不写回；
  `doc.items[i]` 报 "cannot get address of vec"。根因：**没有把"聚合体子对象"当作一等可写存储（place）**。
- **F**：enum/struct 的 payload 内**嵌套** vec 的 clone/drop 仍会泄漏 —— enum/struct 的
  auto clone/drop 各自内联了 payload 处理，未递归走统一值操作。
- **C**（小，独立）：跨模块 `type` 别名不可命名（`md.MdDoc`）。checker 缺口，与内存模型无关。

> 本质：**没有"统一值操作 + 子对象 place"两层抽象**，导致每个操作点（scope-drop / temp-drop /
> element-drop / field-drop / clone-on-read / arg-pass / return / assign / payload-clone）各自重写
> 类型分派，各自都可能漏掉 vec → 霰弹式反模式，正确性依赖"每处都写全"。

---

## 2. 现状盘点（已有的"种子" vs 缺口）

| 能力 | 现状 | 位置 |
|------|------|------|
| place / 左值地址 | **部分存在** `codegen_lvalue_ptr`：支持 ident / 结构体字段(递归+指针解引) / **array** 索引 / `*ptr`。**vec/map 索引返回 NULL** | `codegen.c:3808` |
| clone 分派 | **雏形** `emit_clone_value`（string/vec/has_drop struct/enum）。**map/array 未纳入** | `codegen.c`（本轮新增） |
| drop | **分散**：`emit_struct_drop`/`emit_enum_drop`/`emit_vec_drop_at`(新)/string free/map drop 宏/array 元素 drop，各自实现 | 多处 |
| 每类型 `__drop` 函数 | struct/enum 有 auto `__drop`；vec/map 靠内联循环 | `emit_auto_drop_fn:3381` 等 |
| 临时值 drop | `cg_push_temp_drop`/`cg_flush_temp_drops`（本轮扩 vec） | `codegen.c:2154/2189` |
| 赋值左值 | `AST_ASSIGN` 用 `codegen_lvalue_ptr` 但分支零散 | `codegen.c:13238` |
| 可变方法接收者 | vec/map 方法对"字段/元素接收者"未取 place + 未写回 | vec 方法 `~6775`；map `~9716` |

**结论**：两层抽象的种子都在（`codegen_lvalue_ptr` + `emit_clone_value`），任务是
**补全 + 统一 + 全量改接入**，而非从零造。风险因此可控。

---

## 3. 设计目标与四维权衡

### 3.1 目标
1. vec/map 成为完全一等的堆拥有值：在 **任意嵌套**、作为 **字段/payload/元素**、作为 **左值** 时，
   drop / clone / move / 原地修改 全部正确。
2. 单一权威：每种值操作只有一处实现，新增拥有型 = 改一处。
3. 消除 D / E / F；为后续（如自定义泛型容器）打地基。

### 3.2 非目标
- 不引入 borrow checker / 生命期系统（另案）。
- 不改变 string 三态 / has_drop / move 的语义模型本身。
- 不做 GC。

### 3.3 四维权衡

| 维度 | 设计选择 | 理由 |
|------|----------|------|
| **正确性** | 单一递归 `emit_drop_value`/`emit_clone_value` + 统一 move/owns 判定；唯一所有者释放一次 | 消除"漏一处即泄漏/双释放"；用 memcheck 测试矩阵做回归预言 |
| **性能** | place 原地修改取代 enum COPY+REPLACE（builder push O(1) 摊还，原 O(n²)）；drop/clone 走**每类型生成的 `__drop`/`__clone` 辅助函数**（非全内联）→ 代码体积小、可复用；POD 路径短路（memcpy / no-op） | 原地修改是主要性能提升；辅助函数复用避免 IR 膨胀 |
| **健壮性** | 单点正确 + 全量测试矩阵 + `CG_DEBUG` 追踪每次 drop/clone/move；moved/borrowed 不变式显式化 | 把"分散脆弱"变成"集中可验证" |
| **可维护性** | 4 个权威入口（`emit_place`/`emit_drop_value`/`emit_clone_value`/`emit_move_value`），文档化所有权契约；删除 ~20 处内联分派 | 改一处处处生效 |

---

## 4. 目标架构：两根支柱

### 支柱一：Place（子对象左值）下降

**`codegen_lvalue_ptr` 升级为完整的 place 引擎**：给定一个 place 表达式，返回该存储的**真实可写地址** +
其类型。需补：

- `AST_INDEX` 对 **vec**：先 `codegen_lvalue_ptr(obj)` 取 vec 的 place（*LsVec），load `data`，
  GEP `data[i]` → 元素地址。对 **map**：取 value 槽地址（经 hash 查找/插入返回节点 value 地址）。
- 嵌套自然递归（`a.b[i].c` = 逐层 place）。
- 明确**有效性契约**：返回的元素地址在"容器未 realloc"前有效（push/grow 后失效）—— 与既有
  L-002 借用逃逸约束一致，文档化"持元素地址期间禁止改容器长度"。

**可变方法接收者改造**：`recv.method(...)` 当 method 为可变（push/pop/set/insert/remove/clear/append/...）
且 `recv` 是 place（ident/字段/元素）时：
1. `addr = codegen_lvalue_ptr(recv)`（拿真实地址，不是值拷贝）；
2. 方法在 `addr` 上原地 load 头 → 改 → **store 回 `addr`**（写回是关键，解决 D）；
3. `recv` 为 rvalue 临时（非 place）时：spill 到临时 alloca（即造一个 place）再操作，并登记 temp-drop。

**赋值写回**：`lhs = rhs` → `emit_place(lhs)` → drop 旧值（若 has_drop）→ move/clone rhs 存入。

> D 与 "cannot get address of vec" 同时消失，因为二者本就是同一缺口（vec/map 索引 + 字段方法写回）。

### 支柱二：统一值操作（类型导向、递归、唯一权威）

```
emit_drop_value (ctx, place_ptr, type)     // 释放 place 处拥有的资源
emit_clone_value(ctx, value,   llvm, type) // 返回独立深拷贝（已有雏形，补全 map/array）
emit_move_value (ctx, src_place,dst_place, type) // 拷头 + 失效源（标 moved）
```

`emit_drop_value` 按 `type->kind` 递归，**成为唯一权威**，原 `emit_struct_drop`/`emit_enum_drop`/
`emit_vec_drop_at`/string free/map drop/array 收敛为它的分支或被它调用：
- string → 条件 free(data)（cap>0）
- vec → 循环 `emit_drop_value(elem)` + free(buffer)（cap>0）
- map → 遍历桶链 `emit_drop_value(key/val)` + free 节点 + free 桶
- struct(has_drop) → 调该 struct 的 `__drop`（其体内对每字段调 `emit_drop_value`）
- enum(has_drop) → 调该 enum 的 `__drop`（按活动变体对 payload 调 `emit_drop_value`）
- array → 循环元素 `emit_drop_value`
- Block → drop env
- POD / 非 has_drop → no-op
- 统一 **moved/borrowed 判定**：drop 前检查"是否仍拥有"（string cap、struct/enum moved_flag、
  vec/map cap>0），被移动/借用的跳过。

**每类型辅助函数策略**：struct/enum 已生成 `<T>.__drop`；为 vec/map 也按元素类型**按需生成**
`__drop`/`__clone` 辅助（避免在每个调用点内联展开递归 → 控制 IR 体积、利于复用）。`emit_*_value`
负责"取得/生成对应辅助并 call"。

> **F 自动消失**：enum/struct 的 auto `__drop`/`__clone` 改为对每个 payload/字段调
> `emit_drop_value`/`emit_clone_value` —— 嵌套 vec 随即被正确递归处理，不再各自内联漏掉。

---

## 5. D / E / F 如何被根治（映射表）

| 问题 | 根因 | 由哪根支柱解决 |
|------|------|----------------|
| **D** 字段 vec 原地改不写回 / 元素取址失败 | 无完整 place + 方法不写回 | 支柱一（place 补 vec/map 索引 + 方法接收者取 place 并写回） |
| **E** vec rvalue 临时实参泄漏 | 临时值无统一 drop 归属 | 支柱二（统一 temp-drop 走 `emit_drop_value`）—— 用一条规则取代本轮的点修 |
| **F** enum/struct payload 嵌套 vec clone/drop 泄漏 | auto clone/drop 内联、未递归 | 支柱二（auto `__drop`/`__clone` 改调统一值操作） |
| **C** 跨模块 type alias 不可命名 | checker 不导出/解析模块别名 | 独立小修（与本架构正交，附带做） |

---

## 6. 分阶段实施（每阶段保持 memcheck-green）

> 全程在分支上做（当前在 main，需先开 `feat/vec-first-class`）。每阶段结束跑全量 ctest +
> 新测试矩阵，绿了才进下一阶段。

### Phase 0 — 安全网：容器值语义测试矩阵（1–2 天）
- **先建 `.ls` 测试矩阵再重构**，作为回归预言（全部 `--memcheck`）。
- 维度：`{vec, map, struct, enum, array}` × `{scope-drop, return, assign, arg-by-value,
  arg-by-borrow, element-read(clone), element-write, 原地 push/set}` × `{扁平, 嵌套:
  vec(vec)/vec(struct)/struct{vec}/struct{map}/enum{vec}/map{vec}}`。
- 当前会失败的用例（D/F 场景）先标记 `XFAIL`/单列，锁定"修完应转绿"清单。
- 产出：`tests/samples/container_matrix_*.ls` + ctest 注册。

### Phase 1 — 统一值操作（行为保持 → 顺带修 F）（3–5 天）
1. 引入 `emit_drop_value`，把现有 drop 实现收敛为其分支（先做到**行为等价**，逐个替换 + 跑矩阵）。
2. 补全 `emit_clone_value`（map/array），设为唯一 clone 权威。
3. struct/enum 的 auto `__drop`/`__clone` 改为对字段/payload 调统一值操作 → **F 转绿**。
4. 风险最高（触及所有 drop 路径）：逐点替换、每步跑矩阵 + 92 测试。

### Phase 2 — Place 引擎（解决 D）（4–6 天，核心）
1. `codegen_lvalue_ptr` 补 vec/map 索引取址；嵌套递归验证。
2. 可变方法接收者：取 place + 原地操作 + **写回**；rvalue 接收者 spill 成 place。
3. `AST_ASSIGN` / 复合赋值统一走 place（drop 旧 + move/clone 新）。
4. `AST_INDEX` 读路径：place → load + `emit_clone_value`。
5. **D 与 "cannot get address of vec" 转绿**；解禁 `struct MdDoc { vec }` + 字段 push。

### Phase 3 — move / 临时值统一（收编 E）（2–3 天）
1. 统一"owned rvalue 临时"登记规则：所有 call/get/literal 结果经统一 temp-drop（走 `emit_drop_value`）。
2. 删除本轮 E 的点修（arg 循环里的 vec 专门分支），由统一规则覆盖（含 map/嵌套）。
3. 统一 moved 表示/检查，保证 drop 可靠跳过被移动值。

### Phase 4 — 验收 + std.md 升级（1–2 天）
1. **验收**：把 std.md 改回审定 API（`struct MdDoc { vec(MdBlock) blocks }` + 嵌套
   `vec(vec(MdInline))` lists + `vec(vec(string))` table），用户写 `md.MdDoc`（struct 跨模块可命名，
   C 无需单独做），memcheck clean —— 整个工程的端到端验收。
2. 记录 Q4 性能限制（rvalue 读后即用仍走 clone 而非 move）到 feature_inventory，标"待优化"。
3. 更新文档（feature_inventory L-011*、plan_std_md、本文状态）。

**工期估算**：约 **2–3 周**（Phase 2 最重）。

---

## 7. 风险管理

| 风险 | 缓解 |
|------|------|
| 触及核心 drop/clone，易引入双释放/泄漏，波及 92 测试 | Phase 0 测试矩阵先行；每点替换后立即跑全量；分支隔离 |
| place 写回与既有 ABI（vec 值传 + is_borrowed）交互 | 明确"借用参数永不释放""命名变量由 scope 释放""临时由 temp-drop 释放"三不变式；逐条断言 |
| 元素地址在 realloc 后失效（悬垂） | 文档化 + 在 CG_DEBUG 下加增长检测；长期靠生命期系统（另案） |
| Phase 1 行为漂移 | 强制"行为等价"替换；矩阵 + memcheck 双保险 |
| 体量大、易半途 | 阶段独立、各自可交付可回滚；Phase 1/2 已能解锁主要价值 |

---

## 8. 决策（已敲定 2026-05-31）

| # | 问题 | 决策 |
|---|------|------|
| Q1 | map 是否一起做 | ✅ **一起**：drop / clone / place(索引/value 槽) 同时覆盖 vec 和 map（架构统一，map 现状同样有洞） |
| Q2 | drop/clone 实现形式 | ✅ **每类型生成辅助函数**（`<T>.__drop` / `<T>.__clone`），调用点只 call；控制 IR 体积、可复用、与现有 struct/enum `__drop` 一致 |
| Q3 | C（跨模块 type alias 命名） | ✅ **不单独做**。D 修好后 std.md 改用 `struct MdDoc { vec(MdBlock) blocks }`，struct 类型本就可跨模块命名（`md.MdDoc` 直接可用）→ C 多余。仅当未来想让"类型别名跨模块可引用"成为通用特性时另案 |
| Q4 | move-of-rvalue 免 clone 优化 | ✅ **留后续**：先保证正确性，把"读后即用本可 move 免 clone"记为已知性能限制（feature_inventory），日后专门优化 |

---

## 9. 一句话

> 模型不改，补两层抽象：**子对象 place 下降** 解 D，**统一递归值操作** 解 F 并收编 E；
> 二者的"种子"（`codegen_lvalue_ptr` / `emit_clone_value`）已在，工程是"补全 + 统一 + 全量接入"，
> 以测试矩阵为预言、分阶段保持 memcheck-green 推进。
