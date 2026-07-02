#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Input-mutation fuzzer for LS standard-library text/data parsers.

Where genfuzz.py generates whole typed PROGRAMS (to stress ownership/drop),
this fuzzer feeds mutated/adversarial STRING INPUT to the stdlib's recursive
descent parsers — json / csv / md / html / regex / strconv — which is exactly
the shape that finds parser crashes, infinite loops, and leaks.

Each iteration:
  1. pick a target (json, csv, ...), pick+mutate one of its seed inputs,
  2. escape the mutated bytes into an LS string literal (arbitrary bytes are
     representable: " -> \\" , \\ -> \\\\ , NL/CR/TAB -> \\n\\r\\t, else \\xHH),
  3. emit a tiny driver program that parses/consumes that input,
  4. run it under `lls run --memcheck`.

Oracle:
    exit not in {0,1}          -> CRASH  (segfault / abort / heap corruption)
    timeout                    -> HANG   (parser infinite loop / runaway recursion)
    exit 0 but not "OK clean"  -> MEMCHECK (leak / double-free / invalid free)
    exit 1                     -> DRIVER  (the generated program failed to compile;
                                           a harness/escaping bug, NOT a stdlib bug —
                                           surfaced so it can be fixed, never hidden)

Every program is fully determined by its seed, so any finding regenerates
deterministically (same seed+iter).

Usage:
    python tests/fuzz/stdfuzz.py [--iters 2000] [--seed 0] [--timeout 15]
                                 [--only json,md] [--keep-dir tests/fuzz/crashes]
"""
import os, sys, random, subprocess, argparse, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LS = os.path.join(ROOT, "build", "Release", "lls.exe")


# ---- escape arbitrary bytes into the content of an LS "..." string literal ----
def ls_lit(b):
    out = ['"']
    for ch in b:
        if ch == 0x22:   out.append('\\"')
        elif ch == 0x5c: out.append('\\\\')
        elif ch == 0x0a: out.append('\\n')
        elif ch == 0x0d: out.append('\\r')
        elif ch == 0x09: out.append('\\t')
        elif 0x20 <= ch <= 0x7e: out.append(chr(ch))
        else: out.append('\\x%02x' % ch)
    out.append('"')
    return ''.join(out)


# ---- per-target seed corpora + driver templates ({LIT} = escaped literal) ----
# A driver must ALWAYS type-check (input is data, never code) and must CONSUME
# the parse result so drop paths run. Keep them minimal but result-consuming.
TARGETS = {
    "json": {
        "imports": "import std.text.json as json\nimport std.core.str\n",
        "seeds": [
            b'null', b'true', b'false', b'42', b'-3.14', b'1e10', b'"str"',
            b'[]', b'{}', b'[1,2,3]', b'{"a":1,"b":[2,3],"c":{"d":null}}',
            b'[[[[1]]]]', b'{"k":"v\\u00e9"}', b'  [ 1 , 2 ]  ', b'"\\n\\t\\""',
            b'[true,false,null,0,"",{}]', b'{"nested":{"deep":{"x":[1,[2,[3]]]}}}',
        ],
        "body": (
            "    match json.parse({LIT}) {\n"
            "        Ok(v) => { Str s = json.stringify(v) @print(s.len()) }\n"
            "        Err(e) => { @print(e.len()) }\n"
            "    }\n"
        ),
    },
    "csv": {
        "imports": "import std.text.csv as csv\nimport std.core.str\n",
        "seeds": [
            b'1,2,3\n4,5,6\n', b'a,b,c\n', b'"quoted","field"\n',
            b'x\n', b',,\n', b'1,"a,b",3\n', b'"multi\nline",x\n',
            b'"esc""aped",y\n', b'\n\n\n', b'a,b\r\nc,d\r\n',
        ],
        "body": (
            "    Str a = {LIT}\n"
            "    match csv.parse(&a) {\n"
            "        Ok(t) => { @print(t.nrows() + t.ncols()) }\n"
            "        Err(e) => { @print(e.len()) }\n"
            "    }\n"
        ),
    },
    "md": {
        "imports": "import std.text.md as md\nimport std.core.str\n",
        "seeds": [
            b'# Heading\n', b'plain text', b'**bold** and *italic*',
            b'- a\n- b\n- c\n', b'1. one\n2. two\n', b'`code`',
            b'```\nblock\n```\n', b'[link](http://x)', b'![img](y.png)',
            b'> quote\n> more\n', b'| a | b |\n|---|---|\n| 1 | 2 |\n',
            b'---\n', b'text with # hash', b'nested **bold *and* italic**',
        ],
        "body": (
            "    Str h = md.to_html({LIT})\n"
            "    @print(h.len())\n"
        ),
    },
    "html": {
        "imports": "import std.text.html as html\nimport std.core.str\n",
        "seeds": [
            b'<p>hi</p>', b'<div class="x"><span>y</span></div>',
            b'<br/>', b'<!-- comment -->', b'<a href="u">link</a>',
            b'<ul><li>1</li><li>2</li></ul>', b'plain', b'<b><i>x</i></b>',
            b'<input type="text" value="v">', b'<p>unclosed',
            b'<TAG attr>text</TAG>', b'<script>x<y</script>', b'&amp;&lt;&gt;',
        ],
        "body": (
            "    HtmlDoc d = html.parse({LIT})\n"
            "    @print(1)\n"
        ),
    },
    # regex PATTERN fuzz — the compiler is the target; text is fixed.
    "regex_pat": {
        "imports": "import std.text.regex as re\nimport std.core.vec\nimport std.core.str\n",
        "seeds": [
            b'\\w+', b'\\d{2,4}', b'[a-z]+', b'a*b?c+', b'(foo|bar)',
            b'^\\s*$', b'a{3}', b'[^abc]', b'\\.', b'(a(b(c)))',
            b'a|b|c', b'.*', b'\\b\\w+\\b', b'[0-9]{1,}', b'(?:x)',
        ],
        "body": (
            '    bool m = re.matches("hello world 42 foo_bar", {LIT})\n'
            "    @print(m)\n"
            '    Vec(Str) parts = re.split("a,b,,c,d", {LIT})\n'
            "    @print(parts.len())\n"
            '    Vec(Str) hits = re.find_all("aXbXc 12 34", {LIT})\n'
            "    @print(hits.len())\n"
        ),
    },
    # regex TEXT fuzz — the matcher engine is the target; pattern is fixed valid.
    "regex_text": {
        "imports": "import std.text.regex as re\nimport std.core.vec\nimport std.core.str\n",
        "seeds": [
            b'hello world', b'2024-01-15', b'aaa bbb ccc', b'', b'   ',
            b'a1b2c3', b'the quick brown fox', b'x' * 64,
        ],
        "body": (
            '    Vec(Str) hits = re.find_all({LIT}, "\\\\w+")\n'
            "    @print(hits.len())\n"
            '    bool m = re.matches({LIT}, "[0-9]+")\n'
            "    @print(m)\n"
        ),
    },
    "strconv_fmt": {
        "imports": "import std.text.strconv as sc\nimport std.core.vec\nimport std.core.str\n",
        "seeds": [
            b'{} {} {}', b'no placeholders', b'{}{}{}', b'a{}b{}c',
            b'{', b'}', b'{}{', b'literal {} text', b'{0} {1}', b'%s %d',
        ],
        "body": (
            '    Vec(Str) args = ["x", "y", "z"]\n'
            "    Str r = sc.format({LIT}, args)\n"
            "    @print(r.len())\n"
        ),
    },
}


def build(name, lit):
    t = TARGETS[name]
    return ("%sdef main() -> int {\n%s    return 0\n}\n"
            % (t["imports"], t["body"].replace("{LIT}", lit)))


# ---- byte-level mutation over a seed corpus ----
def mutate(rng, seeds):
    r = rng.random()
    # 12%: deep structural nesting — hunts runaway recursion / stack overflow in
    # the recursive descent parsers (many open brackets/tags/hashes, no close).
    if r < 0.12:
        ch = rng.choice([b'[', b'{', b'<a>', b'<', b'#', b'(', b'"', b'- ',
                         b'[1,', b'{"a":', b'\\'])
        n = rng.randint(50, 4000)
        return ch * n
    # 10%: pure random bytes (any value 0..255)
    if r < 0.22:
        n = rng.randint(0, 200)
        return bytes(rng.randint(0, 255) for _ in range(n))
    # 8%: splice two seeds
    if r < 0.30:
        a = rng.choice(seeds); b = rng.choice(seeds)
        cut = rng.randint(0, len(a))
        return a[:cut] + b
    # otherwise: take one seed and apply 1..6 point mutations
    b = bytearray(rng.choice(seeds))
    for _ in range(rng.randint(1, 6)):
        if not b:
            b = bytearray(rng.choice(seeds)); continue
        op = rng.randint(0, 4)
        i = rng.randint(0, len(b) - 1)
        if op == 0:   b[i] ^= (1 << rng.randint(0, 7))          # bit flip
        elif op == 1: b[i] = rng.randint(0, 255)                # byte set
        elif op == 2: del b[i]                                  # delete
        elif op == 3: b.insert(i, rng.randint(0, 255))          # insert
        else:                                                   # duplicate run
            j = min(len(b), i + rng.randint(1, 8)); b[i:i] = b[i:j]
    return bytes(b)


def classify(rc, err):
    if rc == "timeout": return "hang"
    if rc not in (0, 1): return "crash"
    if rc == 1:          return "driver"   # generated program didn't compile
    if "OK clean" not in err: return "memcheck"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--timeout", type=float, default=15.0)
    ap.add_argument("--only", default=None,
                    help="comma-separated subset of targets (default: all)")
    ap.add_argument("--keep-dir", default=os.path.join(ROOT, "tests", "fuzz", "crashes"))
    args = ap.parse_args()

    if not os.path.exists(LS):
        print("build/Release/lls.exe not found", file=sys.stderr); return 2
    names = list(TARGETS)
    if args.only:
        names = [n for n in args.only.split(",") if n in TARGETS]
        if not names:
            print("no matching targets in --only", file=sys.stderr); return 2
    os.makedirs(args.keep_dir, exist_ok=True)
    env = dict(os.environ); env["LS_HOME"] = ROOT
    # Per-process temp file: parallel instances (different --seed) must NOT share
    # one _std.lls or they clobber each other's source between write and run,
    # producing bogus driver/crash findings. Key it by seed + pid.
    tmp = os.path.join(args.keep_dir, "_std_s%d_p%d.lls" % (args.seed, os.getpid()))

    findings = {"crash": 0, "hang": 0, "memcheck": 0, "driver": 0}
    per = {n: 0 for n in names}
    t0 = time.time()
    for it in range(args.iters):
        rng = random.Random(args.seed * 1000000 + it)
        name = rng.choice(names)
        per[name] += 1
        data = mutate(rng, TARGETS[name]["seeds"])
        src = build(name, ls_lit(data))
        with open(tmp, "wb") as f:
            f.write(src.encode("utf-8"))
        try:
            p = subprocess.run([LS, "run", "--memcheck", tmp],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                               timeout=args.timeout, env=env)
            rc = p.returncode; err = p.stderr.decode("utf-8", "replace")
        except subprocess.TimeoutExpired:
            rc, err = "timeout", ""
        kind = classify(rc, err)
        if kind:
            findings[kind] += 1
            tag = "std_%s_%s_seed%d_it%d" % (name, kind, args.seed, it)
            with open(os.path.join(args.keep_dir, tag + ".lls"), "wb") as f:
                f.write(src.encode("utf-8"))
            with open(os.path.join(args.keep_dir, tag + ".input"), "wb") as f:
                f.write(data)
            with open(os.path.join(args.keep_dir, tag + ".err.txt"), "wb") as f:
                f.write(err.encode("utf-8", "replace"))
        if (it + 1) % 100 == 0:
            print("  %d/%d  crash=%d hang=%d memcheck=%d driver=%d  (%.1f/s)" %
                  (it+1, args.iters, findings["crash"], findings["hang"],
                   findings["memcheck"], findings["driver"], (it+1)/(time.time()-t0)))

    print("\n=== %d iters in %.1fs ===" % (args.iters, time.time()-t0))
    print("per-target:", "  ".join("%s=%d" % (n, per[n]) for n in names))
    print("crashes=%d  hangs=%d  memcheck=%d  driver=%d" %
          (findings["crash"], findings["hang"], findings["memcheck"], findings["driver"]))
    return 1 if any(findings.values()) else 0


if __name__ == "__main__":
    sys.exit(main())
