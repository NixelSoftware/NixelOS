/* SPDX-License-Identifier: GPL-2.0-only
 * Native 3945 firmware-file/DMA lifecycle. RX/TX and firmware algorithms retain
 * the Intel Linux iwlegacy attribution in their corresponding source files.
 */
#include "internal.h"
#include "session.h"
#include "scan.h"
#include "station.h"
#include "fh.h"
#include "../../../../fs.h"
#include "../../../../paging.h"
#include "../../../../mutex.h"
#include "../../../../drivers/delay.h"
#define FW_MAX (24u + 2u * IL3945_INST_MAX + 2u * IL3945_DATA_MAX + IL3945_BOOT_MAX)
typedef struct { uint32_t address, pages; } dma_block_t;
/* If hardware cannot acknowledge DMA stop, retain all posted buffers until
 * reboot. Never hand uncertain DMA memory back to the physical allocator. */
static dma_block_t quarantined[9];
static unsigned quarantine_count;
static mutex_t device_mutex;
static il_station_t station;
static dma_block_t retained[9];
static unsigned retained_count;
static unsigned poll_epoch;
static uint8_t poll_clock_set;
static const pci_device_t* retained_device;
static uint16_t retained_command;
/* All public access is serialized by the Wi-Fi HAL once published. Boot runs
 * under the private mutex before the HAL publishes the connected interface. */
typedef struct {
    const il_network_t* ap;
    const uint8_t* password;
    unsigned password_length;
    wifi_nonce_fn entropy;
    void* entropy_context;
} connect_options_t;

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
/* Linux power_initialize/build_powertable: PCI PM is independent of the
 * firmware's allow-sleep flag. Leave allow-sleep clear for polled operation. */
static int awake_flags(const pci_device_t* dev, uint8_t* flags) {
    uint8_t cap = pci_find_capability(dev,PCI_CAP_PCIE);
    if (!cap || cap > 0xec) return IL_ACCESS;
    uint16_t link = pci_read16(dev,cap + 0x10);
    if (link == 0xffffu) return IL_ACCESS;
    *flags = (link & 1) ? 0 : 8;
    return IL_OK;
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
static void command_snapshot(iwlegacy_info_t* info, const il_command_queue_t* q) {
    info->last_command = q->command;
    info->command_pending = q->pending; info->command_poisoned = q->poisoned;
    info->command_gp1 = q->timeout_gp1;
    info->command_tx_config = q->timeout_tx_config;
    info->command_tx_status = q->timeout_tx_status;
    info->command_gp = q->timeout_gp; info->command_fh = q->timeout_fh;
    info->command_tx_base = q->timeout_tx_base;
    info->command_scd_mode = q->timeout_scd_mode;
    info->command_scd_fifos = q->timeout_scd_fifos;
    info->command_rx_read = q->timeout_rx_read;
    info->command_rx_closed = q->timeout_rx_closed;
    info->command_response_length = q->response_length;
    info->command_response_status = il_le32(q->response);
    info->command_response_id = q->response_id;
    info->command_response_flags = q->response_flags;
}
static int boot_operation(iwlegacy_info_t* info, il_scan_results_t* results, const connect_options_t* connect) {
    MUTEX_GUARD(device_mutex);
    if (retained_count) return IL_ACCESS;
    if (!info || !il3945_matches(info->vendor,info->device) || !info->eeprom.version) return IL_INVALID;
    if (!results && !connect && (info->state == IL_FIRMWARE_READY || info->state == IL_SCAN_COMPLETE)) return IL_OK;
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
    info->scan_completion_length = 0;
    info->scan_expected_channels = info->scan_completed_channels = info->scan_completion_status = 0;
    info->operation_error = 0; info->stop_failures = 0;
    info->error_reply=(il3945_error_reply_t){0};
    info->last_data_tx=(il3945_tx_snapshot_t){0};
    info->firmware_error=(il3945_error_log_t){0};
    info->runtime_error = 0; info->runtime_irq = info->runtime_fh = 0;
    info->data_tx_pending = info->data_tx_poisoned = 0;
    info->init_alive = info->runtime_alive = info->boot_stage = 0;
    info->last_command = info->command_pending = info->command_poisoned = 0;
    command_snapshot(info,&(il_command_queue_t){0});
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
    dma_block_t blocks[9] = {{0}};
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
    if (results || connect) {
        blocks[count] = allocate(PAGE_SIZE);
        if (!blocks[count].address) goto free_blocks;
        command_buffer = blocks[count++].address;
    }
    uint32_t tx_buffer = 0;
    if (connect) {
        blocks[count] = allocate(PAGE_SIZE);
        if (!blocks[count].address) goto free_blocks;
        tx_buffer = blocks[count++].address;
    }
    il_rx_buffer_t buffers[254];
    for (unsigned i = 0; i < 254; i++) buffers[i] = (il_rx_buffer_t){
        (uint8_t*)(unsigned long)(rx + i * IL_RX_BYTES),rx + i * IL_RX_BYTES};
    il_rx_queue_t queue = {0};
    result = il3945_rx_prepare(&queue,(volatile uint32_t*)(unsigned long)control,control,
        (volatile uint16_t*)(unsigned long)(control + PAGE_SIZE),control + PAGE_SIZE,buffers);
    if (result) goto free_blocks;
    uint16_t inherited = pci_read16(dev,4);
    uint16_t command = inherited & ~PCI_COMMAND_MASTER;
    pci_write16(dev,4,command);
    if (pci_read16(dev,4) & PCI_COMMAND_MASTER) { result = IL_ACCESS; goto free_blocks; }
    pci_bar_t bar;
    if (!pci_probe_bar(dev,0,&bar) || bar.io || !bar.address || bar.address > 0xFFFFF000ull ||
        bar.size < PAGE_SIZE || !pci_enable_device(dev,PCI_COMMAND_MEMORY) ||
        !paging_map_mmio_page((uint32_t)bar.address)) {
        pci_write16(dev,4,command); result = IL_ACCESS; goto free_blocks;
    }
    /* Upstream 3945 PCI probe disables RETRY_TIMEOUT: PCI TX retries can
     * interfere with CPU C3 transitions and stall command/data DMA. Apply
     * after D0/BAR setup and before any bus-master activation. */
    pci_write8(dev,0x41,0);
    il_io_t io = {(void*)(unsigned long)bar.address,read_reg,write_reg,wait_us};
    uint32_t gp = read_reg(io.context,CSR_GP_CNTRL);
    if (gp == 0xFFFFFFFFu) {
        pci_write16(dev,4,command); result = IL_ACCESS; goto free_blocks;
    }
    if ((inherited & PCI_COMMAND_MASTER) || (gp & CSR_GP_CNTRL_REG_FLAG_INIT_DONE)) {
        write_reg(io.context,CSR_INT_MASK,0);
        result = il3945_reset_unowned(&io);
        if (result) { pci_write16(dev,4,command); goto free_blocks; }
    }
    info->rfkill = !(gp & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW);
    if (info->rfkill) {
        pci_write16(dev,4,command); result = IL_RFKILL; goto free_blocks;
    }
    pci_write16(dev,4,command | PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_OFF);
    write_reg(io.context,CSR_INT_MASK,0);
    int touched_dma = 0;
    uint8_t power_flags = 0;
    result = awake_flags(dev,&power_flags);
    if (!result) result = il3945_apm_start(&io);
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
            il3945_alive_t alive = {0};
            result = il3945_firmware_run(&io,&queue,file,(uint32_t)bytes,&dma,&alive);
            info->init_alive = alive.init_alive; info->runtime_alive = alive.runtime_alive; info->boot_stage = alive.stage;
            if (!result) info->firmware_version = fw.version;
            if (!result && connect) {
                station = (il_station_t){0}; station.io = io; station.rx = queue; station.awake_flags = power_flags;
                station.error_log_base = alive.error_log;
                result = il3945_command_prepare(&station.commands,(void*)(unsigned long)rings[4],
                    (uint8_t*)(unsigned long)command_buffer,command_buffer);
                if (!result) result = il3945_tx_prepare(&station.tx,(void*)(unsigned long)rings[0],
                    (uint8_t*)(unsigned long)tx_buffer,tx_buffer);
                if (!result) result = il3945_station_connect(&station,&info->eeprom,connect->ap,
                    connect->password,connect->password_length,connect->entropy,connect->entropy_context);
                command_snapshot(info,&station.commands);
                if (!result) {
                    for (unsigned i = 0; i < count; i++) retained[i] = blocks[i];
                    poll_clock_set = 0; retained_count = count; iwlegacy_native_owned = 1; retained_device = dev; retained_command = command;
                    release(raw); info->state = IL_CONNECTED; info->error = IL_OK;
                    return IL_OK;
                }
            }
            if (!result && results) {
                il_command_queue_t commands = {0};
                result = il3945_command_prepare(&commands,(void*)(unsigned long)rings[4],
                    (uint8_t*)(unsigned long)command_buffer,command_buffer);
                /* Match runtime ALIVE startup: awake mode and BT configuration
                 * precede the first RXON, including receive-only scanning. */
                uint8_t awake[32] = {0}, bt[12] = {0};
                awake[0] = power_flags;
                if (!result) result = il3945_command_send(&io,&queue,&commands,0x77,awake,sizeof(awake),1000,0,0);
                if (!result) result = il3945_command_send(&io,&queue,&commands,0x9b,bt,sizeof(bt),1000,0,0);
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
                if (!result) result = il3945_scan_configure(&io,&queue,&commands,&info->eeprom,
                    info->eeprom.channels[first].band,info->eeprom.channels[first].number);
                if (!result) result = il3945_scan_run(&io,&queue,&commands,&info->eeprom,results);
                command_snapshot(info,&commands);
                info->scan_completion_length = results->completion_length;
                info->scan_expected_channels = results->expected_channels;
                info->scan_completed_channels = results->completed_channels;
                info->scan_completion_status = results->completion_status;
            }
        }
    }
    info->operation_error = result;
    int rx_stopped = il3945_rx_stop(&io,&queue);
    int tx_stopped = tx_stop(&io);
    int stopped = il3945_apm_stop(&io);
    pci_set_master(dev,0);
    int master_off = !(pci_read16(dev,4) & PCI_COMMAND_MASTER);
    info->stop_failures = (rx_stopped ? 1 : 0) | (tx_stopped ? 2 : 0) |
        (stopped ? 4 : 0) | (!master_off ? 8 : 0);
    /* MASTER_DISABLED acknowledges the entire device's DMA engine, superseding
     * individual queue-idle timeouts. PCI mastering must also be disabled.
     * Without global acknowledgement, keep the posted allocations quarantined. */
    if (touched_dma && (stopped || !master_off)) {
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
    } else if (connect && info->firmware_version && result != IL_DMA_STOP) info->state = IL_CONNECTION_FAILED;
    else if (results && info->firmware_version && result != IL_DMA_STOP) info->state = IL_SCAN_FAILED;
    return result;
}

int iwlegacy_firmware_check(iwlegacy_info_t* info) { return boot_operation(info,0,0); }
int iwlegacy_scan(iwlegacy_info_t* info, il_scan_results_t* results) {
    if (!results) return IL_INVALID;
    *results = (il_scan_results_t){0};
    int result = boot_operation(info,results,0);
    if (result) results->count = 0; /* Never publish stale/partial scans as successful. */
    return result;
}

int iwlegacy_connect(iwlegacy_info_t* info, const il_network_t* ap,
                     const uint8_t* password, unsigned password_length,
                     wifi_nonce_fn entropy, void* entropy_context) {
    if (!ap || (password_length && !password)) return IL_INVALID;
    connect_options_t options = {ap,password,password_length,entropy,entropy_context};
    return boot_operation(info,0,&options);
}
int iwlegacy_disconnect(void) {
    il3945_station_leave(&station);
    if (!retained_count) return IL_OK;
    int rx = il3945_rx_stop(&station.io,&station.rx);
    int tx = tx_stop(&station.io);
    int apm = il3945_apm_stop(&station.io);
    pci_set_master(retained_device,0);
    (void)rx; (void)tx; /* Global stop acknowledgement supersedes queue-idle status. */
    int stopped = !apm && !(pci_read16(retained_device,4)&PCI_COMMAND_MASTER);
    pci_write16(retained_device,4,retained_command & ~PCI_COMMAND_MASTER);
    for (unsigned i=0;i<retained_count;i++) {
        if(stopped) release(retained[i]);
        else quarantined[quarantine_count++]=retained[i];
    }
    retained_count=0; iwlegacy_native_owned=0; retained_device=0;
    return stopped ? IL_OK : IL_DMA_STOP;
}
int iwlegacy_poll(unsigned elapsed_ms) {
    if(!retained_count) return IL_ACCESS;
    if (!poll_clock_set) { poll_epoch=elapsed_ms-station.now_ms; poll_clock_set=1; }
    int r=il3945_station_poll(&station,elapsed_ms-poll_epoch);
    if(r) iwlegacy_disconnect();
    return r;
}
int iwlegacy_send(const uint8_t* frame,unsigned size) { return il3945_station_send(&station,frame,size); }
int iwlegacy_connected(void) { return station.active; }
void iwlegacy_set_receiver(net_receive_handler_t receive) { station.receive=receive; }
void iwlegacy_get_diagnostics(net_diagnostics_t* out) { if(out) *out=station.diagnostics; }

void iwlegacy_get_runtime_status(iwlegacy_info_t* info) {
    if (!info) return;
    info->error_reply=station.error_reply;
    info->last_data_tx=station.last_data_tx.valid ? station.last_data_tx : station.tx.submitted;
    info->firmware_error=station.firmware_error;
    info->runtime_error=station.runtime_error;
    info->runtime_irq=station.runtime_irq; info->runtime_fh=station.runtime_fh;
    info->data_tx_pending=station.tx.pending; info->data_tx_poisoned=station.tx.poisoned;
}
