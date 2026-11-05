#define main earlier_pci_tests
#include "pci_test.c"
#undef main
#include "../device.c"

static uint8_t pool[2 * 1024 * 1024] __attribute__((aligned(4096)));
static uint8_t used[sizeof(pool) / PAGE_SIZE];
static unsigned allocations, fail_allocation, files;
static int file_mode;
uint32_t paging_alloc_contiguous(uint32_t pages) {
    if (++allocations == fail_allocation) return 0;
    unsigned run = 0;
    for (unsigned i = 0; i < sizeof(used); i++) {
        if (used[i]) { run = 0; continue; }
        if (++run != pages) continue;
        unsigned start = i + 1 - pages;
        for (unsigned j = start; j <= i; j++) used[j] = 1;
        memset(pool + start * PAGE_SIZE,0,pages * PAGE_SIZE);
        return (uint32_t)(unsigned long)(pool + start * PAGE_SIZE);
    }
    return 0;
}
void paging_frame_free(uint32_t address) {
    unsigned long offset = address - (unsigned long)pool;
    assert(offset < sizeof(pool) && !(offset & (PAGE_SIZE - 1)));
    assert(used[offset / PAGE_SIZE]); used[offset / PAGE_SIZE] = 0;
}
void pci_set_master(const pci_device_t* dev, int enabled) {
    assert(dev == devices + 1);
    if (enabled) command |= PCI_COMMAND_MASTER;
    else command &= ~PCI_COMMAND_MASTER;
}
int fs_read_file_at(const char* path, uint8_t* dest, uint32_t capacity, uint32_t offset) {
    assert(!offset && capacity == FW_MAX + 1);
    assert(!strcmp(path,"/lib/firmware/iwlwifi-3945-2.ucode") ||
           !strcmp(path,"/lib/firmware/iwlwifi-3945-1.ucode"));
    files++;
    if (file_mode == 0) return -1;
    if (file_mode == 1) { memset(dest,0,24); return 24; }
    if (file_mode == 2) {
        memset(dest,0,44); dest[0] = 15; dest[1] = 2;
        for (unsigned i = 0; i < 5; i++) dest[4 + i * 4] = 4;
        return 44;
    }
    assert(file_mode == 3); memset(dest,0,capacity); return capacity;
}
static iwlegacy_info_t device_setup(void) {
    setup(); registers[CSR_GP_CNTRL/4] = CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW;
    memset(used,0,sizeof(used)); allocations = fail_allocation = files = 0; file_mode = 2;
    iwlegacy_info_t info = {0};
    info.state = IL_EEPROM_READY; info.vendor = 0x8086; info.device = 0x4222;
    info.pci_bus = 3; info.pci_slot = 7; info.pci_function = 1; info.eeprom.version = 0x2F;
    return info;
}
static unsigned pinned(void) {
    unsigned count = 0; for (unsigned i = 0; i < sizeof(used); i++) count += used[i]; return count;
}
int main(void) {
    iwlegacy_info_t info = device_setup(); file_mode = 0;
    assert(iwlegacy_firmware_check(&info) == IL_NO_FIRMWARE && info.state == IL_FIRMWARE_MISSING);
    assert(files == 2 && !pinned() && !bar_probes);
    for (unsigned mode = 1; mode <= 3; mode += 2) {
        info = device_setup(); file_mode = mode;
        assert(iwlegacy_firmware_check(&info) == IL_INVALID && !pinned() && !bar_probes);
    }
    for (unsigned fail = 1; fail <= 8; fail++) {
        info = device_setup(); fail_allocation = fail;
        assert(iwlegacy_firmware_check(&info) == IL_NOMEM && !pinned() && !bar_probes);
    }
    info = device_setup(); fail_allocation = 9;
    il_scan_results_t found;
    assert(iwlegacy_scan(&info,&found) == IL_NOMEM && !pinned() && !bar_probes && !found.count);
    info = device_setup(); command |= PCI_COMMAND_MASTER;
    assert(iwlegacy_firmware_check(&info) == IL_ACCESS && !pinned() && !bar_probes);
    info = device_setup(); registers[CSR_GP_CNTRL/4] = 0;
    assert(iwlegacy_firmware_check(&info) == IL_RFKILL && !pinned() && command == 1 && info.rfkill);
    info = device_setup(); bar_bad = 1;
    assert(iwlegacy_firmware_check(&info) == IL_ACCESS && !pinned() && command == 1);
    info = device_setup();
    assert(iwlegacy_firmware_check(&info) == IL_TIMEOUT && !pinned() && command == 1);
    /* Clock-ready fixture reaches DMA activation. Missing idle acknowledgments
     * force quarantine, even though PCI bus mastering can be disabled. */
    info = device_setup();
    registers[CSR_GPIO_IN/4] = CSR_GPIO_IN_VAL_VMAIN_PWR_SRC;
    registers[CSR_GP_CNTRL/4] = CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY | CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW;
    assert(iwlegacy_firmware_check(&info) == IL_DMA_STOP && pinned() > 254);
    assert(quarantine_count == 7 && command == 1);
    unsigned saved = allocations, held = pinned();
    assert(iwlegacy_firmware_check(&info) == IL_DMA_STOP && allocations == saved && pinned() == held);
    puts("iwlegacy lifecycle: missing/bad firmware, OOM rollback, busy devices and DMA quarantine passed");
    return 0;
}
