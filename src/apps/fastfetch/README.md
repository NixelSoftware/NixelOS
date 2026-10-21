# Fastfetch adaptation

Standalone ring-3 console app. Build with `make -C src/apps/fastfetch`, or use
the `package` target to create a staging archive for the future package manager.
Run `fastfetch` in the OS shell; `--logo none`, `--help`, and `--version` work.

The logo is generated from `resources/nixelos-trans.ansi.txt`, preserving ANSI
foreground colors and bold styling while removing background colors. Artwork
prints to the left of the stats. Drag the terminal's bottom-right corner to
resize; text reflows to the available width. The terminal retains up to 128
logical lines, each up to 256 characters, and shows the most recent rows.

For testing, root `make` temporarily includes `fastfetch.bin` in `os.img`.
Later remove its dependency/rule from the root Makefile and its `ROOT_FILES`
entry in `mkfat32.py` to return to standalone delivery.

This is a limited, independently implemented Fastfetch-style app, inspired by
https://github.com/fastfetch-cli/fastfetch (main frontend, OS and CPU modules).
It does not contain the full upstream program. Output uses the conventional
OS, Shell, Terminal, CPU, and Memory fields. Unsupported fields are omitted.
CPU information uses CPUID. Memory is shown as used / total in MiB through
`sys_memory_info` (syscall 28).