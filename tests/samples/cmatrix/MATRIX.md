# Container value-semantics test matrix (Phase 0 safety net)

Oracle for the vec/map first-class effort (`docs/vec_first_class_plan.md`).
Each sample runs under `ls run --memcheck`. Baseline-clean cases are registered
as ctests now (regression lock for Phase 1/2). Broken cases are the turn-green
target list — register them as ctests in the phase that fixes them.

Status captured: 2026-05-31 (branch `feat/vec-first-class`, before refactor).

## Baseline — CLEAN now (locked via ctest `test_cmatrix_*`)

| sample | covers |
|--------|--------|
| b01_vec_scope | vec(string) build + scope drop |
| b02_vec_nested_get | vec(vec(string)) build + `.get` + drop (B) |
| b03_vec_rvalue_arg | `f(v.get(i))` nested-vec rvalue arg (E) |
| b04_struct_vec_drop | struct{vec(string)} build + scope drop (A) |
| b05_enum_vec | enum vec(string) payload build + drop |
| b06_map_scope | map(string,string) build + drop |
| t04_map_vec_value | map(string, vec(int)) set + contains + drop |
| t03_enum_nested_vec | **F fixed (Phase 1)**: enum payload nested vec(vec) clone + drop |
| t01_struct_field_push | **D fixed (Phase 2)**: `&!struct` field vec `.push()` persists + clean |
| t02_struct_field_index | **D fixed (Phase 2)**: `doc.items[i]` read via place |
| t05_struct_map | **D fixed (Phase 2)**: struct map field method + drop |
| t06_field_assign | **D fixed (Phase 2)**: `d.items = w` drops old + moves new |

## Targets — all turned green ✅

D (t01/t02/t05/t06) and F (t03) are fixed and registered above. Remaining work
is Phase 3 (move/temp unification — subsumes E's ad-hoc fix) and Phase 4
(std.md upgrade to `struct MdDoc` + nested-vec lists/table as end-to-end
acceptance).

When a target turns green: move its row to the Baseline table and register it as
`test_cmatrix_<name>` (same driver as the baseline ones).
