// Thin wrapper around `ls symbol <file> <line> <col>` (1-based, matching the
// rest of the compiler's error-reporting convention — callers convert from
// LSP's 0-based positions). Never rejects: a spawn failure or malformed
// output resolves to the same "nothing found" shape a real empty result
// would, so hover/definition handlers don't need a separate error path.
const { spawn } = require('child_process');

function querySymbol(compilerPath, filePath, line, col) {
  return new Promise((resolve) => {
    const child = spawn(compilerPath, ['symbol', filePath, String(line), String(col)], {
      windowsHide: true,
    });
    let stdout = '';
    child.stdout.on('data', (chunk) => { stdout += chunk.toString(); });
    child.on('error', () => resolve({ query: null, candidates: [] }));
    child.on('close', () => {
      try {
        const parsed = JSON.parse(stdout);
        if (parsed && Array.isArray(parsed.candidates)) resolve(parsed);
        else resolve({ query: null, candidates: [] });
      } catch {
        resolve({ query: null, candidates: [] });
      }
    });
  });
}

module.exports = { querySymbol };
