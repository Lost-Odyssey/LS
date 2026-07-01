// Maps `ls complete <file>`'s JSON ({"items":[{name,kind,signature,doc}]})
// to LSP CompletionItems. Numeric CompletionItemKind values are hardcoded
// (not imported from vscode-languageserver) so this stays a pure function
// testable without the LSP package — same pattern as parseDiagnostics.js's
// own DiagnosticSeverity constant. Values match the LSP spec's stable enum.
const KIND = {
  struct: 22,    // CompletionItemKind.Struct
  enum: 13,      // CompletionItemKind.Enum
  interface: 8,  // CompletionItemKind.Interface
  function: 3,   // CompletionItemKind.Function
  method: 2,     // CompletionItemKind.Method
};
const KEYWORD_KIND = 14; // CompletionItemKind.Keyword

function toCompletionItems(json) {
  if (!json || !Array.isArray(json.items)) return [];
  return json.items.map((it) => ({
    label: it.name,
    kind: KIND[it.kind] || 6, // 6 = CompletionItemKind.Variable, fallback for an unrecognized kind
    detail: it.signature || undefined,
    documentation: it.doc ? { kind: 'markdown', value: it.doc } : undefined,
  }));
}

function keywordCompletionItems(keywords) {
  return keywords.map((label) => ({ label, kind: KEYWORD_KIND }));
}

module.exports = { toCompletionItems, keywordCompletionItems, KIND, KEYWORD_KIND };
