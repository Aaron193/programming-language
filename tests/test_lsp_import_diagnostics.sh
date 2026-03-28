#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LSP_BIN="$PROJECT_ROOT/build/mog-lsp"

if [[ ! -x "$LSP_BIN" ]]; then
    echo "LSP binary not found at $LSP_BIN"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

python3 - "$LSP_BIN" <<'PY'
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

LSP_BIN = sys.argv[1]


def send_message(proc, payload):
    body = json.dumps(payload)
    header = f"Content-Length: {len(body)}\r\n\r\n"
    proc.stdin.write(header.encode("utf-8"))
    proc.stdin.write(body.encode("utf-8"))
    proc.stdin.flush()


def read_message(proc):
    headers = {}
    while True:
        line = proc.stdout.readline()
        if not line:
            raise RuntimeError("unexpected EOF from LSP server")
        if line == b"\r\n":
            break
        name, value = line.decode("utf-8").split(":", 1)
        headers[name.strip().lower()] = value.strip()
    length = int(headers["content-length"])
    body = proc.stdout.read(length)
    return json.loads(body.decode("utf-8"))


def read_until(proc, predicate):
    while True:
        message = read_message(proc)
        if predicate(message):
            return message


workspace = pathlib.Path(tempfile.mkdtemp(prefix="mog_lsp_import_diag_"))
try:
    dep_path = workspace / "dep.mog"
    importer_path = workspace / "main.mog"
    dep_path.write_text("var broken i32 = 1;\n", encoding="utf-8")
    importer_path.write_text(
        "const { broken } = @import(\"./dep.mog\")\nprint(broken)\n",
        encoding="utf-8",
    )

    dep_uri = "file://" + str(dep_path)
    importer_uri = "file://" + str(importer_path)

    proc = subprocess.Popen(
        [LSP_BIN],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    send_message(proc, {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "rootUri": "file://" + str(workspace),
            "capabilities": {},
        },
    })
    read_until(proc, lambda msg: msg.get("id") == 1)
    send_message(proc, {"jsonrpc": "2.0", "method": "initialized", "params": {}})

    send_message(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": importer_uri,
                "languageId": "mog",
                "version": 1,
                "text": importer_path.read_text(encoding="utf-8"),
            }
        },
    })
    importer_diagnostics = read_until(
        proc,
        lambda msg: msg.get("method") == "textDocument/publishDiagnostics"
        and msg.get("params", {}).get("uri") == importer_uri,
    )
    published = importer_diagnostics["params"]["diagnostics"]
    if len(published) != 1:
        raise AssertionError(f"expected one importer diagnostic, got {published}")
    importer_diag = published[0]
    if importer_diag["message"] != "Semicolons are only allowed inside 'for (...)' clauses.":
        raise AssertionError(f"unexpected importer message: {importer_diag}")
    if importer_diag["range"]["start"]["line"] != 0 or importer_diag["range"]["start"]["character"] != 27:
        raise AssertionError(f"importer diagnostic should point at the import path, got {importer_diag}")
    related = importer_diag.get("relatedInformation", [])
    if not related:
        raise AssertionError(f"expected related information for importer diagnostic: {importer_diag}")
    if related[0]["location"]["uri"] != dep_uri:
        raise AssertionError(f"expected related information to target imported file: {related}")

    send_message(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": dep_uri,
                "languageId": "mog",
                "version": 1,
                "text": dep_path.read_text(encoding="utf-8"),
            }
        },
    })
    dep_diagnostics = read_until(
        proc,
        lambda msg: msg.get("method") == "textDocument/publishDiagnostics"
        and msg.get("params", {}).get("uri") == dep_uri,
    )
    published_dep = dep_diagnostics["params"]["diagnostics"]
    if len(published_dep) != 1:
        raise AssertionError(f"expected one dependency diagnostic, got {published_dep}")
    dep_diag = published_dep[0]
    if dep_diag["message"] != "Semicolons are only allowed inside 'for (...)' clauses.":
        raise AssertionError(f"unexpected dependency message: {dep_diag}")
    if dep_diag["range"]["start"]["line"] != 0 or dep_diag["range"]["start"]["character"] != 18:
        raise AssertionError(f"dependency diagnostic should point at the semicolon token: {dep_diag}")

    malformed_path = workspace / "malformed_import.mog"
    malformed_path.write_text(
        "const value = @import(@ASDAS\"./dep.mog\")\n",
        encoding="utf-8",
    )
    malformed_uri = "file://" + str(malformed_path)

    send_message(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": malformed_uri,
                "languageId": "mog",
                "version": 1,
                "text": malformed_path.read_text(encoding="utf-8"),
            }
        },
    })
    malformed_diagnostics = read_until(
        proc,
        lambda msg: msg.get("method") == "textDocument/publishDiagnostics"
        and msg.get("params", {}).get("uri") == malformed_uri,
    )
    published_malformed = malformed_diagnostics["params"]["diagnostics"]
    if len(published_malformed) != 1:
        raise AssertionError(
            f"expected one malformed-import diagnostic, got {published_malformed}"
        )
    malformed_diag = published_malformed[0]
    if malformed_diag.get("code") != "parse.expected_token":
        raise AssertionError(f"unexpected malformed-import code: {malformed_diag}")
    if malformed_diag["message"] != "Expected string literal but found '@'.":
        raise AssertionError(f"unexpected malformed-import message: {malformed_diag}")
    if malformed_diag["range"]["start"]["line"] != 0 or malformed_diag["range"]["start"]["character"] != 22:
        raise AssertionError(
            f"malformed-import diagnostic should point at the unexpected '@': {malformed_diag}"
        )

    send_message(proc, {"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": {}})
    read_until(proc, lambda msg: msg.get("id") == 2)
    send_message(proc, {"jsonrpc": "2.0", "method": "exit", "params": {}})
    proc.wait(timeout=5)
finally:
    shutil.rmtree(workspace, ignore_errors=True)
PY
