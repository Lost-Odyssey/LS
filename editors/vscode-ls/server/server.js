// Language server: on open/save, shells out to `ls check <file>` and
// republishes its stderr as LSP diagnostics. No incremental/in-process
// type checking — `ls check` always does a full parse+typecheck of the
// file as it sits on disk, same cost shape as `ls fmt`/`ls test`. So
// diagnostics refresh on open and on save, not on every keystroke (no
// debounce/temp-file machinery needed for that scope, see Phase 2 notes
// in docs/plan_editor_lsp.md).
const { fileURLToPath, pathToFileURL } = require('url');
const { spawn } = require('child_process');
const {
  createConnection,
  TextDocuments,
  ProposedFeatures,
  TextDocumentSyncKind,
} = require('vscode-languageserver/node');
const { TextDocument } = require('vscode-languageserver-textdocument');
const { parseDiagnostics } = require('./parseDiagnostics');
const { querySymbol } = require('./querySymbol');
const { queryComplete } = require('./queryComplete');
const { toCompletionItems, keywordCompletionItems } = require('./parseCompletionItems');
const { KEYWORDS } = require('./keywords');

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

let warnedMissingCompiler = false;

// uri -> CompletionItem[] (symbol items only; keywords are appended at
// request time, not cached — they never change). Populated on open/save,
// same cadence as diagnostics, NOT on every keystroke: `ls complete` walks
// the file + its imports from scratch, same cost shape as `ls check`, so
// completion (which fires far more often than open/save) reads from this
// cache instead of spawning a process per request. Consequence: brand-new
// unsaved local declarations won't show up in completion until the next
// save — same staleness tradeoff Phase 2's diagnostics already accepted.
const completionCache = new Map();

connection.onInitialize(() => ({
  capabilities: {
    textDocumentSync: TextDocumentSyncKind.Incremental,
    hoverProvider: true,
    definitionProvider: true,
    completionProvider: { triggerCharacters: ['.'] },
  },
}));

// Fetched fresh on every check (not cached from an onInitialized prefetch):
// onDidOpen for the file that triggered activation can fire before an
// async startup fetch would have resolved, which would silently run with
// the wrong compilerPath default. One extra round-trip per check is cheap
// next to spawning a process either way.
async function resolveCompilerPath() {
  try {
    const config = await connection.workspace.getConfiguration({ section: 'llvmscript' });
    if (config && config.compilerPath) return config.compilerPath;
  } catch {
    // Client doesn't support workspace/configuration — fall through to default.
  }
  return 'ls';
}

async function runCheck(doc) {
  let filePath;
  try {
    filePath = fileURLToPath(doc.uri);
  } catch {
    return; // not a real file:// document (e.g. an unsaved/untitled buffer)
  }

  const compilerPath = await resolveCompilerPath();
  const child = spawn(compilerPath, ['check', filePath], { windowsHide: true });
  let stderr = '';
  child.stderr.on('data', (chunk) => { stderr += chunk.toString(); });
  child.on('error', (err) => {
    if (warnedMissingCompiler) return;
    warnedMissingCompiler = true;
    connection.window.showWarningMessage(
      `LS (LLVM Script): couldn't run '${compilerPath} check' (${err.message}). ` +
      `Set the "llvmscript.compilerPath" setting if ls(.exe) isn't on PATH.`
    );
  });
  child.on('close', () => {
    connection.sendDiagnostics({ uri: doc.uri, diagnostics: parseDiagnostics(stderr) });
  });
}

async function refreshCompletionCache(doc) {
  let filePath;
  try {
    filePath = fileURLToPath(doc.uri);
  } catch {
    return;
  }
  const compilerPath = await resolveCompilerPath();
  const result = await queryComplete(compilerPath, filePath);
  completionCache.set(doc.uri, toCompletionItems(result));
}

documents.onDidOpen((e) => { runCheck(e.document); refreshCompletionCache(e.document); });
documents.onDidSave((e) => { runCheck(e.document); refreshCompletionCache(e.document); });
documents.onDidClose((e) => {
  connection.sendDiagnostics({ uri: e.document.uri, diagnostics: [] });
  completionCache.delete(e.document.uri);
});

// Both hover and definition are the same underlying lookup (see cmd_symbol
// in src/main.c — name-based scan of the file's top-level decls + methods
// blocks + one level of import expansion, not a real scope-resolving symbol
// table query). LSP positions are 0-based; `ls symbol` is 1-based like every
// other line/col the compiler reports.
async function lookupAtPosition(doc, position) {
  let filePath;
  try {
    filePath = fileURLToPath(doc.uri);
  } catch {
    return null;
  }
  const compilerPath = await resolveCompilerPath();
  return querySymbol(compilerPath, filePath, position.line + 1, position.character + 1);
}

connection.onHover(async (params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  const result = await lookupAtPosition(doc, params.position);
  if (!result || result.candidates.length === 0) return null;

  // Multiple candidates (an ambiguous name across imports) — hover can only
  // show one thing, so show the first and note how many others exist rather
  // than silently picking a possibly-wrong one. Go-to-definition (below)
  // hands the full list to the editor instead, which is built to offer a
  // picker for exactly this case.
  const c = result.candidates[0];
  const parts = [`\`\`\`llvmscript\n${c.signature}\n\`\`\``];
  if (c.doc) parts.push(c.doc);
  if (result.candidates.length > 1) {
    parts.push(`_(${result.candidates.length - 1} other declaration(s) named '${result.query}' found in imports)_`);
  }
  return { contents: { kind: 'markdown', value: parts.join('\n\n---\n') } };
});

connection.onDefinition(async (params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  const result = await lookupAtPosition(doc, params.position);
  if (!result || result.candidates.length === 0) return null;

  return result.candidates.map((c) => ({
    uri: pathToFileURL(c.file).toString(),
    range: {
      start: { line: Math.max(0, c.line - 1), character: 0 },
      end: { line: Math.max(0, c.line - 1), character: 0 },
    },
  }));
});

// Coarse and unfiltered by design (see cmd_complete in src/main.c): every
// declared name from the file + its imports, regardless of whether it'd
// actually apply to whatever's before the cursor. The client does the
// fuzzy/prefix filtering as the user keeps typing — same as it already does
// for the static keyword list and Phase 1's snippets.
connection.onCompletion((params) => {
  const cached = completionCache.get(params.textDocument.uri) || [];
  return [...keywordCompletionItems(KEYWORDS), ...cached];
});

documents.listen(connection);
connection.listen();
