from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import urllib.request

def find_cpu_tctl(node):
    if isinstance(node, dict):
        if node.get("Text") == "Core (Tctl/Tdie)":
            return node.get("Value", "N/A")
        for v in node.values():
            r = find_cpu_tctl(v)
            if r:
                return r
    elif isinstance(node, list):
        for i in node:
            r = find_cpu_tctl(i)
            if r:
                return r
    return None

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        try:
            with urllib.request.urlopen("http://localhost:8085/data.json") as f:
                data = json.loads(f.read().decode())
                temp = find_cpu_tctl(data) or "N/A"
                temp = temp.replace("°", "").replace(",", ".")
        except:
            temp = "ERR"

        html = f"""
<html>
<head>
<meta http-equiv="refresh" content="5">
<style>
body {{
    background: #000;
    color: #0f0;
    font-family: monospace;
    text-align: center;
    margin-top: 40px;
}}
h1 {{
    font-size: 20px;
    margin-bottom: 10px;
}}
.temp {{
    font-size: 48px;
    font-weight: bold;
}}
.footer {{
    margin-top: 15px;
    font-size: 12px;
    color: #0a0;
}}
</style>
</head>
<body>
<h1>CPU TEMP</h1>
<div class="temp">{temp}</div>
<div class="footer">update cada 5s</div>
</body>
</html>
"""

        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.end_headers()
        self.wfile.write(html.encode())

print("Servidor activo en puerto 8000")
HTTPServer(("0.0.0.0", 8000), Handler).serve_forever()
