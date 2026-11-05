/* SPDX-License-Identifier: GPL-2.0-only
 * Native 3945 firmware-file/DMA lifecycle. RX/TX and firmware algorithms retain
 * the Intel Linux iwlegacy attribution in their corresponding source files.
 */
#include "internal.h"
#include "session.h"
#include "scan.h"
#include "fh.h"
#include "../../../../fs.h"
#include "../../../../paging.h"
#include "../../../../mutex.h"
#include "../../../../drivers/delay.h"
#define FW_MAX (24u + 2u * IL3945_INST_MAX + 2u * IL3945_DATA_MAX + IL3945_BOOT_MAX)
typedef struct { uint32_t address, pages; } dma_block_t;
/* If hardware cannot acknowledge DMA stop, retain all posted buffers until
 * reboot. Never hand uncertain DMA memory back to the physical allocator. */
static dma_block_t quarantined[8];
static unsigned quarantine_count;
static mutex_t device_mutex;
static dma_block_t allocate(unsigned bytes) {
    dma_block_t b = {0,(bytes + PAGE_SIZE - 1) / PAGE_SIZE};
    b.address = paging_alloc_contiguous(b.pages);
    return b;
}
static void release(dma_block_t b) {
    if (!b.address) return;
    for (unsigned i = 0; i < b.pages; i++) paging_frame_free(b.address + i * PAGE_SIZE);
}
static uint32_t read_reg(void* ctx, uint32_t reg) {
    return *(volatile uint32_t*)((uint8_t*)ctx + reg);
}
static void write_reg(void* ctx, uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)((uint8_t*)ctx + reg) = value;
}
static void wait_us(void* ctx, uint32_t us) { (void)ctx; driver_delay_us(us); }
static int nic_config(const il_io_t* io, const iwlegacy_info_t* info) {
    int result = il_grab(io);
    if (result) return result;
    uint32_t power = il_prph_read(io,APMG_PS_CTRL_REG);
    if (power != 0xFFFFFFFFu)
        il_prph_write(io,APMG_PS_CTRL_REG,power & ~APMG_PS_CTRL_MSK_PWR_SRC);
    il_release(io);
    if (power == 0xFFFFFFFFu) return IL_ACCESS;
    result = il_poll(io,CSR_GPIO_IN,CSR_GPIO_IN_BIT_AUX_POWER,CSR_GPIO_IN_VAL_VMAIN_PWR_SRC,5000);
    if (result) return result;
    uint32_t clear = CSR39_HW_IF_CONFIG_REG_BIT_BOARD_TYPE, set = 0;
    if (!(info->revision & 0x80)) set |= info->revision & 0x40 ?
        CSR39_HW_IF_CONFIG_REG_BIT_3945_MB : CSR39_HW_IF_CONFIG_REG_BIT_3945_MM;
    if (info->eeprom.sku == 0x80) set |= CSR39_HW_IF_CONFIG_REG_BIT_SKU_MRC;
    if ((info->eeprom.board_revision & 0xF0) == 0xD0) set |= CSR39_HW_IF_CONFIG_REG_BIT_BOARD_TYPE;
    if (info->eeprom.radio_revision > 1) set |= CSR39_HW_IF_CONFIG_REG_BITS_SILICON_TYPE_B;
    return il_update(io,CSR_HW_IF_CONFIG_REG,clear,set);
}
static int tx_stop(const il_io_t* io) {
    int result = il_grab(io);
    if (result) return result;
    il_prph_write(io,ALM_SCD_MODE_REG,0); il_prph_write(io,ALM_SCD_TXFACT_REG,0);
    for (unsigned i = 0; i < 5; i++) {
        io->write32(io->context,FH39_TCSR_CONFIG(i),0);
        int stopped = il_poll(io,FH39_TSSR_TX_STATUS,
            FH39_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(i),FH39_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(i),1000);
        if (stopped) result = stopped;
    }
    il_release(io);
    return result;
}
static int boot_operation(iwlegacy_info_t* info, il_scan_results_t* results) {
    MUTEX_GUARD(device_mutex);
    if (!info || !il3945_matches(info->vendor,info->device) || !info->eeprom.version) return IL_INVALID;
    if (!results && (info->state == IL_FIRMWARE_READY || info->state == IL_SCAN_COMPLETE)) return IL_OK;
    if (quarantine_count) { info->state = IL_FIRMWARE_FAILED; return info->error = IL_DMA_STOP; }
    const pci_device_t* dev = 0;
    for (unsigned i = 0; i < pci_device_count(); i++) {
        const pci_device_t* p = pci_device_at(i);
        if (p && p->bus == info->pci_bus && p->slot == info->pci_slot && p->function == info->pci_function &&
            p->vendor_id == info->vendor && p->device_id == info->device) { dev = p; break; }
    }
    if (!dev) return info->error = IL_ACCESS;
    info->state = IL_FIRMWARE_FAILED;
    info->firmware_version = 0; info->scan_count = 0;
    dma_block_t raw = allocate(FW_MAX + 1);
    if (!raw.address) return info->error = IL_NOMEM;
    uint8_t* file = (uint8_t*)(unsigned long)raw.address;
    int bytes = fs_read_file_at("/lib/firmware/iwlwifi-3945-2.ucode",file,FW_MAX + 1,0);
    if (bytes < 0) bytes = fs_read_file_at("/lib/firmware/iwlwifi-3945-1.ucode",file,FW_MAX + 1,0);
    il3945_firmware_t fw;
    if (bytes < 0) {
        release(raw); info->state = IL_FIRMWARE_MISSING; return info->error = IL_NO_FIRMWARE;
    }
    if (!il3945_firmware_parse(file,(uint32_t)bytes,&fw)) {
        release(raw); return info->error = IL_INVALID;
    }
    dma_block_t blocks[8] = {{0}};
    unsigned count = 0;
    il_fw_section_t images[4] = {fw.init_code,fw.init_data,fw.runtime_code,fw.runtime_data};
    int result = IL_NOMEM;
    for (unsigned i = 0; i < 4; i++) {
        blocks[count] = allocate(images[i].length);
        if (!blocks[count].address) goto free_blocks;
        uint8_t* dest = (uint8_t*)(unsigned long)blocks[count++].address;
        for (unsigned j = 0; j < images[i].length; j++) dest[j] = images[i].data[j];
    }
    blocks[count] = allocate(3 * PAGE_SIZE); /* RBD, status, shared table */
    if (!blocks[count].address) goto free_blocks;
    uint32_t control = blocks[count++].address;
    blocks[count] = allocate(5 * 16384); /* Zero-filled TFD rings, no TX posted. */
    if (!blocks[count].address) goto free_blocks;
    uint32_t tx = blocks[count++].address;
    blocks[count] = allocate(254 * IL_RX_BYTES);
    if (!blocks[count].address) goto free_blocks;
    uint32_t rx = blocks[count++].address;
    uint32_t command_buffer = 0;
    if (results) {
        blocks[count] = allocate(PAGE_SIZE);
        if (!blocks[count].address) goto free_blocks;
        command_buffer = blocks[count++].address;
    }
    il_rx_buffer_t buffers[254];
    for (unsigned i = 0; i < 254; i++) buffers[i] = (il_rx_buffer_t){
        (uint8_t*)(unsigned long)(rx + i * IL_RX_BYTES),rx + i * IL_RX_BYTES};
    il_rx_queue_t queue = {0};
    result = il3945_rx_prepare(&queue,(volatile uint32_t*)(unsigned long)control,control,
        (volatile uint16_t*)(unsigned long)(control + PAGE_SIZE),control + PAGE_SIZE,buffers);
    if (result) goto free_blocks;
    uint16_t command = pci_read16(dev,4);
    if (command & PCI_COMMAND_MASTER) { result = IL_ACCESS; goto free_blocks; }
    pci_bar_t bar;
    if (!pci_probe_bar(dev,0,&bar) || bar.io || !bar.address || bar.address > 0xFFFFF000ull ||
        bar.size < PAGE_SIZE || !pci_enable_device(dev,PCI_COMMAND_MEMORY)) {
        pci_write16(dev,4,command); result = IL_ACCESS; goto free_blocks;
    }
    il_io_t io = {(void*)(unsigned long)bar.address,read_reg,write_reg,wait_us};
    uint32_t gp = read_reg(io.context,CSR_GP_CNTRL);
    if (gp == 0xFFFFFFFFu || (gp & CSR_GP_CNTRL_REG_FLAG_INIT_DONE)) {
        pci_write16(dev,4,command); result = IL_ACCESS; goto free_blocks;
    }
    info->rfkill = !(gp & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW);
    if (info->rfkill) {
        pci_write16(dev,4,command); result = IL_RFKILL; goto free_blocks;
    }
    pci_write16(dev,4,command | PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_OFF);
    write_reg(io.context,CSR_INT_MASK,0);
    int touched_dma = 0;
    result = il3945_apm_start(&io);
    if (!result) result = nic_config(&io,info);
    if (!result) result = il3945_rx_start(&io,&queue);
    uint32_t rings[5];
    for (unsigned i = 0; i < 5; i++) rings[i] = tx + i * 16384;
    if (!result) result = il3945_tx_setup(&io,(volatile uint32_t*)(unsigned long)(control + 2*PAGE_SIZE),
                                         control + 2*PAGE_SIZE,rings);
    if (!result) {
        pci_set_master(dev,1);
        touched_dma = 1;
        if (!(pci_read16(dev,4) & PCI_COMMAND_MASTER)) result = IL_ACCESS;
        else {
            il3945_fw_dma_t dma = {blocks[0].address,blocks[1].address,blocks[2].address,blocks[3].address};
            il3945_alive_t alive;
            result = il3945_firmware_run(&io,&queue,file,(uint32_t)bytes,&dma,&alive);
            if (!result) info->firmware_version = fw.version;
            if (!result && results) {
                il_command_queue_t commands = {0};
                result = il3945_command_prepare(&commands,(void*)(unsigned long)rings[4],
                    (uint8_t*)(unsigned long)command_buffer,command_buffer);
                uint8_t rxon[44] = {0};
                for (unsigned i = 0; i < 6; i++) rxon[i] = info->eeprom.mac[i];
                rxon[24] = 3; rxon[25] = 10; rxon[28] = 0xFF; rxon[29] = 15;
                unsigned first = 0;
                while (first < IL3945_CHANNEL_COUNT && !(info->eeprom.channels[first].flags & 1)) first++;
                if (first == IL3945_CHANNEL_COUNT) result = IL_BAD_EEPROM;
                else {
                    rxon[32] = info->eeprom.channels[first].band == 2 ? 5 : 0;
                    rxon[33] = 0x80; /* TSF to host */
                    rxon[36] = 4 | 8 | 16 | 64;
                    rxon[40] = info->eeprom.channels[first].number;
                }
                if (!result) result = il3945_command_send(&io,&queue,&commands,0x10,rxon,sizeof(rxon),1000,0,0);
                if (!result) result = il3945_scan_run(&io,&queue,&commands,&info->eeprom,results);
            }
        }
    }
    int rx_stopped = il3945_rx_stop(&io,&queue);
    int tx_stopped = tx_stop(&io);
    int stopped = il3945_apm_stop(&io);
    pci_set_master(dev,0);
    int master_off = !(pci_read16(dev,4) & PCI_COMMAND_MASTER);
    if (touched_dma && (rx_stopped || tx_stopped || stopped || !master_off)) {
        for (unsigned i = 0; i < count; i++) quarantined[quarantine_count++] = blocks[i];
        count = 0; result = IL_DMA_STOP;
    }
    /* Never restore bus mastering; initial command was checked idle. */
    pci_write16(dev,4,command & ~PCI_COMMAND_MASTER);
free_blocks:
    for (unsigned i = 0; i < count; i++) release(blocks[i]);
    release(raw);
    info->error = result;
    if (!result) {
        info->state = results ? IL_SCAN_COMPLETE : IL_FIRMWARE_READY;
        info->scan_count = results ? results->count : 0;
    } else if (results && info->firmware_version && result != IL_DMA_STOP) info->state = IL_SCAN_FAILED;
    return result;
}

int iwlegacy_firmware_check(iwlegacy_info_t* info) { return boot_operation(info,0); }
int iwlegacy_scan(iwlegacy_info_t* info, il_scan_results_t* results) {
    if (!results) return IL_INVALID;
    *results = (il_scan_results_t){0};
    int result = boot_operation(info,results);
    if (result) results->count = 0; /* Never publish stale/partial scans as successful. */
    return result;
}
