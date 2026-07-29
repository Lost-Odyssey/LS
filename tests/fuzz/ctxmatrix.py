#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Context cross-matrix oracle.

Takes one self-contained code fragment and places it into every syntactic
context the compiler treats differently, then requires all of them to print the
same bytes. The oracle carries its own expected value -- there is nothing to
write down, and nothing to keep in sync.

This aims squarely at the two bug families that have cost this project the most,
both of which are invisible to a crash-or-leak oracle:

  * ast_clone_deep. A generic method body is CLONED per instantiation. When the
    clone missed a node kind, an @time subtree became a use-after-free heap
    corruption; when it dropped closure.move_names, a [move v] list silently
    vanished along with the diagnostic that checks it. Both reproduce only in
    the cloned contexts, which is exactly what this matrix varies.
  * cross-module mangling. Symbol names are built from module path plus type
    arguments; collisions and truncations there produce wrong values at exit
    code 0. The module and duplicate-module contexts put the same fragment
    behind those name-building paths.

Ten contexts total: top-level def, generic method body, generic free function,
closure body, match arm, loop body, imported module, duplicate same-named
modules, trailing-closure-sugar call, and comptime. The last is special-cased:
its restricted evaluator (scalars/arrays/loops/if/locals, no I/O, no heap, no
closures) can only ever run ONE fragment, so it is not a normal CONTEXTS entry
-- see ctx_comptime's docstring.

Output normalization, both empirically necessary:
  * `[jit] compiling function '<name>' (hash=...)` lines name the enclosing
    function, which differs by construction across contexts.
  * @time / @bench print measured elapsed times; the numbers are normalized
    while the line structure is kept, so a context that LOSES a timing line
    still fails.

Usage:
    python tests/fuzz/ctxmatrix.py [--fragments a,b] [--contexts x,y] [--keep]
"""
import os, re, sys, argparse, shutil, subprocess, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LS = os.path.join(ROOT, "build", "Release", "lls.exe")

_JIT = re.compile(r"^\[jit\][^\n]*\n?", re.M)
_TIME = re.compile(r"\[@time\] [0-9.]+ ms")
_BENCH = re.compile(r"mean [0-9.]+ ns")


def normalize(out):
    out = _JIT.sub("", out)
    out = _TIME.sub("[@time] T ms", out)
    return _BENCH.sub("mean T ns", out)


# ---------------------------------------------------------------- fragments --
# Each fragment is a statement sequence that prints deterministic values and
# declares everything it uses. `pre` holds top-level declarations the fragment
# needs; they are emitted once at file scope in every context.
FRAGMENTS = {
    # containers: ownership churn through Vec/Str
    "vec_str": {
        "imports": ["std.core.vec"],
        "pre": "",
        "body": """Vec(Str) v = {}
v.push("alpha")
v.push("beta")
@print(v.len())
@print(v.get!(0))""",
    },
    # @time: the node kind whose shallow clone once corrupted the heap when the
    # fragment landed in a generic body
    "at_time": {
        "imports": [],
        "pre": "",
        "body": """int t = @time 41
@print(t + 1)""",
    },
    # a match whose arm yields an owned local from a block tail -- the shape
    # behind the block-tail double-free
    "match_tail_owned": {
        "imports": ["std.core.str"],
        "pre": "",
        "body": """int k = 1
Str s = match k { 1 => { Str b = "tail" b } _ => { Str c = "other" c } }
@print(s)""",
    },
    # explicit [move v] capture: the list ast_clone_deep used to drop.
    # The move spec precedes the closure literal; there is no void Block type,
    # so the closure yields an int that the call site discards.
    "move_capture": {
        "imports": ["std.core.str"],
        "pre": "type MoveFn = Block() -> int\n",
        # ends on a void statement: in the match-arm context the block tail
        # becomes the arm's value, and a fragment ending in an int expression
        # would clash with the sibling arm
        "body": """Str owned = "captured"
MoveFn f = [move owned] || { @print(owned) 0 }
int r = f()
@print(r)""",
    },
    # A [move v] list naming a variable the closure never references is a
    # checker error. The point is that it must be an error in EVERY context:
    # ast_clone_deep once dropped closure.move_names, so in the cloned contexts
    # (generic bodies) the list vanished along with the diagnostic and the
    # program was silently accepted. stdout comparison cannot see a missing
    # diagnostic; a divergence in ACCEPT/REJECT can.
    "move_unused_reject": {
        "imports": ["std.core.str"],
        "pre": "type UnusedFn = Block() -> int\n",
        "expect": "reject",
        "body": """Str owned = "captured"
UnusedFn f = [move owned] || { 7 }
int r = f()
@print(r)""",
    },
    # fixed-size array reached through a struct field -- the place family that
    # printed nothing and skipped its for-in body at exit code 0
    "array_in_struct": {
        "imports": [],
        "pre": """struct ArrBox {
    array(int, 3) d
}""",
        "body": """ArrBox b = ArrBox{d: [10, 20, 30]}
int sum = 0
for x in b.d { sum = sum + x }
@print(sum)
@print(b.d[1])""",
    },
    # Pure int/loop arithmetic: no imports, no heap types, no closures. This is
    # the ONLY fragment reachable from ctx_comptime, since the comptime
    # evaluator is a restricted interpreter (scalars/arrays/loops/if/locals,
    # no I/O, no heap). `comptime_value` names the local ctx_comptime returns
    # instead of printing, so the same computation is checked both as a
    # compile-time constant and as ordinary runtime code.
    "pure_arith": {
        "imports": [],
        "pre": "",
        "body": """int s = 0
for i in 0..10 { s = s + i }
@print(s)""",
        "comptime_value": "s",
    },
}


# ----------------------------------------------------------------- contexts --
def _imports(frag, in_module=False):
    mods = list(frag["imports"])
    # The compiler auto-injects std.core.str into the ROOT file only, so a
    # module file that mentions Str must import it itself.
    if in_module and "std.core.str" not in mods:
        mods.append("std.core.str")
    return "".join("import %s\n" % m for m in mods)


def _indent(body, n):
    pad = " " * n
    return "\n".join(pad + line for line in body.splitlines())


def ctx_top(frag):
    return {"main": "%s%s\ndef frag() {\n%s\n}\n\ndef main() {\n    frag()\n}\n"
                    % (_imports(frag), frag["pre"], _indent(frag["body"], 4))}


def ctx_generic_method(frag):
    return {"main": "%s%s\nstruct Holder(T) {\n    T seed\n}\n\n"
                    "methods Holder(T) {\n    def frag(&self) {\n%s\n    }\n}\n\n"
                    "def main() {\n    Holder(int) h = Holder(int){seed: 7}\n    h.frag()\n}\n"
                    % (_imports(frag), frag["pre"], _indent(frag["body"], 8))}


def ctx_generic_fn(frag):
    return {"main": "%s%s\ndef frag(T)(T seed) {\n%s\n}\n\n"
                    "def main() {\n    frag(int)(7)\n}\n"
                    % (_imports(frag), frag["pre"], _indent(frag["body"], 4))}


def ctx_closure(frag):
    # Two constraints from the language, both discovered by probing: there is no
    # void Block type (so the closure yields a trailing 0), and a zero-argument
    # Block type cannot be written inline in a declaration -- every use in the
    # tree goes through a `type` alias.
    return {"main": "%s%s\ntype CtxClosureFn = Block() -> int\n\n"
                    "def main() {\n    CtxClosureFn f = || {\n%s\n        0\n    }\n    f()\n}\n"
                    % (_imports(frag), frag["pre"], _indent(frag["body"], 8))}


def ctx_match_arm(frag):
    return {"main": "%s%s\ndef main() {\n    int sel = 1\n    match sel {\n"
                    "        1 => {\n%s\n        }\n        _ => { }\n    }\n}\n"
                    % (_imports(frag), frag["pre"], _indent(frag["body"], 12))}


def ctx_loop_body(frag):
    return {"main": "%s%s\ndef main() {\n    for i in 0..1 {\n%s\n    }\n}\n"
                    % (_imports(frag), frag["pre"], _indent(frag["body"], 8))}


def ctx_module(frag):
    mod = ("module ctxmod\n%s%s\ndef frag() {\n%s\n}\n"
           % (_imports(frag, in_module=True), frag["pre"], _indent(frag["body"], 4)))
    main = "module main\n\nimport ctxmod\n\ndef main() {\n    ctxmod.frag()\n}\n"
    return {"main": main, "ctxmod.lls": mod}


def ctx_trailing_closure(frag):
    """The trailing-closure call sugar (`f() { || ... }` desugars to
    `f(|| ...)`) builds its AST_CLOSURE through a DIFFERENT parser path
    (parser_expr.c's Phase A.5) than the Ruby `|| { ... }` literal every other
    context uses. Its multi-statement tail-expression handling is the same
    generic codegen path the closure-tail-expr fix covers, but this exercises
    it via the sugar's own construction rather than the literal's, so a
    regression specific to that path would show up here and nowhere else."""
    return {"main": "%s%s\ntype CtxTrailFn = Block() -> int\n"
                    "def cx_trailing_apply(CtxTrailFn f) -> int { return f() }\n\n"
                    "def main() {\n    cx_trailing_apply() {\n        ||\n%s\n        0\n    }\n}\n"
                    % (_imports(frag), frag["pre"], _indent(frag["body"], 8))}


def ctx_comptime(frag):
    """comptime is a restricted compile-time interpreter (scalars, arrays,
    loops, if, locals -- no heap types, no I/O, no closures), so it can only
    ever run the one fragment that declares itself comptime-compatible via
    `comptime_value`. That value names the local holding the fragment's
    result; the fragment's OWN body (ending in `@print(<value>)`, used by
    every other context) has its last line stripped and replaced with a
    `return <value>` inside a `comptime { ... }` block, then printed from
    `main()` -- so the same computation is checked as a compile-time constant
    and as ordinary runtime code."""
    if "comptime_value" not in frag:
        return None
    stmt_lines = frag["body"].splitlines()[:-1]   # drop the trailing @print(...)
    stmt = "\n".join(stmt_lines)
    return {"main": "comptime int CTX_R = comptime {\n%s\n    return %s\n}\n\n"
                    "def main() {\n    @print(CTX_R)\n}\n"
                    % (_indent(stmt, 4), frag["comptime_value"])}


def ctx_dup_modules(frag):
    """Two modules declaring the same type and function names, both carrying the
    fragment. Only the first is called; the second exists to make the symbol
    mangler distinguish them. A collision here is a wrong value at exit 0 --
    exactly the shape of the generic-enum instance key collision."""
    body = ("module %s\n" + _imports(frag, in_module=True) + frag["pre"]
            + "\nstruct Shared {\n    int tag\n}\n\ndef frag() {\n"
            + _indent(frag["body"], 4) + "\n}\n")
    main = ("module main\n\nimport ctxdupa\nimport ctxdupb\n\n"
            "def main() {\n    ctxdupa.frag()\n}\n")
    return {"main": main,
            "ctxdupa.lls": body % "ctxdupa",
            "ctxdupb.lls": body % "ctxdupb"}


CONTEXTS = {
    "top": ctx_top,
    "generic_method": ctx_generic_method,
    "generic_fn": ctx_generic_fn,
    "closure": ctx_closure,
    "match_arm": ctx_match_arm,
    "loop_body": ctx_loop_body,
    "module": ctx_module,
    "dup_modules": ctx_dup_modules,
    "trailing_closure": ctx_trailing_closure,
}
# comptime is deliberately NOT a normal CONTEXTS entry: its restricted evaluator
# (scalars/arrays/loops/if/locals, no I/O, no heap, no closures) can only ever
# run the one fragment that opts in via `comptime_value` -- wiring it in like
# the other contexts would report a BUILD failure for every OTHER fragment,
# which is expected rather than a finding and would just be noise. The main
# loop calls ctx_comptime directly, once per fragment, only when eligible.


def build_and_run(files, workdir, timeout):
    """Write files into a fresh dir, run main, return (rc, normalized stdout)."""
    for name, text in files.items():
        fn = "main.lls" if name == "main" else name
        with open(os.path.join(workdir, fn), "w", encoding="utf-8", newline="") as f:
            f.write(text)
    try:
        p = subprocess.run([LS, "run", os.path.join(workdir, "main.lls")],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "<timeout>", ""
    err = p.stderr.decode("utf-8", "replace")
    first = next((l for l in err.splitlines() if l.strip()), "")
    return p.returncode, normalize(p.stdout.decode("utf-8", "replace")), first


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fragments", default=",".join(FRAGMENTS))
    ap.add_argument("--contexts", default=",".join(list(CONTEXTS) + ["comptime"]))
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--keep", action="store_true",
                    help="keep the generated programs for inspection")
    args = ap.parse_args()

    if not os.path.exists(LS):
        print("error: %s not found -- build Release first" % LS)
        return 2

    frags = [f.strip() for f in args.fragments.split(",") if f.strip()]
    ctxs = [c.strip() for c in args.contexts.split(",") if c.strip()]
    print("ctxmatrix: %d fragment(s) x %d context(s)" % (len(frags), len(ctxs)))

    findings = 0
    for fname in frags:
        frag = FRAGMENTS[fname]
        results = {}
        for cname in ctxs:
            if cname == "comptime":
                continue   # not a CONTEXTS entry; handled specially below
            work = tempfile.mkdtemp(prefix="ctxm_%s_%s_" % (fname, cname))
            try:
                rc, out, err = build_and_run(CONTEXTS[cname](frag), work, args.timeout)
                results[cname] = (rc, out, err)
                if args.keep:
                    print("    kept %s" % work)
            finally:
                if not args.keep:
                    shutil.rmtree(work, ignore_errors=True)

        # comptime opts in per-fragment (see ctx_comptime docstring); silently
        # skipped for every other fragment rather than reported as a BUILD
        # failure, since that failure is expected and would just be noise
        if "comptime" in ctxs and "comptime_value" in frag:
            work = tempfile.mkdtemp(prefix="ctxm_%s_comptime_" % fname)
            try:
                rc, out, err = build_and_run(ctx_comptime(frag), work, args.timeout)
                results["comptime"] = (rc, out, err)
            finally:
                if not args.keep:
                    shutil.rmtree(work, ignore_errors=True)

        # every context that COMPILED must agree; a context that fails to
        # compile is reported separately (it may be a generator limitation, so
        # it is a flag rather than a verdict)
        compiled = {c: r for c, r in results.items() if r[0] == 0}
        failed = {c: r for c, r in results.items() if r[0] != 0}

        if frag.get("expect") == "reject":
            # every context must refuse it; a context that accepts is the bug
            if compiled:
                findings += 1
                print("  ACCEPTED %s: %d context(s) accepted a program that must be rejected"
                      % (fname, len(compiled)))
                print("     %s" % ",".join(sorted(compiled)))
            else:
                print("  ok       %s (%d contexts all reject)" % (fname, len(failed)))
            continue

        for c, (rc, out, err) in sorted(failed.items()):
            findings += 1
            print("  BUILD    %s / %s: rc=%s\n           %s" % (fname, c, rc, err))
        vals = {}
        for c, (rc, out, err) in compiled.items():
            vals.setdefault(out, []).append(c)
        if len(vals) > 1:
            findings += 1
            print("  DIVERGE  %s: %d distinct outputs across contexts" % (fname, len(vals)))
            for out, cs in sorted(vals.items(), key=lambda kv: -len(kv[1])):
                print("     %-40s %s" % (",".join(sorted(cs)), repr(out[:120])))
        elif compiled:
            print("  ok       %s (%d contexts agree)" % (fname, len(compiled)))

    print("ctxmatrix: %d finding(s)" % findings)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
