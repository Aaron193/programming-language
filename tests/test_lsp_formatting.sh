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

python3 - "$LSP_BIN" "$PROJECT_ROOT" <<'PY'
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

LSP_BIN = sys.argv[1]
PROJECT_ROOT = Path(sys.argv[2])


def send_message(proc, payload):
    body = json.dumps(payload).encode("utf-8")
    header = f"Content-Length: {len(body)}\r\n\r\n".encode("utf-8")
    proc.stdin.write(header + body)
    proc.stdin.flush()


def read_message(proc):
    headers = {}
    while True:
        line = proc.stdout.readline()
        if not line:
            raise RuntimeError("unexpected EOF from mog-lsp")
        if line == b"\r\n":
            break
        key, value = line.decode("utf-8").split(":", 1)
        headers[key.strip().lower()] = value.strip()

    body = proc.stdout.read(int(headers["content-length"]))
    return json.loads(body.decode("utf-8"))


def read_until(proc, predicate):
    while True:
        message = read_message(proc)
        if predicate(message):
            return message


workspace = Path(tempfile.mkdtemp(prefix="mog_lsp_formatting_"))
try:
    source_path = workspace / "sample.mog"
    source_text = "\n".join([
        "fn add(x i32,y i32)i32{",
        "var total i32= x+y",
        "return total",
        "}",
        "",
    ])
    source_path.write_text(source_text, encoding="utf-8")
    source_uri = source_path.resolve().as_uri()

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
            "rootUri": workspace.resolve().as_uri(),
            "capabilities": {},
        },
    })
    init = read_until(proc, lambda msg: msg.get("id") == 1)
    capabilities = init["result"]["capabilities"]
    if capabilities.get("documentFormattingProvider") is not True:
        raise AssertionError(f"expected formatting provider capability: {capabilities}")
    send_message(proc, {"jsonrpc": "2.0", "method": "initialized", "params": {}})

    send_message(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": source_uri,
                "languageId": "mog",
                "version": 1,
                "text": source_text,
            }
        },
    })
    read_until(
        proc,
        lambda msg: msg.get("method") == "textDocument/publishDiagnostics"
        and msg.get("params", {}).get("uri") == source_uri,
    )

    send_message(proc, {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "textDocument/formatting",
        "params": {
            "textDocument": {"uri": source_uri},
            "options": {"insertSpaces": True, "tabSize": 4},
        },
    })
    formatting = read_until(proc, lambda msg: msg.get("id") == 2)
    edits = formatting["result"]
    if len(edits) != 1:
        raise AssertionError(f"expected one formatting edit, got {edits}")
    edit = edits[0]
    expected_text = "\n".join([
        "fn add(x i32, y i32) i32 {",
        "    var total i32 = x + y",
        "    return total",
        "}",
        "",
    ])
    if edit["newText"] != expected_text:
        raise AssertionError(
            f"unexpected formatted text:\nEXPECTED:\n{expected_text!r}\nACTUAL:\n{edit['newText']!r}"
        )

    send_message(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didChange",
        "params": {
            "textDocument": {"uri": source_uri, "version": 2},
            "contentChanges": [{"text": expected_text}],
        },
    })
    read_until(
        proc,
        lambda msg: msg.get("method") == "textDocument/publishDiagnostics"
        and msg.get("params", {}).get("uri") == source_uri,
    )

    send_message(proc, {
        "jsonrpc": "2.0",
        "id": 3,
        "method": "textDocument/formatting",
        "params": {
            "textDocument": {"uri": source_uri},
            "options": {"insertSpaces": True, "tabSize": 4},
        },
    })
    no_op = read_until(proc, lambda msg: msg.get("id") == 3)
    if no_op["result"] != []:
        raise AssertionError(f"expected no-op formatting result, got {no_op['result']}")

    blank_line_cap_text = "\n".join([
        "fn main() void {",
        "    const first i32 = 1",
        "",
        "",
        "",
        "    const second i32 = 2",
        "}",
        "",
    ])
    send_message(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didChange",
        "params": {
            "textDocument": {"uri": source_uri, "version": 3},
            "contentChanges": [{"text": blank_line_cap_text}],
        },
    })
    read_until(
        proc,
        lambda msg: msg.get("method") == "textDocument/publishDiagnostics"
        and msg.get("params", {}).get("uri") == source_uri,
    )

    send_message(proc, {
        "jsonrpc": "2.0",
        "id": 4,
        "method": "textDocument/formatting",
        "params": {
            "textDocument": {"uri": source_uri},
            "options": {"insertSpaces": True, "tabSize": 4},
        },
    })
    blank_line_cap = read_until(proc, lambda msg: msg.get("id") == 4)
    blank_line_expected = "\n".join([
        "fn main() void {",
        "    const first i32 = 1",
        "",
        "    const second i32 = 2",
        "}",
        "",
    ])
    if len(blank_line_cap["result"]) != 1:
        raise AssertionError(
            f"expected one blank-line-cap formatting edit, got {blank_line_cap['result']}"
        )
    if blank_line_cap["result"][0]["newText"] != blank_line_expected:
        raise AssertionError("blank-line cap formatting did not match expected output")

    fixture_source = (PROJECT_ROOT / "examples" / "test" / "main.mog").read_text(
        encoding="utf-8"
    )
    fixture_expected = (
        PROJECT_ROOT / "examples" / "test" / "main-formatted.mog"
    ).read_text(encoding="utf-8")
    send_message(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didChange",
        "params": {
            "textDocument": {"uri": source_uri, "version": 4},
            "contentChanges": [{"text": fixture_source}],
        },
    })
    read_until(
        proc,
        lambda msg: msg.get("method") == "textDocument/publishDiagnostics"
        and msg.get("params", {}).get("uri") == source_uri,
    )

    send_message(proc, {
        "jsonrpc": "2.0",
        "id": 5,
        "method": "textDocument/formatting",
        "params": {
            "textDocument": {"uri": source_uri},
            "options": {"insertSpaces": True, "tabSize": 4},
        },
    })
    fixture_formatting = read_until(proc, lambda msg: msg.get("id") == 5)
    if len(fixture_formatting["result"]) != 1:
        raise AssertionError(
            f"expected one fixture formatting edit, got {fixture_formatting['result']}"
        )
    if fixture_formatting["result"][0]["newText"] != fixture_expected:
        raise AssertionError("fixture formatting did not preserve expected comments and spacing")

    unsupported_text = "const value i32 = 1 /* inline */ + 2\n"
    send_message(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didChange",
        "params": {
            "textDocument": {"uri": source_uri, "version": 5},
            "contentChanges": [{"text": unsupported_text}],
        },
    })
    read_until(
        proc,
        lambda msg: msg.get("method") == "textDocument/publishDiagnostics"
        and msg.get("params", {}).get("uri") == source_uri,
    )

    send_message(proc, {
        "jsonrpc": "2.0",
        "id": 6,
        "method": "textDocument/formatting",
        "params": {
            "textDocument": {"uri": source_uri},
            "options": {"insertSpaces": True, "tabSize": 4},
        },
    })
    unsupported = read_until(proc, lambda msg: msg.get("id") == 6)
    if unsupported["result"] != []:
        raise AssertionError(
            f"expected unsupported inline comments to produce no edit, got {unsupported['result']}"
        )

    send_message(proc, {"jsonrpc": "2.0", "id": 7, "method": "shutdown", "params": {}})
    read_until(proc, lambda msg: msg.get("id") == 7)
    send_message(proc, {"jsonrpc": "2.0", "method": "exit", "params": {}})
    proc.wait(timeout=5)
finally:
    shutil.rmtree(workspace, ignore_errors=True)
PY
