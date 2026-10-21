#include "fs.h"
#include "drivers/ata.h"
#include "drivers/serial.h"

static fat32_bpb_t bpb;
static uint32_t fat_begin_lba = 0;
static uint32_t cluster_begin_lba = 0;
static uint32_t sectors_per_cluster = 0;
static uint32_t root_cluster = 0;
static uint8_t  sector_buffer[512];
static uint8_t  cluster_scratch[4096];
static uint8_t  fs_ready = 0;
static uint32_t last_alloc_cluster = 2;

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    return c;
}

static int str_len(const char* s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void str_copy(char* dest, const char* src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}


static void filename_to_83(const char* name, char out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    if (!name || !*name) return;

    
    if (name[0] == '.' && name[1] == '\0') {
        out[0] = '.';
        return;
    }
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        out[0] = '.';
        out[1] = '.';
        return;
    }

    int p = 0;
    int out_idx = 0;
    while (name[p] && name[p] != '.' && out_idx < 8) {
        out[out_idx++] = to_upper(name[p++]);
    }
    while (name[p] && name[p] != '.') p++;
    if (name[p] == '.') {
        p++;
        out_idx = 8;
        while (name[p] && out_idx < 11) {
            out[out_idx++] = to_upper(name[p++]);
        }
    }
}


static void format_83_to_filename(const char name[11], char* out) {
    int p = 0;
    for (int i = 0; i < 8; i++) {
        if (name[i] != ' ') out[p++] = name[i];
    }
    if (name[8] != ' ') {
        out[p++] = '.';
        for (int i = 8; i < 11; i++) {
            if (name[i] != ' ') out[p++] = name[i];
        }
    }
    out[p] = '\0';
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return cluster_begin_lba + ((cluster - 2) * sectors_per_cluster);
}

static uint32_t get_next_cluster(uint32_t current_cluster) {
    uint32_t fat_offset = current_cluster * 4;
    uint32_t fat_sector = fat_begin_lba + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    if (!ata_read_sectors(fat_sector, 1, sector_buffer)) return 0x0FFFFFFF;
    uint32_t next = *(uint32_t*)&sector_buffer[entry_offset];
    return next & 0x0FFFFFFF;
}

static void set_cluster_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_begin_lba + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    if (!ata_read_sectors(fat_sector, 1, sector_buffer)) return;
    uint32_t val = (*(uint32_t*)&sector_buffer[entry_offset]) & 0xF0000000;
    val |= (value & 0x0FFFFFFF);
    *(uint32_t*)&sector_buffer[entry_offset] = val;
    ata_write_sectors(fat_sector, 1, sector_buffer);

    
    if (bpb.num_fats > 1) {
        uint32_t mirror_sector = fat_sector + bpb.sectors_per_fat_32;
        ata_write_sectors(mirror_sector, 1, sector_buffer);
    }
}

static uint32_t allocate_free_cluster(void) {
    uint32_t total_clusters = (bpb.total_sectors_32 - (cluster_begin_lba - bpb.hidden_sectors)) / sectors_per_cluster;
    uint32_t start_cluster = last_alloc_cluster;

    for (uint32_t clus = start_cluster; clus < total_clusters; clus++) {
        uint32_t fat_offset = clus * 4;
        uint32_t fat_sector = fat_begin_lba + (fat_offset / 512);
        uint32_t entry_offset = fat_offset % 512;

        if (!ata_read_sectors(fat_sector, 1, sector_buffer)) return 0;
        uint32_t val = *(uint32_t*)&sector_buffer[entry_offset];

        if ((val & 0x0FFFFFFF) == 0) {
            set_cluster_entry(clus, 0x0FFFFFFF);
            last_alloc_cluster = clus + 1;
            return clus;
        }
    }

    for (uint32_t clus = 2; clus < start_cluster; clus++) {
        uint32_t fat_offset = clus * 4;
        uint32_t fat_sector = fat_begin_lba + (fat_offset / 512);
        uint32_t entry_offset = fat_offset % 512;

        if (!ata_read_sectors(fat_sector, 1, sector_buffer)) return 0;
        uint32_t val = *(uint32_t*)&sector_buffer[entry_offset];

        if ((val & 0x0FFFFFFF) == 0) {
            set_cluster_entry(clus, 0x0FFFFFFF);
            last_alloc_cluster = clus + 1;
            return clus;
        }
    }

    return 0;
}

static void free_cluster_chain(uint32_t start_cluster) {
    uint32_t curr = start_cluster;
    while (curr >= 2 && curr < 0x0FFFFFF8) {
        uint32_t next = get_next_cluster(curr);
        set_cluster_entry(curr, 0x00000000);
        curr = next;
    }
}


static int find_in_directory(uint32_t dir_cluster, const char target_83[11],
                             fat32_dir_entry_t* out_entry, uint32_t* out_dir_lba, uint32_t* out_dir_offset) {
    uint32_t curr_cluster = dir_cluster;
    while (curr_cluster >= 2 && curr_cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(curr_cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (!ata_read_sectors(lba + s, 1, sector_buffer)) return 0;
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
            for (int i = 0; i < 16; i++) {
                if ((uint8_t)entries[i].name[0] == 0x00) return 0; 
                if ((uint8_t)entries[i].name[0] == 0xE5) continue; 
                if (entries[i].attr == FAT_ATTR_LFN) continue;

                int match = 1;
                for (int c = 0; c < 11; c++) {
                    if (entries[i].name[c] != target_83[c]) { match = 0; break; }
                }
                if (match) {
                    if (out_entry) *out_entry = entries[i];
                    if (out_dir_lba) *out_dir_lba = lba + s;
                    if (out_dir_offset) *out_dir_offset = i * sizeof(fat32_dir_entry_t);
                    return 1;
                }
            }
        }
        curr_cluster = get_next_cluster(curr_cluster);
    }
    return 0;
}


static int resolve_path(const char* path, uint32_t* parent_cluster, char final_83[11]) {
    *parent_cluster = root_cluster;
    if (!path || !*path) return 0;

    int p = 0;
    if (path[p] == '/') p++;

    char segment[FS_MAX_FILENAME];
    while (path[p]) {
        int seg_len = 0;
        while (path[p] && path[p] != '/' && seg_len < FS_MAX_FILENAME - 1) {
            segment[seg_len++] = path[p++];
        }
        segment[seg_len] = '\0';
        if (path[p] == '/') p++;

        char target_83[11];
        filename_to_83(segment, target_83);

        if (path[p] == '\0') {
            
            for (int i = 0; i < 11; i++) final_83[i] = target_83[i];
            return 1;
        }

        
        fat32_dir_entry_t dir_ent;
        if (!find_in_directory(*parent_cluster, target_83, &dir_ent, 0, 0)) return 0;
        if (!(dir_ent.attr & FAT_ATTR_DIRECTORY)) return 0;

        uint32_t sub_cluster = ((uint32_t)dir_ent.first_cluster_hi << 16) | dir_ent.first_cluster_lo;
        *parent_cluster = (sub_cluster == 0) ? root_cluster : sub_cluster;
    }
    return 0;
}

void fs_init(void) {
    fs_ready = 0;
    debug_print("[FAT32] Initializing filesystem at LBA 500...\n");

    
    uint32_t partition_offset = 500;
    if (!ata_read_sectors(partition_offset, 1, sector_buffer)) {
        debug_print("[FAT32] Failed to read LBA 500.\n");
        return;
    }

    fat32_bpb_t* b = (fat32_bpb_t*)sector_buffer;
    if (b->bytes_per_sector != 512 || b->sectors_per_fat_32 == 0) {
        debug_print("[FAT32] Valid FAT32 Volume not found at LBA 500.\n");
        return;
    }

    bpb = *b;
    sectors_per_cluster = bpb.sectors_per_cluster;
    root_cluster = bpb.root_cluster;
    fat_begin_lba = partition_offset + bpb.reserved_sector_count;
    cluster_begin_lba = fat_begin_lba + (bpb.num_fats * bpb.sectors_per_fat_32);
    fs_ready = 1;

    debug_print("[FAT32] Mounted successfully! Root cluster: ");
    debug_hex(root_cluster);
    debug_print("\n");
}

int fs_list_dir(const char* path, char file_list_out[FS_MAX_FILES][FS_MAX_FILENAME]) {
    if (!fs_ready) return 0;
    uint32_t dir_cluster = root_cluster;

    if (path && path[0] && (path[0] != '/' || path[1] != '\0')) {
        char target_83[11];
        if (!resolve_path(path, &dir_cluster, target_83)) {
            fat32_dir_entry_t ent;
            if (find_in_directory(dir_cluster, target_83, &ent, 0, 0) && (ent.attr & FAT_ATTR_DIRECTORY)) {
                dir_cluster = ((uint32_t)ent.first_cluster_hi << 16) | ent.first_cluster_lo;
                if (dir_cluster == 0) dir_cluster = root_cluster;
            } else {
                return 0;
            }
        }
    }

    int count = 0;
    uint32_t curr_cluster = dir_cluster;
    while (curr_cluster >= 2 && curr_cluster < 0x0FFFFFF8 && count < FS_MAX_FILES) {
        uint32_t lba = cluster_to_lba(curr_cluster);
        for (uint32_t s = 0; s < sectors_per_cluster && count < FS_MAX_FILES; s++) {
            if (!ata_read_sectors(lba + s, 1, sector_buffer)) return count;
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
            for (int i = 0; i < 16 && count < FS_MAX_FILES; i++) {
                if ((uint8_t)entries[i].name[0] == 0x00) return count;
                if ((uint8_t)entries[i].name[0] == 0xE5) continue;
                if (entries[i].attr == FAT_ATTR_LFN || (entries[i].attr & FAT_ATTR_VOLUME_ID)) continue;

                
                if (entries[i].name[0] == '.') continue;

                format_83_to_filename(entries[i].name, file_list_out[count++]);
            }
        }
        curr_cluster = get_next_cluster(curr_cluster);
    }
    return count;
}

int fs_list(char file_list_out[FS_MAX_FILES][FS_MAX_FILENAME]) {
    return fs_list_dir("/", file_list_out);
}

int fs_read_file(const char* filename, uint8_t* dest, uint32_t max_bytes) {
    return fs_read_file_at(filename, dest, max_bytes, 0);
}

int fs_read_file_at(const char* filename, uint8_t* dest, uint32_t capacity,
                    uint32_t offset) {
    if (!fs_ready || !filename) return -1;
    uint32_t parent;
    char name[11];
    fat32_dir_entry_t entry;
    if (!resolve_path(filename, &parent, name) ||
        !find_in_directory(parent, name, &entry, 0, 0) ||
        (entry.attr & FAT_ATTR_DIRECTORY)) return -1;
    if (!dest || !capacity) return (int)entry.file_size;
    if (offset >= entry.file_size) return 0;
    uint32_t remaining = entry.file_size - offset;
    if (remaining > capacity) remaining = capacity;
    uint32_t cluster = ((uint32_t)entry.first_cluster_hi << 16) | entry.first_cluster_lo;
    uint32_t cluster_bytes = sectors_per_cluster * 512u;
    uint32_t hops = 0;
    while (offset >= cluster_bytes && cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (++hops > entry.file_size / cluster_bytes + 1u) return -1;
        cluster = get_next_cluster(cluster);
        offset -= cluster_bytes;
    }
    uint32_t copied = 0;
    while (cluster >= 2 && cluster < 0x0FFFFFF8 && copied < remaining) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t sector = offset / 512u;
             sector < sectors_per_cluster && copied < remaining; sector++) {
            if (!ata_read_sectors(lba + sector, 1, sector_buffer)) return -1;
            uint32_t start = offset % 512u;
            uint32_t chunk = 512u - start;
            if (chunk > remaining - copied) chunk = remaining - copied;
            for (uint32_t i = 0; i < chunk; i++) dest[copied+i] = sector_buffer[start+i];
            copied += chunk;
            offset = 0;
        }
        cluster = get_next_cluster(cluster);
    }
    return copied == remaining ? (int)copied : -1;
}

int fs_delete_file(const char* filename) {
    if (!fs_ready || !filename) return 0;

    uint32_t parent_cluster;
    char target_83[11];
    if (!resolve_path(filename, &parent_cluster, target_83)) return 0;

    fat32_dir_entry_t entry;
    uint32_t dir_lba, dir_offset;
    if (!find_in_directory(parent_cluster, target_83, &entry, &dir_lba, &dir_offset)) return 0;

    
    uint32_t start_cluster = ((uint32_t)entry.first_cluster_hi << 16) | entry.first_cluster_lo;
    if (start_cluster >= 2) free_cluster_chain(start_cluster);

    
    if (ata_read_sectors(dir_lba, 1, sector_buffer)) {
        sector_buffer[dir_offset] = 0xE5;
        ata_write_sectors(dir_lba, 1, sector_buffer);
        return 1;
    }
    return 0;
}

int fs_write_file(const char* filename, const uint8_t* src, uint32_t size_bytes) {
    if (!fs_ready || !filename) return -1;

    uint32_t parent_cluster;
    char target_83[11];
    if (!resolve_path(filename, &parent_cluster, target_83)) return -1;

    fat32_dir_entry_t entry;
    uint32_t dir_lba = 0, dir_offset = 0;
    int exists = find_in_directory(parent_cluster, target_83, &entry, &dir_lba, &dir_offset);

    if (exists) {
        uint32_t old_cluster = ((uint32_t)entry.first_cluster_hi << 16) | entry.first_cluster_lo;
        if (old_cluster >= 2) free_cluster_chain(old_cluster);
    } else {
        uint32_t curr = parent_cluster;
        int found_slot = 0;
        while (curr >= 2 && curr < 0x0FFFFFF8 && !found_slot) {
            uint32_t lba = cluster_to_lba(curr);
            for (uint32_t s = 0; s < sectors_per_cluster && !found_slot; s++) {
                if (!ata_read_sectors(lba + s, 1, sector_buffer)) return -1;
                fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
                for (int i = 0; i < 16; i++) {
                    if ((uint8_t)entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) {
                        dir_lba = lba + s;
                        dir_offset = i * sizeof(fat32_dir_entry_t);
                        found_slot = 1;
                        break;
                    }
                }
            }
            if (!found_slot) {
                uint32_t next = get_next_cluster(curr);
                if (next >= 0x0FFFFFF8) {
                    uint32_t new_c = allocate_free_cluster();
                    if (!new_c) return -1;
                    set_cluster_entry(curr, new_c);
                    next = new_c;
                }
                curr = next;
            }
        }
        if (!found_slot) return -1;
    }

    uint32_t first_cluster = 0;
    uint32_t prev_cluster = 0;
    uint32_t bytes_written = 0;
    uint32_t cluster_bytes = sectors_per_cluster * 512;

    while (bytes_written < size_bytes || (size_bytes == 0 && first_cluster == 0)) {
        uint32_t new_cluster = allocate_free_cluster();
        if (!new_cluster) return -1;

        if (first_cluster == 0) first_cluster = new_cluster;
        if (prev_cluster != 0) set_cluster_entry(prev_cluster, new_cluster);
        prev_cluster = new_cluster;

        uint32_t lba = cluster_to_lba(new_cluster);
        uint32_t remaining = size_bytes - bytes_written;

        if (remaining >= cluster_bytes) {
            
            ata_write_sectors(lba, sectors_per_cluster, src + bytes_written);
            bytes_written += cluster_bytes;
        } else {
            
            for (uint32_t b = 0; b < 512; b++) sector_buffer[b] = 0;
            for (uint32_t b = 0; b < remaining; b++) {
                sector_buffer[b] = src[bytes_written + b];
            }
            ata_write_sectors(lba, 1, sector_buffer);
            bytes_written += remaining;
        }

        if (size_bytes == 0) break;
    }

    
    if (!ata_read_sectors(dir_lba, 1, sector_buffer)) return -1;
    fat32_dir_entry_t* dir_ptr = (fat32_dir_entry_t*)&sector_buffer[dir_offset];
    for (int i = 0; i < 11; i++) dir_ptr->name[i] = target_83[i];
    dir_ptr->attr = FAT_ATTR_ARCHIVE;
    dir_ptr->first_cluster_hi = (uint16_t)(first_cluster >> 16);
    dir_ptr->first_cluster_lo = (uint16_t)(first_cluster & 0xFFFF);
    dir_ptr->file_size = size_bytes;

    ata_write_sectors(dir_lba, 1, sector_buffer);
    return (int)size_bytes;
}
