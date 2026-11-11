#!/usr/bin/env python3
"""Compile C with the device compiler and execute emitted i386 code on Linux.
No emulator, third-party Python modules, or system C library required.
"""
import ctypes as ct
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(*args):
    return subprocess.run(args, cwd=ROOT, check=True, capture_output=True)


with tempfile.TemporaryDirectory(prefix='nixel-sdk-test-') as directory:
    tmp = Path(directory)
    run('cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra',
        'src/sdk/compiler.c', '-o', str(tmp / 'compiler.so'))
    lib = ct.CDLL(str(tmp / 'compiler.so'))
    lib.sdk_compile.argtypes = [ct.c_char_p, ct.c_void_p, ct.c_uint, ct.c_char_p, ct.c_uint]
    runtime = (ROOT / 'sdkhost.bin').read_bytes()
    assert len(runtime) <= 65536
    marker = runtime.index(b'NIXELSDKIMAGE01!')
    assert marker % 4 == 0
    code, capacity = struct.unpack_from('<II', runtime, marker + 20)
    assert code >= 0xc00000 and code - 0xc00000 + capacity <= len(runtime)

    def compile_c(source, success=True):
        image = ct.create_string_buffer(runtime, len(runtime))
        error = ct.create_string_buffer(160)
        result = lib.sdk_compile(source.encode(), image, len(runtime), error, len(error))
        assert (result == 0) == success, (source, error.value)
        if not success:
            assert error.value.startswith(b'line ')
        return image.raw

    # Use a Linux-only freestanding runner to execute the exact emitted bytes.
    # SDK GUI services are deliberately not invoked under Linux.
    (tmp / 'runner.c').write_text(r'''
static int call(int n,int b,int c,int d) {
 int r; __asm__ volatile("int $0x80":"=a"(r):"0"(n),"b"(b),"c"(c),"d"(d):"memory","cc");return r;
}
void run(char *path) {
 unsigned args[]={0xc00000,65536,7,0x32,-1,0};
 int result=call(90,(int)args,0,0);
 if(result!=0xc00000)call(1,251,0,0);
 int fd=call(5,(int)path,0,0);if(fd<0)call(1,252,0,0);
 int n=call(3,fd,0xc00000,65536);if(n<0)call(1,253,0,0);
 unsigned char *p=(void*)0xc00000;int entry=0;
 for(int i=8;i+32<n;i+=4)if(p[i]=='N'&&p[i+5]=='S'&&p[i+8]=='I')entry=*(int*)(p+i+28);
 if(!entry)call(1,254,0,0);
 result=((int(*)(void))entry)();call(1,result&255,0,0);
}
__asm__(".global _start\n_start:\n mov 8(%esp),%eax\n push %eax\n call run\n");
''')
    run('cc', '-m32', '-fno-pie', '-no-pie', '-nostdlib', '-fno-stack-protector',
        '-O2', str(tmp / 'runner.c'), '-o', str(tmp / 'runner'))
    cases = [
        ('int main(void){return 6*7;}', 42),
        ('int g=7; int twice(int x){return x*2;} int main(void){int n=0; int i=0; while(i<5){n=n+twice(i);i=i+1;} return n+g;}', 27),
        ('int main(void){if(3>=2 && 4!=5){return 17;}else{return 99;}}', 17),
        ('int main(void){int n=0; if(0 && (n=1))return 99; if(1 || (n=2))n=n+4;return n;}', 4),
        ('int main(void){return f(2,3,4);} int f(int a,int b,int c){return a*100+b*10+c;}', 234),
        ('int fact(int n){if(n<=1)return 1;return n*fact(n-1);} int main(void){return fact(5);}', 120),
        ('int main(void){int n=-15;return -(n/4)*10 + -(n%4);}', 33),
        ('#include <nixel.h>\nint main(void){char *s="hello";if(s)return 42;return 0;}', 42),
        ('int main(void){return (1<2)+(1>2)+(2==2)+(2!=3)+(3<=3)+(4>=5);}', 4),
    ]
    for i, (source, expected) in enumerate(cases):
        image = tmp / f'case{i}.bin'
        image.write_bytes(compile_c(source))
        proc = subprocess.run([str(tmp / 'runner'), str(image)], timeout=5)
        assert proc.returncode == expected, (source, proc.returncode, expected)
    for bad in ['int main(void){return missing;}', 'int main(void){unknown();}',
                'int main(void){window("hi");}', 'int main(int a){return a;}',
                'int main(void){return "unterminated;}', '/* missing end',
                'int main(void){int a=0;int a=1;}', '#include <stdio.h>\nint main(void){}',
                'int main(void){return ' + '(' * 120 + '0' + ')' * 120 + ';}',
                'int window(void){return 0;}', 'int main(void){return 0;} @']:
        compile_c(bad, False)
    for example in (ROOT / 'resources/sdk').glob('*.c'):
        compile_c(example.read_text())

    run('cc', '-O2', 'tools/tests/sdk_gui_test.c', 'src/sdk/design.c', 'src/sdk/compiler.c', 'src/sdk/editor.c', '-o', str(tmp / 'gui'))
    gui = run(str(tmp / 'gui'))
    print(gui.stdout.decode().strip())

    # Designer serialization: escaped strings, geometry validation, user code.
    run('cc', '-shared', '-fPIC', '-O2', 'src/sdk/design.c', '-o', str(tmp / 'design.so'))
    designlib = ct.CDLL(str(tmp / 'design.so'))

    class Control(ct.Structure):
        _fields_ = [(n, ct.c_int) for n in ('kind','id','x','y','w','h','value')] + [('text', ct.c_char*128)]

    class Design(ct.Structure):
        _fields_ = [('title',ct.c_char*32),('width',ct.c_int),('height',ct.c_int),('count',ct.c_int),('controls',Control*48)]

    designlib.sdk_design_save.argtypes = [ct.POINTER(Design),ct.c_char_p,ct.c_char_p,ct.c_int]
    designlib.sdk_design_load.argtypes = [ct.POINTER(Design),ct.c_char_p]
    d = Design(b'Test "Window"',400,300,2)
    d.controls[0] = Control(1,1,10,40,100,26,0,b'Say "hi" \\')
    d.controls[1] = Control(2,2,10,80,180,26,0,b'editable')
    buf = ct.create_string_buffer(16384)
    assert designlib.sdk_design_save(ct.byref(d),b'',buf,len(buf)) > 0
    generated = buf.value
    compile_c(generated.decode())
    restored = Design()
    assert designlib.sdk_design_load(ct.byref(restored),generated) == 0
    assert restored.title == d.title and restored.controls[0].text == d.controls[0].text
    assert bytes(restored) == bytes(d)
    custom = generated + b'\n/* user event handlers stay here */\n'
    d.controls[0].x = 20
    assert designlib.sdk_design_save(ct.byref(d),custom,buf,len(buf)) > 0
    assert buf.value.endswith(b'/* user event handlers stay here */\n')
    assert designlib.sdk_design_save(ct.byref(d),b'int main(void){}',buf,len(buf)) == -1
    assert designlib.sdk_design_save(ct.byref(d),b'',buf,20) == -1
    before = bytes(restored)
    assert designlib.sdk_design_load(ct.byref(restored),generated.replace(b'400, 300',b'9999, 300')) == -1
    assert bytes(restored) == before
    assert designlib.sdk_design_load(ct.byref(restored),generated.replace(b'button(1,',b'button(2,')) == -1
print('SDK: native i386 execution, compiler diagnostics, examples and designer round trips passed')
