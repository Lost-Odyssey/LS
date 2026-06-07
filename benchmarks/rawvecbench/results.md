# RawVec(T) vs builtin vec(T) — benchmark results (Gate M3)

> AOT-compiled (`default<O2>`), Windows x64 Release. `benchmarks/rawvecbench/bench.ls`.
> 3-run medians; push benches are noise-dominated (raw ≈ vec).

## Final results (after owned-param move ABI)

| op | N | builtin vec | RawVec | ratio raw/vec |
|----|---|-------------|--------|---------------|
| **push int** | 3,000,000 | ~8–9 ms | ~7–8 ms | **~0.9–1.0× (parity)** |
| **get int** (seq read+sum) | 3,000,000 | ~2.0 ms | ~1.0 ms | **~0.5× (RawVec 2× faster)** |
| **push string** | 1,000,000 | ~7–8 ms | ~6–8 ms | **~0.9–1.0× (parity)** |

**Verdict: RawVec matches or beats builtin vec on all measured ops.**
- push (int & string): parity — push moves the element (no clone) thanks to the
  owned-param ABI; under O2 the generic `push`/`reserve` inline to bare GEP+store.
- get (int): RawVec is **2× faster** because `RawVec.get(i)` is an unchecked raw
  read, whereas builtin `vec[i]` does a runtime bounds check (warn + default on
  OOB). This is the one *semantic* difference left vs vec — see "Bounds" below.

> Historical note: before the owned-param ABI fix, RawVec string push was ~7–8×
> slower (a malloc+memcpy clone per element). That gap is closed.

## Bounds check (the remaining get/[] semantic difference)

`vec[i]` is bounds-checked (prints a warning and returns a zero/empty default on
out-of-bounds); `RawVec.get(i)` is unchecked (UB on OOB), which is why it's faster.
This mirrors `vec.get_unsafe` rather than `vec[i]`. Adding a bounds check to
RawVec.get would bring exact `vec[i]` parity at vec's speed; leaving it unchecked
keeps the 2× read advantage. Choice deferred (documented).

## (historical) Analysis

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
- RawVec is **performance-identical for POD**.
- **string: FIXED (2026-06-06)** via the owned-param / move-into-container ABI
  (below). RawVec string push went **52ms → ~7ms ≈ vec** (7–8× → ~1×).
- **struct (has_drop): already at parity** — an rvalue struct push moves (1.0×); no
  change needed.
- **vec/map elements: still clone on push** (params stay `is_borrowed`), but this only
  matters for container-of-container (`RawVec(vec(int))`), currently unreachable due
  to the nested-generic parser/mangle gaps — deferred until those are fixed.

→ **"Pure LS replaces vec" now holds for POD, string, and has_drop struct elements**
(memory- and performance-identical). Only nested generic containers remain (blocked by
generics gaps, not by memory/perf).

## Owned-param / move-into-container ABI (2026-06-06)

The fix that closed the string gap. Model chosen: **rvalue / `__move` arguments
transfer ownership; named-variable arguments keep borrow semantics** (no breaking
change to existing read-after-pass code).

- **Param prologue** (`codegen_fn_decl`): string params no longer force `cap=-2` and
  are no longer marked `is_borrowed`. Cleanup is now **cap-driven** (`emit_string_free`
  frees `cap>0`, skips `cap<=0`), so an owned param the callee doesn't move out is
  still freed.
- **Call site**: a named-variable string arg gets `cap=-2` inserted (borrow, caller
  keeps it); an owned rvalue / `__move(x)` arg keeps `cap>0` and its source temp/var
  is marked moved (caller won't free).
- **`cg_store_owned` (string)**: runtime `cap` branch — `cap>0` → move (store + mark
  source moved); `cap<=0` → clone (preserves the caller's copy; no-op for static).

Verified: `test_rawvec_move` (rvalue move / named-var stays valid / `__move`) +
ctest 151/151, all memcheck 0/0/0. The same cap-driven model extends to `vec`/`map`
params when nested generics are unblocked.
