#ifndef NIXEL_WOLFSSL_USER_SETTINGS_H
#define NIXEL_WOLFSSL_USER_SETTINGS_H
#include <stddef.h>
void *nixel_tls_alloc(size_t);
void nixel_tls_free(void *);
void *nixel_tls_realloc(void *, size_t);
int nixel_tls_seed(unsigned char *, unsigned int);
#define SINGLE_THREADED
#define NO_FILESYSTEM
#define NO_WRITEV
#define WOLFSSL_NO_SOCK
#define WOLFSSL_USER_IO
#define WOLFSSL_NO_SOCKETS
#define NO_DEV_RANDOM
#define NO_WOLFSSL_DIR
#define WOLFSSL_NO_ASM
#define NO_WOLFSSL_SERVER
#define NO_OLD_TLS
#define WOLFSSL_TLS13
#define HAVE_TLS_EXTENSIONS
#define HAVE_SNI
/* Accept a valid path to a trusted root even when the peer also sends a
 * cross-signed certificate whose alternate issuer is absent from our store. */
#define WOLFSSL_ALT_CERT_CHAINS
#define HAVE_SUPPORTED_CURVES
#define HAVE_HKDF
#define HAVE_ECC
#define ECC_TIMING_RESISTANT
#define WC_RSA_PSS
#define WC_RSA_BLINDING
#define WOLFSSL_SP_MATH_ALL
/* Public CA chains, including ISRG Root X1 cross-signatures, use RSA-4096. */
#define WOLFSSL_SP_4096
#define WOLFSSL_SP_SMALL
#define TFM_TIMING_RESISTANT
#define HAVE_AESGCM
#define NO_AES_CBC
#define NO_DES3
#define NO_RC4
#define NO_MD4
#define NO_MD5
#define NO_DSA
#define NO_PSK
#define NO_DH
#define NO_SESSION_CACHE
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512
#define HAVE_HASHDRBG
#define CUSTOM_RAND_GENERATE_SEED nixel_tls_seed
#define XMALLOC_OVERRIDE
#define XMALLOC(s,h,t) nixel_tls_alloc(s)
#define XFREE(p,h,t) nixel_tls_free(p)
#define XREALLOC(p,s,h,t) nixel_tls_realloc(p,s)
#define TIME_OVERRIDES
#define XTIME nixel_tls_time
#define XGMTIME nixel_tls_gmtime
#define WOLFSSL_NO_CURRDIR
#define WOLFSSL_IGNORE_FILE_WARN
#define memcpy nixel_tls_memcpy
#define memmove nixel_tls_memmove
#define memset nixel_tls_memset
#define memcmp nixel_tls_memcmp
#define strlen nixel_tls_strlen
#define strcmp nixel_tls_strcmp
#define strncmp nixel_tls_strncmp
#define strncpy nixel_tls_strncpy
#define strstr nixel_tls_strstr
#define strchr nixel_tls_strchr
#define strcasecmp nixel_tls_strcasecmp
#define strncasecmp nixel_tls_strncasecmp
#endif
