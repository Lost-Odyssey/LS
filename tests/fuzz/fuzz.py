#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Mutation fuzzer for the LS compiler front-end.

Feeds mutated .ls sources to `ls <verb> <file>` and flags any exit code that is
NOT a clean result. The crash oracle (established empirically):

    exit 0  -> compiled / checked OK
    exit 1  -> clean, handled error (parse / type / codegen-reject)
    other   -> CRASH (segfault / abort / assertion / heap corruption)
    timeout -> HANG

Seeds come from tests/samples/*.ls. Mutations are byte- and token-level. Any
crashing input is saved verbatim under tests/fuzz/crashes/ for triage.

Usage:
    python tests/fuzz/fuzz.py [--verb emit-ir] [--iters 1000] [--seed 0]
                              [--timeout 10]

Stdlib-only (Python 3.x). Designed to later wire into ctest as a bounded batch.
"""
import os, sys, random, subprocess, hashlib, argparse, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LS_EXE = os.path.join(ROOT, "build", "Release", "ls.exe")
SAMPLES = os.path.join(ROOT, "tests", "samples")
CRASH_DIR = os.path.join(ROOT, "tests", "fuzz", "crashes")

# Tokens/fragments worth splicing in to reach deeper code paths.
INTERESTING = [
    b"def", b"methods", b"interface", b"struct", b"enum", b"match", b"import",
    b"return", b"if", b"while", b"for", b"in", b"self", b"&self", b"&!self",
    b"Vec(", b"Map(", b"Option(", b"Result(", b"Str", b"int", b"f64",
    b"=>", b"->", b"::", b"|x|", b"{", b"}", b"(", b")", b"[", b"]",
    b"try", b"!", b"?", b"~", b"static", b"public", b"private", b"as",
    b"\"\\{x}\"", b"f\"{}\"", b"0..10", b"[move v]", b".unwrap()", b".get(0)",
    # static reflection / @derive surface (parser -> checker -> emit paths)
    b"@derive(", b"@derive(Equal)", b"@derive(Hash, Equal, Order)",
    b"@derive(Show)", b"@derive(Serialize, Deserialize)", b"@derive(Reflect)",
    b"@derive(Clone)", b"@", b"Reflect", b"Serialize", b"Show",
    b".to_value()", b".reflect()", b".show()", b".from_value(",
    b"TypeInfo", b"Value", b"VInt(", b"struct B(T)", b"methods(T) B(T)",
]

def load_seeds():
    seeds = []
    for fn in os.listdir(SAMPLES):
        if fn.endswith(".ls"):
            try:
                with open(os.path.join(SAMPLES, fn), "rb") as f:
                    seeds.append(f.read())
            except OSError:
                pass
    return seeds

def mutate(data, rng):
    """Apply 1-4 random mutations."""
    b = bytearray(data)
    for _ in range(rng.randint(1, 4)):
        if len(b) == 0:
            b += rng.choice(INTERESTING)
            continue
        op = rng.randint(0, 9)
        if op == 0:                                   # flip a byte
            i = rng.randrange(len(b)); b[i] ^= 1 << rng.randint(0, 7)
        elif op == 1:                                 # delete a span
            i = rng.randrange(len(b)); n = rng.randint(1, max(1, len(b)//8))
            del b[i:i+n]
        elif op == 2:                                 # insert random byte
            i = rng.randrange(len(b)+1); b.insert(i, rng.randint(32, 126))
        elif op == 3:                                 # splice interesting token
            i = rng.randrange(len(b)+1); b[i:i] = rng.choice(INTERESTING)
        elif op == 4:                                 # duplicate a span
            i = rng.randrange(len(b)); n = rng.randint(1, max(1, len(b)//4))
            b[i:i] = b[i:i+n]
        elif op == 5:                                 # truncate
            b = b[:rng.randrange(len(b))]
        elif op == 6:                                 # repeat a structural char
            c = rng.choice(b"()[]{}<>|&*").to_bytes(1, "little") \
                if False else bytes([rng.choice(list(b"()[]{}<>|&*"))])
            i = rng.randrange(len(b)+1); b[i:i] = c * rng.randint(4, 64)
        elif op == 7:                                 # swap two spans
            if len(b) > 4:
                i = rng.randrange(len(b)//2); j = rng.randrange(len(b)//2, len(b))
                b[i], b[j] = b[j], b[i]
        elif op == 8:                                 # overwrite with token
            tok = rng.choice(INTERESTING)
            i = rng.randrange(len(b)); b[i:i+len(tok)] = tok
        else:                                         # inject NUL / high bytes
            i = rng.randrange(len(b)+1); b.insert(i, rng.choice([0, 0xFF, 0x80]))
    return bytes(b)

def splice(a, b, rng):
    """Concatenate halves of two seeds."""
    ca = a[:rng.randrange(len(a)+1)] if a else b""
    cb = b[rng.randrange(len(b)+1):] if b else b""
    return ca + cb

def classify(rc):
    if rc in (0, 1):
        return None
    return rc

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--verb", default="emit-ir",
                    choices=["check", "emit-ir", "compile"])
    ap.add_argument("--iters", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--timeout", type=float, default=10.0)
    args = ap.parse_args()

    if not os.path.exists(LS_EXE):
        print("error: build/Release/ls.exe not found; build first", file=sys.stderr)
        return 2
    os.makedirs(CRASH_DIR, exist_ok=True)
    seeds = load_seeds()
    if not seeds:
        print("error: no seeds in tests/samples", file=sys.stderr)
        return 2
    print("seeds=%d verb=%s iters=%d seed=%d" %
          (len(seeds), args.verb, args.iters, args.seed))

    rng = random.Random(args.seed)
    env = dict(os.environ); env["LS_HOME"] = ROOT
    tmp = os.path.join(CRASH_DIR, "_cur.ls")
    crashes, hangs = {}, 0
    t0 = time.time()
    for it in range(args.iters):
        if rng.random() < 0.15:
            src = splice(rng.choice(seeds), rng.choice(seeds), rng)
        else:
            src = mutate(rng.choice(seeds), rng)
        with open(tmp, "wb") as f:
            f.write(src)
        try:
            p = subprocess.run([LS_EXE, args.verb, tmp],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                               timeout=args.timeout, env=env)
            rc = classify(p.returncode)
        except subprocess.TimeoutExpired:
            rc = "timeout"
        if rc == "timeout":
            hangs += 1
            h = hashlib.sha1(src).hexdigest()[:12]
            with open(os.path.join(CRASH_DIR, "hang_%s.ls" % h), "wb") as f:
                f.write(src)
        elif rc is not None:
            h = hashlib.sha1(src).hexdigest()[:12]
            crashes.setdefault(rc, []).append(h)
            with open(os.path.join(CRASH_DIR, "crash_%d_%s.ls" % (rc & 0xffffffff, h)), "wb") as f:
                f.write(src)
        if (it+1) % 100 == 0:
            print("  %d/%d  crashes=%d hangs=%d  (%.0f/s)" %
                  (it+1, args.iters, sum(len(v) for v in crashes.values()),
                   hangs, (it+1)/(time.time()-t0)))

    print("\n=== done: %d iters in %.1fs ===" % (args.iters, time.time()-t0))
    print("crashes: %d unique inputs across %d exit codes; hangs: %d" %
          (sum(len(v) for v in crashes.values()), len(crashes), hangs))
    for rc, hs in sorted(crashes.items()):
        print("  exit 0x%08X (%d): %d inputs, e.g. crash_%d_%s.ls" %
              (rc & 0xffffffff, rc, len(hs), rc & 0xffffffff, hs[0]))
    return 1 if (crashes or hangs) else 0

if __name__ == "__main__":
    sys.exit(main())
