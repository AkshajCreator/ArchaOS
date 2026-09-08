#!/usr/bin/env python3
"""
ArchaOS NIM Host Proxy — tools/nim_proxy.py
Listens on localhost:8080 and forwards /v1/chat/completions requests
from ArchaOS (plain HTTP) → NVIDIA NIM (HTTPS).

Usage:
  NVIDIA_API_KEY=nvapi-xxxx python3 tools/nim_proxy.py

Or set a local Ollama / LM Studio endpoint:
  NIM_BACKEND=http://localhost:11434 python3 tools/nim_proxy.py
"""

import os, sys, http.server, urllib.request, json, traceback

API_KEY  = os.environ.get("NVIDIA_API_KEY", "")
BACKEND  = os.environ.get("NIM_BACKEND", "https://integrate.api.nvidia.com")
PORT     = int(os.environ.get("NIM_PORT", "8080"))

class ProxyHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[proxy] {fmt % args}")

    def do_POST(self):
        clen = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(clen)

        # Smart model aliasing based on backend provider
        try:
            payload = json.loads(body)
            model = payload.get("model", "")
            if "groq.com" in BACKEND:
                payload["model"] = "openai/gpt-oss-20b"
            elif "localhost:11434" in BACKEND or "127.0.0.1:11434" in BACKEND:
                payload["model"] = "deepseek-r1"

            # Inject a concise system prompt suitable for a retro OS terminal
            msgs = payload.get("messages", [])
            if not any(m.get("role") == "system" for m in msgs):
                msgs.insert(0, {
                    "role": "system",
                    "content": "You are ArchaOS Assistant, a retro OS AI. Answer concisely in 2 to 4 sentences. Be direct and avoid unnecessary fluff."
                })
                payload["messages"] = msgs

            payload["max_tokens"] = max(payload.get("max_tokens", 256), 256)
            body = json.dumps(payload).encode()
        except Exception:
            pass

        path = self.path
        if BACKEND.rstrip("/").endswith("/v1") and path.startswith("/v1/"):
            path = path[3:]  # strip leading /v1 so we don't get /v1/v1/...
        url = BACKEND.rstrip("/") + path

        req = urllib.request.Request(url, data=body, method="POST")
        req.add_header("Content-Type", "application/json")
        req.add_header("User-Agent", "ArchaOS-Client/1.0")
        if API_KEY:
            req.add_header("Authorization", f"Bearer {API_KEY}")

        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                resp_body = resp.read()
                # Clean and strip <think>...</think> reasoning blocks
                try:
                    j = json.loads(resp_body)
                    if "choices" in j and len(j["choices"]) > 0:
                        content = j["choices"][0]["message"].get("content", "")
                        import re
                        content = re.sub(r'(<think>|\\u003cthink\\u003e)[\s\S]*?(</think>|\\u003c/think\\u003e)', '', content, flags=re.DOTALL).lstrip()
                        j["choices"][0]["message"]["content"] = content
                        resp_body = json.dumps(j).encode()
                        print(f"\n[AI Response] {content}\n")
                except Exception:
                    pass

                self.send_response(resp.status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(resp_body)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(resp_body)
        except urllib.error.HTTPError as e:
            err_body = e.read()
            print(f"\n[proxy error] HTTP {e.code}: {err_body.decode(errors='replace')}\n")
            self.send_response(e.code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(err_body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(err_body)
        except Exception as e:
            traceback.print_exc()
            err = json.dumps({"error": str(e)}).encode()
            self.send_response(500)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(err)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(err)

    def do_GET(self):
        msg = b'{"status":"ArchaOS NIM Proxy running"}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(msg)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(msg)


if __name__ == "__main__":
    if not API_KEY and "localhost" not in BACKEND and "127.0" not in BACKEND:
        print("[WARNING] NVIDIA_API_KEY is not set. Set it with:")
        print("  export NVIDIA_API_KEY=nvapi-xxxx")
    print(f"[proxy] Starting on 0.0.0.0:{PORT}")
    print(f"[proxy] Backend: {BACKEND}")
    http.server.HTTPServer.allow_reuse_address = True
    server = http.server.HTTPServer(("0.0.0.0", PORT), ProxyHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[proxy] Stopped.")
