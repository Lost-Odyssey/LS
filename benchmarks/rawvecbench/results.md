# RawVec(T) vs builtin vec(T) — benchmark results (Gate M3)

> AOT-compiled (`default<O2>`), Windows x64 Release. `benchmarks/rawvecbench/bench.ls`.
> Workload: push N elements then sum (int) / push N (string).

## Results

| workload | N | builtin vec | RawVec | ratio raw/vec |
|----------|---|-------------|--------|---------------|
| **int** push + sum | 3,000,000 | ~9–10 ms | ~8–9 ms | **0.80–1.0× (parity)** |
| **string** push | 1,000,000 | ~6 ms | ~52 ms | **~7–8× slower** |

## Analysis

### int (POD): full parity ✅
RawVec matches (and sometimes slightly beats) builtin vec for POD elements. Under
O2 the generic `push`/`get`/`reserve` inline to bare GEP + store/load; `get`'s
`emit_clone_value` is a no-op for POD. There is no measurable cost to managing the
raw buffer in pure LS vs the compiler-internal vec.

### string (has_drop): ~7–8× slower — root cause identified
The cost is **a malloc+memcpy clone on every push**, not present in builtin vec.

Root cause: a by-value `string x` parameter uses LS's **param-borrow ABI**
(`cap = LS_CAP_BORROWED = -2`): the param aliases the caller's string, it does not
own it. So in `RawVec.push`:

```
fn push(&!self, string x) { ...; self.data[self.len] = x; ... }
```

`self.data[len] = x` goes through `cg_store_owned` (STRING path). The source is the
param IDENT `x`, which is `is_borrowed` → it takes the `emit_string_clone_val`
branch. At runtime `cap == -2` means **malloc + memcpy** (codegen.c:1693) — a deep
copy per element. Confirmed: pushing a *static* literal `"item"` is just as slow as
an owned `f"item"`, because the param's borrow cap (-2), not the argument, forces
the clone.

Builtin `vec.push(string)` instead **moves** the argument's buffer into the vec (no
clone) — hence ~6 ms.

## Implication for "can pure LS replace vec"

- **POD / pointer element types: yes, already at parity.**
- **has_drop element types (string/struct/...): a real divergence** — user container
  `push` clones the element because a by-value movable param cannot take ownership;
  the borrow-ABI forces a clone when the param is stored into a container.

This is **not** a RawVec-specific issue: any user container built in LS
(`std.stack` included) that does `self.data.push(x)` with a `string`/has_drop param
pays the same clone. Builtin vec hides it because `vec.push` is a compiler intrinsic
that moves.

## Fix direction (owned parameters / move-into-container)

To reach string parity, a by-value movable parameter (`string`/`vec`/`map`/has_drop
`struct`/`enum`) needs **owned semantics** when the function transfers it into a
container: the caller *moves* the argument into the param (an owned rvalue's buffer
is handed over; a named-var arg is marked moved), and `self.data[i] = x` then moves
(no clone). Options:
1. Owned-by-default value params for movable types (biggest change; flips the
   current read-optimized borrow ABI — affects every string param).
2. An opt-in owned-param marker so a container `push` can declare it consumes the arg.
3. Detect "param stored into a container, last use" and elide the clone (move).

All three are language-level ABI work, shared across all user containers — out of the
RawVec memory-safety scope but decisive for has_drop performance parity.

## Verdict (M3)

- RawVec is **memory-identical** to vec (Gates M1/M2: 0/0/0 across int/string/struct).
- RawVec is **performance-identical for POD**, but **~7–8× slower for has_drop
  elements** due to the param-borrow clone — a general user-container limitation, not
  a RawVec flaw.
- "Pure LS replaces vec" is **true for POD today**; for has_drop it hinges on adding
  owned/move-into-container parameter semantics (a worthwhile general language
  improvement that also speeds up std.stack and every future container).
