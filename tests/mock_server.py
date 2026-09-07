#!/usr/bin/env python3
"""
High-concurrency Mock HTTP Server with full HTTP Range request support
for testing down download manager.
"""
import sys
import os
import re
import socketserver
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

class FastThreadingHTTPServer(ThreadingHTTPServer):
    daemon_threads = True

    def server_bind(self):
        # Override to prevent socket.getfqdn() reverse DNS hangs on macOS Darwin
        socketserver.TCPServer.server_bind(self)
        self.server_name = "127.0.0.1"
        self.server_port = self.server_address[1]

class RangeHTTPRequestHandler(BaseHTTPRequestHandler):
    def address_string(self):
        # Prevent slow reverse DNS resolution on macOS
        return self.client_address[0]
    def check_auth(self):
        if self.path.startswith("/secure/"):
            auth = self.headers.get("Authorization")
            if not auth or not auth.startswith("AWS4-HMAC-SHA256"):
                self.send_error(403, "Forbidden: AWS SigV4 signature required")
                return False
        return True

    def do_HEAD(self):
        if not self.check_auth():
            return
        filepath = self.translate_path(self.path)
        if not os.path.exists(filepath) or os.path.isdir(filepath):
            self.send_error(404, "File Not Found")
            return

        size = os.path.getsize(filepath)
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(size))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Disposition", f'attachment; filename="{os.path.basename(filepath)}"')
        self.end_headers()

    def do_GET(self):
        if not self.check_auth():
            return
        filepath = self.translate_path(self.path)
        if not os.path.exists(filepath) or os.path.isdir(filepath):
            self.send_error(404, "File Not Found")
            return

        total_size = os.path.getsize(filepath)
        range_header = self.headers.get("Range")

        if range_header:
            match = re.match(r"bytes=(\d+)-(\d+)?", range_header)
            if not match:
                self.send_error(400, "Bad Range Request")
                return

            start = int(match.group(1))
            end = int(match.group(2)) if match.group(2) else total_size - 1

            if start >= total_size:
                self.send_error(416, "Requested Range Not Satisfiable")
                return

            if end >= total_size:
                end = total_size - 1

            content_length = end - start + 1

            self.send_response(206)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Range", f"bytes {start}-{end}/{total_size}")
            self.send_header("Content-Length", str(content_length))
            self.send_header("Accept-Ranges", "bytes")
            self.end_headers()

            with open(filepath, "rb") as f:
                f.seek(start)
                remaining = content_length
                while remaining > 0:
                    chunk = f.read(min(remaining, 65536))
                    if not chunk:
                        break
                    try:
                        self.wfile.write(chunk)
                    except (BrokenPipeError, ConnectionResetError):
                        break
                    remaining -= len(chunk)
        else:
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(total_size))
            self.send_header("Accept-Ranges", "bytes")
            self.end_headers()

            with open(filepath, "rb") as f:
                while True:
                    chunk = f.read(65536)
                    if not chunk:
                        break
                    try:
                        self.wfile.write(chunk)
                    except (BrokenPipeError, ConnectionResetError):
                        break

    def translate_path(self, path):
        clean_path = path.split("?")[0].split("#")[0]
        filename = os.path.basename(clean_path)
        return os.path.join(self.server.serve_dir, filename)

    def log_message(self, format, *args):
        # Silence verbose request logging during automated tests
        pass

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <serve_dir> <port>")
        sys.exit(1)

    serve_dir = os.path.abspath(sys.argv[1])
    port = int(sys.argv[2])

    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(line_buffering=True)
    if hasattr(sys.stderr, 'reconfigure'):
        sys.stderr.reconfigure(line_buffering=True)

    server = FastThreadingHTTPServer(("127.0.0.1", port), RangeHTTPRequestHandler)
    server.serve_dir = serve_dir
    actual_port = server.server_port
    print(f"READY {actual_port}", flush=True)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
