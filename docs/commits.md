# LS 提交记录

| 日期 | Commit | 描述 |
|------|--------|------|
| 2026-05-24 | `a5f61a1` | **fix: Bug #17 — 拒绝重名 trait 方法**。`register_method` 增加重名检测（同名即报错），调用点用 `continue` 跳过冲突方法的 body checking。策略：严格拒绝（方案A），记录为 L-002。 |
| 2026-05-24 | `9b58fd1` | **test: json e2e 端到端测试 + Phase H 最小重现**。`json_e2e_test.ls` 通过 io/parse/match/stringify 全链路验证 JSON stdlib；`phase_h_repro.ls` 24 行重现 map subscript double-free。 |
