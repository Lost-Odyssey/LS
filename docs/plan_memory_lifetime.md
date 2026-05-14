# LS Memory Model & Lifetime — Completion Plan

## Current State (as of Phase F.7)

| Feature | Status |
|---------|--------|
| Move semantics: `string`, `struct(has_drop)`, `Block` | ✅ complete |
| Move semantics: `vec(T)`, `map(K,V)` via `[move v]` | ✅ complete |
| Borrow `&T` / `&!T` for function parameters | ✅ `string/vec/map/struct` |
| Closures: by-copy (POD), by-move (string/struct/enum), by-ref (vec/map) | ✅ complete |
| `Block` env deep copy (`Block g = vec[i]`) | ❌ Phase G (planned) |
| Struct deep copy from containers (`MyStruct s = vec_of_struct[i]`) | ❌ Phase H (planned) |
| Borrow as return type / variable declaration / struct field | ❌ needs lifetime system |
| User-defined generic types | ❌ future |

Existing detail documents:
- `docs/block_clone_plan.md` — Phase G design (Block env clone)
- `docs/move_semantics_plan.md` — Phase A–B string/struct move system

This document covers what comes **after** Phase G/H and Phase G/H themselves at a high level.

---

## Phase G — Block Env Deep Copy (from `block_clone_plan.md`)

> Full design already in `docs/block_clone_plan.md`. Summary here for roadmap context.

**Problem**: `Block g = vec_of_blocks[i]` and `Block g = ops.get(k)` are currently rejected
by the checker ("aliasing read not allowed"). Reading a Block out of a container requires
cloning the env so both the container and `g` own independent copies.

**Solution**: Synthesize `__env_clone_N(env_ptr) -> env_ptr` alongside `__env_drop_N`.
The env struct gains a `clone_fn` slot (field 1; drop_fn stays field 0).

**Restriction**: Only POD + string captures are clonable in Phase G.
vec/map/struct captures remain non-cloneable (aliasing checker still rejects).

**Effort**: 5–7 days

---

## Phase H — Struct Deep Copy from Containers

**Problem**: When a `vec(MyStruct)` or `map(K, MyStruct)` contains structs with
`has_drop=true`, reading an element out produces a shallow copy that shares heap
pointers (string fields, nested vec fields) with the container — leading to double-free.

Current checker behaviour: allowed (no guard), runtime double-free.

**Solution** (parallel to Phase G):

1. **Checker**: detect `T s = container[i]` / `T s = m.get(k)` where `T` is a `has_drop`
   struct, and emit an implicit `emit_struct_clone_val` at the read site.
2. **Already exists**: `emit_struct_clone_val` is implemented (used for fn arg passing).
   The fix is to call it at index/get read sites.
3. **vec element read** (`codegen_vec_index`): add `has_drop` branch → clone before store.
4. **map `.get` read** (`codegen_map_method` GET): add `has_drop` branch → clone result.

**Effort**: 2–3 days (infrastructure already exists)

---

## Phase L.1 — Borrow Completeness: `vec(T)` element borrows

**Problem**: You can borrow a whole `vec` (`&vec(T)`), but you cannot borrow a single
element without copying it out.

**Goal**:

```ls
fn print_element(&vec(string) v, int i) {
    &string s = v[i]       // borrow the i-th element; no copy
    print(s)
    // s dropped here; v[i] untouched
}
```

**Design**:
- `&T elem = container[i]` is an element borrow: `sym->is_borrowed = true`,
  `sym->value = GEP into the vec's data buffer`.
- Lifetime constraint (enforced statically): element borrow must not outlive the container.
  Phase L.1 uses a simple scope-depth check: borrow must be in same or inner scope as the vec.
- `&!T elem = container[i]` allows element mutation: `container[i].field = x` becomes legal.

**Restrictions in L.1**: No borrow of struct fields (needs full lifetime tracking).
No function return of element borrows (needs lifetime annotations).

**Effort**: 4–6 days

---

## Phase L.2 — Lifetime Annotations (syntax + checker)

**Motivation**: Enable borrows as return types and struct fields — the two scenarios
currently impossible without a lifetime system.

### Syntax

```ls
// Named lifetime: 'a
fn first<'a>(&'a vec(string) v) -> &'a string {
    return v[0]      // borrow of v[0]; lifetime tied to 'a (= v's lifetime)
}

// Struct with a borrow field (requires lifetime parameter)
struct Slice<'a> {
    &'a vec(int) data
    int start
    int len
}

fn make_slice<'a>(&'a vec(int) v, int s, int l) -> Slice<'a> {
    return Slice { data: v, start: s, len: l }
}
```

### Checker design

- Lifetime parameters are syntactic tags: `'a`, `'b`, `'static`.
- `'static` borrows live for the entire program (string literals, globals).
- Inference: if only one input lifetime exists, the output lifetime is inferred to be that same lifetime (Rust's "first lifetime elision rule").
- Constraint propagation: simple unification — if `return v[0]` where `v: &'a vec(T)`,
  then the return borrow gets lifetime `'a`.
- Error: if a returned borrow outlives any of its source lifetimes, report:
  `[error] returned borrow may outlive its source`.

### Codegen

Lifetime annotations are **erased at codegen**: they are purely checker-level information.
A `&T` return is still just a pointer; the checker guarantees it is valid.

### Effort: 10–14 days (this is the most complex phase)

---

## Phase L.3 — Closure by-ref Safety (lifetimes + closures)

**Current limitation**: By-ref captures (`vec`/`map`) can produce dangling pointers
if the closure outlives the captured variable. The compiler does not check this.

**Goal**: With lifetime annotations in place, require that a closure containing
by-ref captures must not escape its source scope — or must be annotated with
a lifetime parameter tying it to the captured variable.

```ls
type Adder = Block() -> int

// OK: closure used within the same scope as `nums`
vec(int) nums = [1, 2, 3]
Adder f = || { return nums[0] + nums[1] }
print(f())    // safe: f lives <= nums

// ERROR: closure escapes (factory pattern with by-ref vec)
fn make_adder() -> Adder {
    vec(int) nums = [1, 2, 3]
    return || { return nums[0] }   // error: by-ref capture of `nums` escapes fn scope
                                   // hint: use [move nums] to transfer ownership
}
```

**Implementation**: During `codegen_closure_literal`, if any capture is `is_default_by_ref`
(vec/map) and the closure's type is a return value or stored in a longer-lived container,
emit a checker error. This is a dataflow check, not a full lifetime system.

**Effort**: 4–6 days (after Phase L.2)

---

## Phase L.4 — Non-Lexical Lifetimes (NLL) for Move Checks

**Current limitation**: Move state is **lexically** scoped. A variable marked `MAYBE_MOVED`
(e.g., moved in one branch of an `if`) is considered dead everywhere after that point, even
in code paths where the move did not happen.

```ls
string s = "hello".upper()
if cond { vec.push(s) }   // s moved on one path
// s is MAYBE_MOVED here — currently: compile error even in the `else` case
print(s)                   // currently rejected; with NLL: allowed on the !cond path
```

**Goal**: Track move state per control-flow path using a simple bit-vector dataflow
analysis (similar to Rust's NLL Phase 1).

**Design**:
- Build a basic block CFG during checking (reuse the existing AST walk structure).
- Each basic block has an `in` and `out` move-state vector.
- A variable is considered moved only if it is moved on **all** paths reaching the use site.
- `MAYBE_MOVED` becomes a proper "moved on some paths" annotation, and usage in branches
  that did not move is allowed.

**Effort**: 7–10 days (requires CFG construction in checker)

---

## Phase Summary & Recommended Order

```
Phase G  (Block env clone)       ← low risk, closes open checker error
Phase H  (struct container copy) ← 2-3 days, closes silent double-free
Phase L.1 (element borrows)      ← improves ergonomics without lifetime system
Phase L.2 (lifetime annotations) ← large, unlocks borrow-as-return / struct fields
Phase L.3 (closure by-ref safety)← builds on L.2
Phase L.4 (NLL move checks)      ← improves move ergonomics, builds on CFG work
```

| Phase | Prerequisite | Risk | Effort |
|-------|-------------|------|--------|
| G | none | low | 5–7 d |
| H | none | low | 2–3 d |
| L.1 | none | medium | 4–6 d |
| L.2 | L.1 | high | 10–14 d |
| L.3 | L.2 | medium | 4–6 d |
| L.4 | none (independent) | high | 7–10 d |

**Minimum viable next step**: Phase H (silent double-free fix) is the highest
priority because it is a correctness bug, not just an ergonomics gap.
