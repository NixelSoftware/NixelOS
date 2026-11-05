.section .rodata
.global browser_ca_start
.global browser_ca_end
browser_ca_start:
.incbin "src/apps/nixplorer/engine/platform/certificates/ca-bundle.pem"
browser_ca_end:
.section .note.GNU-stack,"",@progbits
