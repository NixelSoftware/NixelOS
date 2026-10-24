#include "png.h"

#define MAX_CANVAS_DIM 513
#define MAX_RAW_IDAT   (128 * 1024)
#define MAX_SCANLINE   (MAX_CANVAS_DIM * 4 + 4)


static uint8_t idat_stream[MAX_RAW_IDAT];
static uint32_t idat_len = 0;


static uint8_t raw_pixels[MAX_RAW_IDAT];
static uint32_t raw_len = 0;


static uint32_t cur_row_pixels[MAX_CANVAS_DIM];
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
    int failed;
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
    if (br->num_bits < count) { br->failed = 1; return 0; }
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
        if (br->failed) return -1;
        int count = tree->counts[len];
        if (code >= first && code - first < count) {
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
    if (in_len < 6 || (in[0] & 15) != 8 || (in[0] >> 4) > 7 || (((unsigned)in[0] << 8) | in[1]) % 31 || (in[1] & 32)) return 0;
    bit_reader_t br = { in + 2, in_len - 6, 0, 0, 0, 0 };

    uint32_t dest_idx = 0;
    int bfinal = 0;

    huff_tree_t lit_tree;
    huff_tree_t dist_tree;

    while (!bfinal) {
        if (br.failed) return 0;
        bfinal = (int)read_bits(&br, 1);
        int btype = (int)read_bits(&br, 2);

        if (btype == 0) { 
            br.bit_buf = 0;
            br.num_bits = 0;
            if (br.byte_pos + 4 > br.src_len) return 0;
            uint16_t len = (uint16_t)(br.src[br.byte_pos] | ((uint16_t)br.src[br.byte_pos + 1] << 8));
            uint16_t inverse = (uint16_t)(br.src[br.byte_pos + 2] | ((uint16_t)br.src[br.byte_pos + 3] << 8));
            if ((uint16_t)(len ^ inverse) != 0xffffu || len > out_max - dest_idx) return 0;
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
                if (hlit > 286 || hdist > 32 || br.failed) return 0;
                int idx = 0;
                while (idx < hlit + hdist) {
                    int sym = decode_huffman_symbol(&br, &cl_tree);
                    if (sym < 0 || br.failed) return 0;
                    if (sym < 16) {
                        total_lens[idx++] = (uint8_t)sym;
                    } else if (sym == 16) {
                        int count = (int)read_bits(&br, 2) + 3;
                        if (!idx || count > hlit + hdist - idx) return 0;
                        uint8_t prev = total_lens[idx - 1];
                        while (count-- && idx < 320) total_lens[idx++] = prev;
                    } else if (sym == 17) {
                        int count = (int)read_bits(&br, 3) + 3;
                        if (count > hlit + hdist - idx) return 0;
                        while (count--) total_lens[idx++] = 0;
                    } else if (sym == 18) {
                        int count = (int)read_bits(&br, 7) + 11;
                        if (count > hlit + hdist - idx) return 0;
                        while (count--) total_lens[idx++] = 0;
                    } else {
                        return 0;
                    }
                }
                build_huffman_tree(&lit_tree, total_lens, hlit);
                build_huffman_tree(&dist_tree, total_lens + hlit, hdist);
            }

            while (1) {
                int sym = decode_huffman_symbol(&br, &lit_tree);
                if (sym < 0 || br.failed) return 0;
                if (sym == 256) break;
                if (sym < 256) {
                    if (dest_idx == out_max) return 0;
                    out[dest_idx++] = (uint8_t)sym;
                } else {
                    sym -= 257;
                    if (sym >= 29) return 0;
                    int length = len_bases[sym] + (int)read_bits(&br, len_extra[sym]);
                    int dist_sym = decode_huffman_symbol(&br, &dist_tree);
                    if (dist_sym < 0 || dist_sym >= 30) return 0;
                    int dist = dist_bases[dist_sym] + (int)read_bits(&br, dist_extra[dist_sym]);

                    if (dist > (int)dest_idx || (uint32_t)length > out_max - dest_idx || br.failed) return 0;
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
    if (br.failed) return 0;
    uint32_t a = 1, b = 0;
    for (uint32_t i = 0; i < dest_idx; i++) { a = (a + out[i]) % 65521u; b = (b + a) % 65521u; }
    if ((b << 16 | a) != read_be32(in + in_len - 4)) return 0;
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




static uint32_t png_crc(const uint8_t* p, uint32_t n) {
    uint32_t crc = 0xffffffffu;
    while (n--) {
        crc ^= *p++;
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}
int png_decode_and_resize(const uint8_t* data, uint32_t len, uint32_t out_pixels[PNG_TARGET_W * PNG_TARGET_H]) {
    static const uint8_t signature[8] = {137,80,78,71,13,10,26,10};
    if (!data || !out_pixels || len < 33) return 0;
    for (unsigned i = 0; i < 8; i++) if (data[i] != signature[i]) return 0;
    uint32_t width = 0, height = 0, palette[256], palette_count = 0;
    uint8_t depth = 0, type = 0; int ihdr = 0, ended = 0;
    uint32_t transparent_gray = 0xffffffffu, transparent_rgb = 0xffffffffu;
    idat_len = 0;
    for (uint32_t offset = 8; offset <= len - 12;) {
        uint32_t size = read_be32(data + offset), kind = read_be32(data + offset + 4);
        if (size > len - offset - 12 || png_crc(data + offset + 4, size + 4) != read_be32(data + offset + 8 + size)) return 0;
        const uint8_t* chunk = data + offset + 8;
        if (!ihdr && kind != 0x49484452u) return 0;
        if (kind == 0x49484452u) {
            if (ihdr || size != 13) return 0;
            width = read_be32(chunk); height = read_be32(chunk + 4); depth = chunk[8]; type = chunk[9];
            if (!width || !height || width > MAX_CANVAS_DIM || height > MAX_CANVAS_DIM || chunk[10] || chunk[11] || chunk[12]) return 0;
            if ((type == 0 || type == 3) ? (depth != 1 && depth != 2 && depth != 4 && depth != 8) : ((type != 2 && type != 4 && type != 6) || depth != 8)) return 0;
            ihdr = 1;
        } else if (kind == 0x504c5445u) {
            if (!size || size % 3 || size > 768 || idat_len) return 0;
            palette_count = size / 3;
            for (uint32_t i = 0; i < palette_count; i++) palette[i] = 0xff000000u | ((uint32_t)chunk[i*3] << 16) | ((uint32_t)chunk[i*3+1] << 8) | chunk[i*3+2];
        } else if (kind == 0x74524e53u) {
            if (idat_len) return 0;
            if (type == 3) {
                if (!palette_count || size > palette_count) return 0;
                for (uint32_t i = 0; i < size; i++) palette[i] = (palette[i] & 0xffffffu) | ((uint32_t)chunk[i] << 24);
            } else if (type == 0 && size == 2) transparent_gray = ((uint32_t)chunk[0] << 8) | chunk[1];
            else if (type == 2 && size == 6) {
                if (chunk[0] || chunk[2] || chunk[4]) return 0;
                transparent_rgb = ((uint32_t)chunk[1] << 16) | ((uint32_t)chunk[3] << 8) | chunk[5];
            } else return 0;
        } else if (kind == 0x49444154u) {
            if (size > sizeof(idat_stream) - idat_len) return 0;
            for (uint32_t i = 0; i < size; i++) idat_stream[idat_len++] = chunk[i];
        } else if (kind == 0x49454e44u) { if (size) return 0; ended = 1; break; }
        else if (!(data[offset + 4] & 32)) return 0; /* Unknown critical chunk. */
        offset += size + 12;
    }
    if (!ended || !ihdr || !idat_len || (type == 3 && (!palette_count || palette_count > (1u << depth)))) return 0;
    unsigned channels = type == 2 ? 3 : type == 4 ? 2 : type == 6 ? 4 : 1;
    unsigned stride = (width * channels * depth + 7) / 8, filter_bytes = (channels * depth + 7) / 8;
    if ((stride + 1) > sizeof(raw_pixels) / height) return 0;
    if (!inflate_zlib(idat_stream, idat_len, raw_pixels, sizeof(raw_pixels), &raw_len) || raw_len != (stride + 1) * height) return 0;
    for (unsigned i = 0; i < stride; i++) prev_raw_scanline[i] = 0;
    for (uint32_t y = 0; y < height; y++) {
        uint8_t filter = raw_pixels[y * (stride + 1)];
        uint8_t* scan = raw_pixels + y * (stride + 1) + 1;
        if (filter > 4) return 0;
        for (unsigned i = 0; i < stride; i++) {
            unsigned left = i >= filter_bytes ? scan[i - filter_bytes] : 0;
            unsigned up = prev_raw_scanline[i], upper_left = i >= filter_bytes ? prev_raw_scanline[i - filter_bytes] : 0;
            if (filter == 1) scan[i] += left;
            else if (filter == 2) scan[i] += up;
            else if (filter == 3) scan[i] += (left + up) / 2;
            else if (filter == 4) scan[i] += paeth_predictor(left, up, upper_left);
        }
        for (uint32_t x = 0; x < width; x++) {
            unsigned sample = depth < 8 ? (scan[(x * depth) / 8] >> (8 - depth - (x * depth) % 8)) & ((1u << depth) - 1) : scan[x * channels];
            unsigned rgb, alpha = 255;
            if (type == 3) { if (sample >= palette_count) return 0; cur_row_pixels[x] = palette[sample]; continue; }
            if (type == 0 || type == 4) {
                unsigned gray = sample * 255 / ((1u << depth) - 1);
                rgb = gray * 0x010101u;
                if (type == 4) alpha = scan[x * 2 + 1]; else if (sample == transparent_gray) alpha = 0;
            } else {
                const uint8_t* pixel = scan + x * channels;
                rgb = ((unsigned)pixel[0] << 16) | ((unsigned)pixel[1] << 8) | pixel[2];
                if (type == 6) alpha = pixel[3]; else if (rgb == transparent_rgb) alpha = 0;
            }
            cur_row_pixels[x] = rgb | (alpha << 24);
        }
        for (unsigned i = 0; i < stride; i++) prev_raw_scanline[i] = scan[i];
        for (unsigned dy = 0; dy < PNG_TARGET_H; dy++) if ((dy * height) / PNG_TARGET_H == y)
            for (unsigned dx = 0; dx < PNG_TARGET_W; dx++) out_pixels[dy * PNG_TARGET_W + dx] = cur_row_pixels[(dx * width) / PNG_TARGET_W];
    }
    return 1;
}
