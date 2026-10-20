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
} acpi_fadt_t;

#pragma pack(pop)

static uint32_t pm1a_cnt = 0;
static uint32_t pm1b_cnt = 0;
static uint16_t slp_typa = 0;
static uint16_t slp_typb = 0;
static uint8_t  acpi_initialized = 0;

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

static int acpi_parse_s5(const uint8_t* dsdt_data, uint32_t length) {
    
    for (uint32_t i = 0; i + 8 < length; i++) {
        if (dsdt_data[i] == 0x08 &&
            dsdt_data[i + 1] == '_' && dsdt_data[i + 2] == 'S' &&
            dsdt_data[i + 3] == '5' && dsdt_data[i + 4] == '_') {

            uint32_t p = i + 5;
            
            if (dsdt_data[p] == 0x12) {
                p++;
                
                uint8_t lead = dsdt_data[p];
                uint8_t byte_count = (lead >> 6) & 0x03;
                p += (byte_count == 0) ? 1 : (byte_count + 1);

                p++; 

                
                if (dsdt_data[p] == 0x0A) { 
                    slp_typa = dsdt_data[p + 1];
                    p += 2;
                } else {
                    slp_typa = dsdt_data[p++];
                }

                
                if (dsdt_data[p] == 0x0A) {
                    slp_typb = dsdt_data[p + 1];
                } else {
                    slp_typb = dsdt_data[p];
                }

                debug_print("[ACPI] Found \\_S5 package: TYPa=");
                debug_hex(slp_typa);
                debug_print(" TYPb=");
                debug_hex(slp_typb);
                debug_print("\n");
                return 1;
            }
        }
    }
    return 0;
}

int acpi_init(void) {
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

    pm1a_cnt = fadt->pm1a_cnt_blk;
    pm1b_cnt = fadt->pm1b_cnt_blk;

    
    if (fadt->smi_cmd && fadt->acpi_enable) {
        if (!(inw((uint16_t)pm1a_cnt) & 1)) {
            outb((uint16_t)fadt->smi_cmd, fadt->acpi_enable);
            
            for (int timeout = 0; timeout < 30000; timeout++) {
                if (inw((uint16_t)pm1a_cnt) & 1) break;
                __asm__ volatile ("pause");
            }
        }
    }

    
    acpi_sdt_header_t* dsdt = (acpi_sdt_header_t*)fadt->dsdt;
    if (dsdt && acpi_validate_checksum(dsdt, dsdt->length)) {
        if (acpi_parse_s5((const uint8_t*)dsdt, dsdt->length)) {
            acpi_initialized = 1;
            debug_print("[ACPI] ACPI driver successfully initialized.\n");
            return 1;
        }
    }

    debug_print("[ACPI] Failed to parse \\_S5 state.\n");
    return 0;
}

void acpi_power_off(void) {
    if (!acpi_initialized || !pm1a_cnt) return;

    debug_print("[ACPI] Sending S5 soft power-off command...\n");

    
    __asm__ volatile ("cli");

    
    uint16_t slp_en = (1 << 13);
    uint16_t val_a = (slp_typa << 10) | slp_en;
    outw((uint16_t)pm1a_cnt, val_a);

    if (pm1b_cnt) {
        uint16_t val_b = (slp_typb << 10) | slp_en;
        outw((uint16_t)pm1b_cnt, val_b);
    }

    
    for (volatile int i = 0; i < 1000000; i++) {
        __asm__ volatile ("pause");
    }
}