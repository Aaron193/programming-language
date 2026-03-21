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
import subprocess
import sys
import tempfile
from pathlib import Path

lsp_bin = sys.argv[1]


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
        decoded = line.decode("utf-8").strip()
        key, value = decoded.split(":", 1)
        headers[key.lower()] = value.strip()

    content_length = int(headers["content-length"])
    body = proc.stdout.read(content_length)
    return json.loads(body.decode("utf-8"))


def read_until(proc, predicate):
    while True:
        message = read_message(proc)
        if predicate(message):
            return message


def changes_for_uri(workspace_edit, uri):
    changes = workspace_edit.get("changes", {})
    return changes.get(uri, [])


source = "\n".join([
    "#!strict",
    "fn add(x i32) i32 {",
    "    var local i32 = x",
    "    return local",
    "}",
    "const Value i32 = add(1)",
    "var broken i32 = \"oops\"",
    "print(Value)",
    ""
])

with tempfile.TemporaryDirectory(prefix="mog_lsp_navigation_") as tmpdir:
    module_source = "\n".join([
        "#!strict",
        "fn Get() i32 {",
        "    return 42",
        "}",
        "const Answer i32 = 42",
        ""
    ])
    module_path = Path(tmpdir) / "dep.mog"
    module_path.write_text(module_source, encoding="utf-8")
    module_uri = module_path.resolve().as_uri()

    source_path = Path(tmpdir) / "sample.mog"
    source_path.write_text(source, encoding="utf-8")
    uri = source_path.resolve().as_uri()
    import_source = "\n".join([
        "#!strict",
        "const { Answer, Get } = @import(\"./dep.mog\")",
        "print(Get())",
        "print(Answer)",
        ""
    ])
    import_path = Path(tmpdir) / "import_sample.mog"
    import_path.write_text(import_source, encoding="utf-8")
    import_uri = import_path.resolve().as_uri()
    alias_import_source = "\n".join([
        "#!strict",
        "const { Answer as Alias } = @import(\"./dep.mog\")",
        "print(Alias)",
        ""
    ])
    alias_import_path = Path(tmpdir) / "alias_import_sample.mog"
    alias_import_path.write_text(alias_import_source, encoding="utf-8")
    alias_import_uri = alias_import_path.resolve().as_uri()
    member_source = "\n".join([
        "#!strict",
        "type Box struct {",
        "    value i32",
        "",
        "    fn get() i32 {",
        "        return this.value",
        "    }",
        "}",
        "fn read(box Box) i32 {",
        "    return box.value + box.get()",
        "}",
        ""
    ])
    member_path = Path(tmpdir) / "member_sample.mog"
    member_path.write_text(member_source, encoding="utf-8")
    member_uri = member_path.resolve().as_uri()
    type_definition_source = "\n".join([
        "#!strict",
        "type Pipe struct {",
        "    x f64",
        "}",
        "fn makePipe(x f64) Pipe {",
        "    return makePipe(x)",
        "}",
        ""
    ])
    type_definition_path = Path(tmpdir) / "type_definition_sample.mog"
    type_definition_path.write_text(type_definition_source, encoding="utf-8")
    type_definition_uri = type_definition_path.resolve().as_uri()
    module_member_source = "\n".join([
        "#!strict",
        "const dep = @import(\"./dep.mog\")",
        "print(dep.Ans)",
        ""
    ])
    module_member_path = Path(tmpdir) / "module_member_sample.mog"
    module_member_path.write_text(module_member_source, encoding="utf-8")
    module_member_uri = module_member_path.resolve().as_uri()
    type_context_source = "\n".join([
        "#!strict",
        "type LocalAlias i32",
        "type LocalBox struct {}",
        "fn use(value Loc) LocalA {",
        "    return 1",
        "}",
        ""
    ])
    type_context_path = Path(tmpdir) / "type_context_sample.mog"
    type_context_path.write_text(type_context_source, encoding="utf-8")
    type_context_uri = type_context_path.resolve().as_uri()
    signature_source = "\n".join([
        "#!strict",
        "fn Add(a i32, b i32) i32 {",
        "    return a + b",
        "}",
        "const value i32 = Add(1, 2)",
        ""
    ])
    signature_path = Path(tmpdir) / "signature_sample.mog"
    signature_path.write_text(signature_source, encoding="utf-8")
    signature_uri = signature_path.resolve().as_uri()
    signature_fail_source = "\n".join([
        "#!strict",
        "fn Add(a i32, b i32) i32 {",
        "    return a + b",
        "}",
        "const value i32 = Add(1,",
        ""
    ])
    signature_fail_path = Path(tmpdir) / "signature_fail_sample.mog"
    signature_fail_path.write_text(signature_fail_source, encoding="utf-8")
    signature_fail_uri = signature_fail_path.resolve().as_uri()
    parse_fail_source = "\n".join([
        "#!strict",
        "fn broken(",
        ""
    ])
    parse_fail_path = Path(tmpdir) / "parse_fail.mog"
    parse_fail_path.write_text(parse_fail_source, encoding="utf-8")
    parse_fail_uri = parse_fail_path.resolve().as_uri()

    proc = subprocess.Popen(
        [lsp_bin],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    try:
        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "processId": None,
                "rootUri": Path(tmpdir).resolve().as_uri(),
                "capabilities": {}
            }
        })
        initialize = read_until(proc, lambda msg: msg.get("id") == 1)
        caps = initialize["result"]["capabilities"]
        if caps.get("documentSymbolProvider") is not True:
            raise AssertionError("initialize response missing documentSymbolProvider")
        if caps.get("workspaceSymbolProvider") is not True:
            raise AssertionError("initialize response missing workspaceSymbolProvider")
        if caps.get("definitionProvider") is not True:
            raise AssertionError("initialize response missing definitionProvider")
        if caps.get("referencesProvider") is not True:
            raise AssertionError("initialize response missing referencesProvider")
        if caps.get("hoverProvider") is not True:
            raise AssertionError("initialize response missing hoverProvider")
        rename_provider = caps.get("renameProvider")
        if not isinstance(rename_provider, dict) or \
                rename_provider.get("prepareProvider") is not True:
            raise AssertionError("initialize response missing renameProvider.prepareProvider")
        completion_provider = caps.get("completionProvider")
        if not isinstance(completion_provider, dict):
            raise AssertionError("initialize response missing completionProvider")
        if completion_provider.get("resolveProvider") is not False:
            raise AssertionError("completionProvider should disable resolveProvider")
        if completion_provider.get("triggerCharacters") != ["."]:
            raise AssertionError(f"unexpected completion trigger characters: {completion_provider}")
        signature_provider = caps.get("signatureHelpProvider")
        if not isinstance(signature_provider, dict):
            raise AssertionError("initialize response missing signatureHelpProvider")
        if signature_provider.get("triggerCharacters") != ["(", ","]:
            raise AssertionError(f"unexpected signatureHelpProvider payload: {signature_provider}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "initialized",
            "params": {}
        })
        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": source
                }
            }
        })
        diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == uri,
        )
        published = diagnostics["params"]["diagnostics"]
        if not published:
            raise AssertionError("expected diagnostics for the broken sample")
        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": module_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": module_source
                }
            }
        })
        module_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == module_uri,
        )
        if module_diagnostics["params"]["diagnostics"]:
            raise AssertionError("expected dependency module to stay diagnostics-free")
        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": import_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": import_source
                }
            }
        })

        import_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == import_uri,
        )
        if import_diagnostics["params"]["diagnostics"]:
            raise AssertionError("expected import sample to stay diagnostics-free")

        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": alias_import_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": alias_import_source
                }
            }
        })
        alias_import_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == alias_import_uri,
        )
        if alias_import_diagnostics["params"]["diagnostics"]:
            raise AssertionError("expected aliased import sample to stay diagnostics-free")

        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": member_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": member_source
                }
            }
        })
        member_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == member_uri,
        )
        if member_diagnostics["params"]["diagnostics"]:
            raise AssertionError("expected member sample to stay diagnostics-free")
        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": type_definition_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": type_definition_source
                }
            }
        })
        type_definition_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == type_definition_uri,
        )
        if type_definition_diagnostics["params"]["diagnostics"]:
            raise AssertionError("expected type definition sample to stay diagnostics-free")
        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": module_member_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": module_member_source
                }
            }
        })
        read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == module_member_uri,
        )
        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": type_context_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": type_context_source
                }
            }
        })
        read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == type_context_uri,
        )
        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": signature_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": signature_source
                }
            }
        })
        signature_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == signature_uri,
        )
        if signature_diagnostics["params"]["diagnostics"]:
            raise AssertionError("expected signature sample to stay diagnostics-free")
        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": signature_fail_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": signature_fail_source
                }
            }
        })
        read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == signature_fail_uri,
        )

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "textDocument/documentSymbol",
            "params": {
                "textDocument": {
                    "uri": uri
                }
            }
        })
        document_symbols = read_until(proc, lambda msg: msg.get("id") == 2)
        names = [item["name"] for item in document_symbols["result"]]
        if names != ["add", "Value", "broken"]:
            raise AssertionError(f"unexpected document symbols: {names}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {
                    "uri": uri
                },
                "position": {
                    "line": 7,
                    "character": 6
                }
            }
        })
        definition = read_until(proc, lambda msg: msg.get("id") == 3)
        result = definition["result"]
        if result["uri"] != uri:
            raise AssertionError("definition should stay within the same file")
        if result["range"]["start"]["line"] != 5 or \
                result["range"]["start"]["character"] != 6:
            raise AssertionError(f"unexpected definition range: {result['range']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "textDocument/references",
            "params": {
                "textDocument": {
                    "uri": uri
                },
                "position": {
                    "line": 7,
                    "character": 6
                },
                "context": {
                    "includeDeclaration": True
                }
            }
        })
        references = read_until(proc, lambda msg: msg.get("id") == 4)
        if len(references["result"]) != 2:
            raise AssertionError(f"unexpected references result: {references['result']}")
        if references["result"][0]["range"]["start"]["line"] != 5:
            raise AssertionError("references should include the declaration first")
        if references["result"][1]["range"]["start"]["line"] != 7:
            raise AssertionError("references should include the usage site")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 5,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {
                    "uri": uri
                },
                "position": {
                    "line": 7,
                    "character": 6
                }
            }
        })
        hover = read_until(proc, lambda msg: msg.get("id") == 5)
        hover_value = hover["result"]["contents"]["value"]
        if hover_value != "const Value: i32":
            raise AssertionError(f"unexpected hover payload: {hover['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": uri
                },
                "position": {
                    "line": 3,
                    "character": 16
                }
            }
        })
        completion = read_until(proc, lambda msg: msg.get("id") == 6)
        labels = [item["label"] for item in completion["result"]]
        if "local" not in labels:
            raise AssertionError(f"expected local completion item: {completion['result']}")
        local_item = next(item for item in completion["result"] if item["label"] == "local")
        if local_item.get("detail") != "var local: i32":
            raise AssertionError(f"unexpected local completion detail: {local_item}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": member_uri
                },
                "position": {
                    "line": 9,
                    "character": 15
                }
            }
        })
        member_completion = read_until(proc, lambda msg: msg.get("id") == 7)
        member_labels = [item["label"] for item in member_completion["result"]]
        if "value" not in member_labels or "get" not in member_labels:
            raise AssertionError(f"expected member completion items: {member_completion['result']}")
        if "box" in member_labels or "return" in member_labels:
            raise AssertionError(f"member completion should exclude scope items: {member_completion['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7.25,
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": "file://" + os.path.abspath("tests/lsp_member_incomplete.mog"),
                    "languageId": "mog",
                    "version": 1,
                    "text": member_source.replace("box.value + box.get()", "box.")
                }
            }
        })
        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7.3,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": "file://" + os.path.abspath("tests/lsp_member_incomplete.mog")
                },
                "position": {
                    "line": 9,
                    "character": 15
                }
            }
        })
        incomplete_member_completion = read_until(proc, lambda msg: msg.get("id") == 7.3)
        incomplete_member_labels = [item["label"] for item in incomplete_member_completion["result"]]
        if "value" not in incomplete_member_labels or "get" not in incomplete_member_labels:
            raise AssertionError(
                f"expected incomplete member completion items: {incomplete_member_completion['result']}")
        if "box" in incomplete_member_labels or "return" in incomplete_member_labels:
            raise AssertionError(
                f"incomplete member completion should exclude scope items: {incomplete_member_completion['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7.5,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": module_member_uri
                },
                "position": {
                    "line": 2,
                    "character": 13
                }
            }
        })
        module_completion = read_until(proc, lambda msg: msg.get("id") == 7.5)
        module_labels = [item["label"] for item in module_completion["result"]]
        if "Answer" not in module_labels:
            raise AssertionError(f"expected exported member completion items: {module_completion['result']}")
        if "print" in module_labels:
            raise AssertionError(f"module member completions should exclude scope items: {module_completion['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7.6,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": type_context_uri
                },
                "position": {
                    "line": 3,
                    "character": 16
                }
            }
        })
        type_completion = read_until(proc, lambda msg: msg.get("id") == 7.6)
        type_labels = [item["label"] for item in type_completion["result"]]
        if "LocalAlias" not in type_labels or "LocalBox" not in type_labels:
            raise AssertionError(f"expected type-context completion items: {type_completion['result']}")
        if "value" in type_labels:
            raise AssertionError(f"type-context completions should exclude value bindings: {type_completion['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 8,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {
                    "uri": member_uri
                },
                "position": {
                    "line": 9,
                    "character": 16
                }
            }
        })
        member_hover = read_until(proc, lambda msg: msg.get("id") == 8)
        member_hover_value = member_hover["result"]["contents"]["value"]
        if member_hover_value != "field value: i32":
            raise AssertionError(f"unexpected member hover payload: {member_hover['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 9,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {
                    "uri": member_uri
                },
                "position": {
                    "line": 9,
                    "character": 28
                }
            }
        })
        member_definition = read_until(proc, lambda msg: msg.get("id") == 9)
        member_result = member_definition["result"]
        if member_result["uri"] != member_uri:
            raise AssertionError("member definition should stay in the same module")
        if member_result["range"]["start"]["line"] != 4 or \
                member_result["range"]["start"]["character"] != 7:
            raise AssertionError(f"unexpected member definition range: {member_result['range']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 9.5,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {
                    "uri": type_definition_uri
                },
                "position": {
                    "line": 4,
                    "character": 20
                }
            }
        })
        type_definition = read_until(proc, lambda msg: msg.get("id") == 9.5)
        type_result = type_definition["result"]
        if type_result["uri"] != type_definition_uri:
            raise AssertionError("type definition should stay in the same module")
        if type_result["range"]["start"]["line"] != 1 or \
                type_result["range"]["start"]["character"] != 5:
            raise AssertionError(f"unexpected type definition range: {type_result['range']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 10,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {
                    "uri": import_uri
                },
                "position": {
                    "line": 3,
                    "character": 7
                }
            }
        })
        cross_definition = read_until(proc, lambda msg: msg.get("id") == 10)
        cross_result = cross_definition["result"]
        if cross_result["uri"] != module_uri:
            raise AssertionError("import definition should jump to the imported module")
        if cross_result["range"]["start"]["line"] != 4 or \
                cross_result["range"]["start"]["character"] != 6:
            raise AssertionError(f"unexpected cross-file definition range: {cross_result['range']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": parse_fail_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": parse_fail_source
                }
            }
        })
        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 11,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": parse_fail_uri
                },
                "position": {
                    "line": 1,
                    "character": 10
                }
            }
        })
        parse_completion = read_until(proc, lambda msg: msg.get("id") == 11)
        parse_labels = [item["label"] for item in parse_completion["result"]]
        if "fn" not in parse_labels or "while" not in parse_labels:
            raise AssertionError(f"expected keyword completions on parse failure: {parse_completion['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 11.5,
            "method": "textDocument/signatureHelp",
            "params": {
                "textDocument": {
                    "uri": signature_uri
                },
                "position": {
                    "line": 4,
                    "character": 27
                }
            }
        })
        signature_help = read_until(proc, lambda msg: msg.get("id") == 11.5)
        if signature_help["result"]["activeParameter"] != 1:
            raise AssertionError(f"unexpected direct signature help payload: {signature_help['result']}")
        if signature_help["result"]["signatures"][0]["label"] != "function(i32, i32) -> i32":
            raise AssertionError(f"unexpected direct signature label: {signature_help['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 11.6,
            "method": "textDocument/signatureHelp",
            "params": {
                "textDocument": {
                    "uri": signature_fail_uri
                },
                "position": {
                    "line": 4,
                    "character": 25
                }
            }
        })
        parse_signature_help = read_until(proc, lambda msg: msg.get("id") == 11.6)
        if parse_signature_help["result"]["activeParameter"] != 1:
            raise AssertionError(f"unexpected parse-fail signature help payload: {parse_signature_help['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 12,
            "method": "workspace/symbol",
            "params": {
                "query": "Answer"
            }
        })
        workspace_symbols = read_until(proc, lambda msg: msg.get("id") == 12)
        workspace_names = [item["name"] for item in workspace_symbols["result"]]
        if workspace_names != ["Answer"]:
            raise AssertionError(f"unexpected workspace symbols: {workspace_symbols['result']}")
        if workspace_symbols["result"][0]["location"]["uri"] != module_uri:
            raise AssertionError("workspace symbol should resolve to the defining module")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 13,
            "method": "textDocument/prepareRename",
            "params": {
                "textDocument": {
                    "uri": uri
                },
                "position": {
                    "line": 7,
                    "character": 6
                }
            }
        })
        prepare_local = read_until(proc, lambda msg: msg.get("id") == 13)
        local_range = prepare_local["result"]["range"]
        if local_range["start"]["line"] != 7 or local_range["start"]["character"] != 6:
            raise AssertionError(f"unexpected prepareRename range: {prepare_local['result']}")
        if prepare_local["result"].get("placeholder") != "Value":
            raise AssertionError(f"unexpected prepareRename placeholder: {prepare_local['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 14,
            "method": "textDocument/prepareRename",
            "params": {
                "textDocument": {
                    "uri": member_uri
                },
                "position": {
                    "line": 9,
                    "character": 16
                }
            }
        })
        prepare_member = read_until(proc, lambda msg: msg.get("id") == 14)
        if prepare_member["result"] is not None:
            raise AssertionError("prepareRename should reject member access")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 15,
            "method": "textDocument/rename",
            "params": {
                "textDocument": {
                    "uri": uri
                },
                "position": {
                    "line": 7,
                    "character": 6
                },
                "newName": "Result"
            }
        })
        local_rename = read_until(proc, lambda msg: msg.get("id") == 15)
        local_changes = changes_for_uri(local_rename["result"], uri)
        if len(local_changes) != 2:
            raise AssertionError(f"unexpected same-file rename edits: {local_rename['result']}")
        if not any(edit["newText"] == "Result" and edit["range"]["start"]["line"] == 5
                   for edit in local_changes):
            raise AssertionError(f"expected declaration rename edit: {local_changes}")
        if not any(edit["newText"] == "Result" and edit["range"]["start"]["line"] == 7
                   for edit in local_changes):
            raise AssertionError(f"expected usage rename edit: {local_changes}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 16,
            "method": "textDocument/rename",
            "params": {
                "textDocument": {
                    "uri": import_uri
                },
                "position": {
                    "line": 3,
                    "character": 7
                },
                "newName": "LocalAnswer"
            }
        })
        import_local_rename = read_until(proc, lambda msg: msg.get("id") == 16)
        import_local_changes = changes_for_uri(import_local_rename["result"], import_uri)
        if len(import_local_changes) != 2:
            raise AssertionError(f"unexpected importer-local rename edits: {import_local_rename['result']}")
        if not any(edit["newText"] == "Answer as LocalAnswer" and
                   edit["range"]["start"]["line"] == 1 and
                   edit["range"]["start"]["character"] == 8
                   for edit in import_local_changes):
            raise AssertionError(f"expected importer alias insertion edit: {import_local_changes}")
        if not any(edit["newText"] == "LocalAnswer" and
                   edit["range"]["start"]["line"] == 3 and
                   edit["range"]["start"]["character"] == 6
                   for edit in import_local_changes):
            raise AssertionError(f"expected importer usage rename edit: {import_local_changes}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 17,
            "method": "textDocument/rename",
            "params": {
                "textDocument": {
                    "uri": module_uri
                },
                "position": {
                    "line": 4,
                    "character": 6
                },
                "newName": "FinalAnswer"
            }
        })
        exported_rename = read_until(proc, lambda msg: msg.get("id") == 17)
        module_changes = changes_for_uri(exported_rename["result"], module_uri)
        importer_changes = changes_for_uri(exported_rename["result"], import_uri)
        alias_changes = changes_for_uri(exported_rename["result"], alias_import_uri)
        if len(module_changes) != 1 or len(importer_changes) != 2 or len(alias_changes) != 1:
            raise AssertionError(f"unexpected project-wide rename edits: {exported_rename['result']}")
        if module_changes[0]["newText"] != "FinalAnswer":
            raise AssertionError(f"unexpected defining-module rename edit: {module_changes}")
        if not any(edit["newText"] == "FinalAnswer" and
                   edit["range"]["start"]["line"] == 1 and
                   edit["range"]["start"]["character"] == 8
                   for edit in importer_changes):
            raise AssertionError(f"expected importer binding export rename: {importer_changes}")
        if not any(edit["newText"] == "FinalAnswer" and
                   edit["range"]["start"]["line"] == 3 and
                   edit["range"]["start"]["character"] == 6
                   for edit in importer_changes):
            raise AssertionError(f"expected importer usage rename: {importer_changes}")
        if alias_changes[0]["newText"] != "FinalAnswer" or \
                alias_changes[0]["range"]["start"]["line"] != 1 or \
                alias_changes[0]["range"]["start"]["character"] != 8:
            raise AssertionError(f"expected aliased importer to rewrite only the export name: {alias_changes}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 18,
            "method": "textDocument/rename",
            "params": {
                "textDocument": {
                    "uri": module_uri
                },
                "position": {
                    "line": 4,
                    "character": 6
                },
                "newName": "answer"
            }
        })
        invalid_exported_rename = read_until(proc, lambda msg: msg.get("id") == 18)
        if invalid_exported_rename.get("error", {}).get("code") != -32602:
            raise AssertionError(f"expected invalid exported rename error: {invalid_exported_rename}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 19,
            "method": "shutdown",
            "params": {}
        })
        read_until(proc, lambda msg: msg.get("id") == 19)
        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "exit",
            "params": {}
        })
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)

print("[PASS] mog-lsp navigation regression")
PY
