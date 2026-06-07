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
| 1 | map_keys.ls | test_map_keys | ⛔ 阻塞 | `m.keys()` 返回内建 vec，无法直接赋值给 Vec。需等 map 改造 |
| 2 | struct_field_defaults_v2_test.ls | test_struct_field_defaults_v2 | ✅ |
| 3 | closure_g.ls | test_phase_g_closure | ⛔ 阻塞 | `Vec(Block)` 不兼容 (VR-LIM-017) |
| 4 | html_parse.ls | test_std_html_parse | 待做 |
| 5 | html_write.ls | test_std_html_write | 待做 |
| 6 | md_build.ls | test_std_md_jit | 待做 |
| 7 | md_inline.ls | test_std_md_inline_jit | 待做 |
| 8 | memcheck_edge.ls | test_memcheck_edge_jit | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 9 | memcheck_overhaul.ls | test_mem_overhaul_jit/aot | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 10 | plot_agg_test.ls | test_plot_agg | 待做 | 需先迁移 std/plot.ls/plottl.ls |
| 11 | plot_cpu_test.ls | test_plot_cpu | 待做 | 同上 |
| 12 | plot_csv_test.ls | test_plot_csv | 待做 | 同上 |
| 13 | plot_html_test.ls | test_plot_html | 待做 | 同上 |
| 14 | plot_skeleton_test.ls | test_plot_skeleton | 待做 | 同上 |
| 15 | plot_svg_test.ls | test_plot_svg | 待做 | 同上 |
| 16 | plot_text_test.ls | test_plot_text | 待做 | 同上 |
| 17 | plot_ticks_test.ls | test_plot_ticks | 待做 | 同上 |
| 18 | plot_timeline_test.ls | test_plot_timeline | 待做 | 同上 |
| 19 | ring_test.ls | test_ring | 待做 | 需先迁移 std/ring.ls |
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
| 31 | modtype_memcheck/*.ls | test_modtype_memcheck | ⛔ 阻塞 | 跨模块同名类型 Vec(T) 单态化冲突 |

### 桶 D — 闭包捕获 vec（需 by-move 改写）

| # | 文件 | 测试名 | 状态 | 说明 |
|---|------|--------|------|------|
| 1 | closure_e1_test.ls | test_phase_e1_closure | 待做 | 闭包捕获 vec |
| 2 | closure_e2_e4_test.ls | test_phase_e2_e4_closure | 待做 | |
| 3 | closure_f1_test.ls | test_phase_f1_closure | 待做 | |
| 4 | closure_f4_test.ls | test_phase_f4_closure | 待做 | |
| 5 | closure_f5_test.ls | test_phase_f5_closure | 待做 | |
| 6 | closure_f7_stress_test.ls | test_phase_f7_stress | 待做 | |
| 7 | closure_g.ls | test_phase_g_closure | 待做 | |
| 8 | closure_phase_c7_test.ls | test_phase_c7_closure | 待做 | |

### 桶 E — vec 借用/move 语义（按 struct 重写）

| # | 文件 | 测试名 | 状态 | 说明 |
|---|------|--------|------|------|
| 1 | vecref_neg_alias.ls | (检查器负向) | 待做 | |
| 2 | vecref_neg_copy_out_mut.ls | | 待做 | |
| 3 | vecref_neg_copy_out_readonly.ls | | 待做 | |
| 4 | vecref_neg_elem_assign_readonly.ls | | 待做 | |
| 5 | vecref_neg_implicit_mut.ls | | 待做 | |
| 6 | vecref_neg_push_readonly.ls | | 待做 | |
| 7 | vecref_neg_readonly_upgrade.ls | | 待做 | |
| 8 | vecref_pos_downgrade.ls | | 待做 | |
| 9 | vecref_pos_forward.ls | | 待做 | |
| 10 | vecref_pos_methods.ls | | 待做 | |
| 11 | vecref_pos_push.ls | | 待做 | |
| 12 | vecref_pos_read.ls | | 待做 | |
| 13 | vecref_pos_string_elem.ls | | 待做 | |
| 14 | vecref_pos_write_elem.ls | | 待做 | |
| 15 | borrow_caller_live_test.ls | | 待做 | |
| 16 | borrow_neg_move.ls | | 待做 | |
| 17 | borrow_neg_move_explicit.ls | | 待做 | |
| 18 | move_explicit_test.ls | test_vec_move | 待做 | |
| 19 | move_phase_b_loop_neg.ls | | 待做 | |
| 20 | move_phase_b_neg.ls | | 待做 | |
| 21 | move_phase_b_test.ls | | 待做 | |
| 22 | move_struct_test.ls | | 待做 | |
| 23 | mutref_step2_neg_move.ls | | 待做 | |

### 非 ctest 文件

| # | 文件 | 状态 | 验证结果 |
|---|------|------|----------|
| 1 | vec_bounds_test.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 2 | vec_get_test.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 3 | vec_string_test.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 4 | vec_struct_test.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 5 | vec_struct_clone_test.ls | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 6 | bug11_compound_move.ls | 待做 | |
| 7 | operator_overload_demo.ls | 待做 | |
| 8 | enum_e1_minimal.ls | 待做 | |
| 9 | enum_has_drop_vec_test.ls | 待做 | |
| 10 | enum_method_has_drop.ls | 待做 | |
| 11 | enum_nested_vec_test.ls | 待做 | |
| 12 | enum_vec_payload_test.ls | test_enum_vec_map_payload | 待做 |
| 13 | enum_borrow_b_test.ls | test_enum_borrow_b | 待做 |
| 14 | cmatrix/b05_enum_vec.ls | test_cmatrix_b05_enum_vec | 待做 |
| 15 | cmatrix/t03_enum_nested_vec.ls | test_cmatrix_t03_enum_nested_vec | 待做 |
| 16 | cmatrix/t08_match_return_call.ls | test_cmatrix_t08_match_return_call | 待做 |
| 17 | global_vec_lit/main.ls | test_global_vec_lit | 待做 |
| 18 | modtype_memcheck/main.ls | test_modtype_memcheck | 待做 |
| 19 | modtype_memcheck/mod_a.ls | test_modtype_memcheck | 待做 |
| 20 | modtype_memcheck/mod_b.ls | test_modtype_memcheck | 待做 |
| 21 | bf044_shortcircuit/main.ls | test_bf044_shortcircuit | 待做 |
| 22 | fs_test.ls | (非 ctest) | 待做 |
| 23 | io_fs_test.ls | (非 ctest) | 待做 |
| 24 | json_infra_test.ls | (非 ctest) | 待做 |
| 25 | json_file_io_test.ls | (非 ctest) | 待做 |
| 26 | json_file_test.ls | (非 ctest) | 待做 |
| 27 | proc_args_test.ls | (非 ctest) | 待做 |
| 28 | proc_test.ls | (非 ctest) | 待做 |
| 29 | test_proc_args.ls | (非 ctest) | 待做 |
| 30 | regex_test.ls | (非 ctest) | 待做 |
| 31 | re_step2.ls | (非 ctest) | 待做 |
| 32 | re_step3.ls | (非 ctest) | 待做 |
| 33 | re_step4.ls | (非 ctest) | 待做 |
| 34 | re_step5.ls | (非 ctest) | 待做 |
| 35 | strconv_test.ls | test_strconv | ✅ | JIT ✅ AOT ✅ Memcheck 0/0/0 |
| 36 | test_bug_22.ls | (非 ctest) | 待做 |
| 37 | rawvec_m1_test.ls | test_vec_m1 | 待做 |
| 38 | stack_test.ls | test_stack | 待做 |
| 39 | ring_test.ls | test_ring | 待做 |

### std 库文件（需优先迁移）

| # | 文件 | 状态 |
|---|------|------|
| 1 | std/strconv.ls | ✅ | 已迁移：`vec(string)`→`Vec(string)`，`args.length`→`args.len()` |
| 2 | std/json.ls | 待做 |
| 3 | std/html.ls | 待做 |
| 4 | std/md.ls | 待做 |
| 5 | std/plot.ls | 待做 |
| 6 | std/plottl.ls | 待做 |
| 7 | std/fs.ls | 待做 |
| 8 | std/proc.ls | 待做 |
| 9 | std/regex.ls | 待做 |
| 10 | std/ring.ls | 待做 |
| 11 | std/stack.ls | 待做 |

---

## 已知限制 / 新发现

| 编号 | 问题 | 触发场景 | 状态 |
|------|------|----------|------|
| VR-LIM-014 | `Vec.pop()` 丢弃返回值时，rvalue `Option(T)` temp 未释放内部 has_drop T | `v.pop()` 不作为赋值右值直接丢弃时，Some 内的 string/vec/map 等不触发 __drop | 新发现。绕行：`Option(T) _ = v.pop()` 赋值给变量 |
| VR-LIM-015 | `Vec(T)` generic 方法的 by-value 参数不标记 named var 为 moved | `Vec(string).push(s)` 调用后 `s` 仍 live（不被标记 moved），因 checker 对 generic `T` 参数不触发 move 分析 | 新发现。内建 vec 可标记 moved；Vec(T) 一律 clone。负向测试（move-after-use 检查）不适用于 Vec(T) |
| ~~VR-LIM-016~~ | ~~全局变量 `Vec(T) v = [literal]` 触发 `__from_list` 缺失~~ | ✅ 已修复（F1，2026-06-08）：`emit_user_from_list_value` 落空时从 pending-generic 队列前向声明 `__from_list`。`test_global_vec_lit` 还原全局字面量，JIT+AOT+memcheck 0/0/0 | 已解除 |
| （参见 plan_vec_replacement.md §6.1 其他已知限制） | | | |
| VR-LIM-017 | `Vec(Block(...))` 不兼容——`push` 内部赋值 Block 参数被 checker 拒绝 | 2026-06-08 迁移 `closure_g.ls` 时发现：`Vec(Fn).push(|x| x + base)` 触发 `cannot assign Block parameter`。Vec.push 的方法体 `self.data[self.len] = x` 中 `x` 是 `T` 类型参数，当 `T=Block` 时 checker 拒绝赋值 Block 参数。 | 本轮不迁移含有 `Vec(Block)` 的文件。保留内建 vec。待编译器允许 Block 参数赋值后再迁。 |

---

## 累计结果

- 基线: 166/166
- 当前: 166/166
- 迁移完成: 20 文件（14 ctest + 5 非 ctest + 1 std 库）
- JIT ✅: 18
- AOT ✅: 18
- Memcheck 0/0/0: 18
- ⛔ 阻塞: map_keys, modtype_memcheck (跨模块 Vec 单态化冲突)
