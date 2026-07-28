#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""LS_NO_* switch parity oracle.

Every LS_NO_* switch shares one contract: it changes the emitted IR but must NOT
change what the program does. That makes the oracle the RUN OUTPUT, never the
IR. Comparing IR here would be a guaranteed false red -- LS_NO_ELIDE, for one,
legitimately turns twelve lines of match_own_stress from a move into a
Str.__clone call, which is exactly why the IR snapshot tests and LS_NO_ELIDE
were a known-broken combination before tests/ir_snapshot.cmake learned to skip.

For each switch S and each sample F:

    run(F) with S unset  ->  (rc, stdout)
    run(F) with S=1      ->  (rc, stdout)

and the two must agree. With --memcheck the leak/double-free verdict joins the
comparison, which is where a switch that corrupts ownership would show up even
if the printed output happened to survive.

Two switches carry a documented exception to that contract (see
SWITCH_CAVEATS) and report under their own kind rather than PARITY:
LS_NO_FMA changes floating-point contraction, and LS_NO_ELIDE is observable
through destructor side effects -- disabling clone elision adds a clone and
therefore extra destructor calls, which a sample that prints from ~ will see.
Neither is a defect; both need domain triage.

Samples are parallelized ACROSS samples, never within one: a sample's baseline
and switched runs stay strictly serialized so that any sample writing an output
file cannot race against itself.

Usage:
    python tests/fuzz/switchparity.py [--switches A,B] [--limit N] [--memcheck]
                                      [--jobs 4] [--timeout 30] [--budget 4.0]
"""
import os, re, sys, argparse, subprocess, time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LS = os.path.join(ROOT, "build", "Release", "lls.exe")
SAMPLES = os.path.join(ROOT, "tests", "samples")

# Switches whose contract is "changes IR, preserves semantics". Verified against
# `grep -rhoE 'getenv\("LS_[A-Z_0-9]+"\)' src/ runtime/` rather than copied from
# the docs. Deliberately excluded: LS_HOME / LS_CLANG / LS_TARGET / LS_OPT
# (inputs, not toggles), LS_DEBUG_* / LS_TYPE_STATS (diagnostic output only).
DEFAULT_SWITCHES = [
    "LS_NO_ELIDE",
    "LS_NO_INTERNALIZE",
    "LS_NO_ENUM_RANGE",
    "LS_NO_LIFETIME",
    "LS_NO_DROP_SENTINEL",
    "LS_NO_MEMCPY_PRIM",
    "LS_NO_TYPETAB",
    "LS_NO_IMPLTAB",
    "LS_NO_NOALIAS",
    "LS_NO_BORROW_ATTRS",
    "LS_FORCE_NOALIAS",
    "LS_OWN_AUDIT",
    "LS_NO_FMA",
]

# Switches whose "preserves semantics" contract has a documented exception.
# A difference here is reported under its own kind and needs domain triage
# before anyone calls it a defect.
SWITCH_CAVEATS = {
    "LS_NO_FMA": ("FMA",
                  "changes floating-point contraction, so a differing last digit "
                  "on a float-heavy sample is legitimate"),
    # Found by this very sweep: struct_param_test.lls prints from its destructor,
    # and disabling clone elision adds one clone and therefore two more `[drop]`
    # lines. Clone elision is observable through destructor side effects, exactly
    # like C++ copy elision -- "preserves semantics" is true only for programs
    # that do not observe how many times a value was copied.
    "LS_NO_ELIDE": ("ELIDE",
                    "clone elision is observable through destructor side effects; "
                    "samples that print from ~ legitimately differ"),
}


_TIME = re.compile(r"\[@time\] [0-9.]+ ms")
_BENCH = re.compile(r"mean [0-9.]+ ns")


def normalize(out):
    """Erase the measured numbers that @time / @bench print, keeping their line
    structure. Two defences against unstable output are needed, not one: this
    handles the patterns we know, and the repeated baseline below handles the
    ones we do not. Neither alone is enough -- generic_clone_attime.lls prints
    `[@bench] mean 0.0 ns` most of the time and `50.0 ns` occasionally, so two
    consecutive baselines agree often enough to slip past the empirical check
    and be reported as a switch parity failure."""
    out = _TIME.sub("[@time] T ms", out)
    return _BENCH.sub("mean T ns", out)


def run_sample(path, switch, memcheck, timeout):
    """Return (rc, normalized stdout) or None on timeout."""
    env = dict(os.environ)
    if switch:
        env[switch] = "1"
    cmd = [LS, "run"] + (["--memcheck"] if memcheck else []) + [path]
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           env=env, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    return p.returncode, normalize(p.stdout.decode("utf-8", "replace"))


def check_sample(path, switches, memcheck, timeout, budget):
    """Serialized per sample. Returns (findings, status).

    status is one of "ok", "slow", "unclean", "nondet".

    A sample is checked against every switch, so its runtime is multiplied by
    len(switches)+2. The corpus contains heavy benchmark samples (one takes ~240
    seconds on its own), and running those against thirteen switches turns a
    ten-minute sweep into an overnight one -- the first full run had to be killed
    while it held lls.exe open. So the baseline is timed, and anything slower
    than `budget` is skipped before any switch is tried, bounding the cost of a
    slow sample to one baseline run."""
    t0 = time.time()
    base = run_sample(path, None, memcheck, timeout)
    if base is None:
        return [], "slow"               # too slow to use as a baseline
    if time.time() - t0 > budget:
        return [], "slow"
    rc0, out0 = base
    if rc0 not in (0, 1):
        return [], "unclean"            # not a clean baseline; that is fuzz.py's job

    # Run the baseline a SECOND time and require it to agree with itself. Some
    # samples print things that vary run to run -- at_time_bench_test.lls prints
    # measured elapsed times, and every one of the 13 switches "failed" on it
    # until this check existed. Detecting that empirically beats maintaining a
    # hand-written list of unstable output patterns, which would silently rot as
    # samples are added.
    base2 = run_sample(path, None, memcheck, timeout)
    if base2 is None or base2 != base:
        return [], "nondet"

    findings = []
    for sw in switches:
        got = run_sample(path, sw, memcheck, timeout)
        if got is None:
            findings.append(("HANG", sw, "timed out under the switch, baseline did not"))
            continue
        rc1, out1 = got
        if rc1 != rc0:
            kind = SWITCH_CAVEATS.get(sw, ("PARITY", ""))[0]
            findings.append((kind, sw, "exit code %d -> %d" % (rc0, rc1)))
        elif out1 != out0:
            kind = SWITCH_CAVEATS.get(sw, ("PARITY", ""))[0]
            la, lb = out0.splitlines(), out1.splitlines()
            n = (sum(1 for a, b in zip(la, lb) if a != b) + abs(len(la) - len(lb)))
            findings.append((kind, sw, "stdout differs (~%d lines)" % n))
    return findings, "ok"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--switches", default=",".join(DEFAULT_SWITCHES))
    ap.add_argument("--limit", type=int, default=0, help="0 = all samples")
    ap.add_argument("--memcheck", action="store_true")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=30)
    ap.add_argument("--budget", type=float, default=4.0,
                    help="skip samples whose baseline run exceeds this many seconds")
    args = ap.parse_args()

    if not os.path.exists(LS):
        print("error: %s not found -- build Release first" % LS)
        return 2

    switches = [s.strip() for s in args.switches.split(",") if s.strip()]
    files = [os.path.join(SAMPLES, n) for n in sorted(os.listdir(SAMPLES))
             if n.endswith((".lls", ".ls")) and not n.startswith("_")]
    if args.limit:
        files = files[:args.limit]

    print("switchparity: %d sample(s) x %d switch(es)%s, jobs=%d"
          % (len(files), len(switches), " +memcheck" if args.memcheck else "", args.jobs))

    t0 = time.time()
    total = 0
    stats = {"ok": 0, "slow": 0, "unclean": 0, "nondet": 0}
    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(check_sample, f, switches, args.memcheck, args.timeout, args.budget): f
                for f in files}
        done = 0
        for fut, f in futs.items():
            fs, status = fut.result()
            stats[status] += 1
            done += 1
            for kind, sw, detail in fs:
                total += 1
                print("  %-7s %-20s %s\n          %s"
                      % (kind, sw, os.path.basename(f), detail))
            if done % 50 == 0:
                print("  ... %d/%d samples, %d finding(s), %.0fs"
                      % (done, len(files), total, time.time() - t0))

    print("switchparity: %d finding(s) in %.0fs" % (total, time.time() - t0))
    print("  compared=%d  skipped: nondeterministic=%d  slow=%d  unclean-baseline=%d"
          % (stats["ok"], stats["nondet"], stats["slow"], stats["unclean"]))
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
