import json
import os
import sys

import requests

URL = os.environ.get("MCP_URL", "http://127.0.0.1:13337/mcp")
SID_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".mcp_sid")


def _headers(sid=None):
    h = {
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
    }
    if sid:
        h["Mcp-Session-Id"] = sid
    return h


def _parse(resp):
    ct = resp.headers.get("content-type", "")
    if "text/event-stream" in ct:
        out = None
        for line in resp.text.splitlines():
            if line.startswith("data:"):
                try:
                    out = json.loads(line[5:].strip())
                except Exception:
                    pass
        return out if out is not None else {"raw": resp.text}
    try:
        return resp.json()
    except Exception:
        return {"status": resp.status_code, "raw": resp.text}


def init():
    r = requests.post(
        URL,
        headers=_headers(),
        json={
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-03-26",
                "capabilities": {},
                "clientInfo": {"name": "qoder", "version": "0.1"},
            },
        },
        timeout=60,
    )
    sid = r.headers.get("mcp-session-id")
    if not sid:
        sid = r.headers.get("Mcp-Session-Id")
    if sid:
        with open(SID_FILE, "w") as f:
            f.write(sid)
    requests.post(
        URL,
        headers=_headers(sid),
        json={"jsonrpc": "2.0", "method": "notifications/initialized"},
        timeout=30,
    )
    return sid, _parse(r)


def sid_get():
    if os.path.exists(SID_FILE):
        return open(SID_FILE).read().strip()
    sid, _ = init()
    return sid


def rpc(method, params=None, use_id=2):
    sid = sid_get()
    body = {"jsonrpc": "2.0", "method": method}
    if params is not None:
        body["params"] = params
    if use_id is not None:
        body["id"] = use_id
    r = requests.post(URL, headers=_headers(sid), json=body, timeout=180)
    if r.status_code in (400, 404) and sid:
        sid, _ = init()
        r = requests.post(URL, headers=_headers(sid), json=body, timeout=180)
    return _parse(r)


def call(tool, args, use_id=3):
    return rpc("tools/call", {"name": tool, "arguments": args}, use_id=use_id)


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
    if cmd == "init":
        print(init())
    elif cmd == "list":
        res = rpc("tools/list", {}, use_id=2)
        tools = (res or {}).get("result", {}).get("tools", [])
        for t in tools:
            print(t.get("name"), "-", (t.get("description") or "")[:100])
        print("count:", len(tools))
    elif cmd == "schema":
        res = rpc("tools/list", {}, use_id=2)
        tools = (res or {}).get("result", {}).get("tools", [])
        want = sys.argv[2:] or None
        for t in tools:
            if want and t.get("name") not in want:
                continue
            print("###", t.get("name"))
            print(json.dumps(t.get("inputSchema", {}), ensure_ascii=False))
    elif cmd == "call":
        tool = sys.argv[2]
        args = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
        res = call(tool, args)
        print(json.dumps(res, indent=2))
    else:
        print("usage: mcp_client.py [init|list|call <tool> '<json>']")
