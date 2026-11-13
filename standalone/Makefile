PS3DEV ?= /opt/ps3sdk
PSL1GHT ?= $(PS3DEV)
export PS3DEV PSL1GHT
include $(PSL1GHT)/ppu_rules
TARGET := neon_lounge
TITLE := Neon Lounge
APPID := SVIZ00001
CONTENTID := UP0001-$(APPID)_00-NEONLOUNGE000001
BUILDDIR := $(CURDIR)/build
PKGFILES := $(CURDIR)/local/package
ICON0 := $(CURDIR)/local/ICON0.PNG
CPPFLAGS += -Isrc -Ithird_party/minimp3 -I$(PORTLIBS)/include/SDL2 -I$(PORTLIBS)/include -I$(PORTLIBS)/include/freetype2 $(LIBPSL1GHT_INC)
CFLAGS += -std=gnu99 -O2 -Wall -Wextra -ffunction-sections -fdata-sections $(MACHDEP)
CPPFLAGS += -MMD -MP
CPPFLAGS += -DNEON_PS3
DISPLAY_BACKEND ?= native
ifeq ($(DISPLAY_BACKEND),native)
CPPFLAGS += -DNL_NATIVE_DISPLAY
endif
LDFLAGS += $(LIBPSL1GHT_LIB) -L$(PORTLIBS)/lib -Wl,--gc-sections
LDFLAGS += -Wl,--wrap=gcmGetFlipStatus
LDFLAGS += -Wl,--wrap=sysThreadCreate
LDLIBS := -lSDL2 -lfreetype -lz -lgcm_sys -lrsx -lsysutil -lio -laudio -lrt -llv2 -lm -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lnet -lnetctl -lsysutil -lsysmodule -lm -lz
OBJECTS := $(addprefix $(BUILDDIR)/,main.o spectrum.o ui.o library.o runtime_ps3.o wait_guard.o system_state.o display.o thread_stack.o clock_est.o visualizer.o gallery.o update.o)
.PHONY: all pkg host-test
all: $(TARGET).self
pkg: $(TARGET).pkg
$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@
PACKAGE_ASSETS := $(shell find $(PKGFILES) -type f 2>/dev/null)
$(TARGET).pkg: $(TARGET).self Makefile $(ICON0) $(PACKAGE_ASSETS)
# Assets are data, so do not search SDK executable/%.o implicit rules for them.
$(PACKAGE_ASSETS): ;
$(BUILDDIR)/%.o: src/%.c Makefile
	mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
host-test:
	mkdir -p build
	gcc -std=c99 -Wall -Wextra -Isrc tests/test_spectrum.c src/spectrum.c -lm -o build/test_spectrum
	./build/test_spectrum
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_library.c src/library.c -o build/test_library
	./build/test_library
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_wait_guard.c src/wait_guard.c -o build/test_wait_guard
	./build/test_wait_guard
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_system_state.c src/system_state.c -o build/test_system_state
	./build/test_system_state
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_thread_stack.c src/thread_stack.c -o build/test_thread_stack
	./build/test_thread_stack
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_clock_est.c src/clock_est.c -o build/test_clock_est
	./build/test_clock_est
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_update.c src/update.c -o build/test_update
	./build/test_update
-include $(OBJECTS:.o=.d)
