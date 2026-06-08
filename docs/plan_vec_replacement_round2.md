# 计划（第二轮）：std 库优先的 Vec 迁移 + 基础设施修复

> 状态：定稿 2026-06-08。承接 [plan_vec_replacement.md](plan_vec_replacement.md)（总蓝图）+
> [vec_replacement_tracking.md](vec_replacement_tracking.md)（逐文件追踪）。
> 基线 ctest 166/166。本文聚焦**第二轮**：先打通 std 库对内建 vec 的依赖，配套一轮
> 基础设施修复，使后续测试迁移无阻塞。每个条目自包含、可单独派发。

---

## 0. 核心分析结论（排序依据）

### 0.1 「先迁 std 库」是对的——且比预想更解耦

grep 核查（2026-06-08）确认：**std 库之间没有 vec-typed 公共 API 的相互依赖**。
- `plotfmt.ls` 完全不用 vec（叶子）。
- `md.to_html` 自带 MdBlock/MdInline→HTML 直出，**零 std.html 依赖**（md.ls:955）。
- json / html / md / fs / proc / regex 各自只 `import io` 或 `std.c`。

**推论**：真正的 ABI 耦合只发生在「**单个 std 库 ↔ 调用它的测试**」之间，不存在 std→std
连锁。因此：
1. 每个 std 库 + 它的直接测试 = **一个原子提交单元**（公共 API 的 `vec(T)→Vec(T)`
   会强制 ripple 到 caller，必须同批改）。
2. std 库之间**可任意并行**，无需排队。
3. 内部私有的 vec 用法可独立迁移；只有出现在**公共签名**（参数/返回/字段/enum payload）
   的 vec 才形成对 caller 的强制耦合。

### 0.2 阻塞物分类：真 bug vs 迁移规约

把 §6.1 与 tracking 的 VR-LIM 重新分类——只有「真 bug」需要本轮修基础设施：

**A. 真编译器 bug（修复后才能迁对应 std/测试）**
| 编号 | 影响面 | 阻塞对象 |
|------|--------|----------|
| VR-LIM-016 | 全局 `Vec(T) v=[lit]` 找不到 `__from_list` | 任何全局 vec 字面量（广泛潜在） |
| VR-LIM-014 | `v.pop()` 丢弃返回值 → `Option(T)` rvalue temp 内 has_drop T 泄漏 | stack.ls + 任何 discard-pop（广泛潜在） |
| VR-LIM-008 | 整个 has_drop struct rvalue 直接喂给 `print`/builtin → `Vec.get` 内 string clone 泄漏 | 打印整元素的样本（广泛潜在） |
| VR-LIM-013 | `Vec(Option(T))` 嵌套泛型实参替换错推成内层 T | ring.ls |
| VR-LIM-017 | `Vec(Block(...))` 的 `push` 赋值 Block 参数被 checker 拒 | closure_g + 任何 Vec(Block) |
| map_keys 阻塞 | `m.keys()` 仍返回内建 vec，无法赋给 Vec | test_map_keys |
| modtype 阻塞 | 跨模块同名 `Vec(T)` 单态化冲突 | test_modtype_memcheck |
| VR-LIM-015 | 泛型 by-value 参数不标记源 moved（move 分析缺失） | 负向 move 测试的语义有效性 |

**B. 迁移规约（非 bug，改测试/绕行即可，不修编译器）**
- VR-LIM-003（越界容错差异）、004（resize 显式 fill）、005（闭包写捕获→桶 D）、
  006（闭包返回 string 形参脆弱）。按 §7 配方/桶 D 处理。

**C. 已解除**（无需关注）：001,002,007,009,010,011,012。

---

## 1. Round 2 基础设施修复（fix sprint）

按「杠杆 / 阻塞广度」排序。**F1~F3 是广泛潜在 bug，建议在动任何 std 库前先修**（否则
json/html/md 这些"看似简单"的迁移会随机踩雷）；F4~F6 是定点阻塞，可与各自消费方配对推进。

### F1 — 全局 `Vec(T) = [literal]` 的 `__from_list`（VR-LIM-016）✅ 已修复（2026-06-08）
- **现象**：局部正常，全局 `Vec(int) nums=[1,2,3]` 报 missing `__from_list`。
- **根因**：`__ls_global_stmts`（含全局 init `emit_global_var_init`）在 codegen
  Pass 中**早于** G1.5 pending-generic-method 发射（`codegen.c:21955`）生成，此时
  `Vec(T).__from_list` 的单态化 body 尚不存在 → 共享的 `emit_user_from_list_value`
  按名查找落空报错。局部 var-decl 之所以正常，是因为 `main` body 同样晚于全局 init，
  且方法被需求处前向声明。
- **修法**：`emit_user_from_list_value`（`codegen.c:1223`）在 `LLVMGetNamedFunction`
  返回 NULL 时，先调 `cg_declare_pending_generic_method(ctx, fl_name)` 从 checker 的
  pending 队列**前向声明**该方法（body 仍在 G1.5 统一发射），声明不到才报错。镜像其他
  泛型调用点（`codegen.c:11674/11807`）与 `cg_ensure_user_struct_drop_decl` 的既有模式。
- **验收**：`tests/samples/global_vec_lit/main.ls` 还原为全局字面量
  `Vec(int)=[1,2,3]` / `Vec(string)=[...]` / `Vec(Tag)=[...]`（has_drop 结构体），
  JIT+AOT+memcheck 0/0/0；全量 ctest 166/166 无回归。

### F2 — `v.pop()` 丢弃返回值时 rvalue `Option(T)` temp 泄漏（VR-LIM-014）✅ 已修复（2026-06-08）
- **现象**：`v.pop()` 作语句（非赋值右值）时，`Option(string)` rvalue 内 `Some` 的
  string buffer 不 `__drop`，泄漏 16 字节。`Option(T) _ = v.pop()` 则 0/0/0。
- **根因**：`AST_EXPR_STMT`（`codegen.c:16052`）丢弃返回 owned has_drop 值（by value）的
  调用时，返回的 rvalue 聚合无人绑定/析构——`cg_flush_temps` 只清 temp-string 与已注册的
  temp-drop，而调用本身不会把自己的返回值注册进 temp-drop。
- **修法**：`AST_EXPR_STMT` 在 `codegen_expr` 后，若被丢弃表达式是 `AST_CALL` 且
  `resolved_type` 为 has_drop struct/enum 或 vec/map，则 spill 到 entry-block alloca +
  `cg_push_temp_drop`，由随后的 `cg_flush_temps` 析构。**严格限定 `AST_CALL`**：只有调用
  产生全新 owned rvalue；裸 ident/field-read 是对 live 绑定的借用，绝不能在此析构。
  `TYPE_STRING` 排除（丢弃的 string 调用已由 temp-string 机制释放，避免双释放）。
- **验收**：`vec_string_test.ls` 还原裸 `v.pop()`（去掉 `Option(T) _=` 绕行）；
  另测 chained `v.map(int)(...)` 丢弃、`filter` 丢弃、函数返回 `Option(string)` 丢弃，
  全部 JIT+AOT+memcheck 0/0/0；全量 ctest 166/166 无回归（含大量既有丢弃调用，未现双释放）。
- **附带收益**：stack.ls 的 `self.data.pop()`（stack.ls:41 丢弃）直接受益。

### F3 — 整 has_drop struct rvalue 喂 print/builtin 泄漏（VR-LIM-008 残留）✅ 已修复（2026-06-08）
- **现象**：`print(vp[0])`（整个 `Person` rvalue 直接做 builtin 实参）泄漏
  `Vec(Person).get`/`__index` 内的 string clone；而 `v[i].field` / `f(v[i])` /
  `Person p=v[i]` 已 0/0/0（test_vec_owndrop）。
- **根因**：print 的 struct 分支（`codegen.c:5334`）`codegen_expr(arg)` 求值得到
  deep-clone 的 struct SSA 值，`codegen_print_struct_value` 只读字段打印，从不析构 →
  owned 字段（string/vec）泄漏。
- **修法**：打印后判断——若 `t->as.strukt.has_drop` 且 `arg->kind` 为 owned-rvalue
  产生者（`AST_INDEX` / `AST_CALL`），把 SSA 值 spill 到 entry-block alloca 并
  `emit_drop_value` 立即析构。**严格限定 index/call**：裸 ident 或对 live 绑定的
  field-read 是借用，析构会破坏/双释放源。vec/map 元素 print 路径实测无此泄漏（用
  `print(vv[0])` 验证 0/0/0），无需改。
- **验收**：`vec_struct_clone_test.ls` 还原 `print(vp[0])`（index-clone）+ 另测
  `print(make())`（call-return），输出正确（clone 独立性：改 p2 不影响 vp[0]），
  JIT+AOT+memcheck 0/0/0；全量 ctest 166/166 无回归。

### F4 — `Vec(Option(T))` 嵌套泛型实参替换（VR-LIM-013，ring 前置）
- **现象**：`Vec(Option(T)).get/set` 单态化后元素类型错推成内层 `T`，报
  `cannot initialize 'tmp'(int) with Option(int)` / `cannot assign *string to *Option(string)`。
- **方向**：泛型方法单态化的类型实参替换不是**递归**的——`T := Option(int)` 时，方法体内
  对 `T` 的引用被替换为 `int`（取了 `Option` 的内层）而非整体 `Option(int)`。审查
  `try_instantiate_method_level_generic` / Vec 单态化里 substitute 类型的逻辑，确保
  复合类型（`Option(X)`/`Vec(X)`）作为实参时整体绑定、不下钻。
- **验收**：还原 ring.ls 的 `vec(Option(T))` backing → `Vec(Option(T))`，`test_ring`
  JIT+AOT+memcheck 0/0/0。

### F5 — `Vec(Block(...))` 的 push 赋值 Block 参数（VR-LIM-017，closure_g 前置）
- **现象**：`Vec(Fn).push(|x|...)` → `self.data[len]=x`（x 是 `T=Block` 参数）触发
  checker `cannot assign Block parameter`。
- **方向**：checker 对「Block 参数被赋值/move 进容器」的禁令在泛型 `T=Block` 单态化体里
  误伤。区分「闭包工厂把 param 当 Block 值赋出」（合法 move-into-container）与既有
  F.2 param-move 禁令。允许 has_drop-by-move 语义下 Block 参数 move 进 `self.data[i]`。
- **验收**：`closure_g.ls` 还原 `Vec(Block)`，`test_phase_g_closure` 三绿。

### F6 — `map.keys()→Vec` + 跨模块同名 Vec 单态化（map_keys / modtype 前置）
- **F6a `map.keys()`**：内建 `map(K,V).keys()` 当前返回内建 vec。两条路线择一——
  (i) 让 `keys()` 返回 `Vec(K)`（需 map runtime 改造，breaking）；
  (ii) 临时桥接：提供 `Vec.from_builtin(vec)` 拷贝构造，迁移期 `Vec(string) ks = Vec.from_builtin(m.keys())`。
  **推荐 (ii)**（低风险、可逆），(i) 留到 map 整体改造。
- **F6b ✅ 已修复（2026-06-08）**：根因——`checker_instantiate_struct` 的实例化 mangled
  name 用 `type_name`（裸名 "Node"），两个模块各自的 `Node` 都 mangle 成 `Vec(Node)` →
  cache 撞名 → 第二个实例化命中第一个，混淆不同元素类型。修法：mangle 时对 struct/enum
  元素类型改用 B-2 的模块前缀 `llvm_name`（`Vec(mod_a__Node)` ≠ `Vec(mod_b__Node)`），
  primitives 仍用裸 `type_name`。`modtype_memcheck` 已迁移到 Vec 并三绿，ctest 168/168。
  （原描述）两个模块各自 `Vec(int)` 单态化
  撞同一 LLVM 符号/类型名。复用 B-2 的 `llvm_name` 前缀化思路，或给单态化实例按
  「定义模块 + 实例化点」唯一命名，避免符号合并。
- **验收**：`test_map_keys`、`test_modtype_memcheck` 解除阻塞，三绿。

### F7 — 泛型 by-value 参数的 move 分析（VR-LIM-015，可延后）
- **现象**：`Vec(string).push(s)` 后 checker 不标记 `s` moved（内建 vec 会标）。
- **影响**：仅负向 move 测试（move-after-use）的语义有效性；正确性/内存无问题（一律 clone）。
- **方向**：checker move 分析对泛型 `T` by-value 参数（且 `T` 解析为 move 类型时）补打
  `moved_out`。**优先级最低**，不阻塞任何 std 迁移，留到桶 E 负向测试重写时一并处理。

---

## 2. std 库迁移顺序（风险递增；每库 + 自身测试 = 一原子提交）

> 前置：F1/F2/F3 已修（消除广泛潜在雷）。各 std 库无 std→std vec 耦合，故下列顺序仅按
> **风险 / 所需新能力** 排序，可并行。每库迁完即跑该库测试三绿 + 全量 `--repeat until-pass:2`。

| 序 | std 库 | 直接测试 | 所需前置 | 风险 | 备注 |
|----|--------|----------|----------|------|------|
| S1 | fs.ls | fs_test / io_fs_test（非 ctest） | 无 | 低 | 仅 `vec(string)` 返回 |
| S2 | proc.ls | proc_*/test_proc_args（非 ctest） | 无 | 低 | 仅 `vec(string)` |
| S3 | regex.ls | regex_test/re_step2..5（非 ctest） | 无 | 低 | 多个 `vec(string)` 返回，自包含 |
| S4 | json.ls | json_*（非 ctest 多） | F3（打印元素时） | 中 | enum payload `vec(JsonValue)`/`vec(string)`；内部 `copy()`；`object_keys` 返回值耦合 caller |
| S5 | html.ls | test_std_html_parse/write | F3 | 中 | enum payload `vec(Attr)`/`vec(HtmlNode)`；`HtmlDoc{vec roots}` 字段 |
| S6 | md.ls | test_std_md_*/test_md_to_html | F3 | 中高 | 已半迁移；含 `vec(vec(MdInline))` 嵌套 + `Blockquote(vec(MdBlock))` 递归 enum；验证 `Vec(Vec(...))` 深 clone/drop |
| S7 | plot.ls + plottl.ls | plot_*（9 个 ctest） | D2 借用重写 | 中高 | 含 `&!vec(string) g`/`&!vec(int)` 可写借用参数 → `&!Vec`；plottl 已半迁移 |
| S8 | stack.ls | test_stack | **F2** | 中 | `vec(T)` 字段 + `self.data.pop()` 丢弃 |
| S9 | ring.ls | test_ring | **F4** | 中 | `vec(Option(T))` backing buffer |

**S7 借用重写要点**（D2）：plot/plottl 的 `fn _put(&!vec(string) g,...)` / `_sort_int(&!vec(int) v)`
等可写借用参数，迁移为 `&!Vec(string)` / `&!Vec(int)`。按 struct 借用语义（pointer ABI），
确认 `&!Vec` 参数在被调用内 `push`/index-set 正常、调用方仍 live。这是把内建 vec 借用
规则切到 struct 借用规则的实测点。

---

## 3. 测试桶迁移（std 全绿后，承接总蓝图 §4）

std 库迁完后，依赖它们的 ctest（plot_*/html/md/json/ring/stack）随 S4~S9 一并绿。
剩余按 plan_vec_replacement.md §4 分桶：

- **桶 B/C/F**（机械）：§7 配方逐文件，可批量并行。
- **桶 D**（闭包捕获 vec）：需 F5（Vec(Block) 涉及时）+ D1 by-move 改写。
- **桶 E**（borrow/move 负向）：按 D2 struct 借用语义重写；F7 修完后负向 move 测试才完整。
- **跨模块**：modtype_memcheck 待 F6b。

---

## 4. 推荐执行顺序（一条优化路径）

```
① fix sprint 第一波（广泛潜在）：F1 → F2 → F3        [解除 json/html/md 随机雷]
② std 易迁批（并行）：S1 fs / S2 proc / S3 regex     [非 ctest，快速验证管线]
③ std 中风险批：S4 json → S5 html → S6 md           [enum payload + 嵌套 Vec]
④ fix sprint 第二波（定点）：F4 → F5 → F6
⑤ std 阻塞批：S7 plot/plottl(D2借用) → S8 stack(F2) → S9 ring(F4)
⑥ 测试桶 B/C/F 机械迁移（批量并行）
⑦ 测试桶 D（F5+D1）、桶 E（D2+F7）、modtype（F6b）
⑧ Phase 3：拆除内建 vec（总蓝图 §5 Phase 3，分 3A~3F）
```

**每步关卡**：该批三绿（JIT+AOT+memcheck 0/0/0）+ 全量 ctest `--repeat until-pass:2`。
每个 fix 一提交、每个 std 库一提交，便于二分回退。

---

## 5. 与既有文档的衔接

- 总蓝图 [plan_vec_replacement.md](plan_vec_replacement.md)：Phase 0/1/1.5/2.5 已完成；本文是
  其 **Phase 2 的 std-优先细化排序** + 新增 fix sprint（F1~F7）。
- 追踪 [vec_replacement_tracking.md](vec_replacement_tracking.md)：每完成一项更新「状态」列与
  「已知限制」表（VR-LIM 修复后划掉）。
- VR-LIM 修复后，在 §6.1 表对应行标 ~~删除线~~ + 验收测试名，与 001/002 等已解除项一致。
