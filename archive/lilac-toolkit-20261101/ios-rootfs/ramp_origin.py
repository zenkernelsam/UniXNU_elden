import http.server, socketserver, sys, urllib.parse
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        q = urllib.parse.urlparse(self.path).query
        n = int(urllib.parse.parse_qs(q).get('n', ['0'])[0])
        body = bytes(bytearray(i % 251 for i in range(n)))
        self.send_response(200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *a): pass
port = int(sys.argv[1])
with socketserver.TCPServer(('127.0.0.1', port), H) as s:
    s.serve_forever()
