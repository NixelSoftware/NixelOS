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
        'src/sdk/compiler.c', 'src/sdk/design.c', 'src/sdk/project.c', '-o', str(tmp / 'compiler.so'))
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
        ('Checkbox Toggle; int read(void){return Toggle.checkboxIsTicked;} int main(void){Toggle.checkboxIsTicked=42;return read();}',42),
        ('Button Button1; Label Status; int main(void){Status.text="hello";return !Button1.buttonIsPressed;}',1),
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
                'int window(void){return 0;}', 'Button B; int main(void){B.buttonIsPressed=1;}', 'Label L;int main(void){return L.checkboxIsTicked;}', 'int main(void){Button B;}', 'int main(void){return 0;} @']:
        compile_c(bad, False)


    run('cc', '-O2', 'tools/tests/sdk_gui_test.c', 'src/sdk/design.c', 'src/sdk/compiler.c', 'src/sdk/editor.c', 'src/sdk/project.c', '-o', str(tmp / 'gui'))
    gui = run(str(tmp / 'gui'))
    print(gui.stdout.decode().strip())

    # Forms contain data, while handwritten C contains only event handlers.
    class Control(ct.Structure):
        _fields_ = [(n, ct.c_int) for n in ('kind','id','x','y','w','h','value')] + [('text', ct.c_char*128)]

    class Design(ct.Structure):
        _fields_ = [('title',ct.c_char*32),('width',ct.c_int),('height',ct.c_int),('count',ct.c_int),('controls',Control*48),('names',(ct.c_char*32)*48)]

    lib.sdk_form_save.argtypes = [ct.POINTER(Design),ct.c_void_p,ct.c_int]
    lib.sdk_form_load.argtypes = [ct.POINTER(Design),ct.c_void_p,ct.c_int]
    lib.sdk_project_source.argtypes = [ct.POINTER(Design),ct.c_char_p,ct.c_char_p,ct.c_int]
    lib.sdk_project_handlers.argtypes = [ct.POINTER(Design),ct.c_char_p,ct.c_int]
    lib.sdk_form_path.argtypes=[ct.c_char_p,ct.c_char_p,ct.c_int]
    path=ct.create_string_buffer(128)
    assert lib.sdk_form_path(b'/home/user/App.c',path,len(path))==0 and path.value==b'/home/user/App.form'
    assert lib.sdk_form_path(b'App.c',path,8)==-1
    lib.sdk_project_compile.argtypes = [ct.POINTER(Design),ct.c_char_p,ct.c_void_p,ct.c_uint,ct.c_char_p,ct.c_uint]
    d = Design(b'Test "Window"',400,300,4)
    specs=[(1,'Button1'),(4,'Checkbox1'),(2,'TextBox1'),(3,'Label1')]
    for i,(kind,name) in enumerate(specs):
        d.controls[i] = Control(kind,i+1,10,40+i*40,180,26,0,b'editable')
        d.names[i].value=name.encode()
    binary=ct.create_string_buffer(10000)
    size=lib.sdk_form_save(ct.byref(d),binary,len(binary))
    assert size==52+188*4 and b'window(' not in binary.raw[:size]
    restored=Design()
    assert lib.sdk_form_load(ct.byref(restored),binary,size)==0
    assert bytes(restored)==bytes(d)
    before=bytes(restored)
    assert lib.sdk_form_load(ct.byref(restored),binary,size-1)==-1
    assert bytes(restored)==before
    assert lib.sdk_project_source(ct.byref(d),b'',ct.create_string_buffer(4),4)==-1
    damaged=bytearray(binary.raw[:size]);struct.pack_into('<I',damaged,40,9999)
    assert lib.sdk_form_load(ct.byref(restored),bytes(damaged),size)==-1
    d.names[1].value=b'Button1'
    assert lib.sdk_form_save(ct.byref(d),binary,len(binary))==-1
    d.names[1].value=b'Checkbox1'
    d.names[1].value=b'bad.name'
    assert lib.sdk_form_save(ct.byref(d),binary,len(binary))==-1
    d.names[1].value=b'Checkbox1'
    logic=ct.create_string_buffer(b'#include <nixel.h>\n// Button1OnPressed() is a comment, not a handler.\n',16384)
    assert lib.sdk_project_handlers(ct.byref(d),logic,len(logic))>0
    small=ct.create_string_buffer(b'int keep=42;',24)
    before_small=small.raw
    assert lib.sdk_project_handlers(ct.byref(d),small,len(small))==-1
    assert small.raw==before_small
    first=logic.value
    assert b'void Button1OnPressed(void)' in first and b'void MainWindowOnLoaded(void)' in first
    assert b'window(' not in first and b'button(' not in first and b'int main(' not in first
    assert lib.sdk_project_handlers(ct.byref(d),logic,len(logic))==len(first)
    assert logic.value==first
    combined=ct.create_string_buffer(49152)
    assert lib.sdk_project_source(ct.byref(d),logic,combined,len(combined))>0
    compile_c(combined.value.decode())
    # Handler discovery ignores braces and handler-looking text inside strings.
    tricky=b'void MainWindowOnLoaded(void){char *s="} Button1OnPressed() {";}\n'
    assert lib.sdk_project_source(ct.byref(d),tricky,combined,len(combined))>0
    assert b'Button1OnPressed();' not in combined.value
    compile_c(combined.value.decode())

    error=ct.create_string_buffer(160)
    image=ct.create_string_buffer(runtime,len(runtime))
    assert lib.sdk_project_compile(ct.byref(d),b'void Button1OnPressed(void){\nButton1.checkboxIsTicked=1;\n}',image,len(runtime),error,len(error))==-1
    assert error.value.startswith(b'line 2:'),error.value
    image=ct.create_string_buffer(runtime,len(runtime))
    assert lib.sdk_project_compile(ct.byref(d),b'void Button1OnPressed(int wrong){}',image,len(runtime),error,len(error))==-1
    assert b'void Name(void)' in error.value,error.value

    # Execute the generated startup, public fields, and actual callback dispatch.
    run('cc','-m32','-fno-pie','-ffreestanding','-fno-stack-protector','-O2','-c','tools/tests/sdk_project_runtime.c','-o',str(tmp/'project.o'))
    run('ld','-m','elf_i386','-T','src/sdk/app.ld',str(tmp/'project.o'),'-o',str(tmp/'project.elf'))
    run('objcopy','-O','binary','--remove-section=.bss',str(tmp/'project.elf'),str(tmp/'project.bin'))
    simulated=(tmp/'project.bin').read_bytes()
    image=ct.create_string_buffer(simulated,len(simulated));error=ct.create_string_buffer(160)
    handlers=b'''#include <nixel.h>
int calls=0;
void MainWindowOnLoaded(void){if(MainWindow.windowLoaded)calls=calls+1;}
void AppLogic(void){if(Button1.buttonIsPressed)calls=calls+10;Label1.text=TextBox1.text;}
void Button1OnPressed(void){AppLogic();}
void Checkbox1OnChanged(void){if(Checkbox1.checkboxIsTicked)calls=calls+100;}
void TextBox1OnTextChanged(void){if(TextBox1.textChanged)calls=calls+1000;}
void MainWindowOnClosed(void){if(calls==1121 && !MainWindow.isOpen && !Button1.isPressed)print("PASS");else print("FAIL");}
'''
    result=lib.sdk_project_compile(ct.byref(d),handlers,image,len(simulated),error,len(error))
    assert result==0,error.value
    (tmp/'events.bin').write_bytes(image.raw)
    proc=subprocess.run([str(tmp/'runner'),str(tmp/'events.bin')],timeout=5)
    assert proc.returncode==42,proc.returncode

    for example in (ROOT/'resources/sdk').glob('*.c'):
        sidecar=example.with_suffix('.form').read_bytes()
        assert lib.sdk_form_load(ct.byref(restored),sidecar,len(sidecar))==0
        image=ct.create_string_buffer(runtime,len(runtime))
        assert lib.sdk_project_compile(ct.byref(restored),example.read_bytes(),image,len(runtime),error,len(error))==0,error.value
print('SDK: native execution, public component fields, event dispatch, separate forms and GUI checks passed')
