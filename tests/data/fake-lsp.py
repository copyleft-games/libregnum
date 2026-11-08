#!/usr/bin/env python3
# fake-lsp.py — a Content-Length JSON-RPC language server for tests.
# Speaks just enough LSP for tests/test-lsp-client.c. Nothing but framed
# messages goes to stdout.

import json
import sys

documents = {}


def read_message():
    length = None
    while True:
        line = sys.stdin.buffer.readline()
        if line == b"":
            return None
        if line in (b"\r\n", b"\n"):
            break
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1].strip())
    if length is None:
        return None
    body = sys.stdin.buffer.read(length)
    if len(body) != length:
        return None
    return json.loads(body.decode("utf-8"))


def write_message(obj):
    data = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    header = "Content-Length: %d\r\n\r\n" % len(data)
    sys.stdout.buffer.write(header.encode("ascii"))
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def reply(msg, result):
    write_message({"id": msg.get("id"), "result": result})


def uri_of(msg):
    params = msg.get("params") or {}
    td = params.get("textDocument") or {}
    return td.get("uri")


def location(uri):
    return {
        "uri": uri,
        "range": {
            "start": {"line": 1, "character": 2},
            "end": {"line": 1, "character": 5},
        },
    }


def publish(uri):
    write_message({
        "method": "textDocument/publishDiagnostics",
        "params": {
            "uri": uri,
            "diagnostics": [{
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 0, "character": 4},
                },
                "severity": 1,
                "message": "boom",
                "source": "fake",
                "code": "E1",
            }],
        },
    })


def main():
    while True:
        msg = read_message()
        if msg is None:
            return 0
        method = msg.get("method")
        if method is None:
            continue
        if "id" not in msg:
            if method == "textDocument/didOpen":
                uri = uri_of(msg)
                text = ((msg.get("params") or {}).get("textDocument") or {}).get("text", "")
                documents[uri] = text
                if uri:
                    publish(uri)
            elif method == "textDocument/didChange":
                uri = uri_of(msg)
                changes = (msg.get("params") or {}).get("contentChanges") or []
                if uri and changes:
                    documents[uri] = changes[-1].get("text", "")
            elif method == "textDocument/didClose":
                documents.pop(uri_of(msg), None)
            elif method == "exit":
                return 0
            continue
        if method == "initialize":
            reply(msg, {
                "capabilities": {
                    "positionEncoding": "utf-16",
                    "textDocumentSync": 1,
                    "completionProvider": {"triggerCharacters": ["."]},
                    "hoverProvider": True,
                    "definitionProvider": True,
                    "referencesProvider": True,
                    "renameProvider": True,
                    "documentFormattingProvider": True,
                },
                "serverInfo": {"name": "fake-lsp", "version": "0"},
            })
        elif method == "shutdown":
            reply(msg, None)
        elif method == "textDocument/completion":
            reply(msg, {"items": [{
                "label": "alpha",
                "kind": 3,
                "detail": "var",
                "insertText": "alpha",
            }]})
        elif method == "textDocument/hover":
            reply(msg, {"contents": "a hover"})
        elif method == "textDocument/definition":
            reply(msg, location(uri_of(msg)))
        elif method == "textDocument/references":
            reply(msg, [location(uri_of(msg))])
        elif method == "textDocument/rename":
            uri = uri_of(msg)
            new_name = (msg.get("params") or {}).get("newName", "renamed")
            reply(msg, {"changes": {uri: [{
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 0, "character": 1},
                },
                "newText": new_name,
            }]}})
        elif method == "textDocument/formatting":
            reply(msg, [{
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 0, "character": 1},
                },
                "newText": "X",
            }])
        else:
            write_message({
                "id": msg.get("id"),
                "error": {"code": -32601, "message": "method %s" % method},
            })
    return 0


if __name__ == "__main__":
    sys.exit(main())
