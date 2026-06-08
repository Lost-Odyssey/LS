# Phase 3：拆除内建 `vec` —— 可独立派发的任务拆解

> 目标：从编译器中删除内建 `vec`（`TYPE_VECTOR`）的全部特殊实现，使 `std.vec` 的纯
> LS `Vec(T)` 成为唯一动态数组。**不可逆**，故分小步、每步保持「构建通过 + ctest 全绿
> （+ 关键步全量 memcheck）」，可二分回退。
>
> 前置已全部完成：Phase 0/1/1.5/2/2.5 + Vec 全量迁移（[vec_replacement_tracking.md](vec_replacement_tracking.md)，57 文件，ctest 169/169）。
> 本文件是 Phase 3 的施工蓝图，每个任务自带「文件锚点 + 步骤 + 验收命令 + 依赖」，
> 可分派给独立 agent 完成与自检。

---

## 0. 迁移完成度审计结论（2026-06-08，基线 ctest 169/169）

| 范畴 | 状态 | 证据 |
|------|------|------|
| `std/*.ls` 库 | ✅ 100% 迁移 | `grep '\bvec('` 仅命中注释（`std/vec.ls` 自述、`std/plottl.ls:451` 注释）；无 `vec(T)` 类型用法 |
| 已追踪 `.ls` 文件（57） | ✅ 完成 | 见 tracking 文档累计结果 |
| `tests/samples` 仍依赖内建 vec | ⚠️ **3 个真实 + 4 处陈旧注释** | 见 §1，是 Phase 3 的前置清理面 |
| `runtime/builtins.c` | ✅ 无 vec 运行时 | `grep -ci vec` = 0（内建 vec 全部 codegen 内联发射，**无 C 运行时要删**） |
| 编译器内建 vec 实现 | ⛔ 待拆 | scanner/parser/types/checker/codegen，见 §2 锚点表 |

**结论**：库与测试数据层已就绪；Phase 3 = 编译器内部拆除 + 收尾 3 个测试样本。

---

## 1. 前置清理：tests/samples 仍依赖内建 vec 的文件

| 文件 | ctest | 类型 | 处理 |
|------|-------|------|------|
| `enum_borrow_b_test.ls` | `test_enum_borrow_b` | **真实用法**：`vec(Jv)` enum payload + `= []` | → 任务 **P3-0a**（机械迁移到 `Vec(Jv)`） |
| `test_mem_m5_neg_push.ls` | `test_mem_m5_neg` | **故意保留**：测 move-after-use 编译期拒绝（载体=内建 vec move 特例）| → 任务 **P3-0b**（换载体到变量绑定 move；**不改语言语义**）|
| `test_mem_m5_neg_index.ls` | `test_mem_m5_neg` | 同上 | 同 P3-0b |
| `test_mem_m5_neg_branch.ls` | `test_mem_m5_neg` | 同上 | 同 P3-0b |
| `vec_literal_test.ls` | （未注册） | 陈旧注释（首行 `/* vec(T) ... */`，正文已全 `Vec`） | → P3-0a 顺带改注释 |
| `closure_f7_stress_test.ls` | `test_phase_f7_stress` | 陈旧注释（`S4. vec(Block)...`） | → P3-0a 顺带改注释 |
| `std/plottl.ls:451`、`std/vec.ls:4-5,21` | — | 陈旧/自述注释 | → P3-0a 顺带改注释 |

---

## 2. 编译器内建 vec 锚点表（拆除目标）

| 文件 | 计量 | 关键锚点 |
|------|------|----------|
| `src/scanner.c` | 关键字 + 调试名 | `:167` `{"vec",3,TOKEN_VEC}`；`:727` `case TOKEN_VEC: return "VEC"` |
| `src/token.h` | 枚举 | `:69` `TOKEN_VEC` |
| `src/parser.c` | 4 处 | `:1466` 字段名谓词、`:1691`/`:1726` 类型解析、`:3122` import 路径段谓词 |
| `src/types.h` | 2 | `TYPE_VECTOR` 枚举 + 字段（`as.vector` 等） |
| `src/types.c` | 5 | `type_vector` 构造/谓词/`type_to_string`/`type_equals` 分支 |
| `src/checker.c` | 26×`TYPE_VECTOR` + 18×`vec_` | 见 §3.3 行号清单（方法表、借用/move 分支、字面量推断、闭包捕获谓词、for-in） |
| `src/codegen.c` | 70×`TYPE_VECTOR` + 335×`vec_` | 最大块 `codegen_vec_method`（`7702–10370`，~2668 行）；helper：`ls_vec_type:598`、`emit_vec_clone_val:2115`、`emit_vec_drop_at:7556`、`emit_vec_elem_drop_at:7466`、`emit_vec_grow_inline:7636`、`codegen_vec_string_borrow:5963`、`is_vec_string_index:3169`、`emit_global_vec_cleanup:20060` |
| `runtime/builtins.c` | 0 | 无（不需改） |

> 字面量 `[..]`：AST 用 `AST_ARRAY_LIT`，`Vec(T)` 经 checker `checker_tag_user_from_list_literal`（`checker.c:838`）路由到 `__from_list`。**Phase 3 保留通用 `[..]`→`__from_list`**，仅删内建 vec 专属字面量路径。

---

## 3. 任务拆解（按依赖排序；每步「构建+ctest 绿」为关）

### 排序原则（关键）
删 `TYPE_VECTOR` 定义（types）必须**最后**——否则 checker/codegen 仍引用它会编译失败。
顺序为：先迁测试（消除源码层用法）→ 前端停止接受 `vec(` 语法（使内部 vec 机制对源码**不可达**）
→ 自后向前删死代码（checker → codegen → types）→ 文档。每一步删的都是**已不可达**的代码，
故每步都能保持构建通过 + ctest 全绿，可二分。

---

### P3-0a — 迁移 `enum_borrow_b_test.ls` + 清理陈旧注释
- **目标**：消除 tests/samples 中最后一处真实内建 vec 用法 + 所有陈旧 `vec(` 注释。
- **文件**：`tests/samples/enum_borrow_b_test.ls`（`:37` `JArr(vec(Jv) items)`、`:102/:183/:186` `vec(Jv) ... = []`）；注释：`vec_literal_test.ls:1`、`closure_f7_stress_test.ls:8`、`std/plottl.ls:451`、`std/vec.ls:4-5,21`。
- **步骤**：
  1. `enum_borrow_b_test.ls`：顶部确认 `import std.vec`；`vec(Jv)`→`Vec(Jv)`；`= []`→`= {}`；按 [§7 配方](plan_vec_replacement.md) 核对 API（此文件应只有类型名 + 空构造，无 `.length` 等）。
  2. 注释类：把 `vec(T)` 字样改为 `Vec(T)` 或删除，不影响逻辑。
- **验收**：`ctest -C Release -R "test_enum_borrow_b" --output-on-failure` 通过；`ls run --memcheck tests/samples/enum_borrow_b_test.ls` SUMMARY 0/0/0；AOT 一致。`grep -rn '\bvec(' std/ tests/samples/ | grep -v 'Vec('` 仅剩**零**或纯英文叙述。
- **依赖**：无。**可独立完成。**
- **风险**：低（机械迁移，路径成熟）。

---

### P3-0b — 重写 `test_mem_m5_neg`（换 move 载体，**不改语言语义**）
- **背景（实证，2026-06-08）**：LS 当前 move 语义的真实分布：
  | 场景 | 行为 | 源标记 moved |
  |------|------|------|
  | 变量绑定 `b = a`（string/struct/vec/map）| **MOVE** | ✓（`mv4` 拒绝）|
  | 用户函数/方法 by-value 参数（具体 struct、string、泛型 struct `Vec` 整体、泛型方法 `T`）| **CLONE** | ✗（`mv1/2/3/6` 源保持 live）|
  | 内建 `vec.push/insert/set`、`v[i]=s` 的 by-value 实参 | **MOVE（特例）** | ✓（`mv5` 拒绝）|

  3 个负向样本用的载体全是**内建 vec 的 MOVE 特例**（`v.push(s)` / `v[0]=s` / 分支内 `v.push(s)`）。
- **关键定性**：`Vec(T)` by-value 参数 = CLONE，**已与所有用户 struct（含泛型 struct）完全一致**——
  Vec 不是特例。真正的不一致是**内建 `vec.push` 的 MOVE**，它与「用户方法 by-value 一律 clone」
  的通用语义相矛盾。故 **~~VR-LIM-015 的「option A：给 `Vec.push` 加 move 标记」已否决~~**——
  那会使 `Vec` 成为唯一一个 by-value 参数 move 的用户容器，**破坏泛型 struct 一致性**。
- **处理（C，推荐）**：把 3 个负向样本的载体从「内建容器 move」换成**变量绑定 move**（语言里
  真实且与容器无关的 move 路径，`mv4` 已证），断言文案不变（`moved variable 's'` /
  `maybe-moved variable 's'`）：
  - `_push` → `string s = ...; string b = s; print(s)`（绑定 move-after-use）。
  - `_index` → 同类绑定 move（或 has_drop struct 绑定 move），不依赖任何容器索引写。
  - `_branch` → `if cond { string b = s }` 后 `print(s)`（分支快照合并 → `maybe-moved`）。
- **替代（B，可接受）**：直接删 3 样本 + `test_mem_m5_neg` 注册。`maybe-moved` 流敏感合并已由
  `move_phase_b_neg.ls` 独立覆盖；绑定 move-after-use 也有 `borrow_neg_*`/`move_phase_b_*` 等覆盖。
  仅损失「内建容器 move 载体」这一冗余路径（该路径随内建 vec 一并消失，本就不应保留）。
- **不做（语言设计另案，超出 Phase 3）**：若将来想让**用户 by-value move-type 参数也 move**
  （Rust 式 consume），那是影响**全部 struct/函数**的语言级变更，与 Vec 无关，单独立项。
- **验收**：`ctest -R "test_mem_m5"` 全绿；样本 `ls run` 返回非 0 且 stderr 命中期望文案；
  `grep -n '\bvec(' tests/samples/test_mem_m5_neg_*.ls` 零命中（载体已无内建 vec）。
- **依赖**：无（与 P3-0a 并行）。**纯测试改动，不动编译器。**

> **P3-1 之前必须完成 P3-0a + P3-0b**：之后全仓 `grep -rn '\bvec(' src=排除 std/ tests/` 应零真实用法。

---

### P3-1 — 前端停收 `vec(` 语法（scanner + parser）
- **目标**：`vec` 不再是关键字；源码写 `vec(int)` 报「unknown type」。内部 `TYPE_VECTOR`
  机制保留但从此**不可达**。
- **文件/锚点**：
  - `src/scanner.c:167` 删 `{"vec",3,TOKEN_VEC}` 关键字行；`:727` 删 `case TOKEN_VEC` 调试名。
  - `src/token.h:69` 删 `TOKEN_VEC` 枚举值。
  - `src/parser.c`：删 `:1691`/`:1726` 的 `vec(T)` 类型解析分支；`:1466` 字段名谓词去掉
    `!check(p, TOKEN_VEC)`（`vec` 现为 IDENTIFIER，本就允许作字段/方法名）；`:3122`
    `is_import_path_segment` 去掉 `t == TOKEN_VEC`（**`vec` 现走 `TOKEN_IDENTIFIER` 分支，
    `import std.vec` 仍合法**）。
- **关键不变量验收**（务必逐条）：
  1. `import std.vec` 仍解析成功（`vec`→IDENTIFIER）→ `ctest -R "test_stack|test_xmod_generic|test_iter_protocol"` 全绿。
  2. 写 `vec(int) v = []` 的 `.ls` 现报错（新增一个 `tests/samples` 负向 smoke，或手验 `ls run` 非 0）。
  3. 全量 `ctest -C Release --repeat until-pass:2` **169/169**（已无测试用 `vec(` 语法）。
- **依赖**：P3-0a、P3-0b。
- **风险**：中——`is_import_path_segment` 回退是唯一易错点；§审计已确认 `vec` 作为模块名经
  IDENTIFIER 通过。务必跑含 `import std.vec` 的测试。

---

### P3-2 — 删 checker 的 `TYPE_VECTOR` 死分支
- **目标**：删除 26 处 `TYPE_VECTOR` + 相关 `vec_` 辅助（P3-1 后 parser 永不产出 `TYPE_VECTOR`，
  这些分支已死）。
- **文件/锚点**：`src/checker.c` 的 `TYPE_VECTOR` 行（`grep -n TYPE_VECTOR src/checker.c`，
  当前 26 处，分类）：
  - 方法解析：vec 内建方法表 / `check_vec_method` 类分支。
  - 借用白名单（`:6633` 一带）、闭包捕获谓词（`:4403–4425` by-ref 语义）、move 分支。
  - 字面量类型推断（`:6035`/`:8074` `AST_ARRAY_LIT` + `TYPE_VECTOR`）—— **保留** `__from_list`
    用户容器路径，仅删内建 vec 字面量分支。
  - for-in 内建 vec 迭代（`:7903`）—— **保留**用户 `Vec` 的 `Iterator(T)` 协议路径。
- **方法**：逐处删除并让相邻逻辑自然收敛；用 `#if 0` 临时隔离再删，便于二分。
- **验收**：`cmake --build` 无警告新增（除既有）；`ctest --repeat until-pass:2` 169/169。
- **依赖**：P3-1（必须，否则删了 checker 分支但 parser 仍产 `TYPE_VECTOR` → 误判/崩溃）。
- **风险**：中——字面量/for-in/捕获三处与用户 `Vec` 路径**共用代码**，删错会波及 `Vec`。
  每删一类立即 `ctest`。

---

### P3-3 — 删 codegen 的内建 vec 发射（最大块）
- **目标**：删除 70 处 `TYPE_VECTOR` + `codegen_vec_method`（~2668 行）+ 全部 `emit_vec_*`/
  `ls_vec_type`/`codegen_vec_string_borrow`/`is_vec_string_index`/`emit_global_vec_cleanup` vec 专属体。
- **文件/锚点**：`src/codegen.c`
  - 删函数：`ls_vec_type:598`、`emit_vec_clone_val:2115`、`emit_vec_drop_at:7556`、
    `emit_vec_elem_drop_at:7466`、`emit_vec_grow_inline:7636`、`codegen_vec_method:7702`、
    `codegen_vec_string_borrow:5963`、`is_vec_string_index:3169`、`emit_global_vec_cleanup:20060`
    + 它们的前置声明（`:295/:310/:449`）。
  - 删调用点：`AST_INDEX` 的 vec 分支（`:13864` 一带）、scope cleanup 的 vec 分支、
    `cg_push_temp_drop` vec 分支、`capture_type_is_*` vec 分支、字面量发射 vec 分支、
    `emit_drop_value`/`emit_clone_value` 的 `TYPE_VECTOR` case。
  - **保留**：所有用户 `Vec(T)`（`TYPE_STRUCT` + 泛型）路径、`__from_list`、`Iterator` 脱糖、
    `emit_struct_*`、map 路径。
- **方法**：**分子步**，每子步一类 + 立即全量 memcheck（最危险阶段）：
  - 3E-1 删 `codegen_vec_method` + 其唯一调用点（方法调度的 `TYPE_VECTOR` 分支）。
  - 3E-2 删 `AST_INDEX` / 字面量 / 借用（`codegen_vec_string_borrow`）vec 分支。
  - 3E-3 删 drop/clone/cleanup/capture/global 的 `TYPE_VECTOR` case + helper 函数本体。
- **验收**（每子步）：`cmake --build` 通过；`ctest --repeat until-pass:2` 169/169；
  **全量 memcheck 抽样**：对 `tests/samples` 中所有含 `Vec(` 的样本跑 `ls run --memcheck`
  脚本（可写一次性 bash 循环断言 `SUMMARY: 0 leak ... 0 double-free ... 0 invalid`）。
- **依赖**：P3-2。
- **风险**：高——体量最大且与 `Vec`/`map`/`struct` 共享 helper。子步 + memcheck + 二分提交是硬要求。

---

### P3-4 — 删 types 的 `TYPE_VECTOR` 定义（收口）
- **目标**：此时全仓 `grep -rn TYPE_VECTOR src/` 应仅剩 types.h/c 自身定义 → 安全删除。
- **文件/锚点**：`src/types.h`（`TYPE_VECTOR` 枚举 + `as.vector`/elem 字段）、
  `src/types.c`（`type_vector` 构造、`type_to_string`/`type_equals`/谓词的 `TYPE_VECTOR` 分支，
  共 5 处）。
- **验收**：`grep -rn 'TYPE_VECTOR\|type_vector\|TOKEN_VEC' src/` **零命中**；`cmake --build`
  无 unused-warning；`ctest --repeat until-pass:2` 169/169。
- **依赖**：P3-3（必须最后）。
- **风险**：低（纯收口，前序做对则零引用）。

---

### P3-5 — 文档收尾
- **文件**：`CLAUDE.md`（§1.2 特性表把 `vec(T)` 标注「已降级为 std.vec `Vec(T)`」；§7 Move 类型
  表、§8 捕获策略表的 `vec(T)` 行改为 `Vec(T)` 用户容器语义）；
  `docs/vec_replacement_tracking.md`（标 Phase 3 完成）；`docs/plan_vec_replacement.md`（勾掉 3A–3F）；
  `docs/features_history.md`（追加 Phase 3 记录）。
- **验收**：文档内不再宣称内建 `vec` 为语言内建类型；CLAUDE.md 与实际行为一致。
- **依赖**：P3-4。
- **风险**：低。

---

## 4. 全局验收门（Phase 3 完成判据）

1. `grep -rn 'TYPE_VECTOR\|type_vector\|TOKEN_VEC\|\"vec\"' src/` 零命中。
2. `grep -rn '\bvec(' std/ tests/` 仅英文叙述注释，无类型用法。
3. `cmake --build build --config Release` 干净。
4. `ctest -C Release --repeat until-pass:2` **169/169**（或迁移后等值）。
5. 全 `Vec(` 样本 `ls run --memcheck` 全部 `0 leak / 0 double-free / 0 invalid`。
6. 抽样 AOT：`Vec` 重度样本 `ls compile` + 运行结果与 JIT 一致。
7. `ls compile` 一个写 `vec(int)` 的 `.ls` → 报「unknown type」（语法已下线）。
8. 跑 alloc bench 复测性能（plan §6 风险：纯 LS Vec 慢于内建，确认 JIT O2 内联兜底）。

---

## 5. 派发建议

| 任务 | 可独立 | 需编译器改动 | 建议模型/工作量 |
|------|--------|--------------|------------------|
| P3-0a | ✅ | 否 | 小 |
| P3-0b | ✅ | 否（纯测试，换载体到变量绑定 move）| 小 |
| P3-1 | 依赖 0a/0b | 是 | 小~中 |
| P3-2 | 依赖 P3-1 | 是 | 中 |
| P3-3 | 依赖 P3-2 | 是 | **大**（拆 3E-1/2/3 子步分派） |
| P3-4 | 依赖 P3-3 | 是 | 小 |
| P3-5 | 依赖 P3-4 | 否 | 小 |

> 串行链 P3-1→P3-4 不可并行（同改 checker/codegen/types，且语义递进）。P3-0a/P3-0b 可与
> 文档预备并行起步。每个任务一提交，commit 信息标 `phase3: P3-x …`，便于二分回退。
