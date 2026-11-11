#ifndef NIXEL_SDK_IMAGE_H
#define NIXEL_SDK_IMAGE_H
#define SDK_BASE 0xC00000u
#define SDK_CODE_SIZE 24576
#define SDK_EXPORTS 14
#define SDK_IMAGE_MAX 65536
#define SDK_MARKER "NIXELSDKIMAGE01!"
typedef struct {
    char name[16];
    unsigned address;
    unsigned arguments;
} sdk_export;
typedef struct {
    unsigned char magic[16];
    unsigned version, code, capacity, entry;
    sdk_export exports[SDK_EXPORTS];
} sdk_image;
#endif
