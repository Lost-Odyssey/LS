#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Semantic-preserving rewrite oracle (spec A3).

Five hand-paired (before, after) programs, each rewriting one construct into a
syntactically different but semantically equivalent form. Both members of a
pair must produce byte-identical output.

This is deliberately NOT a generic source-to-source rewriter run over the whole
corpus: writing a rewriter that understands operator precedence, statement
boundaries and scope well enough to transform arbitrary programs correctly is
a substantial undertaking in its own right, and a bug in the rewriter would
corrupt the oracle rather than find one in the compiler. Each pair here is
instead a small, hand-verified program -- the same discipline ctxmatrix.py's
hand-built module/dup-module templates already use.

The five pairs, and why each one is a genuine equivalence in this language
(verified by hand against docs/stdlib.html and a probe before writing the
generator, per project convention) rather than assumed:

  redundant_parens   -- extra parens around every operand must not change
                        precedence or the result.
  if_to_match        -- `if cond {A} else {B}` vs `match cond {true=>A false=>B}`.
  qualified_call     -- a bare method call vs the L-002 disambiguation syntax
                        `Interface.method(recv)`; the qualified spelling works
                        even when the call is not actually ambiguous (probed).
  range_for_to_c_for -- `for i in 0..n` vs the C-style three-clause form.
  map_index_sugar    -- `m[k] = v` / `m[k]` (docs: "same as set" / "panics if
                        key absent") vs `m.set(k, v)` / `m.get(k).unwrap_or(_)`.
                        `m[k]` returns the raw value, not an Option -- probed,
                        not assumed.

Usage:
    python tests/fuzz/rewrite.py [--pairs a,b] [--keep]
"""
import os, re, sys, argparse, subprocess, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LS = os.path.join(ROOT, "build", "Release", "lls.exe")

_JIT = re.compile(r"^\[jit\][^\n]*\n?", re.M)


def normalize(out):
    return _JIT.sub("", out)


PAIRS = {
    "redundant_parens": {
        "reason": "extra parens around every operand must not change precedence or the result",
        "before": """def main() {
    int a = 2
    int b = 3
    int c = 4
    int r = a + b * c - a / b
    @print(r)
}
""",
        "after": """def main() {
    int a = (2)
    int b = (3)
    int c = (4)
    int r = ((a) + ((b) * (c))) - ((a) / (b))
    @print(r)
}
""",
    },
    "if_to_match": {
        "reason": "if/else on a bool vs an equivalent two-arm match on that bool",
        "before": """def main() {
    int x = 7
    int r = 0
    if x > 5 {
        r = 100
    } else {
        r = 200
    }
    @print(r)
}
""",
        "after": """def main() {
    int x = 7
    int r = 0
    match x > 5 {
        true => { r = 100 }
        false => { r = 200 }
    }
    @print(r)
}
""",
    },
    "qualified_call": {
        "reason": ("a bare method call vs the L-002 disambiguation syntax "
                    "Interface.method(recv) -- probed to confirm the qualified "
                    "spelling also works when there is no actual ambiguity"),
        "before": """interface Loud { def shout(&self) -> int }
struct Box { int n }
methods Box: Loud { def shout(&self) -> int { return self.n * 10 } }
def main() {
    Box b = Box{n: 4}
    @print(b.shout())
}
""",
        "after": """interface Loud { def shout(&self) -> int }
struct Box { int n }
methods Box: Loud { def shout(&self) -> int { return self.n * 10 } }
def main() {
    Box b = Box{n: 4}
    @print(Loud.shout(b))
}
""",
    },
    "range_for_to_c_for": {
        "reason": "`for i in 0..n` vs the equivalent C-style three-clause for",
        "before": """def main() {
    int sum = 0
    for i in 0..10 {
        sum = sum + i
    }
    @print(sum)
}
""",
        "after": """def main() {
    int sum = 0
    for (int i = 0; i < 10; i = i + 1) {
        sum = sum + i
    }
    @print(sum)
}
""",
    },
    "map_index_sugar": {
        "reason": ("m[k]=v / m[k] (docs: \"same as set\" / \"panics if key absent\") "
                    "vs m.set(k,v) / m.get(k).unwrap_or(_) -- m[k] read returns the "
                    "raw value, not an Option, confirmed by probe before writing this"),
        "before": """import std.core.map
import std.core.str
def main() {
    Map(Str, int) m = {}
    m.set("a", 1)
    m.set("b", 2)
    @print(m.get("a").unwrap_or(0))
    @print(m.get("b").unwrap_or(0))
}
""",
        "after": """import std.core.map
import std.core.str
def main() {
    Map(Str, int) m = {}
    m["a"] = 1
    m["b"] = 2
    @print(m["a"])
    @print(m["b"])
}
""",
    },
}


def run_program(text, workdir, timeout):
    path = os.path.join(workdir, "main.lls")
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    try:
        p = subprocess.run([LS, "run", path], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "<timeout>", ""
    err = p.stderr.decode("utf-8", "replace")
    first = next((l for l in err.splitlines() if l.strip()), "")
    return p.returncode, normalize(p.stdout.decode("utf-8", "replace")), first


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pairs", default=",".join(PAIRS))
    ap.add_argument("--timeout", type=int, default=30)
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(LS):
        print("error: %s not found -- build Release first" % LS)
        return 2

    names = [p.strip() for p in args.pairs.split(",") if p.strip()]
    print("rewrite: %d pair(s)" % len(names))

    findings = 0
    for name in names:
        pair = PAIRS[name]
        results = {}
        for side in ("before", "after"):
            work = tempfile.mkdtemp(prefix="rewrite_%s_%s_" % (name, side))
            try:
                rc, out, err = run_program(pair[side], work, args.timeout)
                results[side] = (rc, out, err)
                if args.keep:
                    print("    kept %s" % work)
            finally:
                if not args.keep:
                    import shutil
                    shutil.rmtree(work, ignore_errors=True)

        rc0, out0, err0 = results["before"]
        rc1, out1, err1 = results["after"]
        if rc0 != 0 or rc1 != 0:
            findings += 1
            print("  BUILD    %s: before rc=%s after rc=%s\n           %s\n           %s"
                  % (name, rc0, rc1, err0, err1))
            continue
        if out0 != out1:
            findings += 1
            print("  DIVERGE  %s: %s" % (name, pair["reason"]))
            print("     before: %s" % repr(out0[:120]))
            print("     after:  %s" % repr(out1[:120]))
        else:
            print("  ok       %s" % name)

    print("rewrite: %d finding(s)" % findings)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
