# std.map M-5 builtin-map migration inventory

Purpose: track every ctest/e2e LS sample that still uses the builtin `map(K,V)`
surface while M-5 migrates tests to pure-LS `std.map.Map(K,V)`.

Rules:
- Migrate one test at a time.
- After each migration, run that test's ctest target.
- If a migration is blocked by a real compiler/std.map gap, record the blocker in
  `docs/plan_std_map.md` Section 13, mark it here, and continue with another test.
- `docs/plan_std_map.md` is gitignored in this workspace; this inventory is the
  tracked migration ledger.

## CTest/e2e samples

| Status | CTest target | Sample | Notes |
|---|---|---|---|
| DONE | `test_map_keys` | `tests/samples/map_keys.ls` | migrated to `Map`; `ctest -R '^test_map_keys$'` passed |
| DONE | `test_cmatrix_b06_map_scope` | `tests/samples/cmatrix/b06_map_scope.ls` | migrated to `Map`; ctest passed |
| DONE | `test_cmatrix_t04_map_vec_value` | `tests/samples/cmatrix/t04_map_vec_value.ls` | migrated to `Map`; ctest passed |
| DONE | `test_cmatrix_t05_struct_map` | `tests/samples/cmatrix/t05_struct_map.ls` | migrated to `Map`; ctest passed |
| DONE | `test_move_elision` | `tests/samples/cmatrix/me01_move_elision.ls` | migrated map move/copy-out regression to `Map`; ctest passed |
| DONE | `test_phase_e1_closure` | `tests/samples/closure_e1_test.ls` | migrated to by-move `Map` capture + borrow helper; ctest passed |
| DONE | `test_phase_e2_e4_closure` | `tests/samples/closure_e2_e4_test.ls` | migrated to `Map` Block by-value parameter; ctest passed |
| DONE | `test_phase_f1_closure` | `tests/samples/closure_f1_test.ls` | migrated `[move] Map` factory closures; ctest passed |
| DONE | `test_phase_f4_closure` | `tests/samples/closure_f4_test.ls` | migrated `Map(string, Block)`; ctest passed |
| BLOCKED | `test_phase_c7_closure` | `tests/samples/closure_phase_c7_test.ls` | B-MAP-M5-001: builtin map by-ref closure capture has no `Map` equivalent |
| DONE | `test_phase_g_closure` | `tests/samples/closure_g.ls` | migrated map of closure values to `Map`; ctest passed |
| DONE | `test_enum_vec_map_payload` | `tests/samples/enum_vec_payload_test.ls` | migrated enum map payload to `Map`; ctest passed |
| DONE | `test_std_json` | `tests/samples/json_infra_test.ls` | migrated recursive enum payload to `Map(string, JV)`; `ctest -R '^test_std_json$'` passed |
| DONE | `test_std_json` | `std/json.ls` | B-MAP-M5-004 root cause fixed (has_drop fixpoint); `JsonValue.Object` now uses `Map(string, JsonValue)`; consumers (`json_e2e_test`, `phase_h_repro`) migrated `entries[k]`→`match entries.get(k)`; json_infra/basic/e2e memcheck 0/0/0; ctest passed |
| DONE | `test_regex` | `tests/samples/regex_test.ls` and `std/regex.ls` | `capture_named` returns `Map`; ctest passed |
| DONE | `test_proc_args` | `tests/samples/env_test.ls` and `std/env.ls` | `env.all()` returns `Map`; ctest passed |
| DONE | `test_implicit_empty_init` | `tests/samples/implicit_empty_init_test.ls` | migrated builtin map segment to `Map`; ctest passed |
| DONE | `test_inferred_init` | `tests/samples/inferred_init_test.ls` | migrated builtin map segment to `Map`; ctest passed |
| DONE | `test_memcheck_edge_jit` | `tests/samples/memcheck_edge.ls` | migrated map case to `Map`; ctest passed |
| DONE | `test_mem_overhaul_jit`, `test_mem_overhaul_aot` | `tests/samples/memcheck_overhaul.ls` | migrated map case to `Map`; both ctests passed |
| DONE | `test_mem_m4_jit` | `tests/samples/test_mem_m4_matrix.ls` | migrated map cases to `Map`; ctest passed |
| DONE | `test_mem_m5_jit`, `test_mem_m5_aot` | `tests/samples/test_mem_m5_move_ok.ls` | migrated map move case to `Map`; both ctests passed |
| DONE | `test_struct_byval_arg` | `tests/samples/struct_byval_arg.ls` | migrated struct map field to `Map`; ctest passed |
| DONE | `test_struct_field_readthrough` | `tests/samples/struct_field_readthrough.ls` | migrated map field to `Map`; ctest passed |
| DONE | `test_bf046_map_struct_val` | `tests/samples/bf046_map_struct_val/main.ls` | migrated has_drop struct/enum values to `Map`; ctest passed |
| BLOCKED | `test_map_iter` | `tests/samples/map_iter_test.ls` | B-MAP-M5-005: `each()` side-effect assertion relies on builtin map by-ref closure capture |

## Not directly registered as CTest targets

These remain useful manual/e2e samples, but are not direct ctest targets in the
current build graph.

| Status | Sample | Notes |
|---|---|---|
| DONE | `tests/samples/map_test.ls` | migrated original map e2e to `Map`; direct `ls run --memcheck` clean |
| DONE | `tests/samples/map_minimal.ls` | migrated to `Map`; direct `ls run --memcheck` clean |
| DONE | `tests/samples/map_dynstr_test.ls` | migrated to `Map`; direct `ls run --memcheck` clean |
| BLOCKED | `tests/samples/mapref_pos_*.ls` | builtin `&map` borrow positives; no direct `&Map` parity until user-container borrow/negative diagnostics are designed |
| BLOCKED | `tests/samples/mapref_neg_*.ls` | builtin `&map` borrow negatives; no direct `&Map` parity until user-container borrow/negative diagnostics are designed |
