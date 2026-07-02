#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""ddmin-style minimizer for a crashing/hanging LS input.

Shrinks a fuzz finding to a minimal reproducer (line-level then char-level
deletion, keeping the input only while it still triggers). Default trigger =
the parser hanging under `ls parse`; pass --verb to target another stage and
--crash to minimize a non-{0,1} exit instead of a timeout.

Usage:
    python tests/fuzz/reduce.py <input.lls> [--verb parse] [--timeout 3] [--crash]
"""
import subprocess, sys, os, argparse

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LS = os.path.join(ROOT, "build", "Release", "lls.exe")
SCRATCH = os.path.join(ROOT, "tests", "fuzz", "_red.lls")

def make_triggers(verb, timeout, want_crash):
    def triggers(data):
        with open(SCRATCH, "wb") as f:
            f.write(data)
        try:
            p = subprocess.run([LS, verb, SCRATCH], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=timeout)
            return want_crash and p.returncode not in (0, 1)
        except subprocess.TimeoutExpired:
            return not want_crash   # timeout == hang trigger
    return triggers

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--verb", default="parse")
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--crash", action="store_true",
                    help="minimize a crash (exit not in {0,1}) instead of a hang")
    args = ap.parse_args()

    triggers = make_triggers(args.verb, args.timeout, args.crash)
    data = open(args.input, "rb").read()
    if not triggers(data):
        print("seed does not trigger; nothing to minimize", file=sys.stderr)
        return 1

    changed = True
    while changed:                                  # line-level
        changed = False
        lines = data.split(b"\n")
        for i in range(len(lines)):
            cand = b"\n".join(lines[:i] + lines[i+1:])
            if cand and triggers(cand):
                data = cand; changed = True; break
    changed = True
    while changed:                                  # char-level spans
        changed = False
        for n in (32, 16, 8, 4, 2, 1):
            i = 0
            while i < len(data):
                cand = data[:i] + data[i+n:]
                if cand and triggers(cand):
                    data = cand; changed = True; break
                i += 1
            if changed:
                break

    print("MINIMAL (%d bytes): %r" % (len(data), data))
    return 0

if __name__ == "__main__":
    sys.exit(main())
