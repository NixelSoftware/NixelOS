#include "linux_syscall.h"
#include "linux_abi.h"
#include "../task.h"
#include "../fs.h"
#include "../paging.h"
#include "../drivers/rtc.h"
#include "../drivers/serial.h"

extern void kernel_user_write(const char* text, uint32_t length);

/* User GDT selector 0x33 for Thread Local Storage (TLS / %gs segment) */
#define LINUX_TLS_ENTRY 6
#define LINUX_TLS_SELECTOR 0x33

typedef struct {
    int  entry_number;
    uint32_t base_addr;
    uint32_t limit;
    uint32_t seg_32bit:1;
    uint32_t contents:2;
    uint32_t read_exec_only:1;
    uint32_t limit_in_pages:1;
    uint32_t seg_not_present:1;
    uint32_t useable:1;
} __attribute__((packed)) user_desc_t;

static uint32_t linux_current_brk = 0;

void linux_syscall_dispatch(registers_t* regs) {
    uint32_t nr = regs->eax;
    uint32_t a1 = regs->ebx;
    uint32_t a2 = regs->ecx;
    uint32_t a3 = regs->edx;

    debug_print("[LINUX SYSCALL] EAX=");
    debug_hex(regs->eax);
    debug_print(" EBX=");
    debug_hex(regs->ebx);
    debug_print("\n");

    switch (nr) {
        /* Exit */
        case 1:   /* sys_exit */
        case 252: /* sys_exit_group */
            task_exit();
            regs->eax = 0;
            return;

        /* Write */
        case 4: { /* sys_write(fd, buf, count) */
            int fd = (int)a1;
            const char* buf = (const char*)a2;
            uint32_t len = a3;
            if (fd == 1 || fd == 2) {
                kernel_user_write(buf, len);
                regs->eax = len;
                return;
            }
            regs->eax = -LINUX_EBADF;
            return;
        }

        /* Writev */
        case 146: { /* sys_writev(fd, iov, iovcnt) */
            int fd = (int)a1;
            const struct linux_iovec* iov = (const struct linux_iovec*)a2;
            int count = (int)a3;
            uint32_t written = 0;

            if (fd == 1 || fd == 2) {
                for (int i = 0; i < count; i++) {
                    if (iov[i].iov_len && iov[i].iov_base) {
                        kernel_user_write((const char*)iov[i].iov_base, iov[i].iov_len);
                        written += iov[i].iov_len;
                    }
                }
                regs->eax = written;
                return;
            }
            regs->eax = -LINUX_EBADF;
            return;
        }

        /* Heap (brk) */
        case 45: { /* sys_brk(new_brk) */
            uint32_t new_brk = a1;
            if (new_brk == 0) {
                regs->eax = linux_current_brk ? linux_current_brk : 0x08100000;
                return;
            }
            if (new_brk > linux_current_brk) {
                /* Allocate pages up to new_brk */
                uint32_t cur_page = (linux_current_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                uint32_t end_page = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                for (uint32_t p = cur_page; p < end_page; p += PAGE_SIZE) {
                    paging_frame_alloc(); /* Map user page in current directory */
                }
                linux_current_brk = new_brk;
            }
            regs->eax = linux_current_brk;
            return;
        }

        /* Memory Protection (Libc initializers call this on BSS/data) */
        case 125: /* sys_mprotect(addr, len, prot) */
            regs->eax = 0; /* Return success */
            return;

        /* Thread Local Storage (set_thread_area) — Used by glibc/musl to init %gs */
        case 243: { /* sys_set_thread_area(user_desc_t*) */
            user_desc_t* desc = (user_desc_t*)a1;
            if (!desc) {
                regs->eax = -LINUX_EFAULT;
                return;
            }
            desc->entry_number = LINUX_TLS_ENTRY;
            /* Point TLS segment to user thread descriptor */
            regs->eax = 0;
            return;
        }

        /* Memory Mapping (mmap2) */
        case 192: { /* sys_mmap2(addr, length, prot, flags, fd, pgoff) */
            uint32_t len = a2;
            static uint32_t mmap_alloc_base = 0x40000000u;
            uint32_t addr = mmap_alloc_base;
            mmap_alloc_base += (len + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
            regs->eax = addr;
            return;
        }

        /* Uname */
        case 122: { /* sys_uname */
            struct linux_old_utsname* u = (struct linux_old_utsname*)a1;
            if (!u) { regs->eax = -LINUX_EFAULT; return; }
            const char* s = "Linux";
            int i = 0; while (s[i]) { u->sysname[i] = s[i]; i++; } u->sysname[i] = 0;
            s = "nixel"; i = 0; while (s[i]) { u->nodename[i] = s[i]; i++; } u->nodename[i] = 0;
            s = "6.1.0"; i = 0; while (s[i]) { u->release[i] = s[i]; i++; } u->release[i] = 0;
            s = "#1 SMP NixelOS"; i = 0; while (s[i]) { u->version[i] = s[i]; i++; } u->version[i] = 0;
            s = "i686"; i = 0; while (s[i]) { u->machine[i] = s[i]; i++; } u->machine[i] = 0;
            regs->eax = 0;
            return;
        }

        /* Terminal Query (isatty / tcgetattr) */
        case 54: /* sys_ioctl */
            regs->eax = 0; /* Pretend success */
            return;

        /* Signals (musl and glibc register abort/sigsegv handlers on startup) */
        case 174: /* sys_rt_sigaction */
        case 175: /* sys_rt_sigprocmask */
            regs->eax = 0;
            return;

        /* Query UID / GID */
        case 24: /* sys_getuid */
        case 47: /* sys_getgid */
        case 49: /* sys_geteuid */
        case 50: /* sys_getegid */
            regs->eax = 1000;
            return;

        default:
            debug_print("[NIXEL2LINUX] Unimplemented syscall: ");
            debug_hex(nr);
            debug_print("\n");
            regs->eax = (uint32_t)-LINUX_ENOSYS;
            return;
    }
}