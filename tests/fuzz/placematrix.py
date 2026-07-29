#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Place-shape matrix oracle.

A value's behaviour must not depend on WHERE it lives. This builds the same
read / mutate / re-read sequence over one element type at every place shape --
local, struct field, nested field chain, Vec element, fixed-array element,
global -- and requires identical output from all of them.

That is the exact family behind the two most recent silent-wrong-value bugs:

  * cg_array_place_ptr (2026-07-25). Five sites each hand-rolled "get the array
    address from an IDENT symbol", so every non-identifier place broke: a
    fixed-size array behind a struct field printed nothing, its for-in body never
    ran, and stores into it were dropped -- all at exit code 0, no diagnostic.
  * L-023. Fixed-size arrays with owning elements had a read/return path that
    cloned and a write path with no ownership protocol at all.

Both are invisible to a crash-or-leak oracle and both show up instantly as "this
place disagrees with the others". Every generated program therefore checks
VALUES, never just the exit code.

Usage:
    python tests/fuzz/placematrix.py [--types int,Str] [--places local,field]
                                     [--memcheck] [--keep]
"""
import os, re, sys, argparse, shutil, subprocess, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LS = os.path.join(ROOT, "build", "Release", "lls.exe")

_JIT = re.compile(r"^\[jit\][^\n]*\n?", re.M)


def normalize(out):
    return _JIT.sub("", out)


# ------------------------------------------------------------ element types --
# show(p)   -> statements printing the value at place expression `p`
# mutate(p) -> statements changing it to the second canonical value
ELEMS = {
    "int": {
        "type": "int",
        "imports": [],
        "init": "10",
        "show": lambda p, i: "@print(%s)" % p,
        "mutate": lambda p: "%s = 20" % p,
    },
    "Str": {
        "type": "Str",
        "imports": ["std.core.str"],
        "init": '"alpha"',
        "show": lambda p, i: "@print(%s)" % p,
        "mutate": lambda p: '%s = "beta"' % p,
    },
    # The shape that broke: a fixed-size array is represented by its storage
    # ADDRESS, so every non-identifier place had to re-derive that address --
    # and five sites hand-rolled it for identifiers only. The read here is
    # deliberately three-way (for-in, index, and the sum of both) because the
    # failure modes differed per operation: printing emitted just a newline,
    # for-in skipped its body entirely, and stores were dropped.
    "array": {
        "type": "array(int, 3)",
        "imports": [],
        "init": "[1, 2, 3]",
        "show": lambda p, i: ("int sum%d = 0\nfor e in %s { sum%d = sum%d + e }\n"
                              "@print(sum%d)\n@print(%s[1])" % (i, p, i, i, i, p)),
        "mutate": lambda p: "%s[1] = 99" % p,
    },
    # has_drop struct: every place must clone on read and drop exactly once
    "struct": {
        "type": "Res",
        "imports": ["std.core.str"],
        "decls": "struct Res {\n    Str name\n    int n\n}\n",
        "init": 'Res{name: "r1", n: 1}',
        "show": lambda p, i: "@print(%s.name)\n@print(%s.n)" % (p, p),
        "mutate": lambda p: '%s = Res{name: "r2", n: 2}' % p,
    },
    # has_drop enum: binding is a MOVE since L-019, so the read path must clone
    "enum": {
        "type": "Tag",
        "imports": ["std.core.str"],
        "decls": "enum Tag {\n    A(Str)\n    B(int)\n}\n",
        "init": 'A("t1")',
        "show": lambda p, i: ("match %s {\n    A(s) => { @print(s) }\n"
                              "    B(k) => { @print(k) }\n}" % p),
        "mutate": lambda p: "%s = B(9)" % p,
    },
    # owning container as the element. Mutation goes through push rather than a
    # whole-value store: a list literal converts to a Vec only in initializer
    # position, so `p = [30, 40, 50]` is an array(int,3) and does not assign.
    "vec": {
        "type": "Vec(int)",
        "imports": ["std.core.vec"],
        "init": "[10, 20]",
        "show": lambda p, i: "@print(%s.len())\n@print(%s[0])" % (p, p),
        "mutate": lambda p: "%s.push(30)" % p,
    },
}

# Combinations the language deliberately rejects. Reported as N/A with the
# reason rather than counted as findings -- policy A (2026-07-19) bans by-value
# fixed-size-array parameters outright, and Vec's element handling goes through
# one.
UNSUPPORTED = {
    ("array", "vec_elem"):
        "policy A: Vec(array(T,N)) needs a by-value array parameter, which is banned",
    ("array", "mutref_param"):
        "borrowing a fixed-size array is not supported (`&array(T)` is a sizeless slice)",
    ("enum", "mutref_param"):
        "&! is restricted to structs: `&!Tag` is rejected",
    ("int", "mutref_param"):
        "&!int is not implemented; only &struct / &!struct / &enum exist",
    ("vec", "array_elem"):
        "array(Vec(T), N) cannot be initialized: the nested list literal types as "
        "array(array(int,N),M), not as a list of Vecs",
}


# ------------------------------------------------------------------ places --
# Each returns (file_scope_decls, setup_statements, place_expression).
def place_local(e):
    return "", "%s pv = %s" % (e["type"], e["init"]), "pv"


def place_struct_field(e):
    return ("struct PBox {\n    %s f\n}\n" % e["type"],
            "PBox pb = PBox{f: %s}" % e["init"], "pb.f")


def place_nested_field(e):
    return ("struct PInner {\n    %s f\n}\n\nstruct POuter {\n    PInner i\n}\n" % e["type"],
            "POuter po = POuter{i: PInner{f: %s}}" % e["init"], "po.i.f")


def place_vec_elem(e):
    return ("", "Vec(%s) pvec = {}\npvec.push(%s)" % (e["type"], e["init"]), "pvec[0]")


def place_array_elem(e):
    return ("", "array(%s, 2) parr = [%s, %s]" % (e["type"], e["init"], e["init"]),
            "parr[0]")


def place_global(e):
    return ("%s PG = %s\n" % (e["type"], e["init"]), "", "PG")


def place_mutref_param(e):
    """The value is reached through a `&!T` borrow parameter. Borrows only exist
    in parameter position, so the whole read/mutate/re-read sequence has to run
    inside the callee -- hence `wrap`."""
    return ("", "%s pv = %s" % (e["type"], e["init"]), "p",
            "def via_mut(&!%s p) {\n{BODY}\n}\n" % e["type"],
            "via_mut(&!pv)")


def place_module_global(e):
    """A global living in an imported module: its symbol is module-prefixed
    (P1-1), a path several place readers used to miss by looking up the bare
    name only."""
    return ("", "", "pmod.MG", None, None,
            "module pmod\n%s%s\n%s MG = %s\n"
            % ("".join("import %s\n" % m for m in e["imports"]),
               e.get("decls", ""), e["type"], e["init"]))


PLACES = {
    "local": place_local,
    "struct_field": place_struct_field,
    "nested_field": place_nested_field,
    "vec_elem": place_vec_elem,
    "array_elem": place_array_elem,
    "global": place_global,
    "mutref_param": place_mutref_param,
    "module_global": place_module_global,
}
# A read-only `&T` place is deliberately absent: this matrix's whole shape is
# read / mutate / re-read, and a read-only borrow cannot perform the middle
# step. `&T` reads are covered by the read half of mutref_param.


def _indent(text, n=4):
    pad = " " * n
    return "\n".join(pad + l for l in text.splitlines())


def build_program(elem, place):
    """Return {filename: text}; "main.lls" is the entry point."""
    e = ELEMS[elem]
    spec = PLACES[place](e)
    decls, setup, p = spec[0], spec[1], spec[2]
    wrap = spec[3] if len(spec) > 3 else None
    call = spec[4] if len(spec) > 4 else None
    modfile = spec[5] if len(spec) > 5 else None

    mods = list(e["imports"])
    if elem == "vec" or place == "vec_elem":
        if "std.core.vec" not in mods:
            mods.append("std.core.vec")
    if modfile:
        mods.append("pmod")
    imports = "".join("import %s\n" % m for m in mods)

    seq = "\n".join([e["show"](p, 1), e["mutate"](p), e["show"](p, 2)])

    # element-type declarations belong wherever the value's type is named; with
    # a module file that is the module, so main must not redeclare them
    head = imports + (e.get("decls", "") if not modfile else "") + decls

    if wrap:
        helper = wrap.replace("{BODY}", _indent(seq))
        main_body = "\n".join(x for x in (setup, call) if x)
        text = "%s\n%s\ndef main() {\n%s\n}\n" % (head, helper, _indent(main_body))
    else:
        main_body = "\n".join(x for x in (setup, seq) if x)
        text = "%s\ndef main() {\n%s\n}\n" % (head, _indent(main_body))

    files = {"main.lls": ("module main\n" + text) if modfile else text}
    if modfile:
        files["pmod.lls"] = modfile
    return files


def run_program(files, workdir, memcheck, timeout):
    for name, text in files.items():
        with open(os.path.join(workdir, name), "w", encoding="utf-8", newline="") as f:
            f.write(text)
    path = os.path.join(workdir, "main.lls")
    cmd = [LS, "run"] + (["--memcheck"] if memcheck else []) + [path]
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "<timeout>", ""
    err = p.stderr.decode("utf-8", "replace")
    # skip the JIT's per-function progress lines; the diagnostic is after them
    first = next((l for l in err.splitlines()
                  if l.strip() and not l.startswith("[jit]")), "")
    return p.returncode, normalize(p.stdout.decode("utf-8", "replace")), first


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--types", default=",".join(ELEMS))
    ap.add_argument("--places", default=",".join(PLACES))
    ap.add_argument("--memcheck", action="store_true")
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(LS):
        print("error: %s not found -- build Release first" % LS)
        return 2

    types = [t.strip() for t in args.types.split(",") if t.strip()]
    places = [p.strip() for p in args.places.split(",") if p.strip()]
    print("placematrix: %d type(s) x %d place(s)%s"
          % (len(types), len(places), " +memcheck" if args.memcheck else ""))

    findings = 0
    for elem in types:
        results = {}
        for place in places:
            if (elem, place) in UNSUPPORTED:
                print("  n/a      %s / %s: %s" % (elem, place, UNSUPPORTED[(elem, place)]))
                continue
            work = tempfile.mkdtemp(prefix="plm_%s_%s_" % (elem, place))
            try:
                rc, out, err = run_program(build_program(elem, place), work,
                                           args.memcheck, args.timeout)
                results[place] = (rc, out, err)
                if args.keep:
                    print("    kept %s" % work)
            finally:
                if not args.keep:
                    shutil.rmtree(work, ignore_errors=True)

        ok = {p: r for p, r in results.items() if r[0] == 0}
        for p, (rc, out, err) in sorted(results.items()):
            if rc != 0:
                findings += 1
                print("  BUILD    %s / %s: rc=%s\n           %s" % (elem, p, rc, err))
        vals = {}
        for p, (rc, out, err) in ok.items():
            vals.setdefault(out, []).append(p)
        if len(vals) > 1:
            findings += 1
            print("  DIVERGE  %s: %d distinct outputs across places" % (elem, len(vals)))
            for out, ps in sorted(vals.items(), key=lambda kv: -len(kv[1])):
                print("     %-30s %s" % (",".join(sorted(ps)), repr(out[:100])))
        elif ok:
            print("  ok       %s (%d places agree)" % (elem, len(ok)))

    print("placematrix: %d finding(s)" % findings)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
