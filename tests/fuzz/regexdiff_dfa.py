"""regexdiff_dfa.py -- differential oracle for the D1 lazy-DFA match-only
fast path (runtime/ls_regex.c: __ls_regex_exec_dfa / re_exec_vm_dfa).

Not part of ctest (see the repo's fuzzing discipline: fuzz extensions do not
get registered unless they fixed a real bug).

regexdiff.py already differential-tests the engine through re.capture(),
which is deliberately NOT routed through the DFA (multi-group callers stay
on __ls_regex_exec -- see the D1 report's routing decision). This sibling
script exercises exactly the LS-side entry points that WERE re-routed to
__ls_regex_exec_dfa: the free functions matches() and find(). Same
random-pattern/text generator as regexdiff.py (imported, not duplicated).

TWO independent checks per case, both computed in the SAME lls process so
there is no separate-run confound:

  1. ENGINE PARITY (the primary DFA-specific gate): matches()/find()'s
     group-0 answer (DFA-routed where eligible, __ls_regex_exec's fallback
     otherwise) must equal capture()'s group-0 answer (ALWAYS
     __ls_regex_exec, untouched by D1). Any mismatch here is, by
     construction, attributable to the DFA -- capture() is the pre-existing,
     already-verified answer for the exact same (pattern, text). This is
     the check that would go red if the DFA disagreed with the engine it is
     supposed to be a faster restatement of.
  2. Python comparison (informational, same as regexdiff.py): flags cases
     where BOTH engines agree with each other but disagree with Python --
     i.e. the pre-existing residual (nested-repetition capture-priority,
     see the D1 report) that regexdiff.py already tolerates. Reported
     separately so it is never confused with an ENGINE PARITY finding.

Usage:
    python tests/fuzz/regexdiff_dfa.py --iters 2000
    python tests/fuzz/regexdiff_dfa.py --iters 2000 --seed 12345
"""
import argparse
import os
import random
import re as pyre
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from regexdiff import gen_pattern, gen_text, ls_escape  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LLS = os.path.join(ROOT, "build", "Release", "lls.exe")

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


def run_ls(cases):
    """Run every (pattern, text) case in one lls process. For each, emit
    ONE ROW: "<dfa_answer>##<capture_answer>##<eligible>" where dfa_answer
    is matches()/find()'s group-0 string (or NONE), capture_answer is
    capture()[0] (or NONE) for the SAME pattern/text, and eligible is
    whether re.compile(pattern).is_dfa_eligible() -- so a finding can be
    reported as "this pattern never even reached the DFA" vs "it did, and
    disagreed with the pre-existing engine"."""
    body = []
    for pat, txt in cases:
        body.append('    {')
        body.append('        Str dfa_ans = "NONE"')
        body.append('        bool m = re.matches("%s", "%s")' % (ls_escape(txt), ls_escape(pat)))
        body.append('        if m {')
        body.append('            match re.find("%s", "%s") {' % (ls_escape(txt), ls_escape(pat)))
        body.append('                Some(g) => { dfa_ans = g }')
        body.append('                None    => { dfa_ans = "MATCHES-BUT-NO-FIND" }')
        body.append('            }')
        body.append('        }')
        body.append('        Str cap_ans = "NONE"')
        body.append('        Vec(Str) g = re.capture("%s", "%s")' % (ls_escape(txt), ls_escape(pat)))
        body.append('        if g.len() > 0 { cap_ans = g[0] }')
        body.append('        bool elig = false')
        body.append('        match re.compile("%s", 0) {' % ls_escape(pat))
        body.append('            Ok(r) => { elig = r.is_dfa_eligible() }')
        body.append('            Err(e) => {}')
        body.append('        }')
        body.append('        Str eligs = "0"')
        body.append('        if elig { eligs = "1" }')
        body.append('        rows.push(dfa_ans + "##" + cap_ans + "##" + eligs)')
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
    """Python's group-0 answer: NONE, or the matched substring."""
    try:
        m = pyre.compile(pat).search(txt)
    except pyre.error:
        return None          # pattern both engines may reject; skip
    if not m:
        return "NONE"
    return m.group(0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--batch", type=int, default=50)
    args = ap.parse_args()

    seed = args.seed if args.seed is not None else random.randrange(1 << 30)
    rng = random.Random(seed)
    print("seed=%d iters=%d" % (seed, args.iters))

    parity_findings = 0   # PRIMARY gate: DFA-path vs pre-existing engine
    py_findings = 0       # informational: both engines vs Python (known residual class)
    checked = 0
    dfa_routed = 0
    pending = []

    def flush(batch):
        nonlocal parity_findings, py_findings, checked, dfa_routed
        if not batch:
            return
        got, err = run_ls(batch)
        if got is None:
            print("LS RUN FAILED:\n%s" % err)
            for pat, txt in batch:
                print("  pattern=%r text=%r" % (pat, txt))
            parity_findings += 1
            return
        if len(got) != len(batch):
            print("OUTPUT COUNT MISMATCH: got %d want %d" % (len(got), len(batch)))
            parity_findings += 1
            return
        for (pat, txt), row in zip(batch, got):
            parts = row.split("##")
            if len(parts) != 3:
                parity_findings += 1
                print("MALFORMED ROW pattern=%r text=%r row=%r" % (pat, txt, row))
                continue
            dfa_ans, cap_ans, elig = parts
            checked += 1
            if elig == "1":
                dfa_routed += 1

            if dfa_ans == "MATCHES-BUT-NO-FIND":
                parity_findings += 1
                print("INCONSISTENT pattern=%r text=%r: matches()=true find()=None" % (pat, txt))
                continue

            # PRIMARY gate: DFA-routed answer must equal the pre-existing
            # engine's answer for the SAME (pattern, text), always -- this
            # is true regardless of what Python says.
            if dfa_ans != cap_ans:
                parity_findings += 1
                print("ENGINE PARITY pattern=%r text=%r dfa_eligible=%s\n  dfa(matches/find)=%r\n  capture()[0]     =%r"
                      % (pat, txt, elig, dfa_ans, cap_ans))
                continue

            # Informational: compare the (now-agreed) answer against Python.
            want = py_expect(pat, txt)
            if want is None:
                continue
            if dfa_ans != want:
                py_findings += 1

    for _ in range(args.iters):
        pat = gen_pattern(rng)
        txt = gen_text(rng)
        pending.append((pat, txt))
        if len(pending) >= args.batch:
            flush(pending)
            pending = []
    flush(pending)

    print("checked=%d dfa_routed=%d parity_findings=%d py_findings=%d"
          % (checked, dfa_routed, parity_findings, py_findings))
    return 1 if parity_findings else 0


if __name__ == "__main__":
    sys.exit(main())
