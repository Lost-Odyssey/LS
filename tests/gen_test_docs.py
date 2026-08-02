#!/usr/bin/env python3
"""gen_test_docs.py -- generate docs/tests.html from the test suite itself.

The suite is the authority; this script only reads it. Two kinds of facts go
into the document, and the split is deliberate:

  DERIVED (never annotated) -- oracle modes, sample/driver paths, WIN32 gating,
    LS_* switches, whether the driver checks VALUES or only the exit code.
    These come from parsing the driver, so editing a driver updates the doc.
    Annotating them by hand would let them drift, which is exactly how the
    19 stale `file:line` anchors in src/ happened.

  ANNOTATED (must be written by a human) -- @subsystem, @guards, @sources, and
    the prose narrative. None of these can be inferred from code.

Annotations live in a comment header, either on the `add_test()` registration in
tests/tests.cmake or at the top of an exclusive tests/test_*.cmake driver. When a
driver is shared by several tests (test_plotfmt.cmake drives 46 of them), only
the registration comment is per-test; the driver header describes the harness.

    # test_x.cmake -- one-line summary.
    #
    # Free prose: what broke, how it presented (rc? silent? which oracle saw
    # it?), and why the assertions are shaped the way they are.
    #
    # @subsystem codegen/ownership
    # @guards L-023, BF-046
    # @sources codegen_own.c:emit_drop_value, codegen_expr.c:cg_array_place_ptr

The tag block goes at the END of the comment header. Putting it after the first
line splits the opening sentence in most drivers -- their summary runs across
several lines with no blank in between.

`@sources` names FUNCTIONS, never line numbers -- TU splits rot line anchors.

Usage:
    python tests/gen_test_docs.py [--out docs/tests.html] [--ctest-list FILE]

`--ctest-list` takes the output of `ctest -N` so the doc can flag registrations
that never actually run (and drivers nothing references).
"""

import argparse
import os
import re
import sys
import datetime
import html as _html

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(REPO, "tests")


# --------------------------------------------------------------------------
# CMake-lite reading
# --------------------------------------------------------------------------

def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def strip_comment_prefix(line):
    s = line.lstrip()
    assert s.startswith("#")
    s = s[1:]
    if s.startswith(" "):
        s = s[1:]
    return s.rstrip()


def collect_comment_above(lines, idx):
    """Contiguous comment block immediately above lines[idx] (blank line ends it)."""
    out = []
    j = idx - 1
    while j >= 0 and lines[j].lstrip().startswith("#"):
        out.insert(0, strip_comment_prefix(lines[j]))
        j -= 1
    return out


def balanced_block(lines, start):
    """Return (block_text, end_index) for a call starting at lines[start]."""
    depth = 0
    buf = []
    i = start
    while i < len(lines):
        buf.append(lines[i])
        depth += lines[i].count("(") - lines[i].count(")")
        if depth <= 0:
            break
        i += 1
    return "\n".join(buf), i


# --------------------------------------------------------------------------
# Annotation parsing
# --------------------------------------------------------------------------

TAG_RE = re.compile(r"^@(subsystem|guards|sources)\s+(.*)$")


def parse_annotations(comment_lines):
    """Split a comment block into {tags} and prose lines."""
    tags = {"subsystem": None, "guards": [], "sources": []}
    prose = []
    for ln in comment_lines:
        m = TAG_RE.match(ln.strip())
        if not m:
            prose.append(ln)
            continue
        key, val = m.group(1), m.group(2).strip()
        if key == "subsystem":
            tags["subsystem"] = val
        else:
            tags[key] = [p.strip() for p in val.split(",") if p.strip()]
    # drop leading/trailing blank prose lines
    while prose and not prose[0].strip():
        prose.pop(0)
    while prose and not prose[-1].strip():
        prose.pop()
    return tags, prose


def merge_tags(primary, fallback):
    out = dict(primary)
    if not out.get("subsystem"):
        out["subsystem"] = fallback.get("subsystem")
    for k in ("guards", "sources"):
        if not out.get(k):
            out[k] = fallback.get(k) or []
    return out


# --------------------------------------------------------------------------
# Oracle inference (derived facts)
# --------------------------------------------------------------------------

SUBCMD_ORACLE = {
    "run": "JIT",
    "compile": "AOT",
    "check": "CHECK",
    "emit-ir": "IR",
    "ir": "IR",
    "asm": "ASM",
    "emit-c": "EMIT-C",
    "fmt": "FMT",
    "repl": "REPL",
    "test": "LSTEST",
    "doc": "DOC",
    "symbol": "LSP",
    "complete": "LSP",
    "run-tests": "LSTEST",
}

EXEC_RE = re.compile(r"execute_process\(", re.M)
BUGID_RE = re.compile(r"\b(L-\d{3}|BF-\d{3})\b")


MEMCHECK_BANNER = re.compile(
    r"memcheck|OK clean|leak\\?\(s\\?\)|double-free|invalid free", re.I)


def infer_from_driver(text):
    """Derive oracle modes, env switches and assertion strength from the driver.

    Three things are easy to get subtly wrong here, so they are handled
    explicitly rather than by a single loose regex:

      * `--memcheck` is a MODIFIER, not a mode. `compile --memcheck` is still an
        AOT build; `run --memcheck` is still a JIT run. Treating it as a mode
        made test_memcheck_aot look like it never compiled anything.
      * Several drivers alias the binary (`set(LS "${LS_EXE}")`) and then invoke
        `"${LS}"`. Matching only `${LS_EXE}` reported six drivers as running
        nothing at all.
      * A content assertion on the memcheck banner is NOT a value check. Telling
        the two apart is the whole point of the VALUE badge, so output variables
        are tainted from OUTPUT_VARIABLE/ERROR_VARIABLE and followed through
        `set()` concatenations before the patterns are classified.
    """
    info = {"oracles": [], "env": [], "value_check": False,
            "diag_check": False, "reject": False}
    order = []

    def add(o):
        if o not in order:
            order.append(o)

    # binary aliases: set(LS "${LS_EXE}")
    aliases = {"LS_EXE"}
    for m in re.finditer(r'set\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+"?\$\{(LS_EXE|LS)\}"?\s*\)', text):
        aliases.add(m.group(1))
    alias_re = "|".join(sorted(aliases, key=len, reverse=True))

    lines = text.split("\n")
    for m in EXEC_RE.finditer(text):
        start_line = text[:m.start()].count("\n")
        blk, _ = balanced_block(lines, start_line)
        sub = re.search(r'\$\{(?:%s)\}"?\s+"?([A-Za-z][A-Za-z0-9-]*)' % alias_re, blk)
        if not sub:
            continue
        name = sub.group(1)
        if name in SUBCMD_ORACLE:
            add(SUBCMD_ORACLE[name])
        if "--memcheck" in blk:
            add("MEMCHECK")

    info["env"] = sorted(set(re.findall(r"set\(ENV\{(LS_[A-Z0-9_]+)\}", text)))

    # A driver that demands a NON-zero exit code is a reject corpus.
    if re.search(r"if\s*\(\s*\"?\$?\{?[A-Za-z0-9_]*rc[A-Za-z0-9_]*\}?\"?\s+EQUAL\s+0\s*\)", text):
        info["reject"] = True
        add("REJECT")

    # taint: which variables hold captured program output?
    out_vars, err_vars = set(), set()
    for m in re.finditer(r"OUTPUT_VARIABLE\s+([A-Za-z_][A-Za-z0-9_]*)", text):
        out_vars.add(m.group(1))
    for m in re.finditer(r"ERROR_VARIABLE\s+([A-Za-z_][A-Za-z0-9_]*)", text):
        err_vars.add(m.group(1))
    # cmake function parameters: several drivers factor their assertions into
    # `function(check_corpus_output label out)` and call it with "${jit_out}".
    # Without this the strongest corpora (array_owned_elem, struct_array_field)
    # looked like they only checked the exit code.
    funcs = {}
    for m in re.finditer(r"function\(\s*([A-Za-z_][A-Za-z0-9_]*)((?:\s+[A-Za-z_][A-Za-z0-9_]*)*)\s*\)", text):
        funcs[m.group(1)] = m.group(2).split()

    for _ in range(3):                      # follow set(all "${out}${err}")
        for m in re.finditer(r'set\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+([^)]*)\)', text):
            dst, rhs = m.group(1), m.group(2)
            refs = set(re.findall(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", rhs))
            if refs & out_vars:
                out_vars.add(dst)
            if refs & err_vars:
                err_vars.add(dst)
        for fname, params in funcs.items():
            for call in re.finditer(r"^\s*%s\(([^)]*)\)" % re.escape(fname), text, re.M):
                args = re.findall(r'"[^"]*"|\S+', call.group(1))
                for i, a in enumerate(args):
                    if i >= len(params):
                        break
                    refs = set(re.findall(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", a))
                    if refs & out_vars:
                        out_vars.add(params[i])
                    if refs & err_vars:
                        err_vars.add(params[i])

    def classify(subject, pattern):
        refs = set(re.findall(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", subject)) or {subject.strip()}
        if MEMCHECK_BANNER.search(pattern):
            return
        if refs & out_vars:
            info["value_check"] = True
        elif refs & err_vars:
            info["diag_check"] = True

    for m in re.finditer(r'(?:NOT\s+)?("?\$?\{?[A-Za-z_][A-Za-z0-9_]*\}?"?)\s+MATCHES\s+"([^"]*)"', text):
        classify(m.group(1), m.group(2))
    # STREQUAL against a want-variable is the strongest form: whole-output
    # equality. test_modglobal_assign uses only this and looked unchecked.
    for m in re.finditer(r'(?:NOT\s+)?("?\$?\{?[A-Za-z_][A-Za-z0-9_]*\}?"?)\s+STREQUAL\s+(\S+)', text):
        classify(m.group(1), "")
    for m in re.finditer(r'string\(FIND\s+"([^"]*)"\s+"([^"]*)"', text):
        classify(m.group(1), m.group(2))
    for m in re.finditer(r'string\(REGEX\s+MATCHALL\s+"([^"]*)"\s+[A-Za-z_][A-Za-z0-9_]*\s+"([^"]*)"', text):
        classify(m.group(2), m.group(1))

    info["oracles"] = order
    return info


# --------------------------------------------------------------------------
# Embedding sources: the test corpus and the compiler code it guards
# --------------------------------------------------------------------------

def load_sample(relpath):
    """Read a corpus file. Returns lines, or None."""
    path = os.path.join(REPO, relpath.replace("/", os.sep))
    if not os.path.isfile(path):
        return None
    lines = read_text(path).replace("\r\n", "\n").split("\n")
    while lines and not lines[-1].strip():
        lines.pop()
    return lines


SAMPLE_EXT = r"(?:lls|ls|in)"


def expand_with(base, s, depth=0):
    """Every string this `${...}` template can produce, given list bindings."""
    if depth > 3 or "${" not in s:
        return [s]
    m = re.search(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", s)
    if not m:
        return [s]
    vals = base.get(m.group(1))
    if not vals:
        return []
    out = []
    for v in vals[:24]:
        out.extend(expand_with(base, s[:m.start()] + v + s[m.end():], depth + 1))
    return out


def driver_samples(text, dvars=None):
    """Corpus paths a driver names itself.

    Only a minority of drivers get their corpus from `-DSAMPLE=` on the
    registration. The rest hardcode it, and they do not agree on how:
    `${SAMPLE_DIR}/x.lls`, `${CMAKE_CURRENT_LIST_DIR}/samples/x.lls`, or a
    local alias (`set(SDIR "${CMAKE_CURRENT_LIST_DIR}/samples")`). Resolving
    only the first form left 74 tests showing no corpus at all.

    Rather than enumerate spellings, expand whatever `set()` bindings the
    driver has and keep every resulting path that actually exists on disk --
    so a form nobody has invented yet still resolves, and a typo never
    produces a phantom entry.
    """
    # every binding maps to a LIST of candidate values, because several drivers
    # loop a corpus: set(_samples a b c) + "${SAMPLE_DIR}/${_s}.lls"
    base = {
        "SAMPLE_DIR": [os.path.join(TESTS, "samples")],
        "CMAKE_CURRENT_LIST_DIR": [TESTS],
        "CMAKE_SOURCE_DIR": [REPO],
    }
    for k, v in (dvars or {}).items():
        base.setdefault(k, []).append(v)
    for m in re.finditer(r'set\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+([^)]*)\)', text):
        vals = re.findall(r'"([^"]*)"|(\S+)', m.group(2))
        vals = [a or b for a, b in vals]
        base.setdefault(m.group(1), []).extend(v for v in vals if v)
    # get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
    for m in re.finditer(
            r'get_filename_component\(\s*(\w+)\s+"([^"]*)"\s+DIRECTORY\s*\)', text):
        for v in [os.path.dirname(x) for x in expand_with(base, m.group(2))]:
            base.setdefault(m.group(1), []).append(v)

    # macro(run_jit sample label) / function(check_case name ...) + call sites
    for m in re.finditer(r"(?:macro|function)\(\s*(\w+)((?:\s+\w+)*)\s*\)", text):
        name, params = m.group(1), m.group(2).split()
        for call in re.finditer(r"^\s*%s\(([^)]*)\)" % re.escape(name), text, re.M):
            args = re.findall(r'"([^"]*)"|(\S+)', call.group(1))
            args = [a or b for a, b in args]
            for i, p in enumerate(params):
                if i < len(args):
                    base.setdefault(p, []).append(args[i])

    # The remaining rules only COPY candidates between variables, and the real
    # drivers chain them in every order:
    #   foreach(_case IN LISTS _cases) -> string(REPLACE "#" ";" _parts "${_case}")
    #     -> list(GET _parts 0 _name) -> "${SAMPLE_DIR}/${_name}.lls"
    # A single pass in a fixed order resolves that chain only if the source
    # happens to come first, so iterate to a small fixed point instead.
    def propagate():
        # loop variables: `foreach(_s IN LISTS _samples)` / `foreach(x a b c)`
        for m in re.finditer(r"foreach\(\s*([A-Za-z_]\w*)\s+IN\s+LISTS\s+([A-Za-z_]\w*)", text):
            base.setdefault(m.group(1), []).extend(base.get(m.group(2), []))
        for m in re.finditer(r"foreach\(\s*([A-Za-z_]\w*)\s+((?![Ii][Nn]\b)[^)]*)\)", text):
            vals = re.findall(r'"([^"]*)"|(\S+)', m.group(2))
            base.setdefault(m.group(1), []).extend(a or b for a, b in vals if (a or b))
        # value-carrying commands
        for m in re.finditer(
                r'string\(\s*(?:REPLACE|REGEX\s+REPLACE)[^)]*?(\w+)\s+"\$\{(\w+)\}"\s*\)', text):
            base.setdefault(m.group(1), []).extend(base.get(m.group(2), []))
        for m in re.finditer(r"list\(\s*GET\s+(\w+)\s+\d+\s+(\w+)\s*\)", text):
            base.setdefault(m.group(2), []).extend(base.get(m.group(1), []))
        # list entries packing several fields: "diag_render_type#msg#caret",
        # "sim_viz_test.lls|SIM VIZ PASS" -- the corpus name is the first field
        for k in list(base):
            for v in list(base[k]):
                if ("#" in v or "|" in v or ";" in v):
                    base[k].append(re.split(r"[#|;]", v)[0])

    for _ in range(4):
        before = sum(len(v) for v in base.values())
        propagate()
        for k in base:                                  # keep the sets bounded
            seen, uniq = set(), []
            for v in base[k]:
                if v not in seen:
                    seen.add(v)
                    uniq.append(v)
            base[k] = uniq[:64]
        if sum(len(v) for v in base.values()) == before:
            break

    def expand(s, depth=0):
        return expand_with(base, s, depth)

    found = []

    def keep(p):
        p = os.path.normpath(p)
        cands = [p] if os.path.isabs(p) else [
            os.path.normpath(os.path.join(TESTS, p)),
            # a bare `foo.lls` in a list almost always lives in tests/samples/
            os.path.normpath(os.path.join(TESTS, "samples", p)),
        ]
        for c in cands:
            if os.path.isfile(c):
                rel = os.path.relpath(c, REPO).replace(os.sep, "/")
                if rel not in found:
                    found.append(rel)
                return

    for raw in re.findall(r'["\s(]([^"\s()]*\.%s)\b' % SAMPLE_EXT, text):
        if "CMAKE_BINARY_DIR" in raw:          # a build output, not a corpus
            continue
        for p in expand(raw):
            keep(p)

    # file(GLOB _cases "${CORPUS_DIR}/*.lls") -- a whole directory is the corpus
    for m in re.finditer(r'file\(\s*GLOB\w*\s+\w+\s+"([^"]*\*\.%s)"' % SAMPLE_EXT, text):
        for pat in expand(m.group(1)):
            d, leaf = os.path.split(pat)
            if not os.path.isdir(d):
                continue
            ext = leaf.split(".")[-1]
            for f in sorted(os.listdir(d)):
                if f.endswith("." + ext):
                    keep(os.path.join(d, f))
    return found


def sample_header_prose(lines):
    """The `//` block at the top of a corpus file.

    502 of the 650 corpora carry one, and they are often the best account of the
    defect that exists anywhere -- written by whoever reduced the repro. Free
    detail for a large part of the suite, and it cannot go stale because it
    lives in the file it describes.
    """
    out = []
    for ln in lines:
        s = ln.strip()
        if s.startswith("//"):
            out.append(s[2:].lstrip() if s[2:3] == " " else s[2:])
        elif not s and not out:
            continue
        else:
            break
    while out and not out[-1].strip():
        out.pop()
    return out


def extract_c_function(relpath, fn):
    """Pull a C function body out of the compiler sources.

    Deliberately dumb brace matching rather than a parser: the codebase is
    plain C17 with the definition's return type on the same line, and a wrong
    extraction is visible immediately in the rendered page.

    Returns (first_line_no, last_line_no, [lines], truncated_bool) or None.
    """
    path = os.path.join(REPO, relpath.replace("/", os.sep))
    if not os.path.isfile(path):
        return None
    src = read_text(path).replace("\r\n", "\n").split("\n")
    pat = re.compile(r"\b%s\s*\(" % re.escape(fn))
    for i, line in enumerate(src):
        if not pat.search(line):
            continue
        stripped = line.lstrip()
        if stripped.startswith(("//", "*", "#")) or line.rstrip().endswith(";"):
            continue          # comment, or a prototype
        depth = 0
        started = False
        end = None
        # ast_clone_deep alone is ~410 lines, so the scan window has to be wide
        for j in range(i, min(len(src), i + 900)):
            depth += src[j].count("{") - src[j].count("}")
            if "{" in src[j]:
                started = True
            if started and depth <= 0:
                end = j
                break
        if end is None:
            continue
        # No line cap: the box scrolls, and each function is stored once in the
        # payload, so a 396-line cg_stmt_var_decl costs ~24 KB total. Cutting it
        # at an arbitrary line is worse than scrolling -- the part that matters
        # is usually not in the first N lines.
        return i + 1, end + 1, src[i:end + 1], False
    return None


# --------------------------------------------------------------------------
# Assertion extraction: what does the driver actually demand?
# --------------------------------------------------------------------------

def extract_assertions(text):
    """Derive the pass conditions a driver enforces.

    Returns (required, forbidden) pattern lists. This is what turns "runs three
    ways" into something a reader can act on: it names the exact strings the
    program has to print and the ones that must never appear (an LLVM verifier
    complaint, a stale diagnostic wording, a FAIL marker).
    """
    required, forbidden = [], []

    # `if(NOT out MATCHES "${_expected}")` is useless in a document, so resolve
    # the literal `set()` bindings first and drop whatever stays a bare ${var}
    # (those are foreach loop variables, already collected item by item below).
    literals = dict(re.findall(r'set\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+"([^"$]*)"\s*\)', text))

    def norm(p):
        for _ in range(2):
            p = re.sub(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}",
                       lambda m: literals.get(m.group(1), m.group(0)), p)
        # show the intent, not the CMake regex escaping
        p = p.replace("\\\\", "\\")
        p = re.sub(r"\\([(){}\[\]|.+*?^$])", r"\1", p)
        return re.sub(r"\s+", " ", p).strip()

    for m in re.finditer(
            r'if\s*\(\s*(NOT\s+)?"?[^)\n]*?"?\s+MATCHES\s+"([^"]+)"\s*\)', text):
        (required if m.group(1) else forbidden).append(norm(m.group(2)))

    # foreach(_want "A" "B") ... endforeach() -- the SENSE comes from the body,
    # not the list: test_diag_ownership_wording has a foreach(_bad ...) whose
    # whole point is that those strings must NOT appear, and reading the list
    # alone flips the meaning of the strongest assertions in the suite.
    for m in re.finditer(r"foreach\(\s*(\w+)((?:\s*\"[^\"]*\")+)\s*\)(.*?)endforeach",
                         text, re.S):
        var, items, body = m.group(1), m.group(2), m.group(3)
        ref = r"\$\{%s\}" % re.escape(var)
        negated = bool(re.search(r"if\s*\(\s*NOT\b[^)\n]*%s" % ref, body)) or \
                  bool(re.search(r"if\s*\(\s*\w*\s+EQUAL\s+-1", body))
        bucket = required if negated else forbidden
        for p in re.findall(r'"([^"]*)"', items):
            if p.strip():
                bucket.append(norm(p))

    # string(FIND "<subject>" "<pat>" v) followed by the sense of the test
    for m in re.finditer(
            r'string\(FIND\s+"[^"]*"\s+"([^"]+)"\s+(\w+)\)(.{0,220})', text, re.S):
        pat, var, after = norm(m.group(1)), m.group(2), m.group(3)
        if re.search(r"if\s*\(\s*NOT\s+%s\s+EQUAL\s+-1" % re.escape(var), after):
            forbidden.append(pat)
        elif re.search(r"if\s*\(\s*%s\s+EQUAL\s+-1" % re.escape(var), after):
            required.append(pat)

    def keep(x):
        if not x or "${" in x:                       # unresolved loop variable
            return False
        if MEMCHECK_BANNER.search(x):                # already shown as a badge
            return False
        return not re.fullmatch(r"[\^\$\.\*\+\-0-9\\\[\]|\s]+", x)

    def dedup(xs):
        seen, out = set(), []
        for x in xs:
            if keep(x) and x not in seen:
                seen.add(x)
                out.append(x)
        return out

    return dedup(required), dedup(forbidden)


# --------------------------------------------------------------------------
# tests.cmake parsing
# --------------------------------------------------------------------------

class Test(object):
    def __init__(self, name):
        self.name = name
        self.kind = "e2e"          # e2e | unit | direct | snapshot
        self.direct_cmd = None
        self.driver = None
        self.samples = []
        self.reg_comment = []
        self.win32_only = False
        self.tags = {"subsystem": None, "guards": [], "sources": []}
        self.prose = []
        self.driver_prose = []
        self.shared_prose = []
        self.shared_users = 0
        self.oracles = []
        self.env = []
        self.value_check = False
        self.diag_check = False
        self.reject = False
        self.required = []
        self.forbidden = []
        self.expect = None
        self.dvars = {}
        self.generated_corpus = False
        self.will_fail = False
        self.tags_from = None      # "registration" | "driver" | None


def expand_foreach(lines):
    """Yield (line, win32, loop_bindings) preserving original indices.

    Handles the single `foreach(cm a b c)` form used for the cmatrix block and
    the `if(WIN32)` gate around the IR snapshots. Anything more exotic is left
    alone -- the ctest -N cross-check will flag it if we ever miss a test.
    """
    out = []
    i = 0
    win32 = False
    while i < len(lines):
        ln = lines[i]
        s = ln.strip()
        # skip function bodies: the ls_ir_snapshot() definition contains an
        # add_test() with an unexpanded ${ARG_NAME}, which is a template, not
        # a test. Its call sites are handled separately.
        if re.match(r"function\s*\(", s):
            while i < len(lines) and not re.match(r"\s*endfunction", lines[i]):
                i += 1
            i += 1
            continue
        if re.match(r"if\s*\(\s*WIN32\s*\)", s):
            win32 = True
            i += 1
            continue
        if re.match(r"endif\s*\(", s) and win32:
            win32 = False
            i += 1
            continue
        m = re.match(r"foreach\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*(.*)$", s)
        if m:
            var = m.group(1)
            items = m.group(2)
            j = i
            # gather the item list until the closing paren of foreach(
            depth = ln.count("(") - ln.count(")")
            while depth > 0 and j + 1 < len(lines):
                j += 1
                items += " " + lines[j]
                depth += lines[j].count("(") - lines[j].count(")")
            items = items.replace(")", " ")
            values = [v for v in items.split() if v]
            # body up to endforeach
            k = j + 1
            body = []
            while k < len(lines) and not re.match(r"\s*endforeach", lines[k]):
                body.append((k, lines[k]))
                k += 1
            # Comments for a loop-registered test sit above the `foreach(`, not
            # above the add_test() inside it, so anchor the whole body there --
            # otherwise the 14 cmatrix tests read as unannotated.
            for v in values:
                for _, bl in body:
                    out.append((i, bl.replace("${%s}" % var, v), win32))
            i = k + 1
            continue
        out.append((i, ln, win32))
        i += 1
    return out


def parse_tests_cmake():
    raw = read_text(os.path.join(TESTS, "tests.cmake"))
    lines = raw.split("\n")
    expanded = expand_foreach(lines)
    # CMake builds a binary for exactly the C unit tests; nothing else.
    unit_targets = set(re.findall(r"add_executable\(\s*([A-Za-z0-9_]+)", raw))

    tests = {}
    i = 0
    while i < len(expanded):
        orig_idx, ln, win32 = expanded[i]
        if not re.match(r"\s*add_test\(", ln):
            i += 1
            continue
        # balanced block over the expanded stream
        depth = 0
        buf = []
        k = i
        while k < len(expanded):
            buf.append(expanded[k][1])
            depth += expanded[k][1].count("(") - expanded[k][1].count(")")
            if depth <= 0:
                break
            k += 1
        block = "\n".join(buf)
        nm = re.search(r"NAME\s+([A-Za-z0-9_]+)", block)
        if nm:
            t = Test(nm.group(1))
            t.win32_only = win32
            t.reg_comment = collect_comment_above(lines, orig_idx)
            drv = re.search(r"tests/([a-z0-9_]+\.cmake)", block)
            if drv:
                t.driver = drv.group(1)
            for s in re.findall(r"-D(?:SAMPLE|SCRIPT|FIXTURE)=\$\{CMAKE_SOURCE_DIR\}/(\S+)", block):
                t.samples.append(s)
            # driver-less registrations put the corpus straight on the COMMAND:
            #   add_test(NAME x COMMAND $<TARGET_FILE:ls> run ${CMAKE_SOURCE_DIR}/tests/samples/y.lls)
            for s in re.findall(r"\$\{CMAKE_SOURCE_DIR\}/(\S+\.(?:lls|ls|in))\b", block):
                if s not in t.samples:
                    t.samples.append(s)
            # Every -D the registration passes, so the driver's own path
            # templates (${CORPUS_DIR}/*.lls, ${SAMPLE_DIR}/${sample}/main.lls)
            # can be resolved against them.
            for dm in re.finditer(r"-D([A-Za-z_][A-Za-z0-9_]*)=([^\s\"]+)", block):
                t.dvars.setdefault(dm.group(1), dm.group(2))
            sd = re.search(r"-DSAMPLE_DIR=", block)
            if sd and not t.samples:
                t.samples.append("tests/samples/ (multiple, see driver)")
            ex = re.search(r'"?-DEXPECT=([^"\n]*)"?', block)
            if ex:
                t.expect = ex.group(1).strip().rstrip('"')
            if not t.driver:
                # A C unit test is one CMake builds a binary for. Four tests
                # (ext_lls_smoke, ext_ls_legacy, retired_take, retired_fromlist)
                # also have no -P driver but invoke lls directly, so "no driver"
                # alone misclassified them.
                t.kind = "unit" if t.name in unit_targets else "direct"
                t.direct_cmd = " ".join(
                    re.findall(r"COMMAND\s+(.*)", block)[:1]).strip()
            tests[t.name] = t
        i = k + 1

    # `set_tests_properties(x PROPERTIES WILL_FAIL TRUE)` is a reject oracle
    # expressed in ctest rather than in a driver -- invisible to driver parsing,
    # so these tests looked like ordinary runs when they must in fact FAIL.
    for m in re.finditer(
            r"set_tests_properties\(\s*([A-Za-z0-9_]+)\s+PROPERTIES[^)]*WILL_FAIL\s+TRUE", raw):
        if m.group(1) in tests:
            tests[m.group(1)].will_fail = True

    # C unit tests: the corpus IS the .c file CMake compiles for them
    for m in re.finditer(r"add_executable\(\s*([A-Za-z0-9_]+)\s+(tests/[A-Za-z0-9_]+\.c)", raw):
        if m.group(1) in tests:
            tests[m.group(1)].samples.append(m.group(2))

    # IR snapshots
    for m in re.finditer(r"ls_ir_snapshot\(NAME\s+([A-Za-z0-9_]+)\s+SAMPLE\s+\$\{CMAKE_SOURCE_DIR\}/(\S+?)\)", raw):
        t = Test("test_ir_snapshot_" + m.group(1))
        t.kind = "snapshot"
        t.driver = "ir_snapshot.cmake"
        t.samples = [m.group(2)]
        t.win32_only = True
        idx = raw[:m.start()].count("\n")
        t.reg_comment = collect_comment_above(lines, idx)
        tests[t.name] = t

    return tests, raw


# --------------------------------------------------------------------------
# Assembly
# --------------------------------------------------------------------------

def build():
    tests, raw = parse_tests_cmake()
    driver_cache = {}
    driver_users = {}

    for t in tests.values():
        if t.driver:
            driver_users.setdefault(t.driver, []).append(t.name)

    for t in tests.values():
        reg_tags, reg_prose = parse_annotations(t.reg_comment)
        drv_tags, drv_prose = {"subsystem": None, "guards": [], "sources": []}, []

        if t.driver:
            path = os.path.join(TESTS, t.driver)
            if path not in driver_cache:
                if os.path.exists(path):
                    text = read_text(path)
                    dl = text.split("\n")
                    hdr = []
                    for l in dl:
                        if l.startswith("#"):
                            hdr.append(strip_comment_prefix(l))
                        elif not hdr and not l.strip():
                            continue
                        else:
                            break
                    driver_cache[path] = (text, hdr)
                else:
                    driver_cache[path] = (None, [])
            text, hdr = driver_cache[path]
            if text is not None:
                info = infer_from_driver(text)
                t.oracles = info["oracles"]
                t.env = info["env"]
                t.value_check = info["value_check"]
                t.diag_check = info["diag_check"]
                t.reject = info["reject"]
                t.required, t.forbidden = extract_assertions(text)
                exclusive = len(driver_users.get(t.driver, [])) == 1
                dtags, dprose = parse_annotations(hdr)
                if exclusive:
                    drv_tags, drv_prose = dtags, dprose
                else:
                    # Shared harness: per-test TAGS on it would be wrong, but its
                    # header often carries a family narrative worth showing (the
                    # borrow-escape driver explains all 17 of its corpora), so
                    # keep the prose separately and label it as shared.
                    drv_tags = {"subsystem": dtags.get("subsystem"),
                                "guards": [], "sources": []}
                    t.shared_prose = dprose
                    t.shared_users = len(driver_users.get(t.driver, []))
        elif t.kind == "unit":
            t.oracles = ["C-UNIT"]
        else:
            t.oracles = ["DIRECT"]
        if t.will_fail and "REJECT" not in t.oracles:
            t.oracles.append("REJECT")

        t.tags = merge_tags(reg_tags, drv_tags)
        t.tags_from = ("registration" if any(reg_tags.values()) else
                       ("driver" if any(drv_tags.values()) else None))
        # The registration comment and an exclusive driver header are usually
        # COMPLEMENTARY, not redundant (test_array_owned_elem states the defect
        # class at the registration and enumerates the six sites in the driver),
        # so keep both rather than letting one shadow the other.
        t.prose = reg_prose
        t.driver_prose = drv_prose if drv_prose and drv_prose != reg_prose else []
        if not t.prose:
            t.prose, t.driver_prose = t.driver_prose, []

        # Corpus the driver names itself, merged with whatever the registration
        # passed. Both sources are real: a registration can pass one sample while
        # the driver additionally runs a negative twin.
        if t.driver:
            path = os.path.join(TESTS, t.driver)
            text = driver_cache.get(path, (None, []))[0]
            if text:
                t.generated_corpus = bool(re.search(
                    r'file\(\s*WRITE\s+"[^"]*\.(?:lls|ls)"', text))
                found = driver_samples(text, t.dvars)
                if found:
                    t.samples = [s for s in t.samples
                                 if not s.endswith("(multiple, see driver)")]
                    for s in found:
                        if s not in t.samples:
                            t.samples.append(s)

        # bug ids mentioned anywhere in the narrative but not declared
        blob = "\n".join(t.reg_comment + t.prose)
        found = set(BUGID_RE.findall(blob))
        declared = set(t.tags.get("guards") or [])
        t.implicit_bugids = sorted(found - declared)

    return tests, driver_users


# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------

def esc(s):
    return _html.escape(s or "")


ORACLE_TITLE = {
    "JIT": "lls run (LLJIT)",
    "AOT": "lls compile + run the native exe",
    "MEMCHECK": "lls run --memcheck, requires 0 leak / 0 double-free / 0 invalid free",
    "CHECK": "lls check (type check only)",
    "REJECT": "must FAIL to compile, with the diagnostic text pinned",
    "IR": "emit-ir compared byte-for-byte against a golden file",
    "C-UNIT": "C unit test linked against compiler TUs",
    "DIRECT": "add_test runs lls directly, no driver script",
    "FMT": "lls fmt",
    "REPL": "piped REPL session",
}


def render(tests, driver_users, ctest_names, scope_note):
    BLOBS.clear()
    order = sorted(tests.values(), key=lambda t: t.name)
    by_sub = {}
    for t in order:
        by_sub.setdefault(t.tags.get("subsystem") or "(未标注)", []).append(t)

    annotated = [t for t in order if t.tags.get("subsystem")]
    guarded = [t for t in order if t.tags.get("guards")]

    P = []
    A = P.append
    # Standalone file opened straight from disk: without the charset meta the
    # Chinese body renders as mojibake in every browser.
    A('<!doctype html>')
    A('<html lang="zh-CN"><head><meta charset="utf-8">')
    A('<meta name="viewport" content="width=device-width,initial-scale=1">')
    A("<title>LS 测试套件说明</title>")
    A(STYLE)
    A("</head><body>")
    A("<h1>LS 测试套件说明</h1>")
    A('<p class="sub">由 <code>tests/gen_test_docs.py</code> 从测试套件自身生成 · %s · %s</p>'
      % (datetime.date.today().isoformat(), esc(scope_note)))

    # ---- how to run
    A('<section id="run"><h2>0. 怎么跑</h2>')
    A("""<pre><code>cmake --build build --config Release
cd build &amp;&amp; ctest -j 5 --output-on-failure -C Release</code></pre>
<p><code>-j 5</code> 是并行度（留一核）；全量约 2 分钟。测试之间不共享输出文件，
新增会写盘的测试时把文件名从测试名派生，别用固定字面量。</p>
<p>单跑一个：<code>ctest -R test_array_owned_elem --output-on-failure -C Release</code>。
偶发单个红、单独重跑即过的，是已知的 Defender 锁盘 flake，
<code>--repeat until-pass:2</code> 可兜。</p>""")

    # ---- overview
    A('<section id="overview"><h2>1. 总览</h2>')
    kinds = {}
    for t in order:
        kinds[t.kind] = kinds.get(t.kind, 0) + 1
    A("<table><thead><tr><th>形态</th><th>数量</th><th>说明</th></tr></thead><tbody>")
    A("<tr><td>C 单元测试</td><td>%d</td><td>直接链接编译器 TU，不经过 <code>lls.exe</code></td></tr>" % kinds.get("unit", 0))
    A("<tr><td>IR 快照</td><td>%d</td><td><code>emit-ir</code> 对 golden 逐字节，<code>if(WIN32)</code> 门控</td></tr>" % kinds.get("snapshot", 0))
    A("<tr><td>直接调用</td><td>%d</td><td><code>add_test</code> 直接跑 <code>lls</code>，无驱动脚本</td></tr>" % kinds.get("direct", 0))
    A("<tr><td>端到端驱动</td><td>%d</td><td><code>cmake -P tests/test_*.cmake</code></td></tr>" % kinds.get("e2e", 0))
    A("<tr><td><b>合计</b></td><td><b>%d</b></td><td></td></tr>" % len(order))
    A("</tbody></table>")

    # oracle stats
    ostat = {}
    for t in order:
        for o in t.oracles:
            ostat[o] = ostat.get(o, 0) + 1
    A("<h3>判据分布</h3><table><thead><tr><th>判据</th><th>测试数</th><th>含义</th></tr></thead><tbody>")
    for o, n in sorted(ostat.items(), key=lambda kv: -kv[1]):
        A("<tr><td><span class='ora o-%s'>%s</span></td><td>%d</td><td>%s</td></tr>"
          % (o.lower().replace("-", ""), o, n, esc(ORACLE_TITLE.get(o, ""))))
    A("</tbody></table>")
    vc = sum(1 for t in order if t.value_check)
    triple = sum(1 for t in order if {"JIT", "AOT", "MEMCHECK"} <= set(t.oracles))
    A("""<p class="note"><b>判据强度</b>：%d 个测试做<b>值校验</b>（断言输出内容，而不只是退出码），
%d 个做 JIT+AOT+memcheck 三重验证。本项目最贵的 bug 是 <code>rc=0</code> 的静默错值，
只查退出码的测试对这一类完全无感——所以「是否值校验」这一栏是推导出来的，不是人填的。</p>""" % (vc, triple))

    # ---- per-subsystem
    A('<section id="tests"><h2>2. 按子系统分组的测试清单</h2>')
    for sub in sorted(by_sub, key=lambda s: (s == "(未标注)", s)):
        group = by_sub[sub]
        A('<h3 id="sub-%s">%s <span class="count">%d</span></h3>' % (slug(sub), esc(sub), len(group)))
        A("<table class='tlist'><thead><tr><th>测试</th><th>判据</th><th>守的 bug</th><th>相关源</th></tr></thead><tbody>")
        for t in group:
            A("<tr><td><a href='#%s'><code>%s</code></a></td><td>%s</td><td>%s</td><td>%s</td></tr>"
              % (t.name, esc(t.name), oracle_badges(t), guards_html(t), sources_html(t)))
        A("</tbody></table>")
        for t in group:
            A(render_entry(t, driver_users))

    # ---- reverse index
    A('<section id="index"><h2>3. 反向索引</h2>')
    A("<h3>bug 编号 → 测试</h3>")
    # A guard entry is usually a sentence that CONTAINS the id ("L-024 generic
    # trait-impl signatures were never validated"), not the bare id, so scan.
    bymap = {}
    for t in order:
        for g in (t.tags.get("guards") or []):
            for bid in set(BUGID_RE.findall(g)):
                if t.name not in bymap.setdefault(bid, []):
                    bymap[bid].append(t.name)
    if bymap:
        A("<table><thead><tr><th>编号</th><th>守它的测试</th></tr></thead><tbody>")
        for k in sorted(bymap):
            A("<tr><td><code>%s</code></td><td>%s</td></tr>"
              % (k, " ".join("<a href='#%s'><code>%s</code></a>" % (n, n) for n in sorted(bymap[k]))))
        A("</tbody></table>")
    else:
        A("<p class='gap'>尚无标注。</p>")

    A("<h3>编译器源文件 → 测试</h3>")
    A("<p class='note'>改这些文件之前，先跑这一行里的测试。锚点写函数名不写行号——拆 TU 会让行号腐烂。</p>")
    srcmap = {}
    for t in order:
        for s in (t.tags.get("sources") or []):
            f = s.split(":")[0]
            srcmap.setdefault(f, set()).add(t.name)
    if srcmap:
        A("<table><thead><tr><th>源文件</th><th>测试</th></tr></thead><tbody>")
        for k in sorted(srcmap):
            A("<tr><td><code>%s</code></td><td>%s</td></tr>"
              % (esc(k), " ".join("<a href='#%s'><code>%s</code></a>" % (n, n) for n in sorted(srcmap[k]))))
        A("</tbody></table>")
    else:
        A("<p class='gap'>尚无标注。</p>")

    # ---- self-audit
    A('<section id="gaps"><h2>4. 缺口登记（生成器自审）</h2>')
    A("""<p class="note">这一节是文档的防腐机制：新增测试不写标注，就会在这里露出来。
它由生成器统计，不是人维护的清单。</p>""")
    A("<table><thead><tr><th>项</th><th>数量</th></tr></thead><tbody>")
    A("<tr><td>已标注 <code>@subsystem</code></td><td>%d / %d</td></tr>" % (len(annotated), len(order)))
    A("<tr><td>已标注 <code>@guards</code></td><td>%d / %d</td></tr>" % (len(guarded), len(order)))
    A("<tr><td>无任何说明文字</td><td>%d</td></tr>" % sum(1 for t in order if not t.prose))
    A("<tr><td>只查退出码、不做值校验</td><td>%d</td></tr>" % sum(1 for t in order if not t.value_check))
    unresolved = [t for t in order if t.kind == "e2e" and not t.generated_corpus
                  and not [s for s in t.samples if not s.endswith("(multiple, see driver)")]]
    A("<tr><td>解析不出语料的端到端测试</td><td>%d</td></tr>" % len(unresolved))
    A("</tbody></table>")
    if unresolved:
        A("<p class='gap'>语料未解析：%s —— 驱动用了生成器不认识的路径拼法，"
          "需要扩 <code>driver_samples()</code>。</p>"
          % " ".join("<code>%s</code>" % t.name for t in unresolved))

    missing = [t for t in order if not t.tags.get("subsystem")]
    if missing:
        A("<details><summary>缺 <code>@subsystem</code> 的 %d 个</summary><p class='mono'>%s</p></details>"
          % (len(missing), " ".join("<code>%s</code>" % t.name for t in missing)))
    novc = [t for t in order if not t.value_check and t.kind == "e2e"]
    if novc:
        A("<details><summary>端到端但不做值校验的 %d 个（静默错值风险区）</summary><p class='mono'>%s</p></details>"
          % (len(novc), " ".join("<code>%s</code>" % t.name for t in novc)))
    implicit = [t for t in order if t.implicit_bugids]
    if implicit:
        A("<details><summary>散文里提到 bug 编号但没写进 <code>@guards</code> 的 %d 个</summary><ul>%s</ul></details>"
          % (len(implicit), "".join("<li><code>%s</code> — %s</li>" % (t.name, ", ".join(t.implicit_bugids))
                                    for t in implicit)))

    # orphan drivers
    referenced = set(driver_users)
    on_disk = set(f for f in os.listdir(TESTS)
                  if f.startswith("test_") and f.endswith(".cmake"))
    orphans = sorted(on_disk - referenced)
    if orphans:
        A("<p class='gap'><b>孤儿驱动</b>（文件存在但没有任何 <code>add_test</code> 引用，"
          "即从不执行）：%s</p>" % " ".join("<code>%s</code>" % o for o in orphans))

    if ctest_names is not None:
        parsed = set(tests)
        only_ctest = sorted(ctest_names - parsed)
        only_parsed = sorted(parsed - ctest_names)
        if only_ctest or only_parsed:
            A("<p class='gap'><b>与 <code>ctest -N</code> 不一致</b>："
              "仅 ctest 有 %s；仅解析出 %s。生成器漏掉了某种注册形态。</p>"
              % (" ".join("<code>%s</code>" % n for n in only_ctest) or "无",
                 " ".join("<code>%s</code>" % n for n in only_parsed) or "无"))
        else:
            A("<p class='ok'>与 <code>ctest -N</code> 完全一致（%d 个）。</p>" % len(parsed))

    A("</section>")
    A(render_blob_payload())
    A("</body></html>")
    return "\n".join(P)


def render_blob_payload():
    """One copy of every embedded file/function + the loader that injects them."""
    import json
    payload = json.dumps(BLOBS, ensure_ascii=False)
    # a literal </script> anywhere inside a snippet would end the tag early
    payload = payload.replace("<", "\\u003c").replace(">", "\\u003e")
    return ("""<script id="ls-blobs" type="application/json">%s</script>
<script>
(function(){
  var data = JSON.parse(document.getElementById('ls-blobs').textContent);
  function build(key){
    var b = data[key];
    if (!b) return '<p class="gap">missing blob: ' + key + '</p>';
    var rows = '';
    for (var i = 0; i < b.lines.length; i++) {
      var t = b.lines[i]
        .replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
      rows += '<tr><td class="ln">' + (b.start + i) + '</td><td class="cl">'
            + (t || '&nbsp;') + '</td></tr>';
    }
    return '<div class="codebox"><table class="code">' + rows + '</table></div>';
  }
  function fill(d){
    if (d.dataset.filled) return;
    d.dataset.filled = '1';
    d.querySelector('.slot').innerHTML = build(d.dataset.blob);
  }
  document.querySelectorAll('details.lazy').forEach(function(d){
    if (d.open) fill(d);
    d.addEventListener('toggle', function(){ if (d.open) fill(d); });
  });
})();
</script>""" % payload)


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def oracle_badges(t):
    out = []
    for o in t.oracles:
        out.append("<span class='ora o-%s' title='%s'>%s</span>"
                   % (o.lower().replace("-", ""), esc(ORACLE_TITLE.get(o, "")), o))
    if t.value_check:
        out.append("<span class='ora o-value' title='断言 stdout 内容，不只是退出码'>VALUE</span>")
    if t.diag_check:
        out.append("<span class='ora o-diag' title='钉住 stderr 上的诊断文本'>DIAG</span>")
    return "".join(out)


def guards_html(t):
    g = t.tags.get("guards") or []
    if not g:
        return "<span class='gap'>—</span>"
    return " ".join("<code>%s</code>" % esc(x) for x in g)


def sources_html(t):
    s = t.tags.get("sources") or []
    if not s:
        return "<span class='gap'>—</span>"
    return "<br>".join("<code>%s</code>" % esc(x) for x in s)


def render_entry(t, driver_users):
    P = []
    A = P.append
    A("<div class='entry' id='%s'>" % t.name)
    A("<h4><code>%s</code>%s</h4>" % (esc(t.name),
      " <span class='tag'>Windows only</span>" if t.win32_only else ""))
    A("<div class='meta'>%s</div>" % oracle_badges(t))
    A("<dl>")
    if t.driver:
        shared = len(driver_users.get(t.driver, []))
        extra = " <span class='tag'>共用驱动 ×%d</span>" % shared if shared > 1 else ""
        A("<dt>驱动</dt><dd><code>tests/%s</code>%s</dd>" % (esc(t.driver), extra))
    if t.samples:
        A("<dt>语料</dt><dd>%s</dd>" % "<br>".join("<code>%s</code>" % esc(s) for s in t.samples))
    if t.expect:
        A("<dt>期望输出</dt><dd><code>%s</code></dd>" % esc(t.expect))
    if t.env:
        A("<dt>环境开关</dt><dd>%s</dd>" % " ".join("<code>%s</code>" % e for e in t.env))
    A("<dt>守的 bug</dt><dd>%s</dd>" % guards_html(t))
    A("<dt>相关源</dt><dd>%s</dd>" % sources_html(t))
    A("</dl>")
    if t.prose:
        A("<div class='prose'>%s</div>" % prose_html(t.prose))
    if t.driver_prose:
        A("<details class='prose'><summary>驱动头部详述</summary>%s</details>"
          % prose_html(t.driver_prose))
    if t.shared_prose:
        A("<details class='prose'><summary>共用驱动说明（%s，%d 个测试共用——"
          "描述的是这一族与判据，不是本测试独有）</summary>%s</details>"
          % (esc(t.driver), t.shared_users, prose_html(t.shared_prose)))
    if not t.prose and not t.driver_prose and not t.shared_prose:
        A("<p class='gap'>无说明——需要补 <code>@guards</code> 与散文。</p>")
    A(render_assertions(t))
    A(render_sources_pane(t))
    A("</div>")
    return "\n".join(P)


# Every embedded snippet is stored ONCE here and injected on demand. Without
# this, codegen_own.c:cg_store_owned alone is embedded 13 times (once per test
# that guards it) and the page passes 10 MB.
BLOBS = {}


def render_assertions(t):
    """The concrete pass conditions, read out of the driver."""
    if not t.required and not t.forbidden:
        return ""
    P = ["<div class='asserts'><div class='ptitle'>断言（自驱动抽取）</div>"]
    if t.required:
        P.append("<div class='arow'><span class='alab ok'>必须出现</span><span>%s</span></div>"
                 % " ".join("<code>%s</code>" % esc(x) for x in t.required))
    if t.forbidden:
        P.append("<div class='arow'><span class='alab bad'>不得出现</span><span>%s</span></div>"
                 % " ".join("<code>%s</code>" % esc(x) for x in t.forbidden))
    P.append("</div>")
    return "".join(P)


def blob_key(kind, ident):
    return "%s:%s" % (kind, ident)


def add_blob(key, start, lines):
    if key not in BLOBS:
        BLOBS[key] = {"start": start, "lines": lines}
    return key


def lazy_box(key, head, open_default=False):
    return ("<details class='lazy'%s data-blob=\"%s\"><summary>%s</summary>"
            "<div class='slot'></div></details>"
            % (" open" if open_default else "", esc(key), head))


def render_sources_pane(t):
    """Two columns: the corpus this test runs, and the compiler code it guards."""
    left, right = [], []

    if not [s for s in t.samples if not s.endswith("(multiple, see driver)")] \
            and t.driver and t.generated_corpus:
        left.append("<p class='note'>语料由驱动在运行时用 <code>file(WRITE)</code> "
                    "现场生成，磁盘上没有可展示的文件。</p>")

    for s in t.samples:
        if s.endswith("(multiple, see driver)"):
            continue
        lines = load_sample(s)
        if lines is None:
            left.append("<p class='gap'>找不到语料 <code>%s</code></p>" % esc(s))
            continue
        hdr = sample_header_prose(lines)
        if hdr:
            left.append("<div class='sprose'>%s</div>" % prose_html(hdr))
        key = add_blob(blob_key("sample", s), 1, lines)
        head = ("<span class='fname'>%s</span> <span class='count'>%d 行</span>"
                % (esc(s), len(lines)))
        left.append(lazy_box(key, head, open_default=len(lines) <= 70))

    for spec in (t.tags.get("sources") or []):
        if ":" not in spec:
            # No function named: the whole file is the thing under test. That is
            # the normal case for the pure-LS stdlib (lib/std/core/vec.lls), so
            # embed it rather than printing a bare path.
            whole = load_sample(spec if "/" in spec else "src/" + spec)
            if whole:
                p = spec if "/" in spec else "src/" + spec
                key = add_blob(blob_key("file", p), 1, whole)
                right.append(lazy_box(key,
                    "<span class='fname'>%s</span> <span class='count'>整文件 · %d 行</span>"
                    % (esc(p), len(whole))))
            else:
                right.append("<div class='fhead'><span class='fname'>%s</span> "
                             "<span class='count'>找不到文件</span></div>" % esc(spec))
            continue
        f, fn = spec.split(":", 1)
        relpath = f if "/" in f else ("runtime/" + f if f.startswith("memcheck") else "src/" + f)
        key = blob_key("fn", spec)
        if key not in BLOBS:
            got = extract_c_function(relpath, fn)
            if not got:
                right.append("<p class='gap'>抽不出 <code>%s</code>——函数可能已改名或迁移，"
                             "标注需要更新。</p>" % esc(spec))
                continue
            a, b, body, trunc = got
            add_blob(key, a, body)
            BLOBS[key]["head"] = "%s L%d-%d · %d 行" % (relpath, a, b, b - a + 1)
        head = ("<span class='fname'>%s</span> <span class='count'>%s</span>"
                % (esc(fn), esc(BLOBS[key].get("head", ""))))
        right.append(lazy_box(key, head))

    if not left and not right:
        return ""
    return ("<div class='panes'>"
            "<div class='pane'><div class='ptitle'>测试语料</div>%s</div>"
            "<div class='pane'><div class='ptitle'>它守着的编译器源码</div>%s</div>"
            "</div>"
            % ("".join(left) or "<p class='gap'>—</p>",
               "".join(right) or "<p class='gap'>未标注 <code>@sources</code></p>"))


def prose_html(lines):
    out = []
    para = []
    for ln in lines:
        # cmake section rules like `---- L-023: ... ----` are headings, not prose
        rule = re.match(r"^-{2,}\s*(.+?)\s*-{2,}$", ln.strip())
        if rule:
            if para:
                out.append("<p>%s</p>" % esc(" ".join(para)))
                para = []
            out.append("<p class='lead'><b>%s</b></p>" % esc(rule.group(1)))
            continue
        if not ln.strip():
            if para:
                out.append("<p>%s</p>" % esc(" ".join(para)))
                para = []
        elif re.match(r"^\s{2,}\S", ln) or ln.lstrip().startswith(("①", "②", "③", "④", "⑤", "⑥", "1.", "2.", "|")):
            if para:
                out.append("<p>%s</p>" % esc(" ".join(para)))
                para = []
            out.append("<pre class='snip'>%s</pre>" % esc(ln))
        else:
            para.append(ln.strip())
    if para:
        out.append("<p>%s</p>" % esc(" ".join(para)))
    # merge adjacent snips
    merged = []
    for chunk in out:
        if chunk.startswith("<pre") and merged and merged[-1].startswith("<pre"):
            merged[-1] = merged[-1][:-6] + "\n" + chunk[len("<pre class='snip'>"):]
        else:
            merged.append(chunk)
    return "\n".join(merged)


STYLE = """<style>
:root{--fg:#1c1c1e;--bg:#fff;--mut:#6b6b70;--line:#e3e3e6;--acc:#2f6f4f;--warn:#a8620a;--card:#fafafa}
@media (prefers-color-scheme:dark){:root{--fg:#e6e6e8;--bg:#131315;--mut:#9a9aa0;--line:#2c2c30;--acc:#7fc4a0;--warn:#e0a45c;--card:#1a1a1d}}
:root[data-theme=dark]{--fg:#e6e6e8;--bg:#131315;--mut:#9a9aa0;--line:#2c2c30;--acc:#7fc4a0;--warn:#e0a45c;--card:#1a1a1d}
:root[data-theme=light]{--fg:#1c1c1e;--bg:#fff;--mut:#6b6b70;--line:#e3e3e6;--acc:#2f6f4f;--warn:#a8620a;--card:#fafafa}
body{max-width:92rem;margin:0 auto;padding:2rem 1.25rem 6rem;background:var(--bg);color:var(--fg);
 font:15px/1.65 -apple-system,"Segoe UI","Microsoft YaHei",system-ui,sans-serif}
h1{font-size:1.75rem;margin:0 0 .25rem}
h2{font-size:1.3rem;margin:2.75rem 0 .75rem;padding-bottom:.3rem;border-bottom:2px solid var(--line)}
h3{font-size:1.05rem;margin:2rem 0 .5rem}
h4{font-size:.95rem;margin:0 0 .4rem;font-weight:600}
.sub{color:var(--mut);margin:0 0 1.5rem;font-size:.85rem}
.note,section>p{max-width:62rem}
code{font-family:"Cascadia Mono",Consolas,monospace;font-size:.87em;
 background:color-mix(in srgb,var(--line) 55%,transparent);padding:.08em .35em;border-radius:3px}
pre{background:var(--card);border:1px solid var(--line);border-radius:6px;padding:.7rem .9rem;
 overflow-x:auto;font-size:.85rem}
pre code{background:none;padding:0}
table{border-collapse:collapse;width:100%;margin:.75rem 0;font-size:.88rem;display:block;overflow-x:auto}
th,td{border:1px solid var(--line);padding:.35rem .6rem;text-align:left;vertical-align:top}
th{background:var(--card);font-weight:600}
.tlist td:first-child{white-space:nowrap}
.entry{border:1px solid var(--line);border-left:3px solid var(--acc);border-radius:5px;
 padding:.8rem 1rem;margin:1rem 0;background:var(--card)}
.entry dl{display:grid;grid-template-columns:max-content 1fr;gap:.15rem .9rem;margin:.5rem 0;font-size:.85rem}
.entry dt{color:var(--mut);white-space:nowrap}
.entry dd{margin:0}
.prose p{margin:.5rem 0;font-size:.9rem;max-width:62rem}
.prose .lead{margin-top:.8rem}
.prose .snip{font-size:.8rem;margin:.4rem 0}
.ora{display:inline-block;font:600 10.5px/1.5 "Cascadia Mono",Consolas,monospace;
 padding:.05rem .38rem;border-radius:3px;margin-right:.25rem;border:1px solid var(--line);color:var(--mut)}
.o-jit{color:#2f6f9f;border-color:#2f6f9f66}
.o-aot{color:#6f4fa8;border-color:#6f4fa866}
.o-memcheck{color:var(--acc);border-color:currentColor}
.o-reject{color:var(--warn);border-color:currentColor}
.o-value{color:#b03a4a;border-color:currentColor}
.o-ir{color:#0f8b8d;border-color:currentColor}
.count{color:var(--mut);font-weight:400;font-size:.8rem}
.tag{display:inline-block;font-size:.7rem;color:var(--mut);border:1px solid var(--line);
 border-radius:3px;padding:.05rem .3rem}
.panes{display:grid;grid-template-columns:1fr 1fr;gap:.9rem;margin-top:.9rem}
@media (max-width:64rem){.panes{grid-template-columns:1fr}}
.pane{min-width:0}
.ptitle{font-size:.75rem;font-weight:600;color:var(--mut);text-transform:uppercase;
 letter-spacing:.05em;margin-bottom:.35rem}
.fhead{font-size:.75rem;color:var(--mut);margin:.4rem 0 .2rem}
.sprose{font-size:.84rem;margin:.2rem 0 .5rem;padding-left:.7rem;border-left:2px solid var(--line)}
.sprose p{margin:.35rem 0}
.sprose .snip{font-size:.76rem}
.fname{font-family:"Cascadia Mono",Consolas,monospace;color:var(--fg)}
.codebox{border:1px solid var(--line);border-radius:5px;background:var(--bg);
 max-height:26rem;overflow:auto;margin:.2rem 0 .6rem}
table.code{border-collapse:collapse;width:auto;min-width:100%;margin:0;display:table;font-size:.76rem}
table.code td{border:none;padding:0 .5rem;white-space:pre;
 font-family:"Cascadia Mono",Consolas,monospace;line-height:1.5}
table.code td.ln{text-align:right;color:var(--mut);opacity:.6;user-select:none;
 position:sticky;left:0;background:var(--bg);border-right:1px solid var(--line)}
.pane details>summary{font-size:.75rem;color:var(--mut);margin:.4rem 0 .2rem}
.asserts{margin:.8rem 0 .2rem;padding:.5rem .7rem;border:1px dashed var(--line);border-radius:5px}
.arow{display:flex;gap:.6rem;align-items:baseline;margin:.2rem 0;font-size:.82rem;flex-wrap:wrap}
.alab{flex:0 0 auto;font-size:.72rem;font-weight:600;padding:.05rem .35rem;border-radius:3px;border:1px solid currentColor}
.alab.ok{color:var(--acc)}
.alab.bad{color:#b03a4a}
.gap{color:var(--warn)}
.ok{color:var(--acc)}
.note{color:var(--mut);font-size:.88rem}
.mono{font-size:.8rem;line-height:2}
details{margin:.6rem 0}
summary{cursor:pointer;color:var(--mut);font-size:.88rem}
section{margin-bottom:1rem}
a{color:var(--acc)}
</style>"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "docs", "tests.html"))
    ap.add_argument("--ctest-list", default=None,
                    help="output of `ctest -N`, used to cross-check the parse")
    ap.add_argument("--scope", default="",
                    help="note rendered under the title (e.g. pilot scope)")
    args = ap.parse_args()

    tests, driver_users = build()

    ctest_names = None
    if args.ctest_list and os.path.exists(args.ctest_list):
        blob = read_text(args.ctest_list)
        ctest_names = set(re.findall(r"Test\s+#\d+:\s+([A-Za-z0-9_]+)", blob))

    doc = render(tests, driver_users, ctest_names, args.scope)
    outdir = os.path.dirname(args.out)
    if outdir and not os.path.isdir(outdir):
        os.makedirs(outdir)
    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write(doc)
    print("wrote %s (%d tests)" % (args.out, len(tests)))
    ann = sum(1 for t in tests.values() if t.tags.get("subsystem"))
    print("  annotated @subsystem: %d/%d" % (ann, len(tests)))
    print("  annotated @guards:    %d/%d"
          % (sum(1 for t in tests.values() if t.tags.get("guards")), len(tests)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
