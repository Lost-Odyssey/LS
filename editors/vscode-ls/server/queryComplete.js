// Thin wrapper around `ls complete <file>`, same shape/failure-handling as
// querySymbol.js's wrapper around `ls symbol`.
const { spawn } = require('child_process');

function queryComplete(compilerPath, filePath) {
  return new Promise((resolve) => {
    const child = spawn(compilerPath, ['complete', filePath], { windowsHide: true });
    let stdout = '';
    child.stdout.on('data', (chunk) => { stdout += chunk.toString(); });
    child.on('error', () => resolve({ items: [] }));
    child.on('close', () => {
      try {
        const parsed = JSON.parse(stdout);
        if (parsed && Array.isArray(parsed.items)) resolve(parsed);
        else resolve({ items: [] });
      } catch {
        resolve({ items: [] });
      }
    });
  });
}

module.exports = { queryComplete };
