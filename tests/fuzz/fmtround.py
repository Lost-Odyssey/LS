#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""fmt round-trip oracle for the LS compiler.

Where fuzz.py hunts crashes and genfuzz.py hunts ownership bugs, this hunts the
class those two are blind to: a change that compiles, does not leak, and is
WRONG. `lls fmt` must be behavior-preserving, which makes it a cheap, total
oracle over the whole parser surface -- every construct in the corpus gets
scanned, re-printed, and re-parsed, and any disagreement is a real defect.

Four judgements per file, cheapest first:

  CODE      code text (comments + whitespace stripped) must survive formatting
  COMPILE   if the original emits IR, the formatted source must too
  IR        emitted IR must be byte-identical after normalization
  IDEM      fmt(fmt(x)) == fmt(x)

Normalization for IR strips three things that vary legitimately:
`; ModuleID`, `source_filename`, and -- the one that is easy to miss -- the
line/col constants embedded in panic diagnostics. Force-unwrap failures and
slice bounds checks emit their source position as literal arguments
(`printf(ptr @fuw.fmt, i32 33, i32 21)`), so ANY reflow shifts them.

Sizing that rule honestly: when the @print spacing bug was still live it also
shifted COLUMNS, and struct_string_e2e.lls then reported a 289-line diff that
was entirely source positions -- enough to get the oracle written off as noise.
With that bug fixed only line reflow remains, and disabling the rule today
costs exactly one false finding (slice_test.lls, 2 lines). Still load-bearing,
just no longer dramatic.

This deliberately does not go through CMake: CMake's execute_process/file(WRITE)
re-encodes non-ASCII bytes, which would make every file with a non-ASCII comment
look like an idempotence failure. Python reads and writes bytes.

Oracle discipline: a finding is a claim, not a verdict. Triage each one by hand;
the CODE judgement in particular is a deliberately conservative proxy (see
code_only) and can over-report.

Usage:
    python tests/fuzz/fmtround.py [--corpus samples,stdlib] [--verbose]
"""
import os, re, sys, argparse, subprocess, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LS = os.path.join(ROOT, "build", "Release", "lls.exe")
SAMPLES = os.path.join(ROOT, "tests", "samples")
STDLIB = os.path.join(ROOT, "lib", "std")

_MODID = re.compile(r"^; ModuleID.*$", re.M)
_SRCFN = re.compile(r"^source_filename.*$", re.M)
# panic diagnostics embed their source position as literal printf arguments
_LINECOL = re.compile(r"i32 \d+, i32 \d+\)")
_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
_LINE_COMMENT = re.compile(r"//[^\n]*")


def norm_ir(ir):
    ir = _MODID.sub("; ModuleID = X", ir)
    ir = _SRCFN.sub('source_filename = "X"', ir)
    return _LINECOL.sub("i32 L, i32 C)", ir)


def code_only(src):
    """Comments and whitespace stripped -- a conservative proxy for the token
    stream (the compiler exposes no token-dump subcommand, and adding one just
    for a test would be scope creep).

    Deliberately imprecise: a string literal containing `//` or `/*` gets
    over-stripped, so this can flag a file that is actually fine. It only ever
    FLAGS; every flag is triaged by hand."""
    src = _BLOCK_COMMENT.sub("", src)
    src = _LINE_COMMENT.sub("", src)
    return "".join(src.split())


def run(args):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def fmt_stdout(path):
    """Return (rc, formatted_text)."""
    p = run([LS, "fmt", path, "--stdout"])
    return p.returncode, p.stdout.decode("utf-8", "replace")


def emit_ir(path):
    """Return (rc, ir_text). emit-ir writes the IR to STDERR, not stdout.

    Checker diagnostics share that stream, and they quote the offending source
    line with its line number -- both of which a formatter changes legitimately
    (lib/std/sync/lock.lls emits two block-protocol warnings and so reported a
    4-line "IR difference" that was entirely warning text). The IR proper starts
    at `; ModuleID`, so drop everything before it."""
    p = run([LS, "emit-ir", path])
    text = p.stderr.decode("utf-8", "replace")
    i = text.find("; ModuleID")
    return p.returncode, (text[i:] if i >= 0 else text)


def check_file(src_path, work_dir):
    """Return a list of (kind, detail) findings for one file."""
    findings = []

    rc, fmt_src = fmt_stdout(src_path)
    if rc != 0:
        return [("FMTFAIL", "fmt --stdout rc=%d" % rc)]

    with open(src_path, "r", encoding="utf-8", errors="replace") as f:
        orig_src = f.read()

    if len(orig_src) > 0 and len(fmt_src) == 0:
        return [("EMPTY", "fmt --stdout produced 0 bytes for a %d-char file" % len(orig_src))]

    if code_only(orig_src) != code_only(fmt_src):
        findings.append(("CODE", "code text differs after formatting"))

    # Compare IR from the SAME directory: relative imports must resolve, and the
    # file name feeds module identity, which feeds symbol mangling.
    d = os.path.dirname(src_path)
    a = os.path.join(d, "_fmtround_a.lls")
    b = os.path.join(d, "_fmtround_b.lls")
    try:
        shutil.copyfile(src_path, a)
        with open(b, "w", encoding="utf-8", newline="") as f:
            f.write(fmt_src)
        ra, ir_a = emit_ir(a)
        rb, ir_b = emit_ir(b)
        if ra != 0:
            # The original does not build standalone (reject corpus, or a
            # module meant only to be imported). Only require that formatting
            # does not turn a rejected file into an accepted one.
            if rb == 0:
                findings.append(("ASYM", "original rc=%d but formatted rc=0" % ra))
        elif rb != 0:
            findings.append(("COMPILE", "original compiles, formatted does not (rc=%d)" % rb))
        else:
            na = norm_ir(ir_a).replace("_fmtround_a", "M")
            nb = norm_ir(ir_b).replace("_fmtround_b", "M")
            if na != nb:
                la, lb = na.splitlines(), nb.splitlines()
                nd = sum(1 for x, y in zip(la, lb) if x != y) + abs(len(la) - len(lb))
                findings.append(("IR", "IR differs after normalization (~%d lines)" % nd))
    finally:
        for t in (a, b):
            if os.path.exists(t):
                os.remove(t)

    # idempotence
    tmp = os.path.join(work_dir, "_fmtround_idem.lls")
    with open(tmp, "w", encoding="utf-8", newline="") as f:
        f.write(fmt_src)
    try:
        rc2, fmt2 = fmt_stdout(tmp)
        if rc2 != 0:
            findings.append(("IDEM", "second fmt pass rc=%d" % rc2))
        elif fmt2 != fmt_src:
            findings.append(("IDEM", "fmt(fmt(x)) != fmt(x)"))
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)

    return findings


def collect(corpus):
    files = []
    if "samples" in corpus:
        for n in sorted(os.listdir(SAMPLES)):
            if n.endswith((".lls", ".ls")) and not n.startswith("_"):
                files.append(os.path.join(SAMPLES, n))
    if "stdlib" in corpus:
        for d, _, ns in os.walk(STDLIB):
            for n in sorted(ns):
                if n.endswith((".lls", ".ls")) and not n.startswith("_"):
                    files.append(os.path.join(d, n))
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", default="samples,stdlib",
                    help="comma-separated: samples,stdlib (default both)")
    ap.add_argument("--verbose", action="store_true",
                    help="also print files with no findings")
    args = ap.parse_args()

    if not os.path.exists(LS):
        print("error: %s not found -- build Release first" % LS)
        return 2

    work = os.path.join(ROOT, "tests", "fuzz", "crashes")
    os.makedirs(work, exist_ok=True)

    files = collect([c.strip() for c in args.corpus.split(",")])
    print("fmtround: %d file(s)" % len(files))

    total = 0
    for path in files:
        fs = check_file(path, work)
        for kind, detail in fs:
            total += 1
            print("  %-8s %s\n           %s" % (kind, os.path.basename(path), detail))
        if args.verbose and not fs:
            print("  ok       %s" % os.path.basename(path))

    print("fmtround: %d file(s) checked, %d finding(s)" % (len(files), total))
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
