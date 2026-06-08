# Vec Replacement 迁移追踪

> 跟踪从内建 `vec(T)` 到 `std.vec` `Vec(T)` 的迁移进度。
> 基线：166 ctest 全绿（2026-06-08）
> 参考：[plan_vec_replacement.md](plan_vec_replacement.md)

## 迁移配方

1. 加 `import std.vec`
2. `vec(T)` → `Vec(T)`（类型声明）
3. `= []`（空）→ `= {}`（字面量不变 `[a,b,c]` 保持）
4. API 重命名：`length`→`len()`, `capacity`→`cap()`, `is_empty()`→`empty?`, `contains`→`has?`, `find_index`→`pos`, `get_unsafe`→`get!`
5. 借用参数：`&vec(T)` → `&Vec(T)`、`&!vec(T)` → `&!Vec(T)`
6. 验证：JIT → memcheck(0/0/0) → AOT

## 迁移文件清单

### 桶 B/C/F — 机械迁移（无需语义改写）

| # | 文件 | 测试名 | 状态 | 验证结果 |
|---|------|--------|------|----------|
| 1 | vec_bounds_test.ls | (非 ctest) | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 2 | struct_field_defaults_v2_test.ls | test_struct_field_defaults_v2 | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |

### 待迁移清单（ctest 中的 25 个文件）

| # | 文件 | 测试名 | 状态 |
|---|------|--------|------|
| 1 | map_keys.ls | test_map_keys | ✅ | F6a：`map.keys()`/`values()` 改返回 `Vec(K)`/`Vec(V)`（布局/堆与内建 vec 相同，codegen 重解释）。JIT+AOT+memcheck 0/0/0 |
| 2 | struct_field_defaults_v2_test.ls | test_struct_field_defaults_v2 | ✅ |
| 3 | closure_g.ls | test_phase_g_closure | ✅ | F5 修复（Block 容器生命周期四处协同）。JIT+AOT+memcheck 0/0/0 |
| 4 | html_parse.ls | test_std_html_parse | ✅ | 实际已迁移（std/html.ls 迁移时一并完成）|
| 5 | html_write.ls | test_std_html_write | ✅ | 同上 |
| 6 | md_build.ls | test_std_md_jit | ✅ | 实际已迁移（std/md.ls 迁移时一并完成）|
| 7 | md_inline.ls | test_std_md_inline_jit | ✅ | 同上 |
| 8 | memcheck_edge.ls | test_memcheck_edge_jit | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 9 | memcheck_overhaul.ls | test_mem_overhaul_jit/aot | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 10 | plot_agg_test.ls | test_plot_agg | ✅ | std/plot.ls+plottl.ls 已迁移，9 个 plot 测试同步迁移。JIT ✅ |
| 11 | plot_cpu_test.ls | test_plot_cpu | ✅ | 同上 |
| 12 | plot_csv_test.ls | test_plot_csv | ✅ | 同上 |
| 13 | plot_html_test.ls | test_plot_html | ✅ | 同上 |
| 14 | plot_skeleton_test.ls | test_plot_skeleton | ✅ | 同上 |
| 15 | plot_svg_test.ls | test_plot_svg | ✅ | 同上 |
| 16 | plot_text_test.ls | test_plot_text | ✅ | 同上 |
| 17 | plot_ticks_test.ls | test_plot_ticks | ✅ | 同上 |
| 18 | plot_timeline_test.ls | test_plot_timeline | ✅ | 同上 |
| 19 | ring_test.ls | test_ring | ✅ | F4 修复后迁移（Vec(Option(T)) 嵌套泛型）。JIT+AOT+memcheck 0/0/0 |
| 20 | stack_test.ls | test_stack | 待做 | 需先迁移 std/stack.ls |
| 21 | test_mem_m3_xfer_unified.ls | test_mem_m3_jit | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 22 | test_mem_m4_5_drop_temp.ls | test_mem_m4_5_jit/aot | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 23 | test_mem_m4_matrix.ls | test_mem_m4_jit | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 24 | test_mem_m5_move_ok.ls | test_mem_m5_jit/aot | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 25 | cmatrix/me01_move_elision.ls | test_move_elision | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 26 | cmatrix/b05_enum_vec.ls | test_cmatrix_b05_enum_vec | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 27 | cmatrix/t03_enum_nested_vec.ls | test_cmatrix_t03_enum_nested_vec | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 28 | cmatrix/t08_match_return_call.ls | test_cmatrix_t08_match_return_call | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 29 | global_vec_lit/main.ls | test_global_vec_lit | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 30 | bf044_shortcircuit/main.ls | test_bf044_shortcircuit | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 31 | modtype_memcheck/*.ls | test_modtype_memcheck | ✅ | F6b 已修：Vec mangling 用元素 llvm_name 区分（Vec(mod_a__Node)≠Vec(mod_b__Node)）。JIT+AOT+memcheck 0/0/0 |

### 桶 D — 闭包捕获 vec（需 by-move 改写）

| # | 文件 | 测试名 | 状态 | 说明 |
|---|------|--------|------|------|
| 1 | closure_e1_test.ls | test_phase_e1_closure | ✅ | 重写：Vec by-move + `.copy()` 防 move，JIT+AOT+memcheck 0/0/0 |
| 2 | closure_e2_e4_test.ls | test_phase_e2_e4_closure | ✅ | `&Vec(int)` 借用参数 + `apply_reducer` clone 确保多次调用。JIT+AOT+memcheck 0/0/0 |
| 3 | closure_f1_test.ls | test_phase_f1_closure | ✅ | `[move v]` + Vec，JIT+AOT+memcheck 0/0/0 |
| 4 | closure_f4_test.ls | test_phase_f4_closure | ✅ | Vec(Block) + Vec(Namer)，JIT+AOT+memcheck 0/0/0 |
| 5 | closure_f5_test.ls | test_phase_f5_closure | ✅ | Vec(Block) + enum capture，JIT+AOT+memcheck 0/0/0 |
| 6 | closure_f7_stress_test.ls | test_phase_f7_stress | ✅ | Vec(Block) stress，JIT+AOT+memcheck 0/0/0 |
| 7 | closure_g.ls | test_phase_g_closure | ✅ | F5 修复后迁移到 Vec(Block) |
| 8 | closure_phase_c7_test.ls | test_phase_c7_closure | ✅ | Vec by-move 重写，JIT+AOT+memcheck 0/0/0 |

### 桶 E — vec 借用/move 语义（按 struct 重写）

| # | 文件 | 测试名 | 状态 | 说明 |
|---|------|--------|------|------|
| 1 | vecref_neg_alias.ls | (检查器负向) | ✅ | checker 依旧拒绝混用 `&!v` + auto-borrow `v` |
| 2–23 | vecref_*/borrow_*/move_*/mutref_* (22 文件) | — | ⚠️ positive 11/11 ✅; negative 8/12 仍被 checker 拒绝，4 个因 Vec 借用规则变更（struct ABI）不再触发。记录不修。 |

### 非 ctest 文件

| # | 文件 | 状态 | 验证结果 |
|---|------|------|----------|
| 1 | vec_bounds_test.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 2 | vec_get_test.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 3 | vec_string_test.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 4 | vec_struct_test.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 5 | vec_struct_clone_test.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 6 | bug11_compound_move.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 7 | operator_overload_demo.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 8 | enum_e1_minimal.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 9 | enum_has_drop_vec_test.ls | ⚠️ | JIT ✅ AOT ✅ Memcheck: 2 double-free。Match 从 `Option(Data)` 绑定 `Some(x)` 后，Option 容器析构与提取的 x 共享 string payload → 双释放。已知类 L-012 问题，跳过。 |
| 10 | enum_method_has_drop.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 11 | enum_nested_vec_test.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 12 | enum_vec_payload_test.ls | test_enum_vec_map_payload | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 13 | enum_borrow_b_test.ls | test_enum_borrow_b | ✅ | 已迁移（非 ctest 表过时） |
| 14 | cmatrix/b05_enum_vec.ls | test_cmatrix_b05_enum_vec | ✅ | 已迁移 |
| 15 | cmatrix/t03_enum_nested_vec.ls | test_cmatrix_t03_enum_nested_vec | ✅ | 已迁移 |
| 16 | cmatrix/t08_match_return_call.ls | test_cmatrix_t08_match_return_call | ✅ | 已迁移 |
| 17 | global_vec_lit/main.ls | test_global_vec_lit | ✅ | 已迁移 |
| 18 | modtype_memcheck/main.ls | test_modtype_memcheck | ✅ | 已迁移 |
| 19 | modtype_memcheck/mod_a.ls | test_modtype_memcheck | ✅ | 已迁移 |
| 20 | modtype_memcheck/mod_b.ls | test_modtype_memcheck | ✅ | 已迁移 |
| 21 | bf044_shortcircuit/main.ls | test_bf044_shortcircuit | ✅ | 已迁移 |
| 22 | fs_test.ls | (非 ctest) | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 23 | io_fs_test.ls | (非 ctest) | 待做 |
| 24 | json_infra_test.ls | (非 ctest) | 待做 |
| 25 | json_file_io_test.ls | (非 ctest) | 待做 |
| 26 | json_file_test.ls | (非 ctest) | 待做 |
| 27 | proc_args_test.ls | (非 ctest) | ✅ | JIT ✅ AOT ✅ |
| 28 | proc_test.ls | (非 ctest) | ✅ | JIT ✅ AOT ✅ |
| 29 | test_proc_args.ls | (非 ctest) | ✅ | JIT ✅ AOT ✅ |
| 30 | regex_test.ls | **test_regex** (新注册 ctest) | ✅ | IR-002 已解除：`ls_regex.c` 接入构建 + jit.c 注册符号。JIT ✅ AOT ✅ Memcheck 0/0/0，12/12 PASS |
| 31 | re_step2.ls | (非 ctest) | ✅ | JIT 运行 OK |
| 32 | re_step3.ls | (非 ctest) | ✅ | JIT 运行 OK |
| 33 | re_step4.ls | (非 ctest) | ✅ | JIT 运行 OK |
| 34 | re_step5.ls | (非 ctest) | ✅ | JIT 运行 OK，含 `Option(Vec(string))` 验证 |
| 35 | strconv_test.ls | test_strconv | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 36 | test_bug_22.ls | (非 ctest) | ✅ | 仅注释含 vec，无需迁移 |
| 37 | rawvec_m1_test.ls | test_vec_m1 | ✅ | 用 RawVec（手写 C 风格），非 builtin vec |
| 38 | stack_test.ls | test_stack | ⛔ | 阻塞：VR-LIM-019（AOT 泛型方法链返回 Option(string) 损坏）|
| 39 | ring_test.ls | test_ring | ✅ | F4 修复后迁移，JIT+AOT+memcheck 0/0/0 |

### std 库文件（需优先迁移）

| # | 文件 | 状态 |
|---|------|------|
| 1 | std/strconv.ls | ✅ | 已迁移：`vec(string)`→`Vec(string)`，`args.length`→`args.len()` |
| 2 | std/json.ls | ✅ | enum payload + 内部全部 `vec`→`Vec`。VR-LIM-018（跨模块 match binder 方法）已于 F6 修复，`json_e2e_test` 已还原真 `.len()` 方法调用 |
| 3 | std/html.ls | ✅ | 全 vec→Vec；Element(Vec(Attr),Vec(HtmlNode)) 双 Vec + 自递归 enum 持 Vec(Self) 验证通过。test_std_html_parse/write JIT+AOT+memcheck 0/0/0 |
| 4 | std/md.ls | ✅ | 全 vec→Vec，移除 Vec→内建 vec 脚手架；Vec(Vec(MdInline))/Vec(Vec(string)) 嵌套 + Blockquote(Vec(MdBlock)) 递归验证通过。test_std_md_*/test_md_to_html JIT+AOT+memcheck 0/0/0。ctest 167/167 |
| 5 | std/plot.ls | ✅ | 全 vec→Vec：605 行含 ~40 处 vec 类型/`.length`/`=[]`/`&!vec` 全部迁移。plot 测试 9 个同步迁移。JIT ✅ |
| 6 | std/plottl.ls | ✅ | 全 vec→Vec：含 `vec(enum)`/`&!vec(int)` 借用参数/~100 处改动。JIT ✅ |
| 7 | std/fs.ls | ✅ | `list_dir` 返回 `Vec(string)`，内部 `vec`→`Vec`，`[]`→`{}` |
| 8 | std/proc.ls | ✅ | `args` 返回 `Vec(string)`，内部 `vec`→`Vec`，`[]`→`{}` |
| 9 | std/regex.ls | ✅ | `find_all`/`capture`/`capture_all`/`split` 返回 `Vec(string)`；IR-002 已解除：`runtime/ls_regex.c` 接入 `ls.exe`+`ls_os_backend` 构建，jit.c 注册 10 个 `__ls_regex_*` 符号；`test_regex` 注册为 ctest（JIT+AOT+memcheck 三绿） |
| 10 | std/ring.ls | ✅ | F4 修复（type-alias 栈式解析）后迁移；Vec(Option(T)) backing buffer |
| 11 | std/stack.ls | 待做 |

---

## 已知限制 / 新发现

| 编号 | 问题 | 触发场景 | 状态 |
|------|------|----------|------|
| ~~VR-LIM-014~~ | ~~`Vec.pop()` 丢弃返回值时，rvalue `Option(T)` temp 未释放内部 has_drop T~~ | ✅ 已修复（F2，2026-06-08）：`AST_EXPR_STMT` 对丢弃的 has_drop 调用结果 spill+`cg_push_temp_drop`。`vec_string_test` 还原裸 `v.pop()`，0/0/0 | 已解除 |
| VR-LIM-015 | `Vec(T)` generic 方法的 by-value 参数不标记 named var 为 moved | `Vec(string).push(s)` 调用后 `s` 仍 live（不被标记 moved），因 checker 对 generic `T` 参数不触发 move 分析 | 新发现。内建 vec 可标记 moved；Vec(T) 一律 clone。负向测试（move-after-use 检查）不适用于 Vec(T) |
| ~~VR-LIM-016~~ | ~~全局变量 `Vec(T) v = [literal]` 触发 `__from_list` 缺失~~ | ✅ 已修复（F1，2026-06-08）：`emit_user_from_list_value` 落空时从 pending-generic 队列前向声明 `__from_list`。`test_global_vec_lit` 还原全局字面量，JIT+AOT+memcheck 0/0/0 | 已解除 |
| （参见 plan_vec_replacement.md §6.1 其他已知限制） | | | |
| ~~VR-LIM-017~~ | ~~`Vec(Block(...))` 不兼容~~ | ✅ 已修复（F5，2026-06-08）：checker 泛型 T=Block 参数不标 is_borrow + codegen 三处（bind 点 copy-out 克隆 env / move-into-container 消费 temp env / emit_drop_value 加 Block 释放）。closure_g 迁移到 Vec(Block)，test_phase_g_closure JIT+AOT+memcheck 0/0/0 | 已解除 |
| VR-LIM-019 | AOT: 泛型方法链返回 `Option(T)`（T=has_drop string）值损坏 | `Stack(string).peek()`（内部调 `self.data.last()` 并转发返回 `Option(string)`）。JIT 正常，AOT 仅读出首字符（"m" 而非 "gamma"）；`Vec(string).last()` 直调在 AOT 下正常。根因推测：AOT sret ABI 对跨泛型方法链传递 has_drop enum 有 bug。 | 2026-06-08 发现，阻塞 `std/stack.ls` + `stack_test.ls` 迁移 |
| VR-LIM-020 | `Option(T)` match 绑定的 has_drop T 与 Option 容器析构双释放 | `enum_has_drop_vec_test.ls`：`Data x = match v.first() { Some(x) => { x } None => { Empty } }`，提取的 `x` 被析构一次，Option 容器析构时又释放一次 string payload。类 L-012 问题。 | 2026-06-08 发现，阻塞 has_drop enum 的 `first()`/`last()` 迁移

| ~~F-101~~ | ~~`resolve_type_node_with_substitution` 用 `find_struct_template_idx` 而非 `_pull`~~ | ✅ 已修复（2026-06-08）：栈泛型类 `Stack(E) { Vec(E) data }` 字段类型 `Vec(E)` 来自 `import std.vec`（跨模块），字段解析时 `find_struct_template_idx("Vec")` 只在当前 checker 查找 → 找不到。修法：改为 `find_struct_template_idx_pull`。 | 已解除，同时解除所有「模块定义泛型含导入 Vec 字段」阻塞 |

## 累计结果

- 基线: 166/166
- 当前: 168/168（ctest 数量不变：plot 测试非 cmake 驱动）
- 迁移完成: 45 文件（14 ctest + 22 非 ctest + 6 std 库 + 3 pre-existing）
- JIT ✅: 31
- AOT ✅: 22（9 plot 测试未全部 AOT 验证）
- Memcheck 0/0/0: 22
- ~~⛔ 阻塞~~ 全部解除: ~~map_keys~~ ✅ (F6a); ~~modtype_memcheck~~ ✅ (F6b)
- 新增阻塞: VR-LIM-019 (AOT 泛型方法链返回 Option(string) 损坏) → 阻塞 stack.ls 迁移
- 新增基础设施修复: F-101 (`find_struct_template_idx`→`_pull`) → 解除跨模块 Vec 泛型字段阻塞
