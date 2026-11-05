#!/usr/bin/env python3
"""Build the same configuration for the host and exercise real TLS peers."""
from pathlib import Path
import os
import shlex
import socket
import ssl
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[5]
BASE = ROOT / 'src/system/libraries/tls'
CC = shlex.split(os.environ.get('HOSTCC', 'cc'))

def run(args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)

with tempfile.TemporaryDirectory(prefix='nixel-tls-') as tmp:
    tmp = Path(tmp)
    combined = tmp/'combined.o'
    run(['ld', '-m', 'elf_i386', '-r', '--whole-archive',
         str(BASE/'libnixeltls.a'), '--no-whole-archive', '-o', str(combined)])
    undefined = subprocess.check_output(['nm', '-u', str(combined)], text=True)
    assert not undefined.strip(), undefined
    print('PASS freestanding i386 archive: no unresolved symbols')
    sources = subprocess.check_output(['make', '-s', 'tls-sources'], cwd=ROOT, text=True).split()
    objects = []
    for i, source in enumerate(sources):
        if source.endswith('/tcp.c'):
            continue  # i386 syscalls are checked by the freestanding build.
        obj = tmp / f'{i}.o'
        run(CC + ['-O2', '-fno-builtin', '-U__unix__', '-U__linux__',
                  '-DWOLFSSL_USER_SETTINGS', '-I'+str(BASE), '-I'+str(BASE/'compat'),
                  '-I'+str(BASE/'vendor/wolfssl'), '-c', str(ROOT/source), '-o', str(obj)])
        objects.append(str(obj))
    client = tmp/'client'
    run(CC + [str(BASE/'tests/client.c'), *objects, '-o', str(client)])
    for name in ('server', 'other'):
        run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
             '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
             '-keyout', str(tmp/f'{name}.key'), '-out', str(tmp/f'{name}.pem')],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def case(version, hostname='localhost', ca='server', platform_mode=0, expected=0):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = context.maximum_version = version
        context.load_cert_chain(tmp/'server.pem', tmp/'server.key')
        left, right = socket.socketpair()
        failures = []
        def server():
            try:
                with context.wrap_socket(left, server_side=True) as peer:
                    message = b''
                    while len(message) < 4:
                        part = peer.recv(4-len(message))
                        if not part:
                            raise RuntimeError('unexpected client EOF')
                        message += part
                    assert message == b'ping'
                    peer.sendall(b'pong')
            except (ssl.SSLError, OSError, RuntimeError) as error:
                if expected == 0:
                    failures.append(error)
            finally:
                left.close()
        thread = threading.Thread(target=server, daemon=True)
        thread.start()
        try:
            result = subprocess.run([str(client), str(right.fileno()), str(tmp/f'{ca}.pem'),
                                     hostname, str(platform_mode)],
                                    pass_fds=(right.fileno(),), timeout=30,
                                    capture_output=True, text=True)
        finally:
            right.close()
        thread.join(timeout=5)
        assert not thread.is_alive(), 'TLS server did not finish'
        assert not failures, failures
        assert result.returncode == expected, (result.returncode, result.stderr)
        print(f'PASS {version.name}: host={hostname}, CA={ca}, platform_mode={platform_mode}')

    case(ssl.TLSVersion.TLSv1_2)
    case(ssl.TLSVersion.TLSv1_3)
    case(ssl.TLSVersion.TLSv1_3, hostname='wrong.example', expected=2)
    case(ssl.TLSVersion.TLSv1_3, ca='other', expected=2)
    case(ssl.TLSVersion.TLSv1_3, platform_mode=1, expected=2)
    case(ssl.TLSVersion.TLSv1_3, platform_mode=2, expected=2)  # expired certificate
    case(ssl.TLSVersion.TLSv1_3, platform_mode=3, expected=2)  # unavailable clock
