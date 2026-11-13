"""Exercise the production curl TLS policy against local HTTPS fixtures."""
from pathlib import Path
from http.server import HTTPServer,BaseHTTPRequestHandler
import json,ssl,subprocess,sys,tempfile,threading

binary=Path(sys.argv[1]).resolve()
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        if self.path=='/downgrade':
            self.send_response(302);self.send_header('Location','http://localhost:1/package.pkg');self.end_headers()
        else:
            self.send_response(200);self.end_headers();self.wfile.write(b'local fixture')

with tempfile.TemporaryDirectory(prefix='neon-tls-') as folder:
    root=Path(folder)
    for name in ('trusted','other'):
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1',
            '-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost',
            '-keyout',str(root/(name+'.key')),'-out',str(root/(name+'.pem'))],
            check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15)
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(root/'trusted.pem',root/'trusted.key')
    server=HTTPServer(('127.0.0.1',0),Handler)
    server.socket=context.wrap_socket(server.socket,server_side=True)
    worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
    port=server.server_port
    cases=[('trusted certificate',f'https://localhost:{port}/','trusted.pem',0),
        ('wrong hostname',f'https://127.0.0.1:{port}/','trusted.pem',60),
        ('untrusted certificate',f'https://localhost:{port}/','other.pem',60),
        ('missing trust bundle',f'https://localhost:{port}/','missing.pem',77),
        ('plain HTTP rejected',f'http://localhost:{port}/','trusted.pem',1),
        ('HTTP redirect rejected',f'https://localhost:{port}/downgrade','trusted.pem',1)]
    try:
        for name,url,bundle,expected in cases:
            result=subprocess.run([str(binary),url,str(root/bundle),str(expected)],capture_output=True,text=True,timeout=8)
            print(json.dumps({'case':name,'passed':result.returncode==0,'result':result.stdout.strip()}),flush=True)
            assert result.returncode==0,(name,result.stdout,result.stderr)
    finally:
        server.shutdown();server.server_close();worker.join(timeout=2)
