/* Isolated, allocation-free wolfCrypt build for WPA2 key derivation.
 * Uses the same vendored sources as the TLS library, without TLS global state. */
#ifndef NIXEL_WIFI_CRYPTO_SETTINGS_H
#define NIXEL_WIFI_CRYPTO_SETTINGS_H
#include <stddef.h>
#define NO_CRYPT_BENCHMARK
#define XMALLOC_OVERRIDE
#define XMALLOC(s,h,t) ((void*)0)
#define XFREE(p,h,t) ((void)0)
#define XREALLOC(p,s,h,t) ((void*)0)
#define STRING_USER
#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define WOLFSSL_NO_MALLOC
#define WOLFSSL_NO_ASM
#define WC_BARRIER_DATA(p) __asm__ __volatile__("" : : "r"(p) : "memory")
#define NO_FILESYSTEM
#define NO_DEV_RANDOM
#define WC_NO_RNG
#define NO_ASN
#define NO_ASN_TIME
#define NO_CERTS
#define NO_RSA
#define NO_DH
#define NO_DSA
#define HAVE_AESCCM
#define HAVE_AES_KEYWRAP
#define WOLFSSL_AES_DIRECT
#define NO_AES_CBC
#define NO_DES3
#define NO_RC4
#define NO_MD4
#define NO_MD5
#define NO_SHA512
#define NO_SHA3
#define NO_PSK
#define NO_BIG_INT
#define HAVE_PBKDF2
#define WOLFSSL_NO_SOCK
#define NO_WOLFSSL_DIR
#define WOLFSSL_IGNORE_FILE_WARN
void* wifi_crypto_memcpy(void*, const void*, size_t);
void* wifi_crypto_memset(void*, int, size_t);
int wifi_crypto_memcmp(const void*, const void*, size_t);
size_t wifi_crypto_strlen(const char*);
void wifi_crypto_zero(void*, size_t);
#define XSTRLEN wifi_crypto_strlen
void* wifi_crypto_memmove(void*, const void*, size_t);
#define XMEMMOVE wifi_crypto_memmove
#define XMEMCPY wifi_crypto_memcpy
#define XMEMSET wifi_crypto_memset
#define XMEMCMP wifi_crypto_memcmp
#endif
