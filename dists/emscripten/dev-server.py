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
                if path.endswith("index.json"): 
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

            def end_headers (self):
                self.send_header('Access-Control-Allow-Origin', '*')
                SimpleHTTPRequestHandler.end_headers(self)
          
        server_address = ('', 8001)
        self.httpd = HTTPServer(server_address, ProxyHTTPRequestHandler)
        print('server is running')
        self.httpd.serve_forever()


def exit_now(signum, frame):
    sys.exit(0)

if __name__ == '__main__':
    proxy = ApiProxy()
    signal.signal(signal.SIGTERM, exit_now)
    proxy.start_server()
