#!/usr/bin/env python3
"""Cross-check the isolated wolfCrypt build against Python's independent crypto."""
import ctypes
import hashlib
import hmac
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[6]
DIR = ROOT / 'src/kernel/net/wireless/security'
VENDOR = ROOT / 'src/system/libraries/tls/vendor/wolfssl'
CC = shlex.split(os.environ.get('CC', 'cc'))
with tempfile.TemporaryDirectory(prefix='nixel-wpa2-') as tmp:
    target = Path(tmp) / 'wpa2.so'
    subprocess.run(CC + ['-shared','-fPIC','-O2','-fno-builtin','-DWOLFSSL_USER_SETTINGS',
        '-I'+str(DIR),'-I'+str(VENDOR), str(DIR/'wpa2.c'),
        *[str(VENDOR/f'wolfcrypt/src/{name}.c') for name in ('sha','hmac','hash','pwdbased')],
        '-o',str(target)],check=True)
    lib = ctypes.CDLL(str(target))
    ptr = ctypes.c_void_p
    lib.wifi_wpa2_pmk.argtypes = [ptr,ctypes.c_uint,ptr,ctypes.c_uint,ptr]
    lib.wifi_wpa2_ptk.argtypes = [ptr,ptr,ptr,ptr,ptr,ptr]
    lib.wifi_wpa2_eapol_mic.argtypes = [ptr,ptr,ctypes.c_uint,ptr]
    lib.wifi_wpa2_eapol_verify.argtypes = [ptr,ptr,ctypes.c_uint]
    out = ctypes.create_string_buffer(32)
    for password,ssid in [(b'password',b'IEEE'),(b'ThisIsAPassword',b'ThisIsASSID'),
                           (b'a'*63,bytes(range(32))),(b'12345678',b'\x00')]:
        assert lib.wifi_wpa2_pmk(password,len(password),ssid,len(ssid),out) == 0
        assert out.raw == hashlib.pbkdf2_hmac('sha1',password,ssid,4096,32)
        if ssid == b'IEEE':
            assert out.raw.hex() == 'f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e'
    for password,ssid in [(b'short',b'IEEE'),(b'a'*64,b'IEEE'),(b'pass\x00word',b'IEEE'),
                           (b'password',b''),(b'password',b'a'*33)]:
        assert lib.wifi_wpa2_pmk(password,len(password),ssid,len(ssid),out) == -1
        assert out.raw == bytes(32)
    assert lib.wifi_wpa2_pmk(None,8,b'x',1,out) == -1
    pmk = bytes(range(32)); mac1 = b'\x00\x01\x02\x03\x04\x05'; mac2 = b'\x10\x20\x30\x40\x50\x60'
    nonce1 = bytes(range(32)); nonce2 = bytes(range(32,64)); ptk = ctypes.create_string_buffer(64)
    context = min(mac1,mac2)+max(mac1,mac2)+min(nonce1,nonce2)+max(nonce1,nonce2)
    expected = b''.join(hmac.digest(pmk,b'Pairwise key expansion\0'+context+bytes([i]),'sha1') for i in range(4))[:64]
    for a,b,c,d in [(mac1,mac2,nonce1,nonce2),(mac2,mac1,nonce2,nonce1)]:
        assert lib.wifi_wpa2_ptk(pmk,a,b,c,d,ptk) == 0 and ptk.raw == expected
    assert lib.wifi_wpa2_ptk(None,mac1,mac2,nonce1,nonce2,ptk) == -1 and ptk.raw == bytes(64)
    kck = expected[:16]
    frame = bytearray(103); frame[0:5] = bytes([2,3,0,99,2]); frame[5:7] = b'\x01\x0a'
    frame[97:99] = b'\x00\x04'; frame[99:] = b'abcd'
    expected_mic = hmac.digest(kck,frame,'sha1')[:16]
    frame[81:97] = bytes(range(16))
    original = bytes(frame); mic = ctypes.create_string_buffer(16)
    assert lib.wifi_wpa2_eapol_mic(kck,original,len(original),mic) == 0 and mic.raw == expected_mic
    assert bytes(frame) == original
    frame[81:97] = expected_mic; signed = bytes(frame)
    assert lib.wifi_wpa2_eapol_verify(kck,signed,len(signed)) == 1
    for index in (7,17,81,96,102):
        changed = bytearray(signed); changed[index] ^= 1
        assert lib.wifi_wpa2_eapol_verify(kck,bytes(changed),len(changed)) == 0
    for index,value in [(0,0),(1,0),(3,0),(4,254),(5,0),(6,1),(98,5)]:
        changed = bytearray(signed); changed[index] = value
        assert lib.wifi_wpa2_eapol_mic(kck,bytes(changed),len(changed),mic) == -1
        assert mic.raw == bytes(16)
    for length in range(99):
        assert lib.wifi_wpa2_eapol_mic(kck,signed,length,mic) == -1
    print('WPA2: PMK vectors, PTK ordering/PRF, EAPOL MIC, tampering and invalid-input checks passed')
