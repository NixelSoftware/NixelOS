#include "png.h"

#define MAX_CANVAS_DIM 513
#define MAX_RAW_IDAT   (128 * 1024)
#define MAX_SCANLINE   (MAX_CANVAS_DIM * 4 + 4)


static uint8_t idat_stream[MAX_RAW_IDAT];
static uint32_t idat_len = 0;


static uint8_t raw_pixels[MAX_RAW_IDAT];
static uint32_t raw_len = 0;


static uint32_t cur_row_pixels[MAX_CANVAS_DIM];
static uint32_t prev_row_pixels[MAX_CANVAS_DIM];
static uint8_t  prev_raw_scanline[MAX_SCANLINE];

static inline uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}




typedef struct {
    const uint8_t* src;
    uint32_t src_len;
    uint32_t byte_pos;
    uint32_t bit_buf;
    int num_bits;
} bit_reader_t;

static uint32_t read_bits(bit_reader_t* br, int count) {
    while (br->num_bits < count) {
        if (br->byte_pos < br->src_len) {
            br->bit_buf |= (uint32_t)br->src[br->byte_pos++] << br->num_bits;
            br->num_bits += 8;
        } else {
            break;
        }
    }
    uint32_t val = br->bit_buf & ((1U << count) - 1);
    br->bit_buf >>= count;
    br->num_bits -= count;
    return val;
}

typedef struct {
    uint16_t counts[16];
    uint16_t symbols[288];
} huff_tree_t;

static void build_huffman_tree(huff_tree_t* tree, const uint8_t* bit_lens, int num_symbols) {
    for (int i = 0; i < 16; i++) tree->counts[i] = 0;
    for (int i = 0; i < num_symbols; i++) {
        if (bit_lens[i]) tree->counts[bit_lens[i]]++;
    }

    uint16_t offsets[16];
    offsets[1] = 0;
    for (int i = 1; i < 15; i++) {
        offsets[i + 1] = offsets[i] + tree->counts[i];
    }

    for (int i = 0; i < num_symbols; i++) {
        if (bit_lens[i]) {
            tree->symbols[offsets[bit_lens[i]]++] = (uint16_t)i;
        }
    }
}

static int decode_huffman_symbol(bit_reader_t* br, const huff_tree_t* tree) {
    int code = 0;
    int first = 0;
    int index = 0;

    for (int len = 1; len <= 15; len++) {
        code |= (int)read_bits(br, 1);
        int count = tree->counts[len];
        if (code - count < first) {
            return tree->symbols[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t len_bases[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t dist_bases[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
static const uint8_t cl_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static int inflate_zlib(const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_max, uint32_t* out_len) {
    if (in_len < 6) return 0;
    bit_reader_t br = { in + 2, in_len - 6, 0, 0, 0 };

    uint32_t dest_idx = 0;
    int bfinal = 0;

    huff_tree_t lit_tree;
    huff_tree_t dist_tree;

    while (!bfinal) {
        bfinal = (int)read_bits(&br, 1);
        int btype = (int)read_bits(&br, 2);

        if (btype == 0) { 
            br.bit_buf = 0;
            br.num_bits = 0;
            if (br.byte_pos + 4 > br.src_len) return 0;
            uint16_t len = (uint16_t)(br.src[br.byte_pos] | ((uint16_t)br.src[br.byte_pos + 1] << 8));
            br.byte_pos += 4;
            for (uint16_t i = 0; i < len && dest_idx < out_max; i++) {
                if (br.byte_pos >= br.src_len) return 0;
                out[dest_idx++] = br.src[br.byte_pos++];
            }
        } else if (btype == 1 || btype == 2) {
            if (btype == 1) {
                uint8_t lens[288];
                for (int i = 0; i <= 143; i++) lens[i] = 8;
                for (int i = 144; i <= 255; i++) lens[i] = 9;
                for (int i = 256; i <= 279; i++) lens[i] = 7;
                for (int i = 280; i <= 287; i++) lens[i] = 8;
                build_huffman_tree(&lit_tree, lens, 288);

                uint8_t dlens[32];
                for (int i = 0; i < 32; i++) dlens[i] = 5;
                build_huffman_tree(&dist_tree, dlens, 32);
            } else {
                int hlit = (int)read_bits(&br, 5) + 257;
                int hdist = (int)read_bits(&br, 5) + 1;
                int hclen = (int)read_bits(&br, 4) + 4;

                uint8_t cl_lens[19] = {0};
                for (int i = 0; i < hclen; i++) {
                    cl_lens[cl_order[i]] = (uint8_t)read_bits(&br, 3);
                }
                huff_tree_t cl_tree;
                build_huffman_tree(&cl_tree, cl_lens, 19);

                uint8_t total_lens[320] = {0};
                int idx = 0;
                while (idx < hlit + hdist) {
                    int sym = decode_huffman_symbol(&br, &cl_tree);
                    if (sym < 16) {
                        total_lens[idx++] = (uint8_t)sym;
                    } else if (sym == 16) {
                        int count = (int)read_bits(&br, 2) + 3;
                        uint8_t prev = (idx > 0) ? total_lens[idx - 1] : 0;
                        while (count-- && idx < 320) total_lens[idx++] = prev;
                    } else if (sym == 17) {
                        int count = (int)read_bits(&br, 3) + 3;
                        while (count-- && idx < 320) total_lens[idx++] = 0;
                    } else if (sym == 18) {
                        int count = (int)read_bits(&br, 7) + 11;
                        while (count-- && idx < 320) total_lens[idx++] = 0;
                    } else {
                        return 0;
                    }
                }
                build_huffman_tree(&lit_tree, total_lens, hlit);
                build_huffman_tree(&dist_tree, total_lens + hlit, hdist);
            }

            while (1) {
                int sym = decode_huffman_symbol(&br, &lit_tree);
                if (sym < 0 || sym == 256) break;
                if (sym < 256) {
                    if (dest_idx < out_max) out[dest_idx++] = (uint8_t)sym;
                } else {
                    sym -= 257;
                    if (sym >= 29) return 0;
                    int length = len_bases[sym] + (int)read_bits(&br, len_extra[sym]);
                    int dist_sym = decode_huffman_symbol(&br, &dist_tree);
                    if (dist_sym < 0 || dist_sym >= 30) return 0;
                    int dist = dist_bases[dist_sym] + (int)read_bits(&br, dist_extra[dist_sym]);

                    if (dist > (int)dest_idx) return 0;
                    uint32_t copy_src = dest_idx - dist;
                    while (length-- && dest_idx < out_max) {
                        out[dest_idx++] = out[copy_src++];
                    }
                }
            }
        } else {
            return 0;
        }
    }
    *out_len = dest_idx;
    return 1;
}




static inline int abs_diff(int a, int b) {
    int v = a - b;
    return v < 0 ? -v : v;
}

static uint8_t paeth_predictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = abs_diff(p, a);
    int pb = abs_diff(p, b);
    int pc = abs_diff(p, c);
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    if (pb <= pc) return (uint8_t)b;
    return (uint8_t)c;
}




int png_decode_and_resize(const uint8_t* data, uint32_t len, uint32_t out_pixels[PNG_TARGET_W * PNG_TARGET_H]) {
    if (!data || len < 29) return 0;

    
    if (data[0] != 0x89 || data[1] != 'P' || data[2] != 'N' || data[3] != 'G' ||
        data[4] != 0x0D || data[5] != 0x0A || data[6] != 0x1A || data[7] != 0x0A) {
        return 0;
    }

    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0;
    idat_len = 0;

    uint32_t offset = 8;
    while (offset + 8 <= len) {
        uint32_t chunk_len = read_be32(data + offset);
        uint32_t chunk_type = read_be32(data + offset + 4);
        const uint8_t* chunk_data = data + offset + 8;

        if (offset + 12 + chunk_len > len) break;

        if (chunk_type == 0x49484452) { 
            width = read_be32(chunk_data);
            height = read_be32(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];

            if (bit_depth != 8 || (color_type != 2 && color_type != 6)) {
                return 0;
            }
            if (width == 0 || height == 0 || width > MAX_CANVAS_DIM || height > MAX_CANVAS_DIM) {
                return 0; 
            }
        } else if (chunk_type == 0x49444154) { 
            for (uint32_t i = 0; i < chunk_len && idat_len < MAX_RAW_IDAT; i++) {
                idat_stream[idat_len++] = chunk_data[i];
            }
        } else if (chunk_type == 0x49454E44) { 
            break;
        }

        offset += 12 + chunk_len;
    }

    if (width == 0 || height == 0 || idat_len == 0) return 0;

    
    if (!inflate_zlib(idat_stream, idat_len, raw_pixels, sizeof(raw_pixels), &raw_len)) {
        return 0;
    }

    int bytes_per_pixel = (color_type == 6) ? 4 : 3;
    uint32_t stride = width * bytes_per_pixel;
    uint32_t raw_ptr = 0;

    
    for (uint32_t i = 0; i < stride; i++) prev_raw_scanline[i] = 0;
    for (uint32_t i = 0; i < width; i++) prev_row_pixels[i] = 0;

    
    for (uint32_t y = 0; y < height; y++) {
        if (raw_ptr >= raw_len) break;
        uint8_t filter_type = raw_pixels[raw_ptr++];
        uint8_t* scanline = &raw_pixels[raw_ptr];
        raw_ptr += stride;

        for (uint32_t x = 0; x < width; x++) {
            uint32_t px_offset = x * bytes_per_pixel;
            for (int b = 0; b < bytes_per_pixel; b++) {
                uint8_t left = (x > 0) ? scanline[px_offset + b - bytes_per_pixel] : 0;
                uint8_t up = (y > 0) ? prev_raw_scanline[px_offset + b] : 0;
                uint8_t up_left = (y > 0 && x > 0) ? prev_raw_scanline[px_offset + b - bytes_per_pixel] : 0;

                uint8_t raw_val = scanline[px_offset + b];
                if (filter_type == 1) { 
                    scanline[px_offset + b] = raw_val + left;
                } else if (filter_type == 2) { 
                    scanline[px_offset + b] = raw_val + up;
                } else if (filter_type == 3) { 
                    scanline[px_offset + b] = raw_val + ((left + up) >> 1);
                } else if (filter_type == 4) { 
                    scanline[px_offset + b] = raw_val + paeth_predictor(left, up, up_left);
                }
            }

            uint8_t r = scanline[px_offset + 0];
            uint8_t g = scanline[px_offset + 1];
            uint8_t b = scanline[px_offset + 2];
            uint8_t a = (color_type == 6) ? scanline[px_offset + 3] : 0xFF;

            cur_row_pixels[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }

        
        for (uint32_t i = 0; i < stride; i++) {
            prev_raw_scanline[i] = scanline[i];
        }

        
        for (int dy = 0; dy < PNG_TARGET_H; dy++) {
            uint32_t mapped_src_y = (dy * height) / PNG_TARGET_H;
            if (mapped_src_y == y) {
                
                for (int dx = 0; dx < PNG_TARGET_W; dx++) {
                    uint32_t src_x = (dx * width) / PNG_TARGET_W;
                    out_pixels[dy * PNG_TARGET_W + dx] = cur_row_pixels[src_x];
                }
            }
        }
    }

    return 1;
}
