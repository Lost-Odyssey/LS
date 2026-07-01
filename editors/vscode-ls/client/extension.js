// Extension host entry point. Starts the language client, which spawns
// server/server.js as a child Node process and talks LSP to it over IPC.
const path = require('path');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

let client;

function activate(context) {
  const serverModule = context.asAbsolutePath(path.join('server', 'server.js'));
  const serverOptions = {
    run: { module: serverModule, transport: TransportKind.ipc },
    debug: { module: serverModule, transport: TransportKind.ipc },
  };
  const clientOptions = {
    documentSelector: [{ scheme: 'file', language: 'llvmscript' }],
    synchronize: { configurationSection: 'llvmscript' },
  };

  client = new LanguageClient(
    'llvmscript',
    'LS (LLVM Script) Language Server',
    serverOptions,
    clientOptions
  );
  client.start();
}

function deactivate() {
  if (!client) return undefined;
  return client.stop();
}

module.exports = { activate, deactivate };
