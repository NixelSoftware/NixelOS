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
        '-I'+str(DIR),'-I'+str(VENDOR), str(DIR/'wpa2.c'),str(DIR/'ccmp.c'),str(DIR/'handshake.c'),
        *[str(VENDOR/f'wolfcrypt/src/{name}.c') for name in ('sha','hmac','hash','pwdbased','aes')],
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
    from cryptography.hazmat.primitives.ciphers.aead import AESCCM
    from cryptography.hazmat.primitives.keywrap import aes_key_wrap
    lib.wifi_ccmp_install.argtypes = [ptr,ptr,ctypes.c_uint,ptr,ptr]
    lib.wifi_ccmp_encrypt.argtypes = [ptr,ptr,ctypes.c_uint,ptr,ctypes.c_uint,ptr]
    lib.wifi_ccmp_decrypt.argtypes = [ptr,ptr,ctypes.c_uint,ptr,ctypes.c_uint,ptr]
    lib.wifi_wpa2_unwrap.argtypes = [ptr,ptr,ctypes.c_uint,ptr,ctypes.c_uint,ptr]
    for qos in (False,True):
        sender = ctypes.create_string_buffer(512); receiver = ctypes.create_string_buffer(512)
        key = bytes(range(16))
        assert lib.wifi_ccmp_install(sender,key,0,mac1,mac2) == 0
        assert lib.wifi_ccmp_install(receiver,key,0,mac2,mac1) == 0
        header = bytearray(26 if qos else 24); header[0] = 0x88 if qos else 8; header[1] = 1
        header[4:10] = mac2; header[10:16] = mac1; header[16:22] = b'\x12\x34\x56\x78\x9a\xbc'
        if qos: header[24] = 5
        plaintext = b'\xaa\xaa\x03\x00\x00\x00\x08\x00'+bytes(range(128))
        original = bytes(header)+plaintext
        encrypted = ctypes.create_string_buffer(4096); decoded = ctypes.create_string_buffer(4096)
        written = ctypes.c_uint()
        packets = []
        for pn in (1,2,3):
            if pn == 2: # Reinstall must preserve nonces and replay counters.
                assert lib.wifi_ccmp_install(sender,key,0,mac1,mac2) == 0
                assert lib.wifi_ccmp_install(receiver,key,0,mac2,mac1) == 0
            assert lib.wifi_ccmp_encrypt(sender,original,len(original),encrypted,4096,ctypes.byref(written)) == 0
            packet = encrypted.raw[:written.value]; packets.append(packet)
            aad = bytes([packet[0] & 0x8f,packet[1] & 0xc7])+packet[4:22]+bytes([packet[22] & 15,0])
            if qos: aad += bytes([5,0])
            nonce = bytes([5 if qos else 0])+mac1+pn.to_bytes(6,'big')
            reference = AESCCM(key,tag_length=8).encrypt(nonce,plaintext,aad)
            assert packet[len(header)+8:] == reference
            if pn == 2:
                forged = bytearray(packet); forged[-1] ^= 1
                decoded.raw = b'z'*4096
                assert lib.wifi_ccmp_decrypt(receiver,bytes(forged),len(forged),decoded,4096,ctypes.byref(written)) == -1
                assert written.value == 0 and decoded.raw == b'z'*4096
            assert lib.wifi_ccmp_decrypt(receiver,packet,len(packet),decoded,4096,ctypes.byref(written)) == 0
            assert decoded.raw[:written.value] == original
            assert lib.wifi_ccmp_decrypt(receiver,packet,len(packet),decoded,4096,ctypes.byref(written)) == -1
        assert lib.wifi_ccmp_decrypt(receiver,packets[0],len(packets[0]),decoded,4096,ctypes.byref(written)) == -1
        assert lib.wifi_ccmp_install(sender,key,1,mac1,mac2) == -1
        assert lib.wifi_ccmp_encrypt(sender,original,len(original),encrypted,len(original),ctypes.byref(written)) == -1
    kek = bytes(range(16)); raw_key = bytes(range(32)); wrapped = aes_key_wrap(kek,raw_key)
    recovered = ctypes.create_string_buffer(64)
    assert lib.wifi_wpa2_unwrap(kek,wrapped,len(wrapped),recovered,64,ctypes.byref(written)) == 0
    assert written.value == 32 and recovered.raw[:32] == raw_key
    changed = bytearray(wrapped); changed[-1] ^= 1
    assert lib.wifi_wpa2_unwrap(kek,bytes(changed),len(changed),recovered,64,ctypes.byref(written)) == -1
    assert written.value == 0 and recovered.raw[:32] == bytes(32)
    lib.wifi_wpa2_station_init.argtypes = [ptr,ptr,ptr,ptr,ptr,ctypes.c_uint]
    lib.wifi_wpa2_station_receive.argtypes = [ptr,ptr,ptr,ctypes.c_uint,ctypes.c_uint,ptr,ptr,ptr,ctypes.c_uint,ptr]
    lib.wifi_wpa2_station_authorized.argtypes = [ptr]
    lib.wifi_wpa2_station_expire.argtypes = [ptr,ctypes.c_uint]
    lib.wifi_wpa2_station_encrypt.argtypes = [ptr,ptr,ctypes.c_uint,ptr,ctypes.c_uint,ptr]
    lib.wifi_wpa2_station_clear.argtypes = [ptr]
    rsn = bytes([48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0])
    station = ctypes.create_string_buffer(4096)
    assert lib.wifi_wpa2_station_init(station,pmk,mac1,mac2,rsn,len(rsn)) == 0
    assert lib.wifi_wpa2_station_init(station,pmk,mac1,mac2,rsn,len(rsn)) == -1
    nonce = bytes(range(64,96)); anonce = bytes(range(32,64)); entropy_calls = []
    callback_type = ctypes.CFUNCTYPE(ctypes.c_int,ptr,ptr,ctypes.c_uint)
    @callback_type
    def entropy(ctx,dest,length):
        assert length == 32; entropy_calls.append(1); ctypes.memmove(dest,nonce,32); return 0
    def key_frame(info,counter,data=b'',nonce=bytes(32),rsc=0):
        f = bytearray(99+len(data)); f[:5] = bytes([2,3,0,0,2]); f[2:4] = (len(f)-4).to_bytes(2,'big')
        f[5:7] = info.to_bytes(2,'big'); f[7:9] = b'\x00\x10'; f[9:17] = counter.to_bytes(8,'big')
        f[17:49] = nonce; f[65:71] = rsc.to_bytes(6,'little'); f[97:99] = len(data).to_bytes(2,'big'); f[99:] = data
        return f
    def sign(f,key):
        f[81:97] = bytes(16); f[81:97] = hmac.digest(key,f,'sha1')[:16]; return bytes(f)
    response = ctypes.create_string_buffer(1024)
    def receive(f,now=100,source=mac2,random=entropy):
        return lib.wifi_wpa2_station_receive(station,source,bytes(f),len(f),now,
            ctypes.cast(random,ptr) if random else None,None,response,1024,ctypes.byref(written))
    m1 = key_frame(0x008a,1,nonce=anonce)
    assert receive(m1,random=None) == -1 and not lib.wifi_wpa2_station_authorized(station)
    assert receive(m1) == 1 and written.value == 121 and len(entropy_calls) == 1
    m2 = response.raw[:written.value]
    prf_context = min(mac1,mac2)+max(mac1,mac2)+min(nonce,anonce)+max(nonce,anonce)
    ptk = b''.join(hmac.digest(pmk,b'Pairwise key expansion\0'+prf_context+bytes([i]),'sha1') for i in range(4))[:64]
    assert m2[17:49] == nonce and m2[99:] == rsn
    unsigned_m2 = bytearray(m2); unsigned_m2[81:97] = bytes(16)
    assert m2[81:97] == hmac.digest(ptk[:16],unsigned_m2,'sha1')[:16]
    assert receive(m1,now=101,random=None) == 1 and response.raw[:121] == m2 and len(entropy_calls) == 1
    gtk = bytes(range(16)); kde = bytes([221,22,0,15,172,1,1,0])+gtk
    key_data = rsn+kde+b'\xdd\x00'
    m3 = sign(key_frame(0x13ca,2,aes_key_wrap(ptk[16:32],key_data),anonce),ptk[:16])
    broken = bytearray(m3); broken[81] ^= 1
    assert receive(broken) == -1 and not lib.wifi_wpa2_station_authorized(station)
    assert receive(m3,source=mac1) == -1
    assert receive(m3) == 1 and written.value == 99 and lib.wifi_wpa2_station_authorized(station)
    m4 = response.raw[:99]; unsigned_m4 = bytearray(m4); unsigned_m4[81:97] = bytes(16)
    assert m4[5:7] == b'\x03\x0a' and m4[81:97] == hmac.digest(ptk[:16],unsigned_m4,'sha1')[:16]
    data_frame = bytearray(24); data_frame[0:2] = b'\x08\x01'
    data_frame[4:10] = mac2; data_frame[10:16] = mac1; data_frame[16:22] = mac2
    data_frame += b'\xaa\xaa\x03\x00\x00\x00\x08\x00test'
    cipher = ctypes.create_string_buffer(1024)
    assert lib.wifi_wpa2_station_encrypt(station,bytes(data_frame),len(data_frame),cipher,1024,ctypes.byref(written)) == 0
    assert cipher.raw[24] == 1
    assert receive(m3,random=None) == 1 and response.raw[:99] == m4
    assert lib.wifi_wpa2_station_encrypt(station,bytes(data_frame),len(data_frame),cipher,1024,ctypes.byref(written)) == 0
    assert cipher.raw[24] == 2 # duplicate M3 did not reinstall/reset the key
    gtk2 = bytes(range(16,32)); group_kde = bytes([221,22,0,15,172,1,2,0])+gtk2
    g1 = sign(key_frame(0x1382,3,aes_key_wrap(ptk[16:32],group_kde),rsc=5),ptk[:16])
    assert receive(g1) == 1 and response.raw[5:7] == b'\x03\x02'
    group_reply = response.raw[:99]
    assert receive(g1,random=None) == 1 and response.raw[:99] == group_reply
    assert receive(m3) == -1 # stale old PTK handshake after newer GTK replay counter
    assert lib.wifi_wpa2_station_expire(station,10000) == 0
    lib.wifi_wpa2_station_clear(station)
    assert not lib.wifi_wpa2_station_authorized(station) and station.raw[:32] == bytes(32)
    assert lib.wifi_wpa2_station_init(station,pmk,mac1,mac2,rsn,len(rsn)) == 0
    assert receive(m1,now=0xfffffff0) == 1
    assert lib.wifi_wpa2_station_expire(station,0x00001378) == 1 # wrap-safe 5000 ms timeout
    print('WPA2: key vectors, CCMP/replay, AES unwrap, four-way/GTK handshake, KRACK retransmits and timeouts passed')
