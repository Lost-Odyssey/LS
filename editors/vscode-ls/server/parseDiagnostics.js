// Parses `ls check <file>` stderr output into LSP Diagnostic objects.
//
// src/checker.c and src/parser.c report one error per line in the form:
//   [type error] PATH:LINE:COL: message
//   [move error] PATH:LINE:COL: message
//   [error]      PATH:LINE:COL: message        (syntax errors, from the parser)
// LINE/COL are 1-based; LSP wants 0-based. The compiler reports a point, not
// a span, so each diagnostic covers a single character.
//
// CLAUDE.md documents this exact "[category] file:line:col: message" format
// as a stable convention (§5.3) — this regex tracks that contract, not an
// implementation accident. `(.+)` for the path is greedy so it correctly
// backtracks past Windows drive-letter colons (e.g. `C:\foo\bar.ls:12:5:`)
// to find the rightmost `:LINE:COL:` split.
const DIAG_RE = /^\[(type error|move error|error)\]\s+(.+):(\d+):(\d+):\s*(.*)$/;

const DiagnosticSeverity = { Error: 1 };

function parseDiagnostics(stderrText) {
  const diagnostics = [];
  for (const line of stderrText.split(/\r?\n/)) {
    const m = DIAG_RE.exec(line);
    if (!m) continue;
    const [, category, , lineStr, colStr, message] = m;
    const ln = Math.max(0, parseInt(lineStr, 10) - 1);
    const col = Math.max(0, parseInt(colStr, 10) - 1);
    diagnostics.push({
      severity: DiagnosticSeverity.Error,
      range: {
        start: { line: ln, character: col },
        end: { line: ln, character: col + 1 },
      },
      message: message.trim(),
      source: `llvmscript (${category})`,
    });
  }
  return diagnostics;
}

module.exports = { parseDiagnostics, DIAG_RE };
