const fs = require("fs");
const path = require("path");
const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client = null;

function executableCandidates(workspaceFolder, configuredPath) {
  if (configuredPath) {
    return [configuredPath];
  }

  if (!workspaceFolder) {
    return [];
  }

  const root = workspaceFolder.uri.fsPath;
  const suffix = process.platform === "win32" ? ".exe" : "";
  return [
    path.join(root, "build", `mog-lsp${suffix}`),
    path.join(root, "build", "Debug", `mog-lsp${suffix}`),
    path.join(root, "build", "Release", `mog-lsp${suffix}`)
  ];
}

function findServerPath() {
  const workspaceFolder = vscode.workspace.workspaceFolders
    ? vscode.workspace.workspaceFolders[0]
    : null;
  const configuredPath = vscode.workspace
    .getConfiguration("mog")
    .get("serverPath");

  for (const candidate of executableCandidates(workspaceFolder, configuredPath)) {
    if (candidate && fs.existsSync(candidate)) {
      return candidate;
    }
  }

  return null;
}

function activate(context) {
  const serverPath = findServerPath();
  if (!serverPath) {
    vscode.window.showWarningMessage(
      "Mog language support could not find `mog-lsp`. Build the project first or set `mog.serverPath`."
    );
    return;
  }

  const workspaceFolder = vscode.workspace.workspaceFolders
    ? vscode.workspace.workspaceFolders[0]
    : null;
  const serverOptions = {
    run: {
      command: serverPath,
      transport: TransportKind.stdio,
      options: workspaceFolder ? { cwd: workspaceFolder.uri.fsPath } : undefined
    },
    debug: {
      command: serverPath,
      transport: TransportKind.stdio,
      options: workspaceFolder ? { cwd: workspaceFolder.uri.fsPath } : undefined
    }
  };

  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "mog" }],
    outputChannelName: "Mog Language Support"
  };

  client = new LanguageClient(
    "mog",
    "Mog Language Server",
    serverOptions,
    clientOptions
  );
  context.subscriptions.push(client.start());
}

function deactivate() {
  if (!client) {
    return undefined;
  }

  const stopPromise = client.stop();
  client = null;
  return stopPromise;
}

module.exports = {
  activate,
  deactivate
};
