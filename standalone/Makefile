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
UPDATE_CA := $(PKGFILES)/USRDIR/certs/cacert.pem
SFOXML := $(BUILDDIR)/neon-sfo.xml
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
LDLIBS := -lSDL2 -lfreetype -lz -lvdec -lgcm_sys -lrsx -lsysutil -lio -laudio -lrt -llv2 -lm -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lnet -lnetctl -lsysutil -lsysmodule -lm -lz -ljpeg -lfaad
OBJECTS := $(addprefix $(BUILDDIR)/,main.o spectrum.o ui.o library.o runtime_ps3.o wait_guard.o system_state.o display.o thread_stack.o clock_est.o visualizer.o gallery.o update.o pcm_ring.o eq.o pc_audio.o radio.o podcast.o safety.o ftp_server.o youtube.o yt_net.o yt_worker.o yt_osk.o yt_player.o yt_aac.o yt_adts.o yt_pick.o yt_sync.o yt_demux.o demux-mp4.o mp4.o h264.o video-source.o ps3vdec.o vdecex.o)
.PHONY: all pkg host-test host-tls-test
HOST_CURL_CFLAGS ?= $(shell pkg-config --cflags libcurl 2>/dev/null)
HOST_CURL_LIBS ?= $(shell pkg-config --libs libcurl 2>/dev/null)
all: $(TARGET).self
pkg: $(TARGET).pkg
$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@
PACKAGE_ASSETS := $(shell find $(PKGFILES) -type f 2>/dev/null)
$(TARGET).pkg: $(TARGET).self Makefile $(ICON0) $(PACKAGE_ASSETS) $(UPDATE_CA) $(SFOXML)
$(SFOXML): src/update.h tools/make_sfo_xml.py $(PS3DEV)/bin/sfo.xml
	python3 tools/make_sfo_xml.py src/update.h $(PS3DEV)/bin/sfo.xml $@
$(UPDATE_CA):
	@echo "Missing update CA bundle. Run: python3 tools/fetch_update_ca.py $(PKGFILES)/USRDIR/certs"
	@false
# Assets are data, so do not search SDK executable/%.o implicit rules for them.
$(filter-out $(UPDATE_CA),$(PACKAGE_ASSETS)): ;
$(BUILDDIR)/%.o: src/%.c Makefile
	mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
host-test:
	mkdir -p build
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_stream.c src/pcm_ring.c -o build/test_stream
	./build/test_stream
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
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_ftp_server.c src/ftp_server.c -o build/test_ftp_server
	./build/test_ftp_server
	gcc -std=c99 -Wall -Wextra -Isrc tests/test_youtube.c src/youtube.c src/yt_net.c -o build/test_youtube
	./build/test_youtube
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_yt_playback.c src/yt_pick.c src/yt_sync.c src/yt_adts.c -o build/test_yt_playback
	./build/test_yt_playback
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_yt_osk.c src/yt_osk.c -o build/test_yt_osk
	./build/test_yt_osk
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_settings_layout.c -o build/test_settings_layout
	./build/test_settings_layout
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_eq.c src/eq.c -lm -o build/test_eq
	./build/test_eq
	sh tests/make_yt_fixtures.sh build/ytfx
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_yt_demux.c src/yt_demux.c src/demux-mp4.c src/mp4.c src/h264.c src/video-source.c -o build/test_yt_demux
	./build/test_yt_demux build/ytfx
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_podcast.c src/podcast.c -o build/test_podcast
	./build/test_podcast
	gcc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_queue.c src/library.c -o build/test_queue
	./build/test_queue
host-tls-test:
	mkdir -p build
	gcc -std=c99 -Wall -Wextra -Werror -Isrc $(HOST_CURL_CFLAGS) tests/test_update_tls.c $(HOST_CURL_LIBS) -o build/test_update_tls
	python3 tests/test_update_tls.py build/test_update_tls
-include $(OBJECTS:.o=.d)
