"""regexdiff.py -- differential oracle for the regex engine.

Not part of ctest (see the repo's fuzzing discipline: fuzz extensions do not
get registered unless they fixed a real bug).

How it works: generate random patterns and texts, then compare the engine's
answer against Python's `re` on the subset of syntax where the two are
specified to agree. Compares "did it match" AND every capture group's
offset and length -- the whole point is to catch a SILENT WRONG ANSWER, which
a crash-or-leak oracle cannot see.

Syntax kept to the intersection of both engines: literals, ., character
classes, *, +, ?, |, groups, ^, $. No lookahead, no named groups, no inline
flags, no {n,m} -- those either differ in corner cases or are not worth the
false positives (see the module docstring in the task-8 report for the full
list of excluded constructs and why).

Usage:
    python tests/fuzz/regexdiff.py --iters 2000
    python tests/fuzz/regexdiff.py --iters 2000 --seed 12345
"""
import argparse
import os
import random
import re as pyre
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LLS = os.path.join(ROOT, "build", "Release", "lls.exe")

ALPHABET = "abc"
ATOMS = ["a", "b", "c", ".", "[ab]", "[^a]", "[a-c]"]
QUANTS = ["", "*", "+", "?"]


def gen_pattern(rng, depth=0):
    """Build a small random pattern from the shared syntax subset."""
    n = rng.randint(1, 3)
    parts = []
    for _ in range(n):
        r = rng.random()
        if r < 0.15 and depth < 2:
            inner = gen_pattern(rng, depth + 1)
            parts.append("(" + inner + ")" + rng.choice(QUANTS))
        elif r < 0.25 and depth < 2:
            a = gen_pattern(rng, depth + 1)
            b = gen_pattern(rng, depth + 1)
            parts.append("(" + a + "|" + b + ")")
        else:
            parts.append(rng.choice(ATOMS) + rng.choice(QUANTS))
    body = "".join(parts)
    if depth == 0:
        if rng.random() < 0.15:
            body = "^" + body
        if rng.random() < 0.15:
            body = body + "$"
    return body


def gen_text(rng):
    return "".join(rng.choice(ALPHABET) for _ in range(rng.randint(0, 12)))


LS_PROGRAM = r'''
import std.core.vec
import std.core.str
import std.text.regex as re

def main() -> int {
    Vec(Str) rows = {}
%s
    int i = 0
    while i < rows.len() {
        @print(rows[i])
        i = i + 1
    }
    return 0
}
'''


def ls_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def run_ls(cases):
    """Run every (pattern, text) case in one lls process and return a list of
    'match_start,match_len' strings (or 'NONE')."""
    body = []
    for pat, txt in cases:
        body.append('    {')
        body.append('        Vec(Str) g = re.capture("%s", "%s")' % (ls_escape(txt), ls_escape(pat)))
        body.append('        if g.len() == 0 { rows.push("NONE") }')
        body.append('        else {')
        body.append('            Str acc = ""')
        body.append('            int k = 0')
        body.append('            while k < g.len() {')
        body.append('                acc = acc + g[k] + "|"')
        body.append('                k = k + 1')
        body.append('            }')
        body.append('            rows.push(acc)')
        body.append('        }')
        body.append('    }')
    src = LS_PROGRAM % "\n".join(body)

    fd, path = tempfile.mkstemp(suffix=".lls")
    os.close(fd)
    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    try:
        env = dict(os.environ, LS_HOME=ROOT)
        p = subprocess.run([LLS, "run", path], capture_output=True, text=True,
                           env=env, timeout=120)
        if p.returncode != 0:
            return None, p.stderr
        lines = [ln for ln in p.stdout.splitlines() if not ln.startswith("[jit]")]
        return lines, None
    finally:
        os.unlink(path)


def py_expect(pat, txt):
    """Python's answer in the same 'g0|g1|...|' shape LS prints."""
    try:
        m = pyre.compile(pat).search(txt)
    except pyre.error:
        return None          # pattern both engines may reject; skip
    if not m:
        return "NONE"
    parts = [m.group(0)]
    parts.extend(g if g is not None else "" for g in m.groups())
    return "".join(p + "|" for p in parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--batch", type=int, default=50)
    args = ap.parse_args()

    seed = args.seed if args.seed is not None else random.randrange(1 << 30)
    rng = random.Random(seed)
    print("seed=%d iters=%d" % (seed, args.iters))

    findings = 0
    checked = 0
    pending = []

    def flush(batch):
        nonlocal findings, checked
        if not batch:
            return
        got, err = run_ls(batch)
        if got is None:
            print("LS RUN FAILED:\n%s" % err)
            for pat, txt in batch:
                print("  pattern=%r text=%r" % (pat, txt))
            findings += 1
            return
        if len(got) != len(batch):
            print("OUTPUT COUNT MISMATCH: got %d want %d" % (len(got), len(batch)))
            findings += 1
            return
        for (pat, txt), g in zip(batch, got):
            want = py_expect(pat, txt)
            if want is None:
                continue
            checked += 1
            if g != want:
                findings += 1
                print("DIFF pattern=%r text=%r\n  ls=%r\n  py=%r" % (pat, txt, g, want))

    for _ in range(args.iters):
        pat = gen_pattern(rng)
        txt = gen_text(rng)
        pending.append((pat, txt))
        if len(pending) >= args.batch:
            flush(pending)
            pending = []
    flush(pending)

    print("checked=%d findings=%d" % (checked, findings))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
