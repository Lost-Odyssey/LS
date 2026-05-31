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

## Targets — BROKEN now → must turn green

| sample | issue | current failure | fixed by |
|--------|-------|-----------------|----------|
| t01_struct_field_push | **D** | double-free + push does not persist (len wrong) | Phase 2 (place + method write-back) |
| t02_struct_field_index | **D** | `cannot get address of vec` | Phase 2 (place: vec index) |
| t06_field_assign | **D** | double-free on `d.items = w` | Phase 2 (place assign: drop old + move) |
| t05_struct_map | **D/map** | `cannot get address of map object` | Phase 2 (place: map) |
| t03_enum_nested_vec | **F** | double-free (enum payload nested vec clone/drop) | Phase 1 (auto enum __clone/__drop → unified value ops) |

When a target turns green: move its row to the Baseline table and register it as
`test_cmatrix_<name>` (same driver as the baseline ones).
