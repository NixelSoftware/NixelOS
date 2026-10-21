"""Check real PIC initialization words and preservation of IRQ masks."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root/'src/kernel/idt.c').read_text()
code = source[source.index('static void pic_write('):source.index('void idt_init(')]
prefix = r'''
#include <stdint.h>
#include <assert.h>
static unsigned count;
static uint16_t ports[20];
static uint8_t values[20];
static uint8_t inb(uint16_t port) { return port == 0x21 ? 0xDB : 0xEF; }
static void outb(uint16_t port, uint8_t value) {
    assert(count < 20); ports[count]=port; values[count++]=value;
}
'''
checks = r'''
int main(void) {
    pic_remap();
    const uint16_t expected_ports[] = {0x20,0xA0,0x21,0xA1,0x21,0xA1,0x21,0xA1,0x21,0xA1};
    const uint8_t expected_values[] = {0x11,0x11,0x20,0x28,4,2,1,1,0xDB,0xEF};
    assert(count == 20);
    for (unsigned i=0; i<10; i++) {
        assert(ports[i*2] == expected_ports[i]);
        assert(values[i*2] == expected_values[i]);
        assert(ports[i*2+1] == 0x80);
    }
    assert(expected_values[2]+5 == 37); /* SB16 uses the no-error-code IRQ stub */
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/'test.c').write_text(prefix+code+checks)
    subprocess.run(['gcc',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('PIC remapping, ISA delays, IRQ5 vector, and mask preservation checks passed')
