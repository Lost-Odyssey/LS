#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Grammar-based fuzzer for LS ownership / drop paths.

Unlike the mutation fuzzer (fuzz.py), this generates programs that TYPE-CHECK,
so they reach codegen AND run — exercising the historically buggy ownership
machinery (match on owned payloads, Vec/Map/Str drop, closures, has_drop
structs/enums, nested containers). Each program is run under `ls run --memcheck`
and flagged on:

    exit not in {0,1}          -> CRASH (segfault / heap corruption / double-free)
    SUMMARY not "OK clean"     -> LEAK / DOUBLE-FREE / INVALID-FREE
    (exit 1 = didn't type-check -> discarded; generator imperfection, not a bug)

Programs are built from typed statement templates over a small variable
environment, so most compile. Findings are minimized-by-regeneration with the
same seed (each program is fully determined by its seed).

Usage:
    python tests/fuzz/genfuzz.py [--iters 1000] [--seed 0] [--timeout 15]
                                 [--keep-dir tests/fuzz/crashes]
"""
import os, sys, random, subprocess, argparse, time, hashlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LS = os.path.join(ROOT, "build", "Release", "lls.exe")

WORDS = ["alpha", "beta", "gamma", "x", "y", "hi", "", "a,b,c", "  ", "zzz",
         "the quick", "1", "0", "-5", "key", "val"]

HEADER = """\
import std.core.vec
import std.core.map
import std.core.str
import std.core.set
import std.core.deque
import std.core.heap
import std.sync.lock
import std.mem.arena

struct Box {
    Str name
    Vec(int) nums
}

enum Tag {
    A(Str)
    B(int)
    C
}

enum Tree {
    Leaf(int)
    Node(Vec(Tree))
}

// L-002: two interfaces with a same-named method on Box. Box has no inherent
// `describe`, so a bare `b.describe()` is ambiguous — only the qualified form
// `Loud.describe(b)` / `Quiet.describe(b)` resolves (contended-mangle path).
interface Loud  { def describe(&self) -> Str }
interface Quiet { def describe(&self) -> Str }

methods Box: Loud  { def describe(&self) -> Str { return f"LOUD {self.name}" } }
methods Box: Quiet { def describe(&self) -> Str { return f"quiet {self.name}" } }

def mk_str(int k) -> Str { return f"s{k}" }
def mk_veci(int k) -> Vec(int) { Vec(int) r = [k, k] return r }
def mk_leaf(int k) -> Tree { return Leaf(k) }
"""

class Gen:
    def __init__(self, rng):
        self.rng = rng
        self.env = {}          # name -> type
        self.n = 0
        self.lines = []

    def fresh(self):
        self.n += 1
        return "v%d" % self.n

    def vars_of(self, *types):
        return [n for n, t in self.env.items() if t in types]

    def pick(self, *types):
        vs = self.vars_of(*types)
        return self.rng.choice(vs) if vs else None

    def emit(self, s):
        self.lines.append("    " + s)

    def intexpr(self):
        r = self.rng.random()
        v = self.pick("int")
        if v and r < 0.4:
            return self.rng.choice([v, "%s + 1" % v, "%s * 2" % v])
        return str(self.rng.randint(-3, 9))

    def strlit(self):
        return '"%s"' % self.rng.choice(WORDS)

    def strexpr(self):
        v = self.pick("Str")
        r = self.rng.random()
        if v and r < 0.3:
            return self.rng.choice(["%s.clone()" % v, '%s + "!"' % v,
                                    'f"{%s}-{%s}"' % (v, v)])
        if r < 0.5:
            return 'f"{%s}"' % self.intexpr()
        return self.strlit()

    # ---- statement generators (each keeps the program valid) ----
    def decl_int(self):
        n = self.fresh(); self.emit("int %s = %s" % (n, self.intexpr()))
        self.env[n] = "int"

    def decl_str(self):
        n = self.fresh(); self.emit("Str %s = %s" % (n, self.strexpr()))
        self.env[n] = "Str"

    def decl_veci(self):
        n = self.fresh()
        elems = ", ".join(self.intexpr() for _ in range(self.rng.randint(0, 4)))
        self.emit("Vec(int) %s = [%s]" % (n, elems))
        self.env[n] = "Vec(int)"

    def decl_vecs(self):
        n = self.fresh()
        elems = ", ".join(self.strlit() for _ in range(self.rng.randint(0, 3)))
        self.emit("Vec(Str) %s = [%s]" % (n, elems))
        self.env[n] = "Vec(Str)"

    def decl_map(self):
        n = self.fresh(); self.emit("Map(Str,int) %s = {}" % n)
        self.env[n] = "Map(Str,int)"

    def decl_box(self):
        n = self.fresh()
        elems = ", ".join(self.intexpr() for _ in range(self.rng.randint(0, 3)))
        self.emit("Box %s = Box{ name: %s, nums: [%s] }" %
                  (n, self.strexpr(), elems))
        self.env[n] = "Box"

    def decl_tag(self):
        n = self.fresh()
        ctor = self.rng.choice(["A(%s)" % self.strexpr(),
                                "B(%s)" % self.intexpr(), "C"])
        self.emit("Tag %s = %s" % (n, ctor))
        self.env[n] = "Tag"

    def op_veci(self):
        v = self.pick("Vec(int)")
        if not v: return
        op = self.rng.choice(["push", "pop", "match_get", "forin", "map",
                              "filter", "reduce", "clone", "len"])
        if op == "push":   self.emit("%s.push(%s)" % (v, self.intexpr()))
        elif op == "pop":  self.emit("%s.pop()" % v)        # owned Option drop
        elif op == "len":  self.emit("@print(%s.len())" % v)
        elif op == "clone":
            n = self.fresh(); self.emit("Vec(int) %s = %s.clone()" % (n, v))
            self.env[n] = "Vec(int)"
        elif op == "match_get":
            self.emit("match %s.get(%s) { Some(x) => { @print(x) } None => {} }"
                      % (v, self.intexpr()))
        elif op == "forin":
            self.emit("for x in %s { @print(x) }" % v)
        elif op == "map":
            n = self.fresh()
            self.emit("Vec(int) %s = %s.map(int)(|x| x + 1)" % (n, v))
            self.env[n] = "Vec(int)"
        elif op == "filter":
            n = self.fresh()
            self.emit("Vec(int) %s = %s.filter(|x| x > 0)" % (n, v))
            self.env[n] = "Vec(int)"
        elif op == "reduce":
            n = self.fresh()
            self.emit("int %s = %s.reduce(0, |a, b| a + b)" % (n, v))
            self.env[n] = "int"

    def op_vecs(self):
        v = self.pick("Vec(Str)")
        if not v: return
        op = self.rng.choice(["push", "pop", "match_get", "forin_borrow", "len"])
        if op == "push":   self.emit("%s.push(%s)" % (v, self.strexpr()))
        elif op == "pop":  self.emit("%s.pop()" % v)
        elif op == "len":  self.emit("@print(%s.len())" % v)
        elif op == "match_get":
            self.emit("match %s.get(%s) { Some(s) => { @print(s) } None => {} }"
                      % (v, self.intexpr()))
        elif op == "forin_borrow":
            self.emit("for s in &%s { @print(s.len()) }" % v)

    def op_map(self):
        v = self.pick("Map(Str,int)")
        if not v: return
        op = self.rng.choice(["set", "get", "remove", "len", "forin"])
        if op == "set":  self.emit("%s.set(%s, %s)" % (v, self.strexpr(), self.intexpr()))
        elif op == "len": self.emit("@print(%s.len())" % v)
        elif op == "get":
            self.emit("match %s.get(%s) { Some(n) => { @print(n) } None => {} }"
                      % (v, self.strexpr()))
        elif op == "remove":
            self.emit("%s.remove(%s)" % (v, self.strexpr()))
        elif op == "forin":
            self.emit("for e in %s { @print(e.val) }" % v)

    def op_box(self):
        v = self.pick("Box")
        if not v: return
        op = self.rng.choice(["read_name", "read_nums", "push_num"])
        if op == "read_name": self.emit("@print(%s.name.len())" % v)
        elif op == "read_nums": self.emit("@print(%s.nums.len())" % v)
        elif op == "push_num": self.emit("%s.nums.push(%s)" % (v, self.intexpr()))

    def op_tag(self):
        v = self.pick("Tag")
        if not v: return
        self.emit("match %s { A(s) => { @print(s.len()) } B(n) => { @print(n) } C => {} }"
                  % v)

    # ---- high-risk owned-result patterns (L-013 nest) ----
    def decl_vecveci(self):
        n = self.fresh(); self.emit("Vec(Vec(int)) %s = []" % n)
        self.env[n] = "Vec(Vec(int))"

    def decl_map_vec(self):
        n = self.fresh(); self.emit("Map(Str,Vec(int)) %s = {}" % n)
        self.env[n] = "Map(Str,Vec(int))"

    def op_vecveci(self):
        v = self.pick("Vec(Vec(int))")
        if not v: return
        op = self.rng.choice(["push_mk", "push_clone", "match_get", "len"])
        if op == "push_mk":     self.emit("%s.push(mk_veci(%s))" % (v, self.intexpr()))
        elif op == "push_clone":
            src = self.pick("Vec(int)")
            if src: self.emit("%s.push(%s.clone())" % (v, src))
            else:   self.emit("%s.push([%s])" % (v, self.intexpr()))
        elif op == "len":  self.emit("@print(%s.len())" % v)
        elif op == "match_get":
            self.emit("match %s.get(%s) { Some(inner) => { @print(inner.len()) } None => {} }"
                      % (v, self.intexpr()))

    def op_map_vec(self):
        v = self.pick("Map(Str,Vec(int))")
        if not v: return
        op = self.rng.choice(["set_mk", "get", "len"])
        if op == "set_mk":  self.emit("%s.set(%s, mk_veci(%s))" % (v, self.strexpr(), self.intexpr()))
        elif op == "len":   self.emit("@print(%s.len())" % v)
        elif op == "get":
            self.emit("match %s.get(%s) { Some(inner) => { @print(inner.len()) } None => {} }"
                      % (v, self.strexpr()))

    # match yielding an OWNED payload binder as the RESULT into an outer var
    # (the exact L-013 shape: owned-rvalue match consumed by a binding).
    def match_to_var_str(self):
        v = self.pick("Vec(Str)")
        if not v: return
        n = self.fresh()
        self.emit("Str %s = match %s.get(%s) { Some(x) => x  None => %s }" %
                  (n, v, self.intexpr(), self.strlit()))
        self.env[n] = "Str"

    def match_tag_to_var_str(self):
        v = self.pick("Tag")
        if not v: return
        n = self.fresh()
        self.emit('Str %s = match %s { A(s) => s  B(k) => f"{k}"  C => "c" }' % (n, v))
        self.env[n] = "Str"

    def match_to_var_veci(self):
        v = self.pick("Vec(Vec(int))")
        if not v: return
        n = self.fresh()
        self.emit("Vec(int) %s = match %s.get(%s) { Some(inner) => inner  None => [] }" %
                  (n, v, self.intexpr()))
        self.env[n] = "Vec(int)"

    # recursive has_drop enum with container payload (M5-004 / L-013 nest).
    def decl_tree(self):
        n = self.fresh()
        if self.rng.random() < 0.5:
            self.emit("Tree %s = Leaf(%s)" % (n, self.intexpr()))
        else:
            kids = ", ".join("mk_leaf(%s)" % self.intexpr()
                             for _ in range(self.rng.randint(0, 3)))
            self.emit("Tree %s = Node([%s])" % (n, kids))
        self.env[n] = "Tree"

    def op_tree(self):
        v = self.pick("Tree")
        if not v: return
        op = self.rng.choice(["match_print", "match_to_var", "match_count"])
        if op == "match_print":
            self.emit("match %s { Leaf(n) => { @print(n) } Node(kids) => { @print(kids.len()) } }" % v)
        elif op == "match_to_var":
            # owned Vec(Tree) payload moved out of a recursive enum into a var
            n = self.fresh()
            self.emit("Vec(Tree) %s = match %s { Node(kids) => kids  Leaf(_) => [] }" % (n, v))
            self.env[n] = "Vec(Tree)"
        elif op == "match_count":
            n = self.fresh()
            self.emit("int %s = match %s { Leaf(k) => k  Node(kids) => kids.len() }" % (n, v))
            self.env[n] = "int"

    # ---- concurrency data guards, single-threaded (memcheck-clean; the
    #      tracker is not thread-safe so threads are intentionally avoided) ----
    def decl_guard_vec(self):
        n = self.fresh()
        self.emit("Guard(Vec(int)) %s = {}" % n); self.emit("%s.init()" % n)
        self.env[n] = "Guard(Vec(int))"

    def decl_guard_str(self):
        n = self.fresh()
        self.emit("Guard(Str) %s = {}" % n); self.emit("%s.init()" % n)
        self.env[n] = "Guard(Str)"

    def decl_guard_map(self):
        n = self.fresh()
        self.emit("Guard(Map(Str,int)) %s = {}" % n); self.emit("%s.init()" % n)
        self.env[n] = "Guard(Map(Str,int))"

    def decl_spinguard_str(self):
        n = self.fresh()
        self.emit("SpinGuard(Str) %s = {}" % n)   # no init / no ~
        self.env[n] = "SpinGuard(Str)"

    def decl_rwlock_box(self):
        n = self.fresh()
        self.emit("RwLock(Box) %s = {}" % n); self.emit("%s.init()" % n)
        self.env[n] = "RwLock(Box)"

    def op_guard_vec(self):
        v = self.pick("Guard(Vec(int))")
        if not v: return
        if self.rng.random() < 0.5:
            self.emit("%s.lock(|w| { w.push(%s) })" % (v, self.intexpr()))
        else:
            n = self.fresh()
            self.emit("int %s = %s.get(int)(|w| { return w.len() })" % (n, v))
            self.env[n] = "int"

    def op_guard_str(self):
        v = self.pick("Guard(Str)")
        if not v: return
        r = self.rng.random()
        if r < 0.4:
            self.emit("%s.lock(|w| { w.push_str(%s) })" % (v, self.strlit()))
        elif r < 0.7:
            n = self.fresh()
            self.emit("int %s = %s.get(int)(|w| { return w.len() })" % (n, v))
            self.env[n] = "int"
        else:
            n = self.fresh()   # get(Str) — owned copy-out across the closure
            self.emit("Str %s = %s.get(Str)(|w| { return w.copy() })" % (n, v))
            self.env[n] = "Str"

    def op_guard_map(self):
        v = self.pick("Guard(Map(Str,int))")
        if not v: return
        if self.rng.random() < 0.5:
            self.emit("%s.lock(|w| { w.set(%s, %s) })" % (v, self.strlit(), self.intexpr()))
        else:
            n = self.fresh()
            self.emit("int %s = %s.get(int)(|w| { return w.len() })" % (n, v))
            self.env[n] = "int"

    def op_spinguard_str(self):
        v = self.pick("SpinGuard(Str)")
        if not v: return
        if self.rng.random() < 0.5:
            self.emit("%s.lock(|w| { w.push_str(%s) })" % (v, self.strlit()))
        else:
            n = self.fresh()
            self.emit("int %s = %s.get(int)(|w| { return w.len() })" % (n, v))
            self.env[n] = "int"

    def op_rwlock_box(self):
        v = self.pick("RwLock(Box)")
        if not v: return
        if self.rng.random() < 0.5:
            self.emit("%s.write(|b| { b.nums.push(%s) })" % (v, self.intexpr()))
        else:
            n = self.fresh()   # read borrows &Box (read-only)
            self.emit("int %s = %s.read(int)(|b| { return b.nums.len() })" % (n, v))
            self.env[n] = "int"

    # ---- closure-capture deepening ----
    def op_closure_capture_pod(self):
        # closure captures an outer int by-copy inside a functional method
        src = self.pick("Vec(int)")
        if not src: return
        base = self.pick("int")
        b = base if base else self.intexpr()
        n = self.fresh()
        self.emit("Vec(int) %s = %s.map(int)(|x| x + %s)" % (n, src, b))
        self.env[n] = "Vec(int)"

    def op_closure_capture_move(self):
        # closure captures an OWNED var by-move (Vec/Str) inside a map closure;
        # the captured var is consumed (env removal models use-after-move).
        srcs = self.vars_of("Vec(int)")
        if len(srcs) < 2: return
        src = self.rng.choice(srcs)
        caps = [c for c in srcs if c != src]
        if self.rng.random() < 0.5:
            cap = self.rng.choice(caps); capexpr = "%s.len()" % cap
        else:
            sv = self.pick("Str")
            if not sv: return
            cap = sv; capexpr = "%s.len()" % sv
        n = self.fresh()
        self.emit("Vec(int) %s = %s.map(int)(|x| x + %s)" % (n, src, capexpr))
        self.env[n] = "Vec(int)"
        if cap in self.env: del self.env[cap]   # moved into the closure env

    def op_closure_chain(self):
        # chained functional pipeline: filter -> map (temp Vec drop)
        src = self.pick("Vec(int)")
        if not src: return
        n = self.fresh()
        self.emit("Vec(int) %s = %s.filter(|x| x > 0).map(int)(|x| x + 1)" % (n, src))
        self.env[n] = "Vec(int)"

    # Option/Result combinators on owned types (C1/C2 lower paths).
    def combinator_str(self):
        v = self.pick("Vec(Str)")
        if not v: return
        n = self.fresh()
        self.emit("Str %s = %s.get(%s).unwrap_or(%s)" %
                  (n, v, self.intexpr(), self.strlit()))
        self.env[n] = "Str"

    def combinator_int(self):
        v = self.pick("Vec(int)")
        if not v: return
        n = self.fresh()
        op = self.rng.choice(["unwrap_or", "is_some", "is_none"])
        if op == "unwrap_or":
            self.emit("int %s = %s.get(%s).unwrap_or(%s)" % (n, v, self.intexpr(), self.intexpr()))
            self.env[n] = "int"
        elif op == "is_some":
            self.emit("bool %s = %s.get(%s).is_some?()" % (n, v, self.intexpr()))
            self.env[n] = "bool"
        else:
            self.emit("bool %s = %s.get(%s).is_none?()" % (n, v, self.intexpr()))
            self.env[n] = "bool"

    # ---- match deepening: exercise the int-switch & cond-chain storage points
    #      (the enum path is already heavily covered; these two were not).
    #      See docs/match_codegen_guide.md §1 (6 arm-body storage points). ----
    def match_int_switch_str(self):
        # int subject + int-const patterns + OR-pattern + wildcard, OWNED result.
        # Covers int-switch case + wildcard storage points yielding an owned Str
        # (L-013 shape on the LLVM-switch path — previously never fuzzed).
        n = self.fresh(); subj = self.intexpr()
        self.emit('Str %s = match %s { 0 => f"z"  1 | 2 => %s  _ => %s }' %
                  (n, subj, self.strexpr(), self.strlit()))
        self.env[n] = "Str"

    def match_int_switch_int(self):
        n = self.fresh(); subj = self.intexpr()
        self.emit("int %s = match %s { 0 => 0  1 | 2 | 3 => 1  _ => %s }" %
                  (n, subj, self.intexpr()))
        self.env[n] = "int"

    def match_int_switch_veci(self):
        # owned Vec(int) result on the int-switch path (has_drop result, no temp reg)
        n = self.fresh(); subj = self.intexpr()
        self.emit("Vec(int) %s = match %s { 0 => [] 1 | 2 => mk_veci(%s) _ => [%s] }" %
                  (n, subj, self.intexpr(), self.intexpr()))
        self.env[n] = "Vec(int)"

    def match_float_condchain(self):
        # float subject -> cond-chain (if-else) lowering, OWNED Str result.
        # Covers cond-chain then + wildcard storage points (never fuzzed before).
        n = self.fresh()
        f = self.rng.choice(["0.0", "1.5", "2.5", "-1.0", "3.5"])
        self.emit('Str %s = match %s { 1.5 => %s  2.5 => f"two"  _ => %s }' %
                  (n, f, self.strexpr(), self.strlit()))
        self.env[n] = "Str"

    def match_nested_owned(self):
        # nested match: outer enum arm yields the result of an INNER match that
        # itself moves out an owned binder — the deepest L-013 nesting.
        v = self.pick("Tag")
        if not v: return
        n = self.fresh(); subj = self.intexpr()
        self.emit('Str %s = match %s { A(inner) => match %s { 0 => f"z" _ => inner } '
                  'B(k) => f"{k}" C => "c" }' % (n, v, subj))
        self.env[n] = "Str"
        del self.env[v]                       # Tag consumed (payload moved out)

    # ---- memory deepening: loop-accumulation of owned temps (the L-014 class:
    #      a spilled owned temp per iteration — historically leaked) ----
    def loop_accum_str(self):
        v = self.pick("Str")
        if not v: return
        it = self.fresh(); k = self.rng.randint(2, 6)
        self.emit('for %s in 0..%d { %s = %s + f"{%s}" }' % (it, k, v, v, it))

    def loop_accum_vec(self):
        v = self.pick("Vec(int)")
        if not v: return
        it = self.fresh(); k = self.rng.randint(2, 6)
        self.emit("for %s in 0..%d { %s.push(%s) }" % (it, k, v, it))

    def loop_accum_map(self):
        v = self.pick("Map(Str,int)")
        if not v: return
        it = self.fresh(); k = self.rng.randint(2, 6)
        self.emit('for %s in 0..%d { %s.set(f"k{%s}", %s) }' % (it, k, v, it, it))

    def loop_match_owned(self):
        # owned match result produced & dropped every iteration (spill stress)
        v = self.pick("Vec(Str)")
        if not v: return
        it = self.fresh(); k = self.rng.randint(2, 5)
        self.emit('for %s in 0..%d { Str _m = match %s.get(%s) '
                  '{ Some(x) => x None => f"d{%s}" } @print(_m.len()) }' %
                  (it, k, v, it, it))

    # ---- memory: arena / bump allocator (reset-reuse path) ----
    def decl_arena(self):
        n = self.fresh(); self.emit("Arena(int) %s = {}" % n)
        self.env[n] = "Arena(int)"

    def op_arena(self):
        v = self.pick("Arena(int)")
        if not v: return
        op = self.rng.choice(["alloc", "alloc", "reset", "get"])
        if op == "alloc":
            h = self.fresh()
            self.emit("int %s = %s.alloc(%s)" % (h, v, self.intexpr()))
            self.env[h] = "int"
        elif op == "reset":
            self.emit("%s.reset()" % v)         # O(1) reuse — bump pointer reset
        else:
            self.emit("@print(%s.get(0).unwrap_or(-1))" % v)

    # ---- Set(T): pure-LS hash set over Map(T,bool) (2026-06-30) ----
    #      element type fixed to int (satisfies T: Hash + Equal). Exercises the
    #      __from_list literal path, iterator drop, and set-algebra owned results.
    def decl_set(self):
        n = self.fresh()
        if self.rng.random() < 0.5:
            elems = ", ".join(self.intexpr() for _ in range(self.rng.randint(0, 4)))
            self.emit("Set(int) %s = [%s]" % (n, elems))   # __from_list / FromList
        else:
            self.emit("Set(int) %s = {}" % n)
        self.env[n] = "Set(int)"

    def op_set(self):
        v = self.pick("Set(int)")
        if not v: return
        others = [s for s in self.vars_of("Set(int)") if s != v]
        choices = ["insert", "has", "remove", "len", "to_vec"]
        if others: choices += ["union", "intersect", "difference",
                                "op_add", "op_sub", "is_subset"]
        op = self.rng.choice(choices)
        if op == "insert":   self.emit("%s.insert(%s)" % (v, self.intexpr()))
        elif op == "has":    self.emit("@print(%s.has?(%s))" % (v, self.intexpr()))
        elif op == "remove": self.emit("%s.remove(%s)" % (v, self.intexpr()))
        elif op == "len":    self.emit("@print(%s.len())" % v)
        elif op == "to_vec":
            n = self.fresh(); self.emit("Vec(int) %s = %s.to_vec()" % (n, v))
            self.env[n] = "Vec(int)"
        elif op == "is_subset":
            self.emit("@print(%s.is_subset(%s))" % (v, self.rng.choice(others)))
        else:
            b = self.rng.choice(others); n = self.fresh()
            expr = {"union": "%s.union(%s)", "intersect": "%s.intersect(%s)",
                    "difference": "%s.difference(%s)",
                    "op_add": "%s + %s", "op_sub": "%s - %s"}[op] % (v, b)
            self.emit("Set(int) %s = %s" % (n, expr))
            self.env[n] = "Set(int)"

    # ---- Deque(T): growable ring buffer, self-managed *T buffer (2026-06-30) ----
    #      element type fixed to int. The point is the ring buffer's own
    #      Destroy/Clone (@dispose loop) + owned Option results from pop/front.
    def decl_deque(self):
        n = self.fresh()
        if self.rng.random() < 0.5:
            elems = ", ".join(self.intexpr() for _ in range(self.rng.randint(0, 4)))
            self.emit("Deque(int) %s = [%s]" % (n, elems))
        else:
            self.emit("Deque(int) %s = {}" % n)
        self.env[n] = "Deque(int)"

    def op_deque(self):
        v = self.pick("Deque(int)")
        if not v: return
        op = self.rng.choice(["push_back", "push_front", "pop_back", "pop_front",
                              "match_front", "match_back", "get", "len", "to_vec"])
        if op == "push_back":   self.emit("%s.push_back(%s)" % (v, self.intexpr()))
        elif op == "push_front": self.emit("%s.push_front(%s)" % (v, self.intexpr()))
        elif op == "pop_back":  self.emit("%s.pop_back()" % v)   # owned Option drop
        elif op == "pop_front": self.emit("%s.pop_front()" % v)
        elif op == "len":       self.emit("@print(%s.len())" % v)
        elif op == "match_front":
            self.emit("match %s.front() { Some(x) => { @print(x) } None => {} }" % v)
        elif op == "match_back":
            self.emit("match %s.back() { Some(x) => { @print(x) } None => {} }" % v)
        elif op == "get":
            self.emit("match %s.get(%s) { Some(x) => { @print(x) } None => {} }"
                      % (v, self.intexpr()))
        elif op == "to_vec":
            n = self.fresh(); self.emit("Vec(int) %s = %s.to_vec()" % (n, v))
            self.env[n] = "Vec(int)"

    # ---- BinaryHeap(T:Order): pure-LS max-heap over Vec(T) (2026-06-30) ----
    #      element type fixed to int (satisfies T: Order). Exercises sift-up/down
    #      (Vec.swap = @take moves) + owned Option from pop/peek + __from_list.
    def decl_heap(self):
        n = self.fresh()
        if self.rng.random() < 0.5:
            elems = ", ".join(self.intexpr() for _ in range(self.rng.randint(0, 5)))
            self.emit("BinaryHeap(int) %s = [%s]" % (n, elems))   # heapify via push
        else:
            self.emit("BinaryHeap(int) %s = {}" % n)
        self.env[n] = "BinaryHeap(int)"

    def op_heap(self):
        v = self.pick("BinaryHeap(int)")
        if not v: return
        op = self.rng.choice(["push", "pop", "peek", "len", "clear"])
        if op == "push":  self.emit("%s.push(%s)" % (v, self.intexpr()))
        elif op == "pop":
            self.emit("match %s.pop() { Some(x) => { @print(x) } None => {} }" % v)
        elif op == "peek":
            self.emit("match %s.peek() { Some(x) => { @print(x) } None => {} }" % v)
        elif op == "len":   self.emit("@print(%s.len())" % v)
        elif op == "clear": self.emit("%s.clear()" % v)

    # ---- L-002: qualified same-named interface method call (2026-06-30) ----
    #      Box has no inherent `describe`; Loud & Quiet both define one. The only
    #      way to call is the qualified form — exercises the contended-mangle
    #      symbol path + borrow-shell stripping of the receiver.
    def op_box_qualified(self):
        v = self.pick("Box")
        if not v: return
        iface = self.rng.choice(["Loud", "Quiet"])
        n = self.fresh()
        self.emit("Str %s = %s.describe(%s)" % (n, iface, v))
        self.env[n] = "Str"

    def build(self, nstmts):
        self.lines = ["def main() -> int {"]
        decls = [self.decl_int, self.decl_str, self.decl_veci, self.decl_vecs,
                 self.decl_map, self.decl_box, self.decl_tag,
                 self.decl_vecveci, self.decl_map_vec, self.decl_tree,
                 self.decl_guard_vec, self.decl_guard_str, self.decl_guard_map,
                 self.decl_spinguard_str, self.decl_rwlock_box,
                 self.decl_arena,
                 # new pure-LS containers (2026-06-30)
                 self.decl_set, self.decl_deque, self.decl_heap]
        ops = [self.op_veci, self.op_vecs, self.op_map, self.op_box, self.op_tag,
               self.op_vecveci, self.op_map_vec, self.op_tree,
               # concurrency data guards (single-threaded) + closure deepening
               self.op_guard_vec, self.op_guard_str, self.op_guard_map,
               self.op_spinguard_str, self.op_rwlock_box,
               self.op_closure_capture_pod, self.op_closure_capture_move,
               self.op_closure_chain, self.op_arena,
               # match deepening: int-switch + cond-chain storage points
               self.match_int_switch_str, self.match_int_switch_int,
               self.match_int_switch_veci, self.match_float_condchain,
               self.match_nested_owned,
               # memory deepening: loop-accumulation of owned temps (L-014 class)
               self.loop_accum_str, self.loop_accum_vec, self.loop_accum_map,
               self.loop_match_owned,
               # high-risk owned-result patterns weighted in by listing twice
               self.match_to_var_str, self.match_tag_to_var_str,
               self.match_to_var_veci, self.combinator_str, self.combinator_int,
               self.match_to_var_str, self.match_tag_to_var_str,
               self.combinator_str,
               # new owned-result match paths weighted in (historically buggy)
               self.match_int_switch_str, self.match_nested_owned,
               self.match_float_condchain, self.loop_accum_str,
               # new pure-LS containers + L-002 qualified interface calls (2026-06-30)
               self.op_set, self.op_deque, self.op_heap, self.op_box_qualified]
        # seed a few decls first
        for _ in range(self.rng.randint(2, 4)):
            self.rng.choice(decls)()
        for _ in range(nstmts):
            if self.rng.random() < 0.35:
                self.rng.choice(decls)()
            else:
                self.rng.choice(ops)()
        self.emit("return 0")
        self.lines.append("}")
        return HEADER + "\n" + "\n".join(self.lines) + "\n"


def classify(rc, err):
    if rc not in (0, 1):
        return "crash"
    if rc == 0:
        if "OK clean" not in err:
            return "memcheck"
    return None   # rc==1 (compile reject) or clean


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--timeout", type=float, default=15.0)
    ap.add_argument("--keep-dir", default=os.path.join(ROOT, "tests", "fuzz", "crashes"))
    ap.add_argument("--report-yield", action="store_true",
                    help="print compile-success rate and exit")
    ap.add_argument("--emit-corpus", default=None,
                    help="write the first N memcheck-clean programs to this dir "
                         "(curated regression corpus) instead of bug-hunting")
    ap.add_argument("--count", type=int, default=30,
                    help="number of clean programs to emit with --emit-corpus")
    ap.add_argument("--aot-diff", action="store_true",
                    help="for each program that runs clean, ALSO AOT-compile + run "
                         "it and diff stdout vs JIT — catches codegen-path "
                         "divergence and AOT-only drop/flush bugs")
    args = ap.parse_args()

    if args.emit_corpus:
        os.makedirs(args.emit_corpus, exist_ok=True)
        env = dict(os.environ); env["LS_HOME"] = ROOT
        tmp = os.path.join(args.emit_corpus, "_probe.lls")
        kept = 0; it = 0
        while kept < args.count and it < args.count * 50:
            rng = random.Random(args.seed * 1000000 + it); it += 1
            src = Gen(rng).build(rng.randint(6, 16))
            with open(tmp, "wb") as f:
                f.write(src.encode("utf-8"))
            try:
                p = subprocess.run([LS, "run", "--memcheck", tmp],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                   timeout=args.timeout, env=env)
                err = p.stderr.decode("utf-8", "replace")
            except subprocess.TimeoutExpired:
                continue
            if p.returncode == 0 and "OK clean" in err:
                dst = os.path.join(args.emit_corpus, "owned_%02d.lls" % kept)
                with open(dst, "wb") as f:
                    f.write(src.encode("utf-8"))
                kept += 1
        if os.path.exists(tmp):
            os.remove(tmp)
        print("emitted %d clean programs to %s (from %d candidates)" %
              (kept, args.emit_corpus, it))
        return 0

    if not os.path.exists(LS):
        print("build/Release/lls.exe not found", file=sys.stderr); return 2
    os.makedirs(args.keep_dir, exist_ok=True)
    env = dict(os.environ); env["LS_HOME"] = ROOT
    # Per-process temp files: parallel instances (different --seed) must NOT
    # share one _gen.lls / _gen_aot.exe or they clobber each other between write
    # and run, producing bogus findings. Key by seed + pid.
    _sfx = "s%d_p%d" % (args.seed, os.getpid())
    tmp = os.path.join(args.keep_dir, "_gen_%s.lls" % _sfx)

    findings = {"crash": 0, "memcheck": 0, "aotdiff": 0}
    compiled = 0
    aot_exe = os.path.join(args.keep_dir, "_gen_aot_%s.exe" % _sfx)
    t0 = time.time()
    for it in range(args.iters):
        rng = random.Random(args.seed * 1000000 + it)
        src = Gen(rng).build(rng.randint(4, 20))
        with open(tmp, "wb") as f:
            f.write(src.encode("utf-8"))
        try:
            p = subprocess.run([LS, "run", "--memcheck", tmp],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                               timeout=args.timeout, env=env)
            err = p.stderr.decode("utf-8", "replace")
            rc = p.returncode
        except subprocess.TimeoutExpired:
            rc, err = "timeout", ""
        if rc == "timeout":
            kind = "crash"; rc = -999
        else:
            kind = classify(rc, err)
            if rc == 0:
                compiled += 1
        # ---- AOT differential: clean program must produce identical stdout via
        #      JIT and AOT (different drop-emission + CRT flush paths) ----
        if args.aot_diff and kind is None and rc == 0:
            try:
                j = subprocess.run([LS, "run", tmp], stdout=subprocess.PIPE,
                                   stderr=subprocess.DEVNULL, timeout=args.timeout,
                                   env=env)
                c = subprocess.run([LS, "compile", tmp, "-o", aot_exe],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   timeout=args.timeout, env=env)
                if c.returncode == 0:
                    a = subprocess.run([aot_exe], stdout=subprocess.PIPE,
                                       stderr=subprocess.DEVNULL, timeout=args.timeout,
                                       env=env)
                    if a.returncode != 0 or a.stdout != j.stdout:
                        kind = "aotdiff"; err = (
                            "JIT rc=%d AOT rc=%d\n--- JIT stdout ---\n%s\n"
                            "--- AOT stdout ---\n%s\n" %
                            (j.returncode, a.returncode,
                             j.stdout.decode("utf-8", "replace"),
                             a.stdout.decode("utf-8", "replace")))
            except subprocess.TimeoutExpired:
                pass
        if kind:
            findings[kind] += 1
            tag = "%s_seed%d_it%d" % (kind, args.seed, it)
            with open(os.path.join(args.keep_dir, tag + ".lls"), "wb") as f:
                f.write(src.encode("utf-8"))
            with open(os.path.join(args.keep_dir, tag + ".err.txt"), "wb") as f:
                f.write(err.encode("utf-8", "replace"))
        if (it + 1) % 100 == 0:
            print("  %d/%d  crash=%d memcheck=%d aotdiff=%d  compile-ok=%.0f%%  (%.1f/s)" %
                  (it+1, args.iters, findings["crash"], findings["memcheck"],
                   findings["aotdiff"], 100.0*compiled/(it+1), (it+1)/(time.time()-t0)))

    if os.path.exists(aot_exe):
        try: os.remove(aot_exe)
        except OSError: pass
    print("\n=== %d iters in %.1fs ===" % (args.iters, time.time()-t0))
    print("compile-ok: %d (%.0f%%)  crashes: %d  memcheck: %d  aotdiff: %d" %
          (compiled, 100.0*compiled/max(1,args.iters),
           findings["crash"], findings["memcheck"], findings["aotdiff"]))
    return 1 if any(findings.values()) else 0

if __name__ == "__main__":
    sys.exit(main())
