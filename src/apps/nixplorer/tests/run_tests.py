#!/usr/bin/env python3
"""HTTP/TLS regressions. HOSTCC='cc -m32' exercises the target integer ABI."""
from pathlib import Path
import os
import shlex
import socket
import ssl
import subprocess
import tempfile
import threading
ROOT = Path(__file__).resolve().parents[4]
B = ROOT/'src/apps/nixplorer'
TLS = ROOT/'src/system/libraries/tls'
CC = shlex.split(os.environ.get('HOSTCC','cc'))
def run(args,**kwargs):
    return subprocess.run(args,check=True,**kwargs)
def openssl(*args):
    run(['openssl',*map(str,args)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
with tempfile.TemporaryDirectory(prefix='nixplorer-https-') as directory:
    tmp=Path(directory)
    image=(ROOT/'browser.bin').read_bytes()
    assert image[:8]==b'APP1\x03\0\0\0' and len(image)<=1048576
    run(CC+['-O2',str(B/'tests/response.c'),str(B/'engine/net/response.c'),'-o',str(tmp/'response')])
    run([str(tmp/'response')])
    run(CC+['-O2',str(B/'tests/color_functions.c'),str(B/'engine/parser/css_color.c'),'-o',str(tmp/'colors')])
    run([str(tmp/'colors')])
    style_sources=['tests/styles.c','engine/dom/document.c','engine/parser/html.c',
        'engine/font/font.c','engine/style/style.c','engine/style/selectors.c',
        'engine/parser/css_grid.c','engine/parser/css_color.c']
    run(CC+['-O2',*[str(B/source) for source in style_sources],'-o',str(tmp/'styles')])
    run([str(tmp/'styles')])
    google_sources=['tests/google_css.c',*style_sources[1:], 'engine/layout/box.c']
    run(CC+['-O2',*[str(B/source) for source in google_sources],'-o',str(tmp/'google-css')])
    run([str(tmp/'google-css')])
    resize_sources=['tests/resize.c',*style_sources[1:], 'engine/layout/box.c',
        'engine/layout/flex.c','engine/layout/grid.c','engine/net/url.c']
    run(CC+['-O2','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        *[str(B/source) for source in resize_sources],'-o',str(tmp/'resize')])
    run([str(tmp/'resize')])
    sources=subprocess.check_output(['make','-s','tls-sources'],cwd=ROOT,text=True).split()
    objects=[]
    for i,source in enumerate(sources):
        if source.endswith('/tcp.c'): continue
        obj=tmp/f'{i}.o'
        run(CC+['-O2','-fno-builtin','-U__unix__','-U__linux__','-DWOLFSSL_USER_SETTINGS',
            '-I'+str(TLS),'-I'+str(TLS/'compat'),'-I'+str(TLS/'vendor/wolfssl'),
            '-c',str(ROOT/source),'-o',str(obj)])
        objects.append(str(obj))
    client=tmp/'client'
    run(CC+['-O2',str(B/'tests/http_client.c'),str(B/'engine/net/http.c'),
        str(B/'engine/net/response.c'),str(B/'engine/platform/tls_heap.c'),*objects,'-o',str(client)])
    run([str(client),'0',str(B/'engine/platform/certificates/ca-bundle.pem'),'1','localhost','2','0'])
    print('PASS shipping trust bundle fits bounded 1 MiB TLS heap',flush=True)
    for name in ('trusted','untrusted'):
        openssl('req','-x509','-newkey','rsa:2048','-nodes','-days','2',
            '-subj',f'/CN={name}','-keyout',tmp/f'{name}.key','-out',tmp/f'{name}.pem')
    openssl('req','-new','-key',tmp/'trusted.key','-subj','/CN=trusted','-out',tmp/'trusted.csr')
    (tmp/'ca.ext').write_text('basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\n')
    openssl('x509','-req','-in',tmp/'trusted.csr','-CA',tmp/'untrusted.pem',
        '-CAkey',tmp/'untrusted.key','-CAcreateserial','-days','2','-extfile',tmp/'ca.ext','-out',tmp/'cross.pem')
    openssl('req','-new','-newkey','rsa:2048','-nodes','-subj','/CN=localhost',
        '-keyout',tmp/'leaf.key','-out',tmp/'leaf.csr')
    (tmp/'leaf.ext').write_text('basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:localhost\nauthorityKeyIdentifier=keyid,issuer\n')
    openssl('x509','-req','-in',tmp/'leaf.csr','-CA',tmp/'trusted.pem','-CAkey',tmp/'trusted.key',
        '-CAcreateserial','-days','2','-extfile',tmp/'leaf.ext','-out',tmp/'leaf.pem')
    (tmp/'chain.pem').write_bytes((tmp/'leaf.pem').read_bytes()+(tmp/'cross.pem').read_bytes())
    def case(version=None,host='localhost',trust='trusted',cross=False,large=False,expected=1,unavailable=0,alloc_limit=None):
        left,right=socket.socketpair()
        errors=[]
        def server():
            try:
                peer=left
                if version:
                    ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                    ctx.minimum_version=ctx.maximum_version=version
                    ctx.load_cert_chain(tmp/('chain.pem' if cross else 'leaf.pem'),tmp/'leaf.key')
                    peer=ctx.wrap_socket(left,server_side=True)
                with peer:
                    request=b''
                    while not request.endswith(b'\r\n\r\na=b'):
                        part=peer.recv(4096)
                        if not part: raise RuntimeError(f'client disconnected: {request!r}')
                        request+=part
                    assert request.startswith(b'POST /path?q=1 HTTP/1.1\r\n')
                    body=b'x'*2097152 if large else b'pong'
                    header=b'X-Large: '+b'a'*300000+b'\r\n' if large else b''
                    response=b'HTTP/1.1 200 OK\r\n'+header+b'Content-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body
                    for i in range(0,len(response),4096): peer.sendall(response[i:i+4096])
            except (OSError,RuntimeError) as error:
                if expected==1: errors.append(error)
            finally: left.close()
        thread=threading.Thread(target=server,daemon=True);thread.start()
        try:
            result=subprocess.run([str(client),str(right.fileno()),str(tmp/f'{trust}.pem'),
                str(int(version is not None)),host,str(unavailable),str(expected),'2097152' if large else '4'],
                env={**os.environ, **({"NIX_TEST_ALLOC_MAX": str(alloc_limit)} if alloc_limit else {})},
                pass_fds=(right.fileno(),),capture_output=True,text=True,timeout=40)
        finally: right.close()
        thread.join(5)
        assert not thread.is_alive(),'server hung'
        assert result.returncode==0,(result.returncode,result.stderr,errors)
        assert not errors,errors
        print(f'PASS {version}: cross={cross}, large={large}, host={host}, expected={expected}',flush=True)
    case(large=True)
    case(large=True,alloc_limit=4096,expected=-12)
    case(large=True,alloc_limit=8192,expected=-12)
    case(ssl.TLSVersion.TLSv1_2,cross=True)
    case(ssl.TLSVersion.TLSv1_3,cross=True,large=True)
    case(ssl.TLSVersion.TLSv1_3,host='wrong.example',expected=-8)
    case(ssl.TLSVersion.TLSv1_3,trust='untrusted',expected=-8)
    case(ssl.TLSVersion.TLSv1_3,unavailable=1,expected=-9)
