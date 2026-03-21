const cp = require("child_process");
const fs = require("fs");
const path = require("path");
const vscode = require("vscode");

class RpcClient {
  constructor(serverPath, diagnosticCollection) {
    this.serverPath = serverPath;
    this.diagnosticCollection = diagnosticCollection;
    this.sequence = 1;
    this.buffer = Buffer.alloc(0);
    this.pending = new Map();
    this.process = null;
  }

  start() {
    this.process = cp.spawn(this.serverPath, [], {
      stdio: ["pipe", "pipe", "pipe"]
    });

    this.process.stdout.on("data", (chunk) => this.onData(chunk));
    this.process.stderr.on("data", (chunk) => {
      const text = chunk.toString("utf8").trim();
      if (text.length > 0) {
        console.error("[mog-lsp]", text);
      }
    });
    this.process.on("exit", () => {
      this.process = null;
      this.pending.clear();
    });
  }

  stop() {
    if (!this.process) {
      return;
    }

    try {
      this.notify("shutdown", {});
      this.notify("exit", {});
    } catch (_) {
    }

    this.process.kill();
    this.process = null;
    this.pending.clear();
  }

  initialize(rootUri) {
    return this.request("initialize", {
      processId: process.pid,
      rootUri,
      capabilities: {}
    }).then(() => {
      this.notify("initialized", {});
    });
  }

  request(method, params) {
    const id = this.sequence++;
    const payload = {
      jsonrpc: "2.0",
      id,
      method,
      params
    };

    this.write(payload);
    return new Promise((resolve) => {
      this.pending.set(id, resolve);
    });
  }

  notify(method, params) {
    this.write({
      jsonrpc: "2.0",
      method,
      params
    });
  }

  write(payload) {
    if (!this.process) {
      return;
    }

    const body = Buffer.from(JSON.stringify(payload), "utf8");
    const header = Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, "utf8");
    this.process.stdin.write(Buffer.concat([header, body]));
  }

  onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);

    while (true) {
      const headerEnd = this.buffer.indexOf("\r\n\r\n");
      if (headerEnd === -1) {
        return;
      }

      const header = this.buffer.slice(0, headerEnd).toString("utf8");
      const match = header.match(/Content-Length:\s*(\d+)/i);
      if (!match) {
        this.buffer = Buffer.alloc(0);
        return;
      }

      const bodyLength = Number(match[1]);
      const frameLength = headerEnd + 4 + bodyLength;
      if (this.buffer.length < frameLength) {
        return;
      }

      const body = this.buffer
        .slice(headerEnd + 4, frameLength)
        .toString("utf8");
      this.buffer = this.buffer.slice(frameLength);

      let message;
      try {
        message = JSON.parse(body);
      } catch (_) {
        continue;
      }

      this.dispatch(message);
    }
  }

  dispatch(message) {
    if (typeof message.id === "number" && this.pending.has(message.id)) {
      const resolve = this.pending.get(message.id);
      this.pending.delete(message.id);
      resolve(message.result);
      return;
    }

    if (message.method === "textDocument/publishDiagnostics") {
      this.applyDiagnostics(message.params);
    }
  }

  applyDiagnostics(params) {
    if (!params || typeof params.uri !== "string") {
      return;
    }

    const uri = vscode.Uri.parse(params.uri);
    const diagnostics = Array.isArray(params.diagnostics)
      ? params.diagnostics.map((item) => this.toDiagnostic(item))
      : [];
    this.diagnosticCollection.set(uri, diagnostics);
  }

  toDiagnostic(item) {
    const range = this.toRange(item.range);
    const diagnostic = new vscode.Diagnostic(
      range,
      item.message || "Mog diagnostic",
      this.toSeverity(item.severity)
    );

    diagnostic.source = item.source || "mog";
    if (item.code) {
      diagnostic.code = item.code;
    }

    if (Array.isArray(item.relatedInformation)) {
      diagnostic.relatedInformation = item.relatedInformation
        .map((info) => {
          if (!info.location || typeof info.message !== "string") {
            return null;
          }

          const relatedUri = vscode.Uri.parse(info.location.uri);
          return new vscode.DiagnosticRelatedInformation(
            new vscode.Location(relatedUri, this.toRange(info.location.range)),
            info.message
          );
        })
        .filter(Boolean);
    }

    return diagnostic;
  }

  toRange(range) {
    if (!range || !range.start || !range.end) {
      return new vscode.Range(0, 0, 0, 0);
    }

    return new vscode.Range(
      range.start.line || 0,
      range.start.character || 0,
      range.end.line || 0,
      range.end.character || 0
    );
  }

  toSeverity(severity) {
    switch (severity) {
      case 1:
        return vscode.DiagnosticSeverity.Error;
      case 2:
        return vscode.DiagnosticSeverity.Warning;
      case 3:
        return vscode.DiagnosticSeverity.Information;
      default:
        return vscode.DiagnosticSeverity.Hint;
    }
  }
}

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

function syncDocument(client, document) {
  if (document.languageId !== "mog") {
    return;
  }

  client.notify("textDocument/didOpen", {
    textDocument: {
      uri: document.uri.toString(),
      languageId: "mog",
      version: document.version,
      text: document.getText()
    }
  });
}

function activate(context) {
  const diagnosticCollection = vscode.languages.createDiagnosticCollection("mog");
  context.subscriptions.push(diagnosticCollection);

  const serverPath = findServerPath();
  if (!serverPath) {
    vscode.window.showWarningMessage(
      "Mog language support could not find `mog-lsp`. Build the project first or set `mog.serverPath`."
    );
    return;
  }

  const client = new RpcClient(serverPath, diagnosticCollection);
  client.start();

  const rootUri = vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length > 0
    ? vscode.workspace.workspaceFolders[0].uri.toString()
    : null;
  client.initialize(rootUri).then(() => {
    vscode.workspace.textDocuments.forEach((document) => syncDocument(client, document));
  });

  context.subscriptions.push({
    dispose() {
      client.stop();
    }
  });

  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((document) => syncDocument(client, document))
  );

  context.subscriptions.push(
    vscode.workspace.onDidChangeTextDocument((event) => {
      if (event.document.languageId !== "mog") {
        return;
      }

      client.notify("textDocument/didChange", {
        textDocument: {
          uri: event.document.uri.toString(),
          version: event.document.version
        },
        contentChanges: [
          {
            text: event.document.getText()
          }
        ]
      });
    })
  );

  context.subscriptions.push(
    vscode.workspace.onDidSaveTextDocument((document) => {
      if (document.languageId !== "mog") {
        return;
      }

      client.notify("textDocument/didSave", {
        textDocument: {
          uri: document.uri.toString()
        }
      });
    })
  );

  context.subscriptions.push(
    vscode.workspace.onDidCloseTextDocument((document) => {
      if (document.languageId !== "mog") {
        return;
      }

      client.notify("textDocument/didClose", {
        textDocument: {
          uri: document.uri.toString()
        }
      });
      diagnosticCollection.delete(document.uri);
    })
  );
}

function deactivate() {}

module.exports = {
  activate,
  deactivate
};
