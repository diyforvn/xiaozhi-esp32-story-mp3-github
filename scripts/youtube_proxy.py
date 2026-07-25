#!/usr/bin/env python3
import os
import sys
import subprocess
import shlex
import urllib.parse
import json
from http.server import BaseHTTPRequestHandler, HTTPServer

HOST = "0.0.0.0"
PORT = int(os.environ.get("PORT", "18080"))
YTDLP = os.environ.get("YTDLP_BIN", "yt-dlp")
REQUEST_TIMEOUT_S = int(os.environ.get("YTDLP_TIMEOUT_S", "90"))

class Handler(BaseHTTPRequestHandler):
    def _build_ytdlp_commands(self, url):
        base = [
            YTDLP,
            "--socket-timeout",
            str(max(REQUEST_TIMEOUT_S // 2, 20)),
            "-f",
            "bestaudio/best",
            "--no-playlist",
            "--no-warnings",
            "--get-url",
            url,
        ]
        return [
            base + ["--extractor-args", "youtube:player_client=web"],
            base + ["--extractor-args", "youtube:player_client=ios"],
            base + ["--extractor-args", "youtube:player_client=web_embedded"],
        ]

    def do_GET(self):
        parsed = urllib.parse.urlsplit(self.path)
        if parsed.path == "/health":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ok"}).encode("utf-8"))
            return

        if parsed.path not in ["/proxy", "/search"]:
            self.send_response(404)
            self.end_headers()
            return

        query = urllib.parse.parse_qs(parsed.query)
        if parsed.path == "/proxy":
            url = query.get("url", [""])[0]
            if not url:
                self.send_response(400)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"error": "missing_url"}).encode("utf-8"))
                return
            cmd = self._build_ytdlp_command(url)
        else:
            q = query.get("q", [""])[0]
            if not q:
                self.send_response(400)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"error": "missing_query"}).encode("utf-8"))
                return
            cmd = self._build_ytdlp_command(f"ytsearch1:{q}")

        last_error = None
        last_stdout = None
        for attempt_cmd in self._build_ytdlp_commands(url if parsed.path == "/proxy" else f"ytsearch1:{q}"):
            try:
                proc = subprocess.run(attempt_cmd, capture_output=True, text=True, timeout=REQUEST_TIMEOUT_S + 10)
            except subprocess.TimeoutExpired:
                last_error = "yt_dlp_timeout"
                continue

            if proc.returncode == 0:
                audio_url = proc.stdout.strip().splitlines()[-1]
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"audio_url": audio_url, "status": "ok"}).encode("utf-8"))
                return

            last_error = (proc.stderr or proc.stdout or "failed").strip()
            last_stdout = proc.stdout.strip()

        self.send_response(502)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps({"error": "yt_dlp_failed", "detail": last_error}).encode("utf-8"))
        return

    def log_message(self, format, *args):
        return

if __name__ == "__main__":
    print(f"Starting YouTube proxy on http://{HOST}:{PORT}")
    server = HTTPServer((HOST, PORT), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
