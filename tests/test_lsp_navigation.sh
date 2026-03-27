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


def decode_semantic_tokens(payload, token_types, token_modifiers):
    data = payload.get("data", [])
    tokens = []
    line = 0
    character = 0
    for index in range(0, len(data), 5):
        delta_line = int(data[index])
        delta_start = int(data[index + 1])
        length = int(data[index + 2])
        token_type = token_types[int(data[index + 3])]
        modifier_mask = int(data[index + 4])
        if index == 0:
            line = delta_line
            character = delta_start
        else:
            line += delta_line
            character = delta_start if delta_line else character + delta_start

        modifiers = []
        for modifier_index, modifier in enumerate(token_modifiers):
            if modifier_mask & (1 << modifier_index):
                modifiers.append(modifier)

        tokens.append({
            "line": line,
            "character": character,
            "length": length,
            "type": token_type,
            "modifiers": modifiers,
        })
    return tokens


def find_semantic_token(tokens, line, character, token_type):
    for token in tokens:
        if token["line"] == line and token["character"] == character and \
                token["type"] == token_type:
            return token
    return None


def changes_for_uri(workspace_edit, uri):
    changes = workspace_edit.get("changes", {})
    return changes.get(uri, [])


source = "\n".join([
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
        "const { Answer, Get } = @import(\"./dep.mog\")",
        "print(Get())",
        "print(Answer)",
        ""
    ])
    import_path = Path(tmpdir) / "import_sample.mog"
    import_path.write_text(import_source, encoding="utf-8")
    import_uri = import_path.resolve().as_uri()
    import_path_line = import_source.splitlines()[0]
    import_path_character = import_path_line.index("./dep.mog") + 2
    import_keyword_character = import_path_line.index("@import") + 2
    alias_import_source = "\n".join([
        "const { Answer as Alias } = @import(\"./dep.mog\")",
        "print(Alias)",
        ""
    ])
    alias_import_path = Path(tmpdir) / "alias_import_sample.mog"
    alias_import_path.write_text(alias_import_source, encoding="utf-8")
    alias_import_uri = alias_import_path.resolve().as_uri()
    member_source = "\n".join([
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
    collection_source = "\n".join([
        "var arr Array<i32> = Array<i32>()",
        "arr.push(1)",
        "var dict Dict<str, i32> = Dict<str, i32>()",
        "dict.set(\"a\", 1)",
        "var set Set<str> = Set<str>()",
        "set.add(\"x\")",
        "print(arr.push(2))",
        "print(dict.keys())",
        "print(set.union(set))",
        ""
    ])
    collection_path = Path(tmpdir) / "collection_sample.mog"
    collection_path.write_text(collection_source, encoding="utf-8")
    collection_uri = collection_path.resolve().as_uri()
    imported_state_source = "\n".join([
        "type GameState struct {",
        "    birdY f64",
        "    spawnTimer f64",
        "}",
        ""
    ])
    imported_state_path = Path(tmpdir) / "imported_state.mog"
    imported_state_path.write_text(imported_state_source, encoding="utf-8")
    imported_state_uri = imported_state_path.resolve().as_uri()
    imported_member_source = "\n".join([
        "const { GameState } = @import(\"./imported_state.mog\")",
        "fn update(state GameState) void {",
        "    state.",
        "}",
        ""
    ])
    imported_member_path = Path(tmpdir) / "imported_member_sample.mog"
    imported_member_path.write_text(imported_member_source, encoding="utf-8")
    imported_member_uri = imported_member_path.resolve().as_uri()
    type_definition_source = "\n".join([
        "type Pipe struct {",
        "    x f64",
        "}",
        "fn makePipe(x f64) Pipe {",
        "    var pipes Array<Pipe> = []",
        "    return makePipe(x)",
        "}",
        ""
    ])
    type_definition_path = Path(tmpdir) / "type_definition_sample.mog"
    type_definition_path.write_text(type_definition_source, encoding="utf-8")
    type_definition_uri = type_definition_path.resolve().as_uri()
    module_member_source = "\n".join([
        "const dep = @import(\"./dep.mog\")",
        "print(dep.Ans)",
        ""
    ])
    module_member_path = Path(tmpdir) / "module_member_sample.mog"
    module_member_path.write_text(module_member_source, encoding="utf-8")
    module_member_uri = module_member_path.resolve().as_uri()
    type_context_source = "\n".join([
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
        "fn Add(a i32, b i32) i32 {",
        "    return a + b",
        "}",
        "const value i32 = Add(1,",
        ""
    ])
    signature_fail_path = Path(tmpdir) / "signature_fail_sample.mog"
    signature_fail_path.write_text(signature_fail_source, encoding="utf-8")
    signature_fail_uri = signature_fail_path.resolve().as_uri()
    builtin_source = "\n".join([
        "const value f64 = sqrt(9.0)",
        "print(value)",
        ""
    ])
    builtin_path = Path(tmpdir) / "builtin_sample.mog"
    builtin_path.write_text(builtin_source, encoding="utf-8")
    builtin_uri = builtin_path.resolve().as_uri()
    parse_fail_source = "\n".join([
        "fn broken(",
        ""
    ])
    parse_fail_path = Path(tmpdir) / "parse_fail.mog"
    parse_fail_path.write_text(parse_fail_source, encoding="utf-8")
    parse_fail_uri = parse_fail_path.resolve().as_uri()
    undefined_source = "\n".join([
        "fn main() void {",
        "    app.update(1)",
        "    asdasdas.update(1)",
        "}",
        ""
    ])
    undefined_path = Path(tmpdir) / "undefined_receiver_sample.mog"
    undefined_path.write_text(undefined_source, encoding="utf-8")
    undefined_uri = undefined_path.resolve().as_uri()
    special_builtin_source = "\n".join([
        "var keys Set<str> = Set()",
        "var text str = str(42)",
        "print(type(text))",
        ""
    ])
    special_builtin_path = Path(tmpdir) / "special_builtin_sample.mog"
    special_builtin_path.write_text(special_builtin_source, encoding="utf-8")
    special_builtin_uri = special_builtin_path.resolve().as_uri()

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
        semantic_provider = caps.get("semanticTokensProvider")
        if not isinstance(semantic_provider, dict):
            raise AssertionError("initialize response missing semanticTokensProvider")
        if semantic_provider.get("full") is not True:
            raise AssertionError(f"semantic tokens should support full requests: {semantic_provider}")
        semantic_legend = semantic_provider.get("legend")
        if not isinstance(semantic_legend, dict):
            raise AssertionError(f"semantic tokens should include a legend: {semantic_provider}")
        if semantic_legend.get("tokenTypes") != [
                "type", "function", "method", "property", "parameter", "variable"]:
            raise AssertionError(f"unexpected semantic token types: {semantic_legend}")
        if semantic_legend.get("tokenModifiers") != ["declaration", "readonly"]:
            raise AssertionError(f"unexpected semantic token modifiers: {semantic_legend}")

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
                    "uri": collection_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": collection_source
                }
            }
        })
        collection_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == collection_uri,
        )
        if collection_diagnostics["params"]["diagnostics"]:
            raise AssertionError("expected collection sample to stay diagnostics-free")
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
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": builtin_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": builtin_source
                }
            }
        })
        builtin_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == builtin_uri,
        )
        if builtin_diagnostics["params"]["diagnostics"]:
            raise AssertionError("expected builtin sample to stay diagnostics-free")
        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": undefined_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": undefined_source
                }
            }
        })
        undefined_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == undefined_uri,
        )
        published_undefined = undefined_diagnostics["params"]["diagnostics"]
        if len(published_undefined) != 2:
            raise AssertionError(
                f"expected two undefined identifier diagnostics, got {published_undefined}")
        if published_undefined[0]["message"] != "Type error: unknown identifier 'app'.":
            raise AssertionError(
                f"unexpected first undefined identifier diagnostic: {published_undefined[0]}")
        if published_undefined[0]["range"]["start"] != {"line": 1, "character": 4}:
            raise AssertionError(
                f"first undefined identifier diagnostic should point at app: {published_undefined[0]}")
        if published_undefined[1]["message"] != "Type error: unknown identifier 'asdasdas'.":
            raise AssertionError(
                f"unexpected second undefined identifier diagnostic: {published_undefined[1]}")
        if published_undefined[1]["range"]["start"] != {"line": 2, "character": 4}:
            raise AssertionError(
                f"second undefined identifier diagnostic should point at asdasdas: {published_undefined[1]}")
        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": special_builtin_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": special_builtin_source
                }
            }
        })
        special_builtin_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == special_builtin_uri,
        )
        if special_builtin_diagnostics["params"]["diagnostics"]:
            raise AssertionError(
                f"special builtin sample should stay diagnostics-free: {special_builtin_diagnostics['params']['diagnostics']}")

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
                    "line": 6,
                    "character": 6
                }
            }
        })
        definition = read_until(proc, lambda msg: msg.get("id") == 3)
        result = definition["result"]
        if result["uri"] != uri:
            raise AssertionError("definition should stay within the same file")
        if result["range"]["start"]["line"] != 4 or \
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
                    "line": 6,
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
        if references["result"][0]["range"]["start"]["line"] != 4:
            raise AssertionError("references should include the declaration first")
        if references["result"][1]["range"]["start"]["line"] != 6:
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
                    "line": 6,
                    "character": 6
                }
            }
        })
        hover = read_until(proc, lambda msg: msg.get("id") == 5)
        if hover["result"]["contents"]["kind"] != "markdown":
            raise AssertionError(f"unexpected hover markup kind: {hover['result']}")
        hover_value = hover["result"]["contents"]["value"]
        if hover_value != "```mog\nconst Value i32\n```":
            raise AssertionError(f"unexpected hover payload: {hover['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 5.1,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {
                    "uri": builtin_uri
                },
                "position": {
                    "line": 0,
                    "character": 20
                }
            }
        })
        builtin_hover = read_until(proc, lambda msg: msg.get("id") == 5.1)
        builtin_hover_value = builtin_hover["result"]["contents"]["value"]
        if builtin_hover_value != "```mog\n(function) fn sqrt(f64) f64\n```":
            raise AssertionError(f"unexpected builtin hover payload: {builtin_hover['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 5.25,
            "method": "textDocument/semanticTokens/full",
            "params": {
                "textDocument": {
                    "uri": uri
                }
            }
        })
        semantic_tokens = read_until(proc, lambda msg: msg.get("id") == 5.25)
        main_tokens = decode_semantic_tokens(
            semantic_tokens["result"],
            semantic_legend["tokenTypes"],
            semantic_legend["tokenModifiers"],
        )
        add_decl = find_semantic_token(main_tokens, 0, 3, "function")
        if add_decl is None or "declaration" not in add_decl["modifiers"]:
            raise AssertionError(f"expected function declaration semantic token: {main_tokens}")
        if find_semantic_token(main_tokens, 0, 7, "parameter") is None:
            raise AssertionError(f"expected parameter semantic token: {main_tokens}")
        value_decl = find_semantic_token(main_tokens, 4, 6, "variable")
        if value_decl is None or "declaration" not in value_decl["modifiers"] or \
                "readonly" not in value_decl["modifiers"]:
            raise AssertionError(f"expected readonly const declaration semantic token: {main_tokens}")
        if find_semantic_token(main_tokens, 4, 18, "function") is None:
            raise AssertionError(f"expected free-function call semantic token: {main_tokens}")
        value_use = find_semantic_token(main_tokens, 6, 6, "variable")
        if value_use is None or "readonly" not in value_use["modifiers"]:
            raise AssertionError(f"expected readonly const use semantic token: {main_tokens}")
        if find_semantic_token(main_tokens, 6, 0, "function") is None:
            raise AssertionError(f"expected print semantic token: {main_tokens}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 5.4,
            "method": "textDocument/semanticTokens/full",
            "params": {
                "textDocument": {
                    "uri": builtin_uri
                }
            }
        })
        builtin_semantic_tokens = read_until(proc, lambda msg: msg.get("id") == 5.4)
        builtin_tokens = decode_semantic_tokens(
            builtin_semantic_tokens["result"],
            semantic_legend["tokenTypes"],
            semantic_legend["tokenModifiers"],
        )
        if find_semantic_token(builtin_tokens, 0, 18, "function") is None:
            raise AssertionError(f"expected builtin stdlib semantic token: {builtin_tokens}")
        if find_semantic_token(builtin_tokens, 1, 0, "function") is None:
            raise AssertionError(f"expected builtin print semantic token: {builtin_tokens}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 5.5,
            "method": "textDocument/semanticTokens/full",
            "params": {
                "textDocument": {
                    "uri": member_uri
                }
            }
        })
        member_semantic_tokens = read_until(proc, lambda msg: msg.get("id") == 5.5)
        member_tokens = decode_semantic_tokens(
            member_semantic_tokens["result"],
            semantic_legend["tokenTypes"],
            semantic_legend["tokenModifiers"],
        )
        property_decl = find_semantic_token(member_tokens, 1, 4, "property")
        if property_decl is None or "declaration" not in property_decl["modifiers"]:
            raise AssertionError(f"expected property declaration semantic token: {member_tokens}")
        method_decl = find_semantic_token(member_tokens, 3, 7, "method")
        if method_decl is None or "declaration" not in method_decl["modifiers"]:
            raise AssertionError(f"expected method declaration semantic token: {member_tokens}")
        if find_semantic_token(member_tokens, 8, 15, "property") is None:
            raise AssertionError(f"expected property access semantic token: {member_tokens}")
        if find_semantic_token(member_tokens, 8, 27, "method") is None:
            raise AssertionError(f"expected method call semantic token: {member_tokens}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 5.75,
            "method": "textDocument/semanticTokens/full",
            "params": {
                "textDocument": {
                    "uri": type_definition_uri
                }
            }
        })
        type_semantic_tokens = read_until(proc, lambda msg: msg.get("id") == 5.75)
        type_tokens = decode_semantic_tokens(
            type_semantic_tokens["result"],
            semantic_legend["tokenTypes"],
            semantic_legend["tokenModifiers"],
        )
        pipe_decl = find_semantic_token(type_tokens, 0, 5, "type")
        if pipe_decl is None or "declaration" not in pipe_decl["modifiers"]:
            raise AssertionError(f"expected type declaration semantic token: {type_tokens}")
        if find_semantic_token(type_tokens, 1, 6, "type") is None:
            raise AssertionError(f"expected built-in type semantic token: {type_tokens}")
        if find_semantic_token(type_tokens, 3, 19, "type") is None:
            raise AssertionError(f"expected custom type reference semantic token: {type_tokens}")
        generic_pipe_ref = find_semantic_token(type_tokens, 4, 20, "type")
        if generic_pipe_ref is None:
            raise AssertionError(f"expected generic custom type reference semantic token: {type_tokens}")
        if generic_pipe_ref["length"] != 4:
            raise AssertionError(f"expected generic type semantic token length to match 'Pipe': {type_tokens}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": uri
                },
                "position": {
                    "line": 2,
                    "character": 16
                }
            }
        })
        completion = read_until(proc, lambda msg: msg.get("id") == 6)
        labels = [item["label"] for item in completion["result"]]
        if "local" not in labels:
            raise AssertionError(f"expected local completion item: {completion['result']}")
        local_item = next(item for item in completion["result"] if item["label"] == "local")
        if local_item.get("detail") != "var local i32":
            raise AssertionError(f"unexpected local completion detail: {local_item}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 6.1,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": builtin_uri
                },
                "position": {
                    "line": 2,
                    "character": 0
                }
            }
        })
        builtin_completion = read_until(proc, lambda msg: msg.get("id") == 6.1)
        builtin_labels = [item["label"] for item in builtin_completion["result"]]
        if "sqrt" not in builtin_labels or "print" not in builtin_labels:
            raise AssertionError(f"expected builtin completion items: {builtin_completion['result']}")
        sqrt_item = next(item for item in builtin_completion["result"] if item["label"] == "sqrt")
        print_item = next(item for item in builtin_completion["result"] if item["label"] == "print")
        if sqrt_item.get("detail") != "fn sqrt(f64) f64":
            raise AssertionError(f"unexpected sqrt completion detail: {sqrt_item}")
        if print_item.get("detail") != "fn print(any) void":
            raise AssertionError(f"unexpected print completion detail: {print_item}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": member_uri
                },
                "position": {
                    "line": 8,
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
                    "line": 8,
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
            "id": 7.31,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": collection_uri
                },
                "position": {
                    "line": 6,
                    "character": 10
                }
            }
        })
        array_completion = read_until(proc, lambda msg: msg.get("id") == 7.31)
        array_labels = [item["label"] for item in array_completion["result"]]
        for label in ["clear", "first", "has", "insert", "isEmpty",
                      "last", "pop", "push", "remove", "size"]:
            if label not in array_labels:
                raise AssertionError(f"expected array completion item {label}: {array_completion['result']}")
        if "arr" in array_labels or "dict" in array_labels:
            raise AssertionError(f"array member completion should exclude scope items: {array_completion['result']}")
        array_push_item = next(item for item in array_completion["result"] if item["label"] == "push")
        if array_push_item.get("detail") != "fn push(i32) i64":
            raise AssertionError(f"unexpected array push completion detail: {array_push_item}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7.32,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": collection_uri
                },
                "position": {
                    "line": 7,
                    "character": 11
                }
            }
        })
        dict_completion = read_until(proc, lambda msg: msg.get("id") == 7.32)
        dict_labels = [item["label"] for item in dict_completion["result"]]
        for label in ["clear", "get", "getOr", "has", "isEmpty",
                      "keys", "remove", "set", "size", "values"]:
            if label not in dict_labels:
                raise AssertionError(f"expected dict completion item {label}: {dict_completion['result']}")
        dict_keys_item = next(item for item in dict_completion["result"] if item["label"] == "keys")
        if dict_keys_item.get("detail") != "fn keys() Array<str>":
            raise AssertionError(f"unexpected dict keys completion detail: {dict_keys_item}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7.33,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": collection_uri
                },
                "position": {
                    "line": 8,
                    "character": 10
                }
            }
        })
        set_completion = read_until(proc, lambda msg: msg.get("id") == 7.33)
        set_labels = [item["label"] for item in set_completion["result"]]
        for label in ["add", "clear", "difference", "has", "intersect",
                      "isEmpty", "remove", "size", "toArray", "union"]:
            if label not in set_labels:
                raise AssertionError(f"expected set completion item {label}: {set_completion['result']}")
        set_union_item = next(item for item in set_completion["result"] if item["label"] == "union")
        if set_union_item.get("detail") != "fn union(Set<str>) Set<str>":
            raise AssertionError(f"unexpected set union completion detail: {set_union_item}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7.34,
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": "file://" + os.path.abspath("tests/lsp_collection_incomplete.mog"),
                    "languageId": "mog",
                    "version": 1,
                    "text": collection_source.replace("print(arr.push(2))", "arr.")
                }
            }
        })
        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7.35,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": "file://" + os.path.abspath("tests/lsp_collection_incomplete.mog")
                },
                "position": {
                    "line": 6,
                    "character": 4
                }
            }
        })
        incomplete_collection_completion = read_until(proc, lambda msg: msg.get("id") == 7.35)
        incomplete_collection_labels = [item["label"] for item in incomplete_collection_completion["result"]]
        for label in ["clear", "pop", "push", "size"]:
            if label not in incomplete_collection_labels:
                raise AssertionError(
                    f"expected incomplete collection completion item {label}: {incomplete_collection_completion['result']}")
        if "arr" in incomplete_collection_labels or "print" in incomplete_collection_labels:
            raise AssertionError(
                f"incomplete collection completion should exclude scope items: {incomplete_collection_completion['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": imported_state_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": imported_state_source
                }
            }
        })
        imported_state_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == imported_state_uri,
        )
        if imported_state_diagnostics["params"]["diagnostics"]:
            raise AssertionError("expected imported state module to stay diagnostics-free")

        send_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": imported_member_uri,
                    "languageId": "mog",
                    "version": 1,
                    "text": imported_member_source
                }
            }
        })
        imported_member_diagnostics = read_until(
            proc,
            lambda msg: msg.get("method") == "textDocument/publishDiagnostics" and
            msg.get("params", {}).get("uri") == imported_member_uri,
        )

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7.4,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": imported_member_uri
                },
                "position": {
                    "line": 2,
                    "character": 10
                }
            }
        })
        imported_member_completion = read_until(proc, lambda msg: msg.get("id") == 7.4)
        imported_member_labels = [item["label"] for item in imported_member_completion["result"]]
        if "birdY" not in imported_member_labels or "spawnTimer" not in imported_member_labels:
            raise AssertionError(
                f"expected imported member completion items: {imported_member_completion['result']}")
        if "update" in imported_member_labels or "state" in imported_member_labels:
            raise AssertionError(
                f"imported member completion should exclude scope items: {imported_member_completion['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 7.5,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {
                    "uri": module_member_uri
                },
                "position": {
                    "line": 1,
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
                    "line": 2,
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
                    "line": 8,
                    "character": 16
                }
            }
        })
        member_hover = read_until(proc, lambda msg: msg.get("id") == 8)
        member_hover_value = member_hover["result"]["contents"]["value"]
        if member_hover_value != "```mog\n(property) value i32\n```":
            raise AssertionError(f"unexpected member hover payload: {member_hover['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 8.1,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {
                    "uri": collection_uri
                },
                "position": {
                    "line": 7,
                    "character": 12
                }
            }
        })
        collection_hover = read_until(proc, lambda msg: msg.get("id") == 8.1)
        collection_hover_value = collection_hover["result"]["contents"]["value"]
        if collection_hover_value != "```mog\n(method) fn keys() Array<str>\n```":
            raise AssertionError(f"unexpected collection hover payload: {collection_hover['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 9,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {
                    "uri": member_uri
                },
                "position": {
                    "line": 8,
                    "character": 28
                }
            }
        })
        member_definition = read_until(proc, lambda msg: msg.get("id") == 9)
        member_result = member_definition["result"]
        if member_result["uri"] != member_uri:
            raise AssertionError("member definition should stay in the same module")
        if member_result["range"]["start"]["line"] != 3 or \
                member_result["range"]["start"]["character"] != 7:
            raise AssertionError(f"unexpected member definition range: {member_result['range']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 9.25,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {
                    "uri": member_uri
                },
                "position": {
                    "line": 1,
                    "character": 5
                }
            }
        })
        field_declaration_definition = read_until(proc, lambda msg: msg.get("id") == 9.25)
        field_declaration_result = field_declaration_definition["result"]
        if not isinstance(field_declaration_result, list):
            raise AssertionError(
                f"field declaration definition should return a location list: {field_declaration_result}")
        if len(field_declaration_result) != 3:
            raise AssertionError(
                f"field declaration definition should include declaration and references: {field_declaration_result}")
        if field_declaration_result[0]["uri"] != member_uri or \
                field_declaration_result[0]["range"]["start"]["line"] != 1:
            raise AssertionError(
                f"field declaration definition should start at the declaration: {field_declaration_result}")
        if field_declaration_result[1]["uri"] != member_uri or \
                field_declaration_result[1]["range"]["start"]["line"] != 4:
            raise AssertionError(
                f"field declaration definition should include this.field usage: {field_declaration_result}")
        if field_declaration_result[2]["uri"] != member_uri or \
                field_declaration_result[2]["range"]["start"]["line"] != 8:
            raise AssertionError(
                f"field declaration definition should include object.field usage: {field_declaration_result}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 9.5,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {
                    "uri": type_definition_uri
                },
                "position": {
                    "line": 3,
                    "character": 20
                }
            }
        })
        type_definition = read_until(proc, lambda msg: msg.get("id") == 9.5)
        type_result = type_definition["result"]
        if type_result["uri"] != type_definition_uri:
            raise AssertionError("type definition should stay in the same module")
        if type_result["range"]["start"]["line"] != 0 or \
                type_result["range"]["start"]["character"] != 5:
            raise AssertionError(f"unexpected type definition range: {type_result['range']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 9.6,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {
                    "uri": type_definition_uri
                },
                "position": {
                    "line": 0,
                    "character": 6
                }
            }
        })
        type_declaration_definition = read_until(proc, lambda msg: msg.get("id") == 9.6)
        type_declaration_result = type_declaration_definition["result"]
        if not isinstance(type_declaration_result, list):
            raise AssertionError(
                f"type declaration definition should return a location list: {type_declaration_result}")
        if len(type_declaration_result) != 3:
            raise AssertionError(
                f"type declaration definition should include declaration and references: {type_declaration_result}")
        if type_declaration_result[0]["uri"] != type_definition_uri or \
                type_declaration_result[0]["range"]["start"]["line"] != 0:
            raise AssertionError(
                f"type declaration definition should start at the declaration: {type_declaration_result}")
        if type_declaration_result[1]["uri"] != type_definition_uri or \
                type_declaration_result[1]["range"]["start"]["line"] != 3:
            raise AssertionError(
                f"type declaration definition should include type references: {type_declaration_result}")
        if type_declaration_result[2]["uri"] != type_definition_uri or \
                type_declaration_result[2]["range"]["start"]["line"] != 4:
            raise AssertionError(
                f"type declaration definition should include generic type references: {type_declaration_result}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 10,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {
                    "uri": import_uri
                },
                "position": {
                    "line": 2,
                    "character": 7
                }
            }
        })
        cross_definition = read_until(proc, lambda msg: msg.get("id") == 10)
        cross_result = cross_definition["result"]
        if cross_result["uri"] != module_uri:
            raise AssertionError("import definition should jump to the imported module")
        if cross_result["range"]["start"]["line"] != 3 or \
                cross_result["range"]["start"]["character"] != 6:
            raise AssertionError(f"unexpected cross-file definition range: {cross_result['range']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 10.25,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {
                    "uri": import_uri
                },
                "position": {
                    "line": 0,
                    "character": import_path_character
                }
            }
        })
        import_path_definition = read_until(proc, lambda msg: msg.get("id") == 10.25)
        import_path_result = import_path_definition["result"]
        if import_path_result["uri"] != module_uri:
            raise AssertionError(
                f"import path definition should jump to imported source file: {import_path_result}")
        if import_path_result["range"]["start"]["line"] != 0 or \
                import_path_result["range"]["start"]["character"] != 0:
            raise AssertionError(
                f"import path definition should jump to file start: {import_path_result['range']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 10.5,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {
                    "uri": import_uri
                },
                "position": {
                    "line": 0,
                    "character": import_keyword_character
                }
            }
        })
        import_keyword_definition = read_until(proc, lambda msg: msg.get("id") == 10.5)
        if import_keyword_definition["result"] is not None:
            raise AssertionError(
                f"import keyword definition should stay unresolved: {import_keyword_definition['result']}")

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
        if "fn" not in parse_labels or "while" not in parse_labels or \
                "sqrt" not in parse_labels or "print" not in parse_labels:
            raise AssertionError(f"expected keyword and builtin completions on parse failure: {parse_completion['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 11.5,
            "method": "textDocument/signatureHelp",
            "params": {
                "textDocument": {
                    "uri": signature_uri
                },
                "position": {
                    "line": 3,
                    "character": 27
                }
            }
        })
        signature_help = read_until(proc, lambda msg: msg.get("id") == 11.5)
        if signature_help["result"]["activeParameter"] != 1:
            raise AssertionError(f"unexpected direct signature help payload: {signature_help['result']}")
        if signature_help["result"]["signatures"][0]["label"] != "fn Add(a i32, b i32) i32":
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
                    "line": 3,
                    "character": 25
                }
            }
        })
        parse_signature_help = read_until(proc, lambda msg: msg.get("id") == 11.6)
        if parse_signature_help["result"]["activeParameter"] != 1:
            raise AssertionError(f"unexpected parse-fail signature help payload: {parse_signature_help['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 11.55,
            "method": "textDocument/signatureHelp",
            "params": {
                "textDocument": {
                    "uri": builtin_uri
                },
                "position": {
                    "line": 0,
                    "character": 24
                }
            }
        })
        builtin_signature_help = read_until(proc, lambda msg: msg.get("id") == 11.55)
        if builtin_signature_help["result"]["activeParameter"] != 0:
            raise AssertionError(f"unexpected builtin signature help payload: {builtin_signature_help['result']}")
        if builtin_signature_help["result"]["signatures"][0]["label"] != "fn sqrt(f64) f64":
            raise AssertionError(f"unexpected builtin signature label: {builtin_signature_help['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 11.57,
            "method": "textDocument/signatureHelp",
            "params": {
                "textDocument": {
                    "uri": collection_uri
                },
                "position": {
                    "line": 8,
                    "character": 19
                }
            }
        })
        collection_signature_help = read_until(proc, lambda msg: msg.get("id") == 11.57)
        if collection_signature_help["result"]["activeParameter"] != 0:
            raise AssertionError(
                f"unexpected collection signature help payload: {collection_signature_help['result']}")
        if collection_signature_help["result"]["signatures"][0]["label"] != \
                "fn union(Set<str>) Set<str>":
            raise AssertionError(
                f"unexpected collection signature label: {collection_signature_help['result']}")

        send_message(proc, {
            "jsonrpc": "2.0",
            "id": 11.56,
            "method": "textDocument/signatureHelp",
            "params": {
                "textDocument": {
                    "uri": builtin_uri
                },
                "position": {
                    "line": 1,
                    "character": 7
                }
            }
        })
        print_signature_help = read_until(proc, lambda msg: msg.get("id") == 11.56)
        if print_signature_help["result"]["activeParameter"] != 0:
            raise AssertionError(f"unexpected print signature help payload: {print_signature_help['result']}")
        if print_signature_help["result"]["signatures"][0]["label"] != "fn print(any) void":
            raise AssertionError(f"unexpected print signature label: {print_signature_help['result']}")

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
                    "line": 6,
                    "character": 6
                }
            }
        })
        prepare_local = read_until(proc, lambda msg: msg.get("id") == 13)
        local_range = prepare_local["result"]["range"]
        if local_range["start"]["line"] != 6 or local_range["start"]["character"] != 6:
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
                    "line": 8,
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
                    "line": 6,
                    "character": 6
                },
                "newName": "Result"
            }
        })
        local_rename = read_until(proc, lambda msg: msg.get("id") == 15)
        local_changes = changes_for_uri(local_rename["result"], uri)
        if len(local_changes) != 2:
            raise AssertionError(f"unexpected same-file rename edits: {local_rename['result']}")
        if not any(edit["newText"] == "Result" and edit["range"]["start"]["line"] == 4
                   for edit in local_changes):
            raise AssertionError(f"expected declaration rename edit: {local_changes}")
        if not any(edit["newText"] == "Result" and edit["range"]["start"]["line"] == 6
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
                    "line": 2,
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
                   edit["range"]["start"]["line"] == 0 and
                   edit["range"]["start"]["character"] == 8
                   for edit in import_local_changes):
            raise AssertionError(f"expected importer alias insertion edit: {import_local_changes}")
        if not any(edit["newText"] == "LocalAnswer" and
                   edit["range"]["start"]["line"] == 2 and
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
                    "line": 3,
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
                   edit["range"]["start"]["line"] == 0 and
                   edit["range"]["start"]["character"] == 8
                   for edit in importer_changes):
            raise AssertionError(f"expected importer binding export rename: {importer_changes}")
        if not any(edit["newText"] == "FinalAnswer" and
                   edit["range"]["start"]["line"] == 2 and
                   edit["range"]["start"]["character"] == 6
                   for edit in importer_changes):
            raise AssertionError(f"expected importer usage rename: {importer_changes}")
        if alias_changes[0]["newText"] != "FinalAnswer" or \
                alias_changes[0]["range"]["start"]["line"] != 0 or \
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
                    "line": 3,
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
