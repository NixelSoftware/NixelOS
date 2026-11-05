TLS_DIR := src/system/libraries/tls
TLS_VENDOR := $(TLS_DIR)/vendor/wolfssl
TLS_CRYPTO := aes asn coding ecc error hash hmac logging memory random rsa sha sha256 sha512 sp_int wc_port wc_encrypt signature kdf wolfmath pwdbased
TLS_PROTOCOL := ssl tls tls13 internal keys wolfio
TLS_SOURCES := $(TLS_DIR)/tls.c $(TLS_DIR)/runtime.c $(TLS_DIR)/div64.c $(TLS_DIR)/tcp.c \
 $(addprefix $(TLS_VENDOR)/wolfcrypt/src/,$(addsuffix .c,$(TLS_CRYPTO))) \
 $(addprefix $(TLS_VENDOR)/src/,$(addsuffix .c,$(TLS_PROTOCOL)))
TLS_OBJECTS := $(TLS_SOURCES:.c=.o)
TLS_CPPFLAGS := -U__unix__ -U__linux__ -DWOLFSSL_USER_SETTINGS -I$(TLS_DIR) -I$(TLS_DIR)/compat -I$(TLS_VENDOR)
all: tls
clean-build: clean-tls

$(TLS_OBJECTS): CFLAGS += $(TLS_CPPFLAGS) -fno-builtin
$(TLS_OBJECTS): $(TLS_DIR)/tls.mk $(wildcard $(TLS_DIR)/compat/*.h) $(TLS_DIR)/user_settings.h $(TLS_DIR)/tls.h $(wildcard $(TLS_VENDOR)/wolfssl/*.h) $(wildcard $(TLS_VENDOR)/wolfssl/wolfcrypt/*.h)
$(TLS_OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
-include $(TLS_OBJECTS:.o=.d)

.PHONY: tls clean-tls
tls: $(TLS_DIR)/libnixeltls.a
$(TLS_DIR)/libnixeltls.a: $(TLS_OBJECTS)
	$(AR) rcs $@ $^
clean-tls:
	$(PYTHON) -c "from pathlib import Path; [p.unlink(missing_ok=True) for p in [$(foreach p,$(TLS_OBJECTS) $(TLS_OBJECTS:.o=.d) $(TLS_DIR)/libnixeltls.a,'$(p)',)]]"

.PHONY: tls-sources test-tls
tls-sources:
	@echo $(TLS_SOURCES)
test-tls: tls
	$(PYTHON) $(TLS_DIR)/tests/run_tests.py
