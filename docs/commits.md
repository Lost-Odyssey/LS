# LS 提交记录

| 日期 | Commit | 描述 |
|------|--------|------|
| 2026-05-24 | `6c25fa8` | **fix: bugs/20 — has-drop enum double-free（Phase H）**。两处根因：① `AST_ASSIGN` 对 has-drop enum 变量赋值未 drop 旧值、未 clone 新值（两方共享同一指针 → double-free）；② `AST_RETURN` 返回具名 has-drop enum 变量时未加 skip-cleanup，scope cleanup 与调用方各 free 一次 → double-free。附加：checker 允许 `impl` 里的 `__drop` 覆盖自动生成版本。`json_e2e_test.ls --memcheck` 结果：0 double-free / 0 leak，ctest 55/55 通过。 |
| 2026-05-24 | — | **fix: Phase E-1/E-2 — vec(has_drop_enum) 内存安全补全**。① 8 处 `elem_needs_drop` 守卫条件补加 `TYPE_ENUM(has_drop)`（覆盖 scope cleanup / clear / truncate / extend / resize / copy / slice / closure env drop）；② 3 处 clone loop（extend/copy/slice）补加 `emit_enum_clone_val` 分支（原来只处理 string/struct）。新增测试 `enum_has_drop_vec_test.ls`（9 种 vec 操作 + memcheck 0/0/0），ctest 55/55 通过。 |
| 2026-05-24 | — | **docs: plan_enum_infra.md — enum 基础设施完善计划**。系统分析 has-drop enum 在 codegen.c 中的 14 处内存安全缺口：8 处 `elem_needs_drop` 守卫漏检 TYPE_ENUM、4 处 vec 方法 clone 路径缺失 enum 分支、var_decl IDENT 不 clone + 未初始化无零值。制定 Phase E-1~E-4 实施计划（2.5 天），含 TDD 测试计划（3 个新测试文件 + memcheck 矩阵）。Step 4 JSON mutation API 推荐 consume-and-return 模式。 |
| 2026-05-24 | `a5f61a1` | **fix: Bug #17 — 拒绝重名 trait 方法**。`register_method` 增加重名检测（同名即报错），调用点用 `continue` 跳过冲突方法的 body checking。策略：严格拒绝（方案A），记录为 L-002。 |
| 2026-05-24 | `9b58fd1` | **test: json e2e 端到端测试 + Phase H 最小重现**。`json_e2e_test.ls` 通过 io/parse/match/stringify 全链路验证 JSON stdlib；`phase_h_repro.ls` 24 行重现 map subscript double-free。 |
