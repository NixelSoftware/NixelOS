GLLIB_DIR := src/system/libraries/gl
GLLIB_SOURCES := \
    $(GLLIB_DIR)/gl.c \
    $(GLLIB_DIR)/glut.c \
    $(GLLIB_DIR)/font8x8.c \
    $(GLLIB_DIR)/mem.c
GLLIB_OBJECTS := $(GLLIB_SOURCES:.c=.o)

all: nixelgl
clean-build: clean-nixelgl

$(GLLIB_OBJECTS): CFLAGS += -Isrc/system/libraries/m -fno-builtin -fno-stack-protector
$(GLLIB_OBJECTS): $(GLLIB_DIR)/gl.mk $(GLLIB_DIR)/mem.h $(GLLIB_DIR)/font8x8.h \
                  src/include/GL/gl.h src/include/GL/glut.h src/system/libraries/m/math.h
$(GLLIB_OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: nixelgl clean-nixelgl
nixelgl: $(GLLIB_DIR)/libnixelgl.a
$(GLLIB_DIR)/libnixelgl.a: $(GLLIB_OBJECTS) Makefile $(GLLIB_DIR)/gl.mk
	$(AR) rcs $@ $(filter %.o,$^)
clean-nixelgl:
	$(PYTHON) -c "from pathlib import Path; [Path(p).unlink(missing_ok=True) for p in [$(foreach p,$(GLLIB_OBJECTS) $(GLLIB_DIR)/libnixelgl.a,'$(p)',)]]"
