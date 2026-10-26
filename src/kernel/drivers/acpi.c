#include "acpi.h"
#include "serial.h"

#pragma pack(push, 1)
typedef struct {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
} acpi_rsdp_t;

typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

typedef struct {
    uint8_t  address_space; 
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  access_size;
    uint64_t address;
} acpi_gas_t;

typedef struct {
    acpi_sdt_header_t header;
    uint32_t          firmware_ctrl;
    uint32_t          dsdt;
    uint8_t           reserved;
    uint8_t           preferred_pm_profile;
    uint16_t          sci_int;
    uint32_t          smi_cmd;
    uint8_t           acpi_enable;
    uint8_t           acpi_disable;
    uint8_t           s4bios_req;
    uint8_t           pstate_cnt;
    uint32_t          pm1a_evt_blk;
    uint32_t          pm1b_evt_blk;
    uint32_t          pm1a_cnt_blk;
    uint32_t          pm1b_cnt_blk;
    uint32_t          pm2_cnt_blk;
    uint32_t          pm_tmr_blk;
    uint32_t          gpe0_blk;
    uint32_t          gpe1_blk;
    uint8_t           pm1_evt_len;
    uint8_t           pm1_cnt_len;
    uint8_t           pm2_cnt_len;
    uint8_t           pm_tmr_len;
    uint8_t           gpe0_len;
    uint8_t           gpe1_len;
    uint8_t           gpe1_base;
    uint8_t           cst_cnt;
    uint16_t          p_lvl2_lat;
    uint16_t          p_lvl3_lat;
    uint16_t          flush_size;
    uint16_t          flush_stride;
    uint8_t           duty_offset;
    uint8_t           duty_width;
    uint8_t           day_alarm;
    uint8_t           month_alarm;
    uint8_t           century;
    uint16_t          boot_architecture_flags;
    uint8_t           reserved2;
    uint32_t          flags;
    
    acpi_gas_t        reset_reg;
    uint8_t           reset_value;
    uint8_t           reserved3[3];
} acpi_fadt_t;
#pragma pack(pop)

static uint32_t pm1a_cnt = 0;
static uint32_t pm1b_cnt = 0;
static uint16_t slp_typa = 0;
static uint16_t slp_typb = 0;
static uint8_t  acpi_initialized = 0;
static uint16_t smi_command;
static uint8_t enable_command;
static const char* power_error = "ACPI tables unavailable";
const char* acpi_power_error(void) { return power_error; }

static void acpi_delay_ms(void) {
    for (unsigned i = 0; i < 1000; ++i) outb(0x80, 0);
}
static int acpi_enable_mode(void) {
    if ((inw((uint16_t)pm1a_cnt) & 1) &&
        (!pm1b_cnt || (inw((uint16_t)pm1b_cnt) & 1))) return 1;
    if (!smi_command || !enable_command) return 0;
    outb(smi_command, enable_command);
    for (unsigned i = 0; i < 3000; ++i) {
        if ((inw((uint16_t)pm1a_cnt) & 1) &&
            (!pm1b_cnt || (inw((uint16_t)pm1b_cnt) & 1))) return 1;
        acpi_delay_ms();
    }
    return 0;
}


static uint8_t  reset_supported = 0;
static uint8_t  reset_space = 0;
static uint64_t reset_address = 0;
static uint8_t  reset_value = 0;

static uint8_t acpi_validate_checksum(const void* table, uint32_t length) {
    const uint8_t* bytes = (const uint8_t*)table;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) {
        sum += bytes[i];
    }
    return sum == 0;
}

static int mem_compare(const void* a, const void* b, uint32_t len) {
    const uint8_t* p1 = (const uint8_t*)a;
    const uint8_t* p2 = (const uint8_t*)b;
    for (uint32_t i = 0; i < len; i++) {
        if (p1[i] != p2[i]) return 0;
    }
    return 1;
}

static acpi_rsdp_t* acpi_find_rsdp(void) {
    uint32_t ebda_base = ((uint32_t)*(uint16_t*)0x040E) << 4;
    if (ebda_base >= 0x80000 && ebda_base <= 0x9FFFF) {
        for (uint32_t addr = ebda_base; addr < ebda_base + 1024; addr += 16) {
            if (mem_compare((void*)addr, "RSD PTR ", 8)) {
                if (acpi_validate_checksum((void*)addr, 20)) {
                    return (acpi_rsdp_t*)addr;
                }
            }
        }
    }

    for (uint32_t addr = 0x000E0000; addr < 0x00100000; addr += 16) {
        if (mem_compare((void*)addr, "RSD PTR ", 8)) {
            if (acpi_validate_checksum((void*)addr, 20)) {
                return (acpi_rsdp_t*)addr;
            }
        }
    }
    return 0;
}

/* Decode only literal sleep packages. This is not an AML interpreter. */
static int aml_sleep_integer(const uint8_t* data, uint32_t* cursor,
                             uint32_t end, uint16_t* result) {
    if (*cursor >= end) return 0;
    uint8_t op = data[(*cursor)++];
    uint32_t bytes, value = 0;
    if (op <= 1) { *result = op; return 1; }
    if (op == 0x0A) bytes = 1;
    else if (op == 0x0B) bytes = 2;
    else if (op == 0x0C) bytes = 4;
    else if (op == 0x0E) bytes = 8;
    else return 0;
    if (bytes > end - *cursor) return 0;
    for (uint32_t j = 0; j < bytes; ++j) {
        uint8_t v = data[(*cursor)++];
        if (j < 4) value |= (uint32_t)v << (j * 8);
        else if (v) return 0;
    }
    if (value > 7) return 0;
    *result = (uint16_t)value;
    return 1;
}
static int acpi_parse_s5(const uint8_t* data, uint32_t length) {
    for (uint32_t i = 0; i < length; ++i) {
        if (data[i] != 0x08) continue; /* NameOp */
        uint32_t p = i + 1;
        if (p < length && data[p] == 0x5C) ++p; /* RootChar */
        if (length - p < 6 || !mem_compare(data + p, "_S5_", 4)) continue;
        p += 4;
        if (data[p++] != 0x12) continue; /* PackageOp */
        uint32_t start = p;
        uint8_t lead = data[p++], extra = lead >> 6;
        if (extra > length - p) continue;
        uint32_t size = lead & (extra ? 0x0F : 0x3F);
        for (uint32_t j = 0; j < extra; ++j)
            size |= (uint32_t)data[p++] << (4 + j * 8);
        /* PkgLength includes its own encoding, not the PackageOp. */
        if (size > length - start || size < p - start + 1) continue;
        uint32_t end = start + size;
        if (data[p++] < 2) continue;
        uint16_t a, b;
        if (!aml_sleep_integer(data, &p, end, &a) ||
            !aml_sleep_integer(data, &p, end, &b)) continue;
        slp_typa = a;
        slp_typb = b;
        return 1;
    }
    return 0;
}

int acpi_init(void) {
    acpi_initialized = reset_supported = 0;
    pm1a_cnt = pm1b_cnt = 0;
    smi_command = enable_command = 0;
    power_error = "ACPI tables unavailable";
    debug_print("[ACPI] Locating RSDP...\n");
    acpi_rsdp_t* rsdp = acpi_find_rsdp();
    if (!rsdp) {
        debug_print("[ACPI] RSDP not found.\n");
        return 0;
    }
    acpi_sdt_header_t* rsdt = (acpi_sdt_header_t*)rsdp->rsdt_address;
    if (!rsdt || !acpi_validate_checksum(rsdt, rsdt->length)) {
        debug_print("[ACPI] Invalid RSDT checksum.\n");
        return 0;
    }
    uint32_t num_tables = (rsdt->length - sizeof(acpi_sdt_header_t)) / 4;
    uint32_t* table_pointers = (uint32_t*)((uint32_t)rsdt + sizeof(acpi_sdt_header_t));
    acpi_fadt_t* fadt = 0;
    for (uint32_t i = 0; i < num_tables; i++) {
        acpi_sdt_header_t* header = (acpi_sdt_header_t*)table_pointers[i];
        if (header && mem_compare(header->signature, "FACP", 4)) {
            if (acpi_validate_checksum(header, header->length)) {
                fadt = (acpi_fadt_t*)header;
                break;
            }
        }
    }
    if (!fadt) {
        debug_print("[ACPI] FADT not found.\n");
        return 0;
    }
    if (fadt->header.length < 116) {
        power_error = "ACPI FADT too short";
        return 0;
    }
    pm1a_cnt = fadt->pm1a_cnt_blk;
    pm1b_cnt = fadt->pm1b_cnt_blk;

    
    if ((fadt->flags & (1 << 10)) && (fadt->header.length >= 129)) {
        reset_space = fadt->reset_reg.address_space;
        reset_address = fadt->reset_reg.address;
        reset_value = fadt->reset_value;
        reset_supported = 1;
        debug_print("[ACPI] Reset Register supported: space=");
        debug_hex(reset_space);
        debug_print(" addr=");
        debug_hex((uint32_t)reset_address);
        debug_print(" val=");
        debug_hex(reset_value);
        debug_print("\n");
    }

    if (!pm1a_cnt || pm1a_cnt > 0xFFFE || pm1b_cnt > 0xFFFE ||
        fadt->pm1_cnt_len < 2 || fadt->smi_cmd > 0xFFFF) {
        power_error = "ACPI PM control registers unsupported";
        return 0;
    }
    smi_command = (uint16_t)fadt->smi_cmd;
    enable_command = fadt->acpi_enable;

    acpi_sdt_header_t* dsdt = (acpi_sdt_header_t*)fadt->dsdt;
    if (dsdt && mem_compare(dsdt->signature, "DSDT", 4) &&
        dsdt->length >= sizeof(*dsdt) && dsdt->length <= 1024 * 1024 &&
        acpi_validate_checksum(dsdt, dsdt->length)) {
        if (acpi_parse_s5((const uint8_t*)(dsdt + 1), dsdt->length - sizeof(*dsdt))) {
            acpi_initialized = 1;
            power_error = "ACPI S5 command returned";
            debug_print("[ACPI] ACPI driver successfully initialized.\n");
            return 1;
        }
    }
    power_error = "ACPI literal _S5 package not found";
    debug_print("[ACPI] Failed to parse \\_S5 state.\n");
    return 0;
}

void acpi_reboot(void) {
    if (!reset_supported) return;

    debug_print("[ACPI] Triggering hardware reset via ACPI Reset Register...\n");
    __asm__ volatile ("cli");

    if (reset_space == 1) {
        
        outb((uint16_t)reset_address, reset_value);
    } else if (reset_space == 0) {
        
        volatile uint8_t* ptr = (volatile uint8_t*)((uint32_t)reset_address);
        *ptr = reset_value;
    }

    for (volatile int i = 0; i < 1000000; i++) {
        __asm__ volatile ("pause");
    }
}

void acpi_power_off(void) {
    if (!acpi_initialized || !pm1a_cnt) return;
    /* Firmware may need milliseconds to handle ACPI_ENABLE in SMM. */
    if (!acpi_enable_mode()) {
        power_error = "ACPI enable timed out (SCI_EN clear)";
        return;
    }
    debug_print("[ACPI] Sending S5 soft power-off command...\n");
    __asm__ volatile ("cli");
    const uint16_t sleep_mask = (7u << 10) | (1u << 13);
    uint16_t a = (inw((uint16_t)pm1a_cnt) & ~sleep_mask) | (slp_typa << 10);
    uint16_t b = pm1b_cnt ?
        (inw((uint16_t)pm1b_cnt) & ~sleep_mask) | (slp_typb << 10) : 0;
    /* Program both sleep types before asserting either sleep-enable bit.
     * Preserve SCI_EN and the remaining PM1 control fields. */
    outw((uint16_t)pm1a_cnt, a);
    if (pm1b_cnt) outw((uint16_t)pm1b_cnt, b);
    __asm__ volatile ("wbinvd" ::: "memory");
    outw((uint16_t)pm1a_cnt, a | (1u << 13));
    if (pm1b_cnt) outw((uint16_t)pm1b_cnt, b | (1u << 13));
    for (unsigned i = 0; i < 1000; ++i) acpi_delay_ms();
    power_error = "ACPI S5 command returned";
}
