M_DIR := src/system/libraries/m
M_SOURCES := $(M_DIR)/math.c
M_OBJECTS := $(M_SOURCES:.c=.o)

all: nixelm
clean-build: clean-nixelm

$(M_OBJECTS): CFLAGS += -fno-builtin -fno-stack-protector
$(M_OBJECTS): $(M_DIR)/m.mk $(M_DIR)/math.h
$(M_OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: nixelm clean-nixelm
nixelm: $(M_DIR)/libnixelm.a
$(M_DIR)/libnixelm.a: $(M_OBJECTS) Makefile $(M_DIR)/m.mk
	$(AR) rcs $@ $(filter %.o,$^)
clean-nixelm:
	$(PYTHON) -c "from pathlib import Path; [Path(p).unlink(missing_ok=True) for p in [$(foreach p,$(M_OBJECTS) $(M_DIR)/libnixelm.a,'$(p)',)]]"
