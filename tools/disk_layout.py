"""Shared boot image layout and BIOS staging bounds (all sizes in bytes)."""
SECTOR_SIZE = 512
BOOT_LBA = 0
STAGE2_LBA = 1
STAGE2_SECTORS = 4
KERNEL_LBA = STAGE2_LBA + STAGE2_SECTORS
FILESYSTEM_LBA = 2048
KERNEL_STAGING_ADDRESS = 0x10000
BIOS_VIDEO_ADDRESS = 0xA0000
# Leave the final 64 KiB below VGA available to BIOS/EBDA during disk reads.
KERNEL_STAGING_LIMIT = BIOS_VIDEO_ADDRESS - 0x10000
MAX_KERNEL_BYTES = min(
    (FILESYSTEM_LBA - KERNEL_LBA) * SECTOR_SIZE,
    KERNEL_STAGING_LIMIT - KERNEL_STAGING_ADDRESS,
)


def validate_kernel(size):
    if not 0 < size <= MAX_KERNEL_BYTES:
        raise ValueError(
            f"Kernel image is {size} bytes; boot layout allows 1..{MAX_KERNEL_BYTES} "
            "bytes without overlapping the filesystem or BIOS staging memory"
        )
