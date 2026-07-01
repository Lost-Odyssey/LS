# LS (LLVM Script) — VS Code extension

Syntax highlighting, live diagnostics, hover, go-to-definition, and
completion for `.ls` files (the LS / LLVM Script language). All 5 phases of
the editor-support design plan (`docs/plan_editor_lsp.md` in the repo root):
TextMate grammar + editing config + snippets + a language server backed by
the real compiler.

## Settings

- `llvmscript.compilerPath` (string, default `"ls"`) — path to the `ls`
  compiler binary used for diagnostics (`ls check`), hover/go-to-def
  (`ls symbol`), and completion (`ls complete`). Defaults to resolving
  `ls`/`ls.exe` on `PATH`; set this explicitly (e.g. to
  `C:\YANG\10003_language\LS\build\Release\ls.exe`) if the compiler isn't
  on `PATH` — which it usually isn't unless you put it there yourself, the
  build doesn't install it system-wide.

## Why `llvmscript` and not `ls`

The `.ls` extension is already claimed by LiveScript's VS Code extension,
which registers the language id `ls`. Reusing that id would collide with
LiveScript's grammar registration. This extension uses `llvmscript` as the
internal language id instead — the file extension stays `.ls`.

## Installation

There's no marketplace listing yet — install from a locally built `.vsix`.
The `.vsix` itself isn't committed to the repo (see `.gitignore`), so every
machine either builds its own or gets a copy of one someone else built.

### Prerequisites

- VS Code with the `code` CLI on your `PATH`. If `code --version` fails in
  your terminal: open VS Code, press `Ctrl+Shift+P`, run **Shell Command:
  Install 'code' command in PATH**, then restart the terminal.
- Node.js + npm — only needed if you're *building* the `.vsix` on this
  machine (method A). Not needed if you're just installing a `.vsix` someone
  already built (method B).

### Method A — build from source (this repo has the source of truth)

On the target machine, with this repo cloned/pulled:

```
cd editors/vscode-ls
npm install
npx -y @vscode/vsce package --skip-license -o llvmscript.vsix
code --install-extension llvmscript.vsix
```

`npm install` pulls in `vscode-languageclient`/`vscode-languageserver` (the
language server needs them at runtime; `vsce package` bundles them into the
`.vsix`, so the *target* machine doesn't need Node — only the *build*
machine does). Re-run all three commands any time anything under
`client/`, `server/`, `syntaxes/`, or `snippets/` changes — re-running
`npm install` is a no-op if dependencies haven't changed, and
`--install-extension` overwrites the previous version in place, no need to
uninstall first.

### Method B — install a prebuilt `.vsix` (no Node required)

If you already have `llvmscript.vsix` (copied via USB/network share/cloud,
or built on another machine with method A), skip straight to installing it:

```
code --install-extension /path/to/llvmscript.vsix
```

or, from inside VS Code: Extensions panel → `...` menu (top right) →
**Install from VSIX...** → pick the file.

### Verify it worked

Open any `.ls` file (e.g. `lib/std/core/vec.ls`). The language indicator in
VS Code's bottom-right status bar should read **LS (LLVM Script)**, and the
code should be colorized. If it still says "Plain Text", click that status
bar item and pick "LS (LLVM Script)" from the list manually — the extension
is installed, VS Code just didn't auto-detect it for that file.

To check diagnostics: set `llvmscript.compilerPath` (see Settings above) if
`ls` isn't on your `PATH`, then open a `.ls` file with a deliberate type
error — a red squiggle should appear on save (and on open). If nothing
shows up and you don't see a "couldn't run `ls check`" warning notification
either, check the Output panel → "LS (LLVM Script) Language Server" channel.

To check hover/go-to-definition: hover over a `struct`/`enum`/function/method
name (e.g. `push` in `v.push(x)` on a `Vec(T)`) — a tooltip with its
signature and doc comment should appear. Ctrl-click (or F12, or right-click
→ Go to Definition) jumps to the declaration.

To check completion: type `.` after a variable, or press Ctrl+Space anywhere
— you should see keywords plus every struct/enum/function/method name known
from the current file and its imports (see "How completion works" below for
why that list isn't filtered to "things that actually apply here").

## Status

- [x] Phase 0 — grammar + scaffold + basic bracket/comment config
- [x] Phase 1 — grammar coverage polish (f-string edge cases, numeric literal
      accuracy) + snippets (`def`/`struct`/`methods`/`match`/... — type a
      prefix and hit Tab)
- [x] Phase 2 — LSP diagnostics (spawns `ls check <file>` on open/save,
      republishes its stderr as inline errors — see "How diagnostics work"
      below)
- [x] Phase 3 — LSP hover / go-to-definition (spawns `ls symbol <file> <line>
      <col>`, a small new CLI command — see "How hover/go-to-def work" below)
- [x] Phase 4 — LSP completion (spawns `ls complete <file>` on open/save,
      cached — see "How completion works" below)

## How diagnostics work

No in-process type checking, and zero changes to the LS compiler itself —
same "consume what already exists" approach as `ls fmt` (which reuses the
scanner's token stream rather than re-implementing tokenization). The
language server (`server/server.js`) shells out to `ls check <file>` —
a CLI subcommand that already existed — on `textDocument/didOpen` and
`textDocument/didSave`, and regex-parses its stderr (`[type error]` /
`[move error]` / `[error]` in the form `category] path:line:col: message`,
the exact convention CLAUDE.md §5.3 documents) into LSP diagnostics
(`server/parseDiagnostics.js`).

Consequence: diagnostics reflect what's on disk, not unsaved keystrokes —
they refresh on open and on save, not on every edit. `ls check` is a full
parse + typecheck of the file, the same cost shape as a real compile, so
checking on every keystroke would mean spawning a process per keystroke;
checking on save is the same tradeoff most compile-based language servers
(without an incremental/in-process checker) make. If that cadence ever
feels too slow in practice, the next step would be writing the live buffer
to a temp file and checking that on a debounced timer instead — deliberately
not built yet since it adds real complexity (temp-file lifecycle, ensuring
LS's module-path import resolution still works from a temp location) for a
need nobody's hit yet.

## How hover/go-to-def work

**Not real semantic symbol resolution** — the type checker's symbol table is
a stack-local variable inside `checker_check()` in the compiler and is gone
by the time it returns (see docs/plan_editor_lsp.md §9 for why that route
was ruled out). Instead `ls symbol <file> <line> <col>` (`cmd_symbol` in
`src/main.c`) is a **name-based lookup**: it grabs the identifier text under
the cursor, then scans the current file's top-level declarations and
`methods X { }` bodies for a matching name, plus one level into each
`import`ed file (resolved via the same `module_resolve_import_path()` the
real compiler's import handling uses). It reuses the exact signature/doc
extraction `ls doc` already had (`extract_sig`/`doc_block_top`/
`extract_comment_range`) rather than re-deriving that from scratch.

Consequences worth knowing:
- **Local variables don't resolve** — only `struct`/`enum`/`interface`/
  `def`/methods declarations are indexed (not top-level themselves, so a
  local `int x` inside a function body won't hover/jump to anything). This
  covers the more common case (jump to a type or function definition) at a
  fraction of the engineering cost of a real position-aware AST/type query.
- **A name defined in more than one imported file returns multiple
  candidates** — no scope/shadowing resolution happens. Go-to-definition
  hands the editor a `Location[]`, which VS Code natively renders as a
  picker (exactly the right way to be honest about the ambiguity instead of
  silently guessing). Hover can only show one thing, so it shows the first
  candidate and notes how many others exist.
- Import expansion is **one level deep** — an import inside an imported
  file isn't followed. Good enough for "jump to a stdlib type/method used
  directly in this file"; won't find something three imports away.

## How completion works

`ls complete <file>` (`cmd_complete`) is `ls symbol` with the name filter
removed — every non-internal (`_`-prefixed names are skipped, same
convention `ls doc` already uses) top-level declaration and method in the
file + one level of imports, dumped unfiltered. **Deliberately not
receiver-type-aware**: typing `v.` on a `Vec(T)` suggests `push` (correct)
but also every method from anything else imported into the file, `Map`
methods included if `std.core.map` happens to be imported too. The LSP
client's own fuzzy/prefix filtering narrows it down as you keep typing —
same mechanism it already applies to the static keyword list and Phase 1's
snippets, so this isn't a new kind of imprecision, just a wider list feeding
into it. Real member completion (only what actually applies to a given
receiver) needs receiver-type inference, which needs the symbol table Phase
3 already established doesn't survive past `checker_check()` — out of scope
here for the same reason it was out of scope for go-to-definition.

Performance-motivated design choice, not a correctness one: `ls complete`
does a full parse of the file + its imports, same cost as `ls check`/
`ls symbol`. Completion fires far more often than hover or save (every `.`,
every manual invocation), so `server.js` doesn't spawn a process per
completion request — it refreshes a per-document cache on
`textDocument/didOpen` / `didSave` (piggybacking on the exact same trigger
points diagnostics already use) and `onCompletion` just reads from that
cache. Consequence: a struct/function you just typed and haven't saved yet
won't appear in completion until you save — the same staleness diagnostics
already have, not a new tradeoff introduced here.
