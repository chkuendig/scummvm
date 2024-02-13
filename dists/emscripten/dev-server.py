import io
import json
import os
import requests
import signal
import sys
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, HTTPServer

# TODO: add https://gist.github.com/shivakar/82ac5c9cb17c95500db1906600e5e1ea#file-rangehttpserver-py

cloud_backends = ["dropbox", "onedrive", "gdrive", "box"]
class ApiProxy:
    def start_server(self):
        class ProxyHTTPRequestHandler(SimpleHTTPRequestHandler):
            protocol_version = "HTTP/1.0"

            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=None, **kwargs)

            def do_GET(self):
                path = self.translate_path(self.path)
        
                print(path)
                # url for cloud.scummvm.org
                if self.path[1:].startswith(tuple(cloud_backends)):
                    print("cloud.scummvm.org"+self.path)
                    if self.path[1:].endswith("/refresh"): 
                        #refresh tokens still need to be proxied to avoid CORS issues
                        self._proxy_request("get", requests.get)
                        return
                    else:
                        #forward all other requests
                        self.send_response(HTTPStatus.MOVED_PERMANENTLY)
                        self.send_header(
                            'Location', "https://cloud.scummvm.org"+self.path)
                        self.end_headers()
                        return
                
                elif path.endswith("index.json"): 
                        dir = os.path.dirname(path)
                        if(os.path.isdir(dir)): 
                            print("create json for %s"%dir)
                            enc = sys.getfilesystemencoding()
                            r = {}
                            children = os.listdir(dir)
                            for child in children:
                                if child.startswith("."): #skip hidden files
                                   continue;
                                fullname = os.path.join(dir, child)
                                if os.path.isfile(fullname):
                                    r[child] = os.path.getsize(fullname)
                                if os.path.isdir(fullname):
                                    r[child] = {}
                            encoded = json.dumps(r).encode(enc, 'surrogateescape')
                            file = io.BytesIO()
                            file.write(encoded)
                            file.seek(0)
                            self.send_response(HTTPStatus.OK)
                            self.send_header("Content-type", "application/json")
                            self.send_header("Content-Length", str(len(encoded)))
                            self.end_headers()
                            self.copyfile(file, self.wfile)

                            file.close()
                            return 

                        else: 
                            super().do_GET()
                else:
                    super().do_GET()

            def do_DELETE(self):
                self._proxy_request("delete", requests.delete)

            def do_POST(self):
                self._proxy_request("post", requests.post)

            def do_PUT(self):
                self._proxy_request("put", requests.put)

            def do_PATCH(self):
                self._proxy_request("patch", requests.patch)

            def end_headers (self):
                self.send_header('Access-Control-Allow-Origin', '*')
               # self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
                self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
                SimpleHTTPRequestHandler.end_headers(self)
            
            def _proxy_request(self, method, requests_func):
                url = "https://cloud.scummvm.org"+self.path
                headers = dict(self.headers)
                headers['Host'] = "cloud.scummvm.org"
                print(url)

                body = None
                if "content-length" in self.headers:
                    print("Theres content-length")
                    body = self.rfile.read(int(self.headers["content-length"]))

                resp = requests_func(url, data=body, headers=headers)
                self.send_response(resp.status_code)
                for key in resp.headers:
                    # server and date are already sent. and we break the transfer encoding, so don't send that either
                    if (key not in ["Transfer-Encoding", "Server", "Date", "Connection"]):
                        self.send_header(key, resp.headers[key])
                self.end_headers()
                self.wfile.write(resp.content)

        server_address = ('', 8001)
        self.httpd = HTTPServer(server_address, ProxyHTTPRequestHandler)
        print('proxy server is running at port 8001')
        self.httpd.serve_forever()


def exit_now(signum, frame):
    sys.exit(0)

if __name__ == '__main__':
    proxy = ApiProxy()
    signal.signal(signal.SIGTERM, exit_now)
    proxy.start_server()
