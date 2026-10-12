#
# Makefile for a machine kernel image
#

CIRCLE_STDLIB_HOME ?= third_party/circle-stdlib
CIRCLEHOME ?= $(CIRCLE_STDLIB_HOME)/libs/circle
SRC_DIR ?= src
BUILD_ROOT ?= build
SD_LAYOUT_TOML ?= sd-layout.toml
UPDATE_PATH_POLICY_GENERATOR ?= tools/generate_update_path_policy.py
UPDATE_PATH_POLICY_MODULE ?= tools/update_path_policy.py
UPDATE_PATH_POLICY_HEADER ?= $(SRC_DIR)/update/generated/update_path_policy_v1.h
VICE_SOURCE ?= third_party/vice-3.10/src
# VICE points at generated build artifacts.  It may equal VICE_SOURCE for
# legacy in-tree builds, but the normal BMX wrappers use an isolated
# board/profile build tree.
VICE ?= $(VICE_SOURCE)
VICE_ARCH ?= $(VICE_SOURCE)/arch/raspi
VICE_SHARED ?= $(VICE_SOURCE)/arch/shared
VICE_INCLUDE_DIRS ?= \
	$(VICE) \
	$(VICE)/resid \
	$(VICE)/resid-dtv \
	$(VICE_SOURCE) \
	$(VICE_ARCH) \
	$(VICE_SHARED) \
	$(VICE_SHARED)/hotkeys \
	$(VICE_SHARED)/hwsiddrv \
	$(VICE_SHARED)/mididrv \
	$(VICE_SHARED)/socketdrv \
	$(VICE_SHARED)/sounddrv \
	$(VICE_SOURCE)/drive \
	$(VICE_SOURCE)/c64 \
	$(VICE_SOURCE)/c64/cart \
	$(VICE_SOURCE)/scpu64 \
	$(VICE_SOURCE)/c64dtv \
	$(VICE_SOURCE)/c128 \
	$(VICE_SOURCE)/c128/cart \
	$(VICE_SOURCE)/vic20 \
	$(VICE_SOURCE)/pet \
	$(VICE_SOURCE)/cbm2 \
	$(VICE_SOURCE)/plus4 \
	$(VICE_SOURCE)/raster \
	$(VICE_SOURCE)/core \
	$(VICE_SOURCE)/core/rtc \
	$(VICE_SOURCE)/crtc \
	$(VICE_SOURCE)/datasette \
	$(VICE_SOURCE)/diskimage \
	$(VICE_SOURCE)/drive/iec \
	$(VICE_SOURCE)/drive/iec128dcr \
	$(VICE_SOURCE)/drive/iec/c64exp \
	$(VICE_SOURCE)/drive/iecieee \
	$(VICE_SOURCE)/drive/ieee \
	$(VICE_SOURCE)/drive/tcbm \
	$(VICE_SOURCE)/fileio \
	$(VICE_SOURCE)/fsdevice \
	$(VICE_SOURCE)/iecbus \
	$(VICE_SOURCE)/monitor \
	$(VICE_SOURCE)/parallel \
	$(VICE_SOURCE)/printerdrv \
	$(VICE_SOURCE)/rs232drv \
	$(VICE_SOURCE)/samplerdrv \
	$(VICE_SOURCE)/serial \
	$(VICE_SOURCE)/sid \
	$(VICE_SOURCE)/tape \
	$(VICE_SOURCE)/userport \
	$(VICE_SOURCE)/vdrive \
	$(VICE_SOURCE)/vicii \
	$(VICE_SOURCE)/vdc \
	$(VICE_SOURCE)/viciisc \
	$(VICE_SOURCE)/video \
	$(VICE_SOURCE)/lib/md5 \
	$(VICE_SOURCE)/lib/p64 \
	$(VICE_SOURCE)/platform \
	$(VICE_SOURCE)/joyport \
	$(VICE_SOURCE)/gfxoutputdrv \
	$(VICE_SOURCE)/tapeport \
	$(VICE_SOURCE)/imagecontents

-include $(CIRCLE_STDLIB_HOME)/Config.mk
-include $(CIRCLEHOME)/Config.mk
NEWLIBDIR ?= $(CIRCLE_STDLIB_INSTALL_DIR)
AARCH ?= 32

BOARD ?= pi4

# Keep the original Pi5 knobs as compatibility aliases while exposing the
# board-independent configuration used by the Pi4 multicore A/B builds.
ifneq ($(origin BMX_PI5_SID_WORKER),undefined)
ifneq ($(origin BMX_SID_WORKER),undefined)
ifneq ($(BMX_PI5_SID_WORKER),$(BMX_SID_WORKER))
$(error BMX_PI5_SID_WORKER and BMX_SID_WORKER disagree)
endif
else
BMX_SID_WORKER := $(BMX_PI5_SID_WORKER)
endif
endif
ifneq ($(origin BMX_PI5_SID_DIAGNOSTICS),undefined)
ifneq ($(origin BMX_SID_DIAGNOSTICS),undefined)
ifneq ($(BMX_PI5_SID_DIAGNOSTICS),$(BMX_SID_DIAGNOSTICS))
$(error BMX_PI5_SID_DIAGNOSTICS and BMX_SID_DIAGNOSTICS disagree)
endif
else
BMX_SID_DIAGNOSTICS := $(BMX_PI5_SID_DIAGNOSTICS)
endif
endif

BMX_EMU_MULTICORE ?= 1
BMX_SID_WORKER ?= 1
BMX_SID_DIAGNOSTICS ?= 0
BMX_V3D_RENDER_TEST_KERNEL ?= 0

ifneq ($(strip $(BMX_EMU_MULTICORE)),0)
ifneq ($(strip $(BMX_EMU_MULTICORE)),1)
$(error BMX_EMU_MULTICORE must be exactly 0 or 1)
endif
endif
ifneq ($(strip $(BMX_SID_WORKER)),0)
ifneq ($(strip $(BMX_SID_WORKER)),1)
$(error BMX_SID_WORKER must be exactly 0 or 1)
endif
endif
ifneq ($(strip $(BMX_SID_DIAGNOSTICS)),0)
ifneq ($(strip $(BMX_SID_DIAGNOSTICS)),1)
$(error BMX_SID_DIAGNOSTICS must be exactly 0 or 1)
endif
endif
ifneq ($(strip $(BMX_V3D_RENDER_TEST_KERNEL)),0)
ifneq ($(strip $(BMX_V3D_RENDER_TEST_KERNEL)),1)
$(error BMX_V3D_RENDER_TEST_KERNEL must be exactly 0 or 1)
endif
endif
ifeq ($(BMX_V3D_RENDER_TEST_KERNEL),1)
ifneq ($(BMX_EMU_MULTICORE),1)
$(error BMX_V3D_RENDER_TEST_KERNEL=1 requires BMX_EMU_MULTICORE=1)
endif
ifneq ($(AARCH),64)
$(error BMX_V3D_RENDER_TEST_KERNEL=1 requires AARCH=64)
endif
endif
ifeq ($(BMX_SID_WORKER),1)
ifneq ($(BMX_EMU_MULTICORE),1)
$(error BMX_SID_WORKER=1 requires BMX_EMU_MULTICORE=1)
endif
endif

ifeq ($(BOARD),pi3)
RASPPI := 3
else ifeq ($(BOARD),pi4)
RASPPI := 4
else ifeq ($(BOARD),pi5)
RASPPI := 5
else
$(error Unsupported BOARD '$(BOARD)'; supported boards are pi3, pi4 and pi5)
endif

BUILD_BOARD ?= $(if $(BOARD),$(BOARD),raspi$(RASPPI))
BUILD_DIR ?= $(BUILD_ROOT)/$(BUILD_BOARD)/$(MACHINE_CLASS)
BMX_COMMON_BUILD_DIR ?= $(BUILD_DIR)
BMC64_COMMON_LIB ?= third_party/common/libbmc64common.a
BMX_GENERATE_LISTING ?= 1

ifeq ($(strip $(RASPPI)),3)
# Pi3: 32-bit only, VideoCore IV firmware dispmanx + legacy GLES2
# (same maximal-GPU legacy path as Pi4 AArch32, kernel7l.img).
ifneq ($(strip $(AARCH)),32)
$(error Pi3 builds require AARCH=32)
endif
TARGET_BASENAME ?= kernel7l
C_STANDARD += -Wno-incompatible-pointer-types
else ifeq ($(strip $(RASPPI)),4)
ifneq ($(filter $(strip $(AARCH)),32 64),$(strip $(AARCH)))
$(error Pi4 builds require AARCH=32 or AARCH=64)
endif
TARGET_BASENAME ?= $(if $(filter 64,$(strip $(AARCH))),kernel8,kernel7l)
ifeq ($(strip $(AARCH)),32)
C_STANDARD += -Wno-incompatible-pointer-types
endif
else ifeq ($(strip $(RASPPI)),5)
ifneq ($(strip $(AARCH)),64)
$(error Pi5 builds require 64-bit Circle)
endif
TARGET_BASENAME ?= kernel_2712
endif

TARGET ?= $(BUILD_DIR)/$(TARGET_BASENAME)
.DEFAULT_GOAL := $(TARGET).img

ATARI800_SRC_DIR = third_party/atari800/src
A2600_SRC_DIR = src/a2600
STELLA_SRC_DIR = third_party/stella
# Stella includes headers by bare name; generate one -I per directory.
# src/a2600 is FIRST so the BMX backend header wins if ever shadowed.
STELLA_INCLUDE_DIRS = -I$(A2600_SRC_DIR) -I$(STELLA_SRC_DIR)/emucore \
	-I$(STELLA_SRC_DIR)/emucore/tia -I$(STELLA_SRC_DIR)/emucore/elf \
	-I$(STELLA_SRC_DIR)/common -I$(STELLA_SRC_DIR)/common/tv_filters \
	-I$(STELLA_SRC_DIR)/lib/json -I$(STELLA_SRC_DIR)/lib
# Upstream libretro port builds with C++23 + no RTTI; same here.
# __BMX__ selects our OSystem/EventHandler/FBBackend/Sound in the
# vendored MediaFactory.hxx. SOUND_SUPPORT enables the real TIA mix.
STELLA_DEFS = -D__BMX__ -DSOUND_SUPPORT
STELLA_CXXFLAGS = -std=c++23 -fno-rtti -Wno-error -Wno-unused-parameter
# Forced prefix for vendored Stella objects only (never for BMX glue):
# no-op std::mutex stand-ins, since bare-metal newlib has no <mutex>.
STELLA_FORCE_INCLUDE = -include third_party/stella/bmx-shim/stella_prefix.h
# Vendored Stella core objects (emucore all, common subset: no SDL, no
# libretro frontend, no main(); tv_filters for the NTSC palette path).
STELLA_UPSTREAM_OBJS = $(patsubst $(STELLA_SRC_DIR)/%.cxx,stella-upstream/%.o,$(STELLA_EMUCORE_SRCS) $(STELLA_COMMON_SRCS))
STELLA_EMUCORE_SRCS = $(filter-out %/elf/EncodingTest.cxx %/elf/LinkerTest.cxx %/EmulationWorker.cxx %/OSystemStandalone.cxx %/ProfilingRunner.cxx,$(shell find $(STELLA_SRC_DIR)/emucore -name "*.cxx" | sort))
STELLA_COMMON_SRCS = $(STELLA_SRC_DIR)/common/AudioQueue.cxx \
	$(STELLA_SRC_DIR)/common/AudioSettings.cxx \
	$(STELLA_SRC_DIR)/common/Base.cxx \
	$(STELLA_SRC_DIR)/common/Bezel.cxx \
	$(STELLA_SRC_DIR)/common/DevSettingsHandler.cxx \
	$(STELLA_SRC_DIR)/common/FpsMeter.cxx \
	$(STELLA_SRC_DIR)/common/HighScoresManager.cxx \
	$(STELLA_SRC_DIR)/common/JoyMap.cxx \
	$(STELLA_SRC_DIR)/common/KeyMap.cxx \
	$(STELLA_SRC_DIR)/common/Logger.cxx \
	$(STELLA_SRC_DIR)/common/MouseControl.cxx \
	$(STELLA_SRC_DIR)/common/PaletteHandler.cxx \
	$(STELLA_SRC_DIR)/common/PhosphorHandler.cxx \
	$(STELLA_SRC_DIR)/common/PhysicalJoystick.cxx \
	$(STELLA_SRC_DIR)/common/PJoystickHandler.cxx \
	$(STELLA_SRC_DIR)/common/PKeyboardHandler.cxx \
	$(STELLA_SRC_DIR)/common/PNGLibrary.cxx \
	$(STELLA_SRC_DIR)/common/RewindManager.cxx \
	$(STELLA_SRC_DIR)/common/StaggeredLogger.cxx \
	$(STELLA_SRC_DIR)/common/StateManager.cxx \
	$(STELLA_SRC_DIR)/common/VideoModeHandler.cxx \
	$(STELLA_SRC_DIR)/common/tv_filters/NTSCFilter.cxx \
	$(STELLA_SRC_DIR)/common/repository/CompositeKeyValueRepository.cxx \
	$(STELLA_SRC_DIR)/common/repository/CompositeKVRJsonAdapter.cxx \
	$(STELLA_SRC_DIR)/common/repository/KeyValueRepositoryConfigfile.cxx \
	$(STELLA_SRC_DIR)/common/repository/KeyValueRepositoryJsonFile.cxx \
	$(STELLA_SRC_DIR)/common/repository/KeyValueRepositoryPropertyFile.cxx
# BASILISK2 (Macintosh 68k, core vendored under third_party/basilisk2).
# CPU tables (cpudefs/cpustbl/cpuemu + cputbl.h) are generated by host tools
# (build68k, gencpu) into third_party/basilisk2/gen, mirroring the Hatari
# gen/ precedent and upstream Unix/Makefile.in lines 208-230.
BASILISK2_ROOT = third_party/basilisk2
BASILISK2_SHIM_DIR = third_party/basilisk2/bmx-shim
BASILISK2_GEN_DIR = third_party/basilisk2/gen
BASILISK2_INCLUDE_DIRS = -I$(BASILISK2_SHIM_DIR) -I$(BASILISK2_ROOT) -I$(BASILISK2_ROOT)/include -I$(BASILISK2_ROOT)/CrossPlatform -I$(BASILISK2_ROOT)/uae_cpu -I$(BASILISK2_GEN_DIR)
BASILISK2_FORCE_INCLUDE = -include third_party/basilisk2/bmx-shim/basilisk_prefix.h
BASILISK2_DEFS = -D__BMX__ -DUSE_JIT=0 -DFPU_UAE -DUSE_CPU_EMUL_SERVICES
BASILISK2_CXXFLAGS = -std=c++17 -fno-rtti -Wno-error -Wno-unused-parameter
# MINIVMAC (Macintosh 128K, core vendored under third_party/minivmac).
# Config headers (CNFUDALL/CNFUDOSG/CNFUDPIC/CNFUIALL/CNFUIPIC) were
# generated once with the upstream setup tool (-m 128K, see
# third_party/minivmac/README.BMX); the backend is
# src/mac128k/mac128k_bmx_backend.c, no setup tool runs at build time.
MINIVMAC_ROOT = third_party/minivmac
MINIVMAC_SHIM_DIR = third_party/minivmac/bmx-shim
MINIVMAC_CFG_DIR = third_party/minivmac/cfg
MINIVMAC_SRC_DIR = third_party/minivmac/src
MINIVMAC_INCLUDE_DIRS = -I$(MINIVMAC_SHIM_DIR) -I$(MINIVMAC_CFG_DIR) -I$(MINIVMAC_SRC_DIR) -Isrc/mac128k
MINIVMAC_DEFS =
MINIVMAC_CFLAGS = -Wall -Wno-error -Wno-unused-parameter
MINIVMAC_CXXFLAGS = -std=c++17 -fno-rtti -Wno-error -Wno-unused-parameter
ZXSPECTRUM_SRC_DIR = third_party/zesarux
# ZEsarUX includes headers by bare name from every subdir (like vAmiga);
# generate one -I per directory. bmx-shim first for compileoptions.h.
ZXSPECTRUM_SHIM_DIR = third_party/zesarux/bmx-shim
ZXSPECTRUM_INCLUDE_DIRS = -I$(ZXSPECTRUM_SHIM_DIR) -I$(ZXSPECTRUM_SRC_DIR) \
	$(shell find $(ZXSPECTRUM_SRC_DIR) -type d | sed 's/^/-I/' | sort -u)
DOS_SRC_DIR = src/dos
APPLE2_SRC_DIR = third_party/apple2
# KEGS headers are included by bare name (like vAmiga); bmx-shim first for
# sys/ioctl.h, then the vendored dir. APPLE2_DEFS selects the BMX clock
# branch in clock.c.
APPLE2_SHIM_DIR = third_party/apple2/bmx-shim
APPLE2_INCLUDE_DIRS = -I$(APPLE2_SHIM_DIR) -I$(APPLE2_SRC_DIR)
APPLE2_DEFS = -D__BMX__
BBC_SRC_DIR = third_party/bbc
# CLK headers are included by relative path (e.g. "Outputs/ScanTarget.hpp",
# like vAmiga); bmx-shim first so Concurrency/AsyncTaskQueue.hpp resolves to
# the synchronous BMX shadow. BBC_DEFS selects no upstream branches (the
# prefix header carries the mutex/condvar stand-ins).
BBC_SHIM_DIR = third_party/bbc/bmx-shim
BBC_INCLUDE_DIRS = -I$(BBC_SHIM_DIR) -I$(BBC_SRC_DIR)
BBC_DEFS =
BBC_FORCE_INCLUDE = -include third_party/bbc/bmx-shim/bbc_prefix.h
# C++20 for concepts/<=>/span; RTTI kept (vAmiga precedent; CLK uses
# dynamic_cast on three host-facing paths). No -fno-rtti here.
BBC_CXXFLAGS = -std=c++20 -Wno-error -Wno-unused-parameter
ATARIST_SRC_DIR = src/atarist
ATARIST_X_SRC_DIR = third_party/hatari/src
# ATARIST include dirs. third_party/hatari/src is FIRST so the BMX-written
# src/config.h is found as "config.h" (leftmost -I wins over VICE config.h).
# includes/ holds main.h etc., cpu/ holds sysdeps.h/hatari-glue.h, debug/
# holds log.h/natfeats.h, bmx-shim/ holds the opaque SDL.h for sdlgui.h,
# gen/ holds the host-generated CPU tables (cpustbl.h/cputbl.h).
ATARIST_GEN_DIR = third_party/hatari/gen
ATARIST_INCLUDE_DIRS = -Ithird_party/hatari/src -Ithird_party/hatari/src/includes \
	-Ithird_party/hatari/src/cpu -Ithird_party/hatari/src/debug \
	-Ithird_party/hatari/src/falcon \
	-Ithird_party/hatari/bmx-shim -I$(ATARIST_GEN_DIR)
# Upstream passes these as -D flags from CMake (BMX has no config files or
# installed data dir; the global atarist.cfg lookup then harmlessly misses
# and defaults apply).
ATARIST_DEFS = -DCONFDIR='"/"' -DBIN2DATADIR='"."'
# Forced prefix for vendored Hatari objects only (never for BMX glue):
# idempotent system headers that some Hatari headers assume pre-included.
ATARIST_FORCE_INCLUDE = -include third_party/hatari/bmx-shim/hatari_prefix.h
# VAMIGA (Amiga A500 OCS, core vendored under third_party/vamiga, see
# third_party/vamiga/README.BMX). Only the BMX glue in src/amiga is built
# from outside the vendored tree.
# NOTE: kernel builds always run with CWD at the repo root (the build
# scripts cd "$SRC_DIR" first), so CURDIR-anchoring is stable here.
# (MAKEFILE_LIST is NOT stable: Circle Config.mk includes change its tail.)
VAMIGA_ROOT := $(abspath $(CURDIR)/third_party/vamiga)
VAMIGA_CORE_DIR = $(VAMIGA_ROOT)/Core
VAMIGA_SHIM_DIR = third_party/vamiga/bmx-shim
# Shim first so sys/socket.h + arpa/inet.h resolve to the bare-metal
# versions, then every upstream Core directory (vAmiga includes by bare
# header name, include dirs come from CMake subdirs upstream).
VAMIGA_INCLUDE_DIRS = -I$(VAMIGA_SHIM_DIR) -I$(VAMIGA_CORE_DIR) \
	$(shell find $(VAMIGA_CORE_DIR) -type d | sed 's/^/-I/' | sort -u)
VAMIGA_FORCE_INCLUDE = -include third_party/vamiga/bmx-shim/vamiga_prefix.h -include vaconfig.h
# Upstream needs C++20 (span/latch/source_location); keep warnings
# non-fatal on BMX (Circle newlib headers are not -Wall-clean).
VAMIGA_CXXFLAGS = -std=c++20 -fconcepts -Wno-error -Wno-unused-parameter -Wno-restrict
# Everything except the desktop headless app (BMX drives VAmiga directly)
# and the cpp-httplib transport (needs OpenSSL/Brotli; stubbed in
# src/amiga/amiga_bmx_stubs.cpp).
VAMIGA_EXCLUDE = %/Headless.cpp %/Misc/RemoteServers/HttpTransport.cpp
VAMIGA_ALL_SRCS = $(filter-out $(VAMIGA_EXCLUDE),$(shell find $(VAMIGA_CORE_DIR) -name "*.cpp" | sort))
VAMIGA_ALL_CSRCS = $(shell find $(VAMIGA_CORE_DIR)/utlib/thirdparty -maxdepth 1 -name "*.c" | sort)
# xDMS unpacker (memory-to-memory extractDMS used by rvlib DMSFile; the CLI
# main in xdms.c, the legacy _old_pfile.c and the host-only maketbl tool
# are excluded).
VAMIGA_XDMS_DIR = $(VAMIGA_CORE_DIR)/rvlib/ThirdParty/xdms
VAMIGA_ALL_CSRCS += $(VAMIGA_XDMS_DIR)/pfile.c \
	$(VAMIGA_XDMS_DIR)/u_init.c $(VAMIGA_XDMS_DIR)/u_rle.c \
	$(VAMIGA_XDMS_DIR)/u_quick.c $(VAMIGA_XDMS_DIR)/u_medium.c \
	$(VAMIGA_XDMS_DIR)/u_deep.c $(VAMIGA_XDMS_DIR)/u_heavy.c \
	$(VAMIGA_XDMS_DIR)/getbits.c $(VAMIGA_XDMS_DIR)/crc_csum.c \
	$(VAMIGA_XDMS_DIR)/tables.c $(VAMIGA_XDMS_DIR)/maketbl.c
VAMIGA_UPSTREAM_OBJS = $(patsubst $(VAMIGA_CORE_DIR)/%.cpp,vamiga-upstream/%.o,$(VAMIGA_ALL_SRCS)) \
	$(patsubst $(VAMIGA_CORE_DIR)/%.c,vamiga-upstream/%.o,$(VAMIGA_ALL_CSRCS))
DOS_X_SRC_DIR = third_party/dosbox-x/src
DOS_X_INC_DIR = third_party/dosbox-x/include
# DOS include dirs. third_party/dosbox-x/src is FIRST so the BMX-written
# src/config.h is found as "config.h" (leftmost -I wins over VICE config.h).
# third_party/dosbox-x itself is included so upstream "include/menu.h"-style
# rooted includes (mapper.h:22) resolve into the vendored tree.
DOS_INCLUDE_DIRS = -Ithird_party/dosbox-x -Ithird_party/dosbox-x/src -Ithird_party/dosbox-x/include \
	-Ithird_party/dosbox-x/src/cpu -Ithird_party/dosbox-x/src/hardware \
	-Ithird_party/dosbox-x/src/dos -Ithird_party/dosbox-x/src/shell \
	-Ithird_party/dosbox-x/src/ints -Ithird_party/dosbox-x/src/misc \
	-Ithird_party/dosbox-x/src/fpu -Ithird_party/dosbox-x/src/aviwriter
# Forced stub-wall header for vendored dosbox-x objects only (never for BMX
# glue): declares the SDL identifiers the subset uses without real SDL.
# png.h is force-included too: bios.cpp uses png_* unconditionally while its
# `#include <png.h>` is C_LIBPNG-gated (off on BMX); the shim's failing
# stubs make the boot-logo path skip safely.
# cstdarg: messages.cpp uses va_start/va_end without including it (relies on
# transitive includes present in glibc/macOS headers but absent in newlib).
DOS_FORCE_INCLUDE = -include third_party/dosbox-x/src/bmx_sdl_stubs.h \
	-include third_party/dosbox-x/src/png.h \
	-include cstdarg

ifeq ($(MACHINE_CLASS),RASPI_ATARI800)

# ATARI800 build: exclude VICE-specific objects, include atari800 core
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
	             remote/bounded_json_writer.o \
	             remote/circle_http_transport.o remote/command_mailbox.o \
	             remote/bmx_api_router.o \
	             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
	             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o \
             update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o \
             update/zipbrowse.o \
             $(ATARI800_SRC_DIR)/acidtest.o \
             $(ATARI800_SRC_DIR)/afile.o \
             $(ATARI800_SRC_DIR)/antic.o \
             $(ATARI800_SRC_DIR)/atari.o \
             $(ATARI800_SRC_DIR)/binload.o \
             $(ATARI800_SRC_DIR)/cartridge.o \
             $(ATARI800_SRC_DIR)/cartridge_info.o \
             $(ATARI800_SRC_DIR)/cassette.o \
             $(ATARI800_SRC_DIR)/cfg.o \
             $(ATARI800_SRC_DIR)/compfile.o \
             $(ATARI800_SRC_DIR)/cpu.o \
             $(ATARI800_SRC_DIR)/crc32.o \
             $(ATARI800_SRC_DIR)/devices.o \
             $(ATARI800_SRC_DIR)/esc.o \
             $(ATARI800_SRC_DIR)/gtia.o \
             $(ATARI800_SRC_DIR)/img_tape.o \
             $(ATARI800_SRC_DIR)/ide.o \
             $(ATARI800_SRC_DIR)/log.o \
             $(ATARI800_SRC_DIR)/memory.o \
             $(ATARI800_SRC_DIR)/monitor.o \
             $(ATARI800_SRC_DIR)/mzpokeysnd.o \
             $(ATARI800_SRC_DIR)/pbi.o \
             $(ATARI800_SRC_DIR)/pbi_bb.o \
             $(ATARI800_SRC_DIR)/pbi_mio.o \
             $(ATARI800_SRC_DIR)/pbi_scsi.o \
             $(ATARI800_SRC_DIR)/pia.o \
             $(ATARI800_SRC_DIR)/pokey.o \
             $(ATARI800_SRC_DIR)/pokeyrec.o \
             $(ATARI800_SRC_DIR)/pokeysnd.o \
             $(ATARI800_SRC_DIR)/remez.o \
             $(ATARI800_SRC_DIR)/rtime.o \
             $(ATARI800_SRC_DIR)/sio.o \
             $(ATARI800_SRC_DIR)/sound.o \
             $(ATARI800_SRC_DIR)/sysrom.o \
             $(ATARI800_SRC_DIR)/util.o \
             $(ATARI800_SRC_DIR)/input.o \
             $(ATARI800_SRC_DIR)/statesav.o \
             $(ATARI800_SRC_DIR)/artifact.o \
             $(ATARI800_SRC_DIR)/colours.o \
             $(ATARI800_SRC_DIR)/colours_external.o \
             $(ATARI800_SRC_DIR)/colours_ntsc.o \
             $(ATARI800_SRC_DIR)/colours_pal.o \
             $(ATARI800_SRC_DIR)/screen.o \
             $(ATARI800_SRC_DIR)/cycle_map.o \
             $(ATARI800_SRC_DIR)/libatari800/api.o \
             $(ATARI800_SRC_DIR)/libatari800/main.o \
             $(ATARI800_SRC_DIR)/libatari800/init.o \
             $(ATARI800_SRC_DIR)/libatari800/exit.o \
             $(ATARI800_SRC_DIR)/libatari800/input.o \
             $(ATARI800_SRC_DIR)/libatari800/video.o \
             $(ATARI800_SRC_DIR)/libatari800/statesav.o \
             $(ATARI800_SRC_DIR)/libatari800/sound.o \
             $(ATARI800_SRC_DIR)/roms/altirraos_800.o \
             $(ATARI800_SRC_DIR)/roms/altirraos_xl.o \
             $(ATARI800_SRC_DIR)/roms/altirra_basic.o \
             $(ATARI800_SRC_DIR)/roms/altirra_5200_os.o \
             $(ATARI800_SRC_DIR)/roms/altirra_5200_charset.o \
             atari800/atari800_emulator_core.o \
             atari800/atari800_video.o \
             atari800/atari800_sound.o \
             atari800/atari800_input.o \
             atari800/atari800_config.o \
             atari800/atari800_menu.o \
             atari800/atari800_debug.o \
             atari800/atari800_bridge.o

else
ifeq ($(MACHINE_CLASS),RASPI_DOS)

# DOS build: exclude VICE-specific objects, placeholder compiled
# from src/dos only (no vendored core yet).
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
	             remote/bounded_json_writer.o \
	             remote/circle_http_transport.o remote/command_mailbox.o \
	             remote/bmx_api_router.o \
	             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
	             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o \
             update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o \
             update/zipbrowse.o \
             dos/dos_emulator_core.o \
             dos/dos_video.o \
             dos/dos_sound.o \
             dos/dos_perf.o \
             dos/dos_sound_oplboards.o \
             dos/dos_input.o \
             dos/dos_config.o \
             dos/dos_menu.o \
             dos/dos_debug.o \
             dos/dos_bridge.o \
             dos/dos_boot.o \
             dos/dos_builtin_stubs.o
# Bisect switches to isolate static-init/boot hangs:
#  DOS_MINIMAL=1   -> glue only (no stubs, no vendored core)
#  DOS_STUBS_ONLY=1 -> glue + 4 link stubs, no vendored core
#  default             -> full subset.
ifndef DOS_MINIMAL
BMC64_OBJS += dos/dos_savestate_stubs.o \
             dos/dos_globals.o \
             dos/dos_ide_cdrom_stubs.o \
             dos/dos_deleted_owner_stubs.o \
             dos/dos_phase1_link2.o
ifndef DOS_STUBS_ONLY
BMC64_OBJS += $(DOS_X_SRC_DIR)/dosbox.o \
             $(DOS_X_SRC_DIR)/misc/setup.o \
             $(DOS_X_SRC_DIR)/misc/messages.o \
             $(DOS_X_SRC_DIR)/misc/support.o \
             $(DOS_X_SRC_DIR)/misc/cross.o \
             $(DOS_X_SRC_DIR)/misc/programs.o \
             $(DOS_X_SRC_DIR)/misc/regionalloctracking.o \
             $(DOS_X_SRC_DIR)/cpu/cpu.o \
             $(DOS_X_SRC_DIR)/cpu/callback.o \
             $(DOS_X_SRC_DIR)/cpu/flags.o \
             $(DOS_X_SRC_DIR)/cpu/paging.o \
             $(DOS_X_SRC_DIR)/cpu/modrm.o \
             $(DOS_X_SRC_DIR)/cpu/core_normal.o \
             $(DOS_X_SRC_DIR)/cpu/core_normal_286.o \
             $(DOS_X_SRC_DIR)/cpu/core_normal_8086.o \
             $(DOS_X_SRC_DIR)/cpu/core_full.o \
             $(DOS_X_SRC_DIR)/cpu/core_prefetch.o \
             $(DOS_X_SRC_DIR)/cpu/core_prefetch_286.o \
             $(DOS_X_SRC_DIR)/cpu/core_prefetch_8086.o \
             $(DOS_X_SRC_DIR)/cpu/core_simple.o \
             $(DOS_X_SRC_DIR)/hardware/memory.o \
             $(DOS_X_SRC_DIR)/hardware/hardware.o \
             $(DOS_X_SRC_DIR)/hardware/iohandler.o \
             $(DOS_X_SRC_DIR)/hardware/pic.o \
             $(DOS_X_SRC_DIR)/hardware/timer.o \
             $(DOS_X_SRC_DIR)/hardware/dma.o \
             $(DOS_X_SRC_DIR)/hardware/keyboard.o \
             $(DOS_X_SRC_DIR)/hardware/cmos.o \
             $(DOS_X_SRC_DIR)/hardware/vga.o \
             $(DOS_X_SRC_DIR)/hardware/vga_attr.o \
             $(DOS_X_SRC_DIR)/hardware/vga_crtc.o \
             $(DOS_X_SRC_DIR)/hardware/vga_dac.o \
             $(DOS_X_SRC_DIR)/hardware/vga_gfx.o \
             $(DOS_X_SRC_DIR)/hardware/vga_memory.o \
             $(DOS_X_SRC_DIR)/hardware/vga_misc.o \
             $(DOS_X_SRC_DIR)/hardware/vga_seq.o \
             $(DOS_X_SRC_DIR)/hardware/vga_other.o \
             $(DOS_X_SRC_DIR)/hardware/vga_draw.o \
             $(DOS_X_SRC_DIR)/hardware/mixer.o \
             $(DOS_X_SRC_DIR)/hardware/pcspeaker.o \
             $(DOS_X_SRC_DIR)/hardware/sblaster.o \
             $(DOS_X_SRC_DIR)/hardware/adlib.o \
             $(DOS_X_SRC_DIR)/hardware/dbopl.o \
             $(DOS_X_SRC_DIR)/hardware/nukedopl.o \
             $(DOS_X_SRC_DIR)/hardware/cqm.o \
             $(DOS_X_SRC_DIR)/hardware/mame/fmopl.o \
             $(DOS_X_SRC_DIR)/hardware/mame/ymf262.o \
             $(DOS_X_SRC_DIR)/hardware/mame/ymdeltat.o \
             $(DOS_X_SRC_DIR)/hardware/esfmu/esfm.o \
             $(DOS_X_SRC_DIR)/hardware/esfmu/esfm_registers.o \
             $(DOS_X_SRC_DIR)/dos/dos.o \
             $(DOS_X_SRC_DIR)/dos/dos_files.o \
             $(DOS_X_SRC_DIR)/dos/dos_execute.o \
             $(DOS_X_SRC_DIR)/dos/dos_devices.o \
             $(DOS_X_SRC_DIR)/dos/dos_tables.o \
             $(DOS_X_SRC_DIR)/dos/dos_classes.o \
             $(DOS_X_SRC_DIR)/dos/dos_memory.o \
             $(DOS_X_SRC_DIR)/dos/dos_misc.o \
             $(DOS_X_SRC_DIR)/dos/dos_programs.o \
             $(DOS_X_SRC_DIR)/dos/eltorito.o \
             $(DOS_X_SRC_DIR)/dos/dos_ioctl.o \
             $(DOS_X_SRC_DIR)/dos/drives.o \
             $(DOS_X_SRC_DIR)/dos/drive_local.o \
             $(DOS_X_SRC_DIR)/dos/drive_fat.o \
             $(DOS_X_SRC_DIR)/dos/drive_cache.o \
             $(DOS_X_SRC_DIR)/dos/drive_virtual.o \
             $(DOS_X_SRC_DIR)/shell/shell.o \
             $(DOS_X_SRC_DIR)/shell/shell_batch.o \
             $(DOS_X_SRC_DIR)/shell/shell_cmds.o \
             $(DOS_X_SRC_DIR)/shell/shell_misc.o \
             $(DOS_X_SRC_DIR)/ints/bios.o \
             $(DOS_X_SRC_DIR)/ints/bios_disk.o \
             $(DOS_X_SRC_DIR)/ints/bios_keyboard.o \
             $(DOS_X_SRC_DIR)/ints/int10.o \
             $(DOS_X_SRC_DIR)/ints/int10_char.o \
             $(DOS_X_SRC_DIR)/ints/int10_modes.o \
             $(DOS_X_SRC_DIR)/ints/int10_memory.o \
             $(DOS_X_SRC_DIR)/ints/int10_misc.o \
             $(DOS_X_SRC_DIR)/ints/int10_pal.o \
             $(DOS_X_SRC_DIR)/ints/int10_put_pixel.o \
             $(DOS_X_SRC_DIR)/ints/int10_vptable.o \
             $(DOS_X_SRC_DIR)/ints/mouse.o \
             $(DOS_X_SRC_DIR)/ints/xms.o \
             $(DOS_X_SRC_DIR)/ints/ems.o \
             $(DOS_X_SRC_DIR)/ints/bios_memdisk.o \
             $(DOS_X_SRC_DIR)/ints/imagedisk_emptydrive.o \
             $(DOS_X_SRC_DIR)/fpu/fpu.o
endif
endif

else
ifeq ($(MACHINE_CLASS),RASPI_ATARIST)

# ATARIST build: exclude VICE-specific objects; BMX glue from src/atarist
# plus the vendored Hatari 2.6.1 core (third_party/hatari/src, ST/STE +
# MegaSTE subset; Falcon/TT/GUI/SDL backends excluded, BMX providers in
# src/atarist/atarist_bmx_*.cpp take their place).
# CPU tables (cpustbl/cpuemu_*) are generated by host tools into
# third_party/hatari/gen (see ATARIST_GEN rules below).
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
	             remote/bounded_json_writer.o \
	             remote/circle_http_transport.o remote/command_mailbox.o \
	             remote/bmx_api_router.o \
	             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
	             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o \
             update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o \
             update/zipbrowse.o \
             atarist/atarist_emulator_core.o \
             atarist/atarist_video.o \
             atarist/atarist_sound.o \
             atarist/atarist_input.o \
             atarist/atarist_config.o \
             atarist/atarist_menu.o \
             atarist/atarist_debug.o \
             atarist/atarist_bridge.o \
             atarist/atarist_bmx_platform.o \
             atarist/atarist_bmx_audio.o \
             atarist/atarist_bmx_screen.o \
             atarist/atarist_bmx_stubs.o \
             $(ATARIST_X_SRC_DIR)/main.o \
             $(ATARIST_X_SRC_DIR)/acia.o \
             $(ATARIST_X_SRC_DIR)/avi_record.o \
             $(ATARIST_X_SRC_DIR)/bios.o \
             $(ATARIST_X_SRC_DIR)/blitter.o \
             $(ATARIST_X_SRC_DIR)/cart.o \
             $(ATARIST_X_SRC_DIR)/cfgopts.o \
             $(ATARIST_X_SRC_DIR)/change.o \
             $(ATARIST_X_SRC_DIR)/clocks_timings.o \
             $(ATARIST_X_SRC_DIR)/configuration.o \
             $(ATARIST_X_SRC_DIR)/control.o \
             $(ATARIST_X_SRC_DIR)/conv_gen.o \
             $(ATARIST_X_SRC_DIR)/conv_st.o \
             $(ATARIST_X_SRC_DIR)/createBlankImage.o \
             $(ATARIST_X_SRC_DIR)/cycInt.o \
             $(ATARIST_X_SRC_DIR)/cycles.o \
             $(ATARIST_X_SRC_DIR)/dmaSnd.o \
             $(ATARIST_X_SRC_DIR)/event.o \
             $(ATARIST_X_SRC_DIR)/fdc.o \
             $(ATARIST_X_SRC_DIR)/file.o \
             $(ATARIST_X_SRC_DIR)/floppy.o \
             $(ATARIST_X_SRC_DIR)/gemdos.o \
             $(ATARIST_X_SRC_DIR)/ikbd.o \
             $(ATARIST_X_SRC_DIR)/inffile.o \
             $(ATARIST_X_SRC_DIR)/ioMem.o \
             $(ATARIST_X_SRC_DIR)/ioMemTabST.o \
             $(ATARIST_X_SRC_DIR)/ioMemTabSTE.o \
             $(ATARIST_X_SRC_DIR)/ioMemTabFalcon.o \
             $(ATARIST_X_SRC_DIR)/ioMemTabTT.o \
             $(ATARIST_X_SRC_DIR)/joy.o \
             $(ATARIST_X_SRC_DIR)/lilo.o \
             $(ATARIST_X_SRC_DIR)/m68000.o \
             $(ATARIST_X_SRC_DIR)/memorySnapShot.o \
             $(ATARIST_X_SRC_DIR)/mfp.o \
             $(ATARIST_X_SRC_DIR)/midi.o \
             $(ATARIST_X_SRC_DIR)/options.o \
             $(ATARIST_X_SRC_DIR)/paths.o \
             $(ATARIST_X_SRC_DIR)/printer.o \
             $(ATARIST_X_SRC_DIR)/psg.o \
             $(ATARIST_X_SRC_DIR)/reset.o \
             $(ATARIST_X_SRC_DIR)/rs232.o \
             $(ATARIST_X_SRC_DIR)/rtc.o \
             $(ATARIST_X_SRC_DIR)/scc.o \
             $(ATARIST_X_SRC_DIR)/screenSnapShot.o \
             $(ATARIST_X_SRC_DIR)/shortcut.o \
             $(ATARIST_X_SRC_DIR)/sound.o \
             $(ATARIST_X_SRC_DIR)/spec512.o \
             $(ATARIST_X_SRC_DIR)/stMemory.o \
             $(ATARIST_X_SRC_DIR)/str.o \
             $(ATARIST_X_SRC_DIR)/tos.o \
             $(ATARIST_X_SRC_DIR)/utils.o \
             $(ATARIST_X_SRC_DIR)/vdi.o \
             $(ATARIST_X_SRC_DIR)/video.o \
             $(ATARIST_X_SRC_DIR)/wavFormat.o \
             $(ATARIST_X_SRC_DIR)/xbios.o \
             $(ATARIST_X_SRC_DIR)/ymFormat.o \
             $(ATARIST_X_SRC_DIR)/floppies/dim.o \
             $(ATARIST_X_SRC_DIR)/floppies/msa.o \
             $(ATARIST_X_SRC_DIR)/floppies/st.o \
             $(ATARIST_X_SRC_DIR)/floppies/stx.o \
             $(ATARIST_X_SRC_DIR)/floppies/scp.o \
             $(ATARIST_X_SRC_DIR)/floppies/kfs.o \
             $(ATARIST_X_SRC_DIR)/cpu/newcpu.o \
             $(ATARIST_X_SRC_DIR)/cpu/newcpu_common.o \
             $(ATARIST_X_SRC_DIR)/cpu/hatari-glue.o \
             $(ATARIST_X_SRC_DIR)/cpu/custom.o \
             $(ATARIST_X_SRC_DIR)/cpu/events.o \
             $(ATARIST_X_SRC_DIR)/cpu/memory.o \
             $(ATARIST_X_SRC_DIR)/cpu/readcpu.o \
             $(ATARIST_X_SRC_DIR)/cpu/cpummu.o \
             $(ATARIST_X_SRC_DIR)/cpu/cpummu030.o \
             $(ATARIST_X_SRC_DIR)/cpu/debug.o \
             $(ATARIST_X_SRC_DIR)/cpu/disasm.o \
             $(ATARIST_X_SRC_DIR)/cpu/fpp.o \
             $(ATARIST_X_SRC_DIR)/cpu/fpp_native.o \
             $(ATARIST_X_SRC_DIR)/cpu/fpp_softfloat.o \
             $(ATARIST_X_SRC_DIR)/cpu/softfloat/softfloat.o \
             $(ATARIST_X_SRC_DIR)/cpu/softfloat/softfloat_decimal.o \
             $(ATARIST_X_SRC_DIR)/cpu/softfloat/softfloat_fpsp.o \
             $(ATARIST_X_SRC_DIR)/cpu/machdep/m68k.o \
             $(ATARIST_X_SRC_DIR)/cpu/writelog.o \
             $(ATARIST_GEN_DIR)/cpudefs.o \
             $(ATARIST_GEN_DIR)/cpustbl.o \
             $(ATARIST_GEN_DIR)/cpuemu_0.o \
             $(ATARIST_GEN_DIR)/cpuemu_11.o \
             $(ATARIST_GEN_DIR)/cpuemu_13.o \
             $(ATARIST_GEN_DIR)/cpuemu_20.o \
             $(ATARIST_GEN_DIR)/cpuemu_21.o \
             $(ATARIST_GEN_DIR)/cpuemu_22.o \
             $(ATARIST_GEN_DIR)/cpuemu_23.o \
             $(ATARIST_GEN_DIR)/cpuemu_24.o \
             $(ATARIST_GEN_DIR)/cpuemu_31.o \
             $(ATARIST_GEN_DIR)/cpuemu_32.o \
             $(ATARIST_GEN_DIR)/cpuemu_33.o \
             $(ATARIST_GEN_DIR)/cpuemu_34.o \
             $(ATARIST_GEN_DIR)/cpuemu_35.o \
             $(ATARIST_GEN_DIR)/cpuemu_40.o \
             $(ATARIST_GEN_DIR)/cpuemu_50.o \
             $(ATARIST_X_SRC_DIR)/debug/log.o \
             $(ATARIST_X_SRC_DIR)/debug/symbols.o \
             $(ATARIST_X_SRC_DIR)/debug/natfeats.o \
             $(ATARIST_X_SRC_DIR)/falcon/nvram.o

else
ifeq ($(MACHINE_CLASS),RASPI_AMIGA)

# AMIGA build: exclude VICE-specific objects; BMX glue from src/amiga plus
# the upstream vAmiga Core used by reference (VAMIGA_UPSTREAM_OBJS, A500
# OCS subset; desktop facade + emulator thread + httplib transport
# excluded, see VAMIGA_EXCLUDE and third_party/vamiga/README.BMX).
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
             remote/bounded_json_writer.o \
             remote/circle_http_transport.o remote/command_mailbox.o \
             remote/bmx_api_router.o \
             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o \
             update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o \
             update/zipbrowse.o \
             amiga/amiga_emulator_core.o \
             amiga/amiga_video.o \
             amiga/amiga_video_tex.o \
             amiga/amiga_sound.o \
             amiga/amiga_input.o \
             amiga/amiga_config.o \
             amiga/amiga_menu.o \
             amiga/amiga_debug.o \
             amiga/amiga_bridge.o \
             amiga/amiga_bmx_stubs.o \
             $(VAMIGA_UPSTREAM_OBJS)

else
ifeq ($(MACHINE_CLASS),RASPI_ZXSPECTRUM)

# ZXSPECTRUM build: exclude VICE-specific objects; BMX glue from
# src/zxspectrum plus the vendored ZEsarUX spectrum subset
# (third_party/zesarux, reduced core; see third_party/zesarux/README.BMX).
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
	             remote/bounded_json_writer.o \
	             remote/circle_http_transport.o remote/command_mailbox.o \
	             remote/bmx_api_router.o \
	             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
	             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o \
             update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o \
             update/zipbrowse.o \
             $(ZXSPECTRUM_SRC_DIR)/cpu.o \
             $(ZXSPECTRUM_SRC_DIR)/operaciones.o \
             $(ZXSPECTRUM_SRC_DIR)/ula.o \
             $(ZXSPECTRUM_SRC_DIR)/contend.o \
             $(ZXSPECTRUM_SRC_DIR)/mem128.o \
             $(ZXSPECTRUM_SRC_DIR)/settings.o \
             $(ZXSPECTRUM_SRC_DIR)/charset.o \
             $(ZXSPECTRUM_SRC_DIR)/cpus/z80_codsinpr.o \
             $(ZXSPECTRUM_SRC_DIR)/cpus/z80_codpred.o \
             $(ZXSPECTRUM_SRC_DIR)/cpus/z80_codprddfd.o \
             $(ZXSPECTRUM_SRC_DIR)/cores/core_reduced_spectrum.o \
             $(ZXSPECTRUM_SRC_DIR)/cores/core_spectrum.o \
             $(ZXSPECTRUM_SRC_DIR)/video/screen.o \
             $(ZXSPECTRUM_SRC_DIR)/audio/audio.o \
             $(ZXSPECTRUM_SRC_DIR)/audio/audio_sine_table.o \
             $(ZXSPECTRUM_SRC_DIR)/audio/audionull.o \
             $(ZXSPECTRUM_SRC_DIR)/soundchips/ay38912.o \
             zxspectrum/zx_emulator_core.o \
             zxspectrum/zx_video.o \
             zxspectrum/zx_sound.o \
             zxspectrum/zx_input.o \
             zxspectrum/zx_machine.o \
             zxspectrum/zx_tape.o \
             zxspectrum/zx_timer.o \
             zxspectrum/zx_shim.o \
             zxspectrum/zx_autostub.o \
             zxspectrum/zx_config.o \
             zxspectrum/zx_menu.o \
             zxspectrum/zx_debug.o \
             zxspectrum/zx_bridge.o

else
ifeq ($(MACHINE_CLASS),RASPI_CPC)

# CPC build: exclude VICE-specific objects; BMX glue from src/cpc, the CPC
# hardware module, plus the vendored Z80/AY/audio subset (the same common
# objects the ZX build uses; Spectrum-only units like ula/contend/screen
# and the reduced Spectrum core are NOT linked here).
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
	             remote/bounded_json_writer.o \
	             remote/circle_http_transport.o remote/command_mailbox.o \
	             remote/bmx_api_router.o \
	             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
	             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o \
             update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o update/zipbrowse.o \
             $(ZXSPECTRUM_SRC_DIR)/cpu.o \
             $(ZXSPECTRUM_SRC_DIR)/operaciones.o \
             $(ZXSPECTRUM_SRC_DIR)/settings.o \
             $(ZXSPECTRUM_SRC_DIR)/cpus/z80_codsinpr.o \
             $(ZXSPECTRUM_SRC_DIR)/cpus/z80_codpred.o \
             $(ZXSPECTRUM_SRC_DIR)/cpus/z80_codprddfd.o \
             $(ZXSPECTRUM_SRC_DIR)/audio/audio.o \
             $(ZXSPECTRUM_SRC_DIR)/audio/audio_sine_table.o \
             $(ZXSPECTRUM_SRC_DIR)/audio/audionull.o \
             $(ZXSPECTRUM_SRC_DIR)/soundchips/ay38912.o \
             cpc/cpc_emulator_core.o \
             cpc/cpc_video.o \
             cpc/cpc_sound.o \
             cpc/cpc_input.o \
             cpc/cpc_machine.o \
             cpc/cpc_tape.o \
             cpc/cpc_timer.o \
             cpc/cpc_shim.o \
             cpc/cpc_autostub.o \
             cpc/cpc_config.o \
             cpc/cpc_menu.o \
             cpc/cpc_debug.o \
             cpc/cpc_bridge.o \
             cpc/cpc_hw.o \
             cpc/cpc_core.o \
             cpc/cpc_dsk.o \
             cpc/cpc_fdc.o

else
ifeq ($(MACHINE_CLASS),RASPI_MSX)

# MSX build: exclude VICE-specific objects; BMX glue from src/msx, the
# vendored MSX hardware module (machines/msx.c) + TMS9918A renderer
# (video_chips/vdp_9918a.c), plus the vendored Z80/AY/audio subset (the
# same common objects the ZX/CPC builds use; Spectrum-only units like
# ula/contend/screen and the reduced Spectrum core are NOT linked here).
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
	             remote/bounded_json_writer.o \
	             remote/circle_http_transport.o remote/command_mailbox.o \
	             remote/bmx_api_router.o \
	             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
	             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o \
             update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o update/zipbrowse.o \
             $(ZXSPECTRUM_SRC_DIR)/cpu.o \
             $(ZXSPECTRUM_SRC_DIR)/operaciones.o \
             $(ZXSPECTRUM_SRC_DIR)/settings.o \
             $(ZXSPECTRUM_SRC_DIR)/cpus/z80_codsinpr.o \
             $(ZXSPECTRUM_SRC_DIR)/cpus/z80_codpred.o \
             $(ZXSPECTRUM_SRC_DIR)/cpus/z80_codprddfd.o \
             $(ZXSPECTRUM_SRC_DIR)/audio/audio.o \
             $(ZXSPECTRUM_SRC_DIR)/audio/audio_sine_table.o \
             $(ZXSPECTRUM_SRC_DIR)/audio/audionull.o \
             $(ZXSPECTRUM_SRC_DIR)/soundchips/ay38912.o \
             $(ZXSPECTRUM_SRC_DIR)/machines/msx.o \
             $(ZXSPECTRUM_SRC_DIR)/video_chips/vdp_9918a.o \
             msx/msx_emulator_core.o \
             msx/msx_video.o \
             msx/msx_sound.o \
             msx/msx_input.o \
             msx/msx_machine.o \
             msx/msx_tape.o \
             msx/msx_cart.o \
             msx/msx_timer.o \
             msx/msx_shim.o \
             msx/msx_autostub.o \
             msx/msx_config.o \
             msx/msx_menu.o \
             msx/msx_debug.o \
             msx/msx_bridge.o \
             msx/msx_core.o

else
ifeq ($(MACHINE_CLASS),RASPI_A2600)

# A2600 build: exclude VICE-specific objects; BMX glue from src/a2600,
# the BMX Stella backend (a2600_stella_backend) plus the vendored Stella
# 8.0 emucore + common subset (STELLA_*_SRCS above; no SDL, no libretro
# frontend, no debugger/gui/cheat).
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
	             remote/bounded_json_writer.o \
	             remote/circle_http_transport.o remote/command_mailbox.o \
	             remote/bmx_api_router.o \
	             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
	             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o \
             update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o update/zipbrowse.o \
             $(STELLA_UPSTREAM_OBJS) \
             a2600/a2600_emulator_core.o \
             a2600/a2600_video.o \
             a2600/a2600_sound.o \
             a2600/a2600_input.o \
             a2600/a2600_cart.o \
             a2600/a2600_timer.o \
             a2600/a2600_config.o \
             a2600/a2600_menu.o \
             a2600/a2600_debug.o \
              a2600/a2600_bridge.o \
              a2600/a2600_stella_backend.o

else
ifeq ($(MACHINE_CLASS),RASPI_MAC68K)

# MAC68K build: exclude VICE-specific objects; BMX glue from src/mac68k
# plus a small initial Basilisk II upstream set (CrossPlatform + uae_cpu).
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
	             remote/bounded_json_writer.o \
	             remote/circle_http_transport.o remote/command_mailbox.o \
	             remote/bmx_api_router.o \
	             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
	             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o \
             update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o update/zipbrowse.o \
             mac68k/mac68k_emulator_core.o \
             mac68k/mac68k_video.o \
             mac68k/mac68k_sound.o \
             mac68k/mac68k_input.o \
             mac68k/mac68k_config.o \
             mac68k/mac68k_menu.o \
             mac68k/mac68k_debug.o \
             mac68k/mac68k_bridge.o \
             mac68k/mac68k_boot.o \
             mac68k/mac68k_basilisk_globals.o \
              mac68k/mac68k_bmx_sys.o \
              mac68k/mac68k_bmx_prefs.o \
              mac68k/mac68k_bmx_stubs_misc.o \
              mac68k/mac68k_bmx_xpram_file.o \
              mac68k/mac68k_bmx_main.o \
              mac68k/mac68k_bmx_tick.o \
              mac68k/mac68k_bmx_video.o \
              mac68k/mac68k_bmx_audio.o \
              basilisk-upstream/main.o \
              basilisk-upstream/prefs.o \
              basilisk-upstream/prefs_items.o \
              basilisk-upstream/video.o \
              basilisk-upstream/audio.o \
              basilisk-upstream/disk.o \
              basilisk-upstream/sony.o \
              basilisk-upstream/timer.o \
              basilisk-upstream/adb.o \
              basilisk-upstream/emul_op.o \
              basilisk-upstream/rom_patches.o \
              basilisk-upstream/rsrc_patches.o \
              basilisk-upstream/slot_rom.o \
              basilisk-upstream/xpram.o \
              basilisk-upstream/macos_util.o \
              basilisk-upstream/user_strings.o \
              basilisk-upstream/serial.o \
              basilisk-upstream/ether.o \
              basilisk-upstream/scsi.o \
              basilisk-upstream/cdrom.o \
              basilisk-upstream/CrossPlatform/vm_alloc.o \
             basilisk-upstream/CrossPlatform/video_blit.o \
             basilisk-upstream/uae_cpu/basilisk_glue.o \
             basilisk-upstream/uae_cpu/memory.o \
             basilisk-upstream/uae_cpu/newcpu.o \
             basilisk-upstream/uae_cpu/readcpu.o \
             basilisk-upstream/uae_cpu/fpu/fpu_uae.o \
              basilisk-gen/cpudefs.o \
              basilisk-gen/cpustbl.o \
              basilisk-gen/cpuemu.o

else ifeq ($(MACHINE_CLASS),RASPI_MAC128K)

# MAC128K build: exclude VICE-specific objects; BMX glue from src/mac128k
# plus the Mini vMac -m 128K core (GLOBGLUE/M68KITAB/MINEM68K/devices/
# PROGMAIN) driven by src/mac128k/mac128k_bmx_backend.c.
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
             remote/bounded_json_writer.o \
             remote/circle_http_transport.o remote/command_mailbox.o \
             remote/bmx_api_router.o \
             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o \
             update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o update/zipbrowse.o \
             mac128k/mac128k_emulator_core.o \
             mac128k/mac128k_video.o \
             mac128k/mac128k_sound.o \
             mac128k/mac128k_input.o \
             mac128k/mac128k_config.o \
             mac128k/mac128k_menu.o \
             mac128k/mac128k_debug.o \
             mac128k/mac128k_bridge.o \
             mac128k/mac128k_boot.o \
             mac128k/mac128k_bmx_video.o \
             mac128k/mac128k_bmx_audio.o \
             mac128k/mac128k_bmx_backend.o \
             minivmac-upstream/GLOBGLUE.o \
             minivmac-upstream/M68KITAB.o \
             minivmac-upstream/MINEM68K.o \
             minivmac-upstream/VIAEMDEV.o \
             minivmac-upstream/IWMEMDEV.o \
             minivmac-upstream/SCCEMDEV.o \
             minivmac-upstream/RTCEMDEV.o \
             minivmac-upstream/ROMEMDEV.o \
             minivmac-upstream/SCSIEMDV.o \
             minivmac-upstream/SONYEMDV.o \
             minivmac-upstream/SCRNEMDV.o \
             minivmac-upstream/MOUSEMDV.o \
             minivmac-upstream/KBRDEMDV.o \
             minivmac-upstream/SNDEMDEV.o \
             minivmac-upstream/PROGMAIN.o

else ifeq ($(MACHINE_CLASS),RASPI_APPLE2)

# APPLE2 build: exclude VICE-specific objects; BMX glue from src/apple2
# plus the vendored GSplus/KEGS core subset (third_party/apple2, 24 files;
# platform drivers excluded, replaced by apple2_bmx_shim.c + glue).
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
	             remote/bounded_json_writer.o \
	             remote/circle_http_transport.o remote/command_mailbox.o \
	             remote/bmx_api_router.o \
	             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
	             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o update/zipbrowse.o \
             apple2/apple2_emulator_core.o \
             apple2/apple2_video.o \
             apple2/apple2_sound.o \
             apple2/apple2_input.o \
             apple2/apple2_machine.o \
             apple2/apple2_drive.o \
             apple2/apple2_timer.o \
             apple2/apple2_config.o \
             apple2/apple2_menu.o \
             apple2/apple2_debug.o \
             apple2/apple2_bridge.o \
             apple2/apple2_bmx_shim.o \
             third_party/apple2/sim65816.o \
             third_party/apple2/engine_c.o \
             third_party/apple2/moremem.o \
             third_party/apple2/video.o \
             third_party/apple2/voc.o \
             third_party/apple2/sound.o \
             third_party/apple2/doc.o \
             third_party/apple2/mockingboard.o \
             third_party/apple2/iwm.o \
             third_party/apple2/smartport.o \
             third_party/apple2/woz.o \
             third_party/apple2/dynapro.o \
             third_party/apple2/dyna_type.o \
             third_party/apple2/dyna_validate.o \
             third_party/apple2/dyna_filt.o \
             third_party/apple2/unshk.o \
             third_party/apple2/undeflate.o \
             third_party/apple2/applesingle.o \
             third_party/apple2/adb.o \
             third_party/apple2/paddles.o \
             third_party/apple2/scc.o \
             third_party/apple2/clock.o \
             third_party/apple2/config.o \
             third_party/apple2/debugger.o

else ifeq ($(MACHINE_CLASS),RASPI_BBC)

# BBC build: exclude VICE-specific objects; BMX glue from src/bbc plus the
# vendored CLK BBC Micro subset (third_party/bbc, 32 files; threads are
# shadowed by the synchronous bmx-shim queue, mutex/condvar by the prefix).
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o \
             remote/circle_discovery_responder.o \
	             remote/bounded_json_writer.o \
	             remote/circle_http_transport.o remote/command_mailbox.o \
	             remote/bmx_api_router.o \
	             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
	             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o update/zipbrowse.o \
             bbc/bbc_emulator_core.o \
             bbc/bbc_video.o \
             bbc/bbc_sound.o \
             bbc/bbc_input.o \
             bbc/bbc_machine.o \
             bbc/bbc_drive.o \
             bbc/bbc_timer.o \
             bbc/bbc_config.o \
             bbc/bbc_menu.o \
             bbc/bbc_debug.o \
             bbc/bbc_bridge.o \
             bbc/bbc_fan.o \
             third_party/bbc/Components/1770/1770.o \
             third_party/bbc/Components/Serial/Line.o \
             third_party/bbc/Components/6522/Implementation/IRQDelegatePortHandler.o \
             third_party/bbc/Components/6850/6850.o \
             third_party/bbc/Components/SAA5050/SAA5050.o \
             third_party/bbc/Components/SID/SID.o \
             third_party/bbc/Components/SN76489/SN76489.o \
             third_party/bbc/Components/uPD7002/uPD7002.o \
             third_party/bbc/Inputs/Keyboard.o \
             third_party/bbc/Machines/Acorn/BBCMicro/BBCMicro.o \
             third_party/bbc/Machines/Acorn/Floppy/1770.o \
             third_party/bbc/Machines/KeyboardMachine.o \
             third_party/bbc/Machines/Utility/MemoryFuzzer.o \
             third_party/bbc/Machines/Utility/ROMCatalogue.o \
             third_party/bbc/Machines/Utility/Typer.o \
             third_party/bbc/Outputs/CRT/CRT.o \
             third_party/bbc/Outputs/ScanTarget.o \
             third_party/bbc/Processors/Z80/Implementation/PartialMachineCycle.o \
             third_party/bbc/Processors/Z80/Implementation/Z80Storage.o \
             third_party/bbc/Reflection/Struct.o \
             third_party/bbc/SignalProcessing/FIRFilter.o \
             third_party/bbc/Storage/Disk/Controller/DiskController.o \
             third_party/bbc/Storage/Disk/Controller/MFMDiskController.o \
             third_party/bbc/Storage/Disk/DiskImage/Formats/AcornADF.o \
             third_party/bbc/Storage/Disk/DiskImage/Formats/MFMSectorDump.o \
             third_party/bbc/Storage/Disk/DiskImage/Formats/SSD.o \
             third_party/bbc/Storage/Disk/DiskImage/Formats/Utility/ImplicitSectors.o \
             third_party/bbc/Storage/Disk/Drive.o \
             third_party/bbc/Storage/Disk/Encodings/MFM/Encoder.o \
             third_party/bbc/Storage/Disk/Encodings/MFM/Parser.o \
             third_party/bbc/Storage/Disk/Encodings/MFM/SegmentParser.o \
             third_party/bbc/Storage/Disk/Encodings/MFM/Shifter.o \
             third_party/bbc/Storage/Disk/Track/PCMSegment.o \
             third_party/bbc/Storage/Disk/Track/PCMTrack.o \
             third_party/bbc/Storage/Disk/Track/TrackSerialiser.o \
             third_party/bbc/Storage/Disk/Track/UnformattedTrack.o \
             third_party/bbc/Storage/FileHolder.o \
             third_party/bbc/Storage/TimedEventLoop.o

else # VICE

# VICE-based build (C64, C128, etc.)
BMC64_OBJS = main.o kernel.o mouse_input.o usb_keyboard_state.o viceoptions.o viceapp.o \
             crt_pi_idx.o crt_pi_rgb.o \
             vicesound.o new_io.o errno_stubs.o async_network.o \
             config/runtime_config.o machines/machine_descriptor.o \
             network/network_service.o \
             platform/platform.o vice_api.o \
             remote/circle_discovery_responder.o \
	             remote/bounded_json_writer.o \
	             remote/circle_http_transport.o remote/command_mailbox.o \
	             remote/bmx_api_router.o \
	             remote/remote_capture.o \
             remote/developer_discovery_codec.o \
             remote/developer_file_transaction.o \
             remote/developer_log_device.o remote/developer_log_ring.o \
             remote/circle_usb_diagnostic_adapter.o \
             remote/developer_usb_diagnostic.o \
             remote/developer_router.o remote/developer_ui_router.o \
	             remote/file_response_stream.o \
             remote/http_parser.o \
             remote/http_response_writer.o remote/http_router.o \
             remote/http_server.o remote/http_types.o \
             remote/remote_service.o remote/v3d_test_status.o \
             update/build_info.o update/body_sinks.o \
             update/circle_secure_stream.o \
             update/config_migration.o update/config_schema.o \
             update/draft_test_ticket.o \
             update/fat_path_policy.o \
             update/github_ca_bundle.o \
             update/fatfs_config_snapshot.o \
             update/fatfs_update_filesystem.o \
             update/fatfs_update_storage.o \
             update/github_draft_client.o update/github_release_client.o \
             update/github_release_parser.o \
             update/http_response_parser.o update/https_stream.o \
             update/atarimania_client.o \
             update/netcheck.o \
             update/json_parser.o \
             update/release_manifest.o update/release_offer.o \
             update/simple_update_installer.o \
             update/sha256.o update/signature_envelope.o \
             update/signature_verifier.o \
             update/trust_store.o \
             update/update_foreground_progress.o \
             update/menu_update_progress_bridge.o \
             update/update_policy.o \
             update/update_service.o update/update_types.o \
             update/url_policy.o update/zip_reader.o update/zipbrowse.o

endif
endif
endif
endif
endif
endif
endif
endif
endif

# Only these translation units inspect the RASPI_<machine> preprocessor
# define.  Everything else is built once per board/configuration and reused by
# all machine kernels without changing the existing direct-object link order.
BMC64_MACHINE_OBJS := kernel.o viceapp.o machines/machine_descriptor.o

ifneq ($(BMX_SID_WORKER)$(BMX_SID_DIAGNOSTICS),00)
BMC64_OBJS += sidworker.o
BMC64_MACHINE_OBJS += sidworker.o
endif
BMC64_COMMON_OBJS := $(filter-out $(BMC64_MACHINE_OBJS),$(BMC64_OBJS))
OBJS = $(addprefix $(BMX_COMMON_BUILD_DIR)/,$(BMC64_COMMON_OBJS)) \
	$(addprefix $(BUILD_DIR)/,$(BMC64_MACHINE_OBJS))

MINIZ_TINFL_OBJ := $(BMX_COMMON_BUILD_DIR)/update/miniz_tinfl.o
OBJS += $(MINIZ_TINFL_OBJ)

ifeq ($(BMX_V3D_RENDER_TEST_KERNEL),1)
V3D_RENDER_TEST_SOURCE_DIR := tools/pi5/v3d-render-test
OBJS += $(BMX_COMMON_BUILD_DIR)/v3dtest/network_render_test.o \
	$(BMX_COMMON_BUILD_DIR)/v3dtest/review_capture.o \
	$(BMX_COMMON_BUILD_DIR)/v3dtest/kms_mode_runner.o \
	$(BMX_COMMON_BUILD_DIR)/v3dtest/render_matrix_runner.o \
	$(BMX_COMMON_BUILD_DIR)/v3dtest/render_test_support.o

$(BMX_COMMON_BUILD_DIR)/v3dtest/kms_mode_runner.o: \
	$(V3D_RENDER_TEST_SOURCE_DIR)/kms_mode_runner.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) -I$(V3D_RENDER_TEST_SOURCE_DIR) \
		-MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/v3dtest/render_matrix_runner.o: \
	$(V3D_RENDER_TEST_SOURCE_DIR)/render_matrix_runner.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) -I$(V3D_RENDER_TEST_SOURCE_DIR) \
		-MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/v3dtest/render_test_support.o: \
	$(V3D_RENDER_TEST_SOURCE_DIR)/render_test_support.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) -I$(V3D_RENDER_TEST_SOURCE_DIR) \
		-MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

OBJS	+= $(BMX_COMMON_BUILD_DIR)/v3dcrt/v3d_crt.o
OBJS	+= $(BMX_COMMON_BUILD_DIR)/v3dcrt/effect_params.o
OBJS	+= $(BMX_COMMON_BUILD_DIR)/v3dcrt/semantic_values.o
OBJS	+= $(BMX_COMMON_BUILD_DIR)/v3dcrt/rgba8_texture_layout.o
OBJS	+= $(BMX_COMMON_BUILD_DIR)/v3dcrt/output_response.o
OBJS	+= $(BMX_COMMON_BUILD_DIR)/v3dcrt/shaders/shader_package.o
OBJS	+= $(BMX_COMMON_BUILD_DIR)/kms/kms_mode.o
ifeq ($(filter RASPI_ATARI800 RASPI_DOS RASPI_ATARIST RASPI_AMIGA RASPI_ZXSPECTRUM RASPI_CPC RASPI_MSX RASPI_A2600 RASPI_MAC68K RASPI_MAC128K RASPI_APPLE2 RASPI_BBC,$(MACHINE_CLASS)),)
OBJS	+= $(BMX_COMMON_BUILD_DIR)/viceemulatorcore.o
ifneq ($(wildcard $(VICE_SOURCE)/blockdev.h),)
OBJS	+= $(BMX_COMMON_BUILD_DIR)/vice_blockdev.o
endif
endif
ifeq ($(RASPPI),5)
ifeq ($(filter RASPI_ATARI800 RASPI_DOS RASPI_ATARIST RASPI_AMIGA RASPI_ZXSPECTRUM RASPI_CPC RASPI_MSX RASPI_A2600 RASPI_MAC68K RASPI_MAC128K RASPI_APPLE2 RASPI_BBC,$(MACHINE_CLASS)),)
ifneq ($(wildcard $(VICE)/arch/shared/sounddrv/libsounddrv.a),)
else ifneq ($(wildcard $(VICE)/arch/shared/sounddrv/soundraspi.o),)
VICELIBS += $(VICE)/arch/shared/sounddrv/soundraspi.o
else
VICELIBS += $(VICE)/sounddrv/soundraspi.o
endif
endif
endif

ifeq ($(RASPPI),5)
OBJS += $(BMX_COMMON_BUILD_DIR)/fbl_native_kms.o \
	$(BMX_COMMON_BUILD_DIR)/pi5_kms.o \
	$(BMX_COMMON_BUILD_DIR)/pi5v3d/pi5_v3d.o \
	$(BMX_COMMON_BUILD_DIR)/pi5v3d/pi5_v3d71_texture_state.o \
	$(BMX_COMMON_BUILD_DIR)/pi5v3d/pi5_v3d_shader.o \
	$(BMX_COMMON_BUILD_DIR)/pi5v3d/shaders/pi5_shader_package_adapter.o \
	$(BMX_COMMON_BUILD_DIR)/pi5v3d/shaders/shader_artifact_materializer.o
else ifeq ($(RASPPI),3)
# Pi3: pure VideoCore IV firmware path (dispmanx + GLES2 + crt-pi).
# pi4kms/pi4v3d are BCM2711-specific and do not compile for BCM2710;
# all their call sites in fbl.cpp are #if RASPPI==4-guarded.
OBJS += $(BMX_COMMON_BUILD_DIR)/fbl.o
else ifeq ($(AARCH),64)
OBJS += $(BMX_COMMON_BUILD_DIR)/fbl_native_kms.o \
	$(BMX_COMMON_BUILD_DIR)/pi4kms/pi4_native_kms.o \
	$(BMX_COMMON_BUILD_DIR)/pi4kms/pi4_kms.o \
	$(BMX_COMMON_BUILD_DIR)/pi4kms/pi4_kms_dlist.o \
	$(BMX_COMMON_BUILD_DIR)/pi4kms/pi4_kms_mode.o \
	$(BMX_COMMON_BUILD_DIR)/pi4kms/pi4_kms_probe.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d_identity.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d_mmu.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d_power.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d42_shader_package_adapter.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d42_texture_state.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d_render.o
else
OBJS += $(BMX_COMMON_BUILD_DIR)/fbl.o \
	$(BMX_COMMON_BUILD_DIR)/pi4kms/pi4_kms.o \
	$(BMX_COMMON_BUILD_DIR)/pi4kms/pi4_kms_dlist.o \
	$(BMX_COMMON_BUILD_DIR)/pi4kms/pi4_kms_mode.o \
	$(BMX_COMMON_BUILD_DIR)/pi4kms/pi4_kms_probe.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d_identity.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d_mmu.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d_power.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d42_shader_package_adapter.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d42_texture_state.o \
	$(BMX_COMMON_BUILD_DIR)/pi4v3d/pi4_v3d_render.o
endif

$(BMX_COMMON_BUILD_DIR)/pi5_kms.o: $(SRC_DIR)/pi5kms/pi5_kms.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

CFLAGS += -I $(SRC_DIR) -I . -I third_party/common -I "$(NEWLIBDIR)/include" -I $(STDDEF_INCPATH) \
          -I $(CIRCLE_STDLIB_HOME)/include \
          -I $(CIRCLE_STDLIB_HOME)/libs/mbedtls/include \
          -I $(CIRCLE_STDLIB_HOME)/libs/circle-newlib/libgloss/circle \
          -I $(CIRCLEHOME)/addon \
          $(addprefix -I ,$(wildcard $(VICE_INCLUDE_DIRS))) \
          -I $(CIRCLEHOME)/addon/fatfs \
          -DRASPI_COMPILE

CFLAGS += -DBMX_EMU_MULTICORE=$(BMX_EMU_MULTICORE) \
	-DBMX_SID_WORKER=$(BMX_SID_WORKER) \
	-DBMX_SID_DIAGNOSTICS=$(BMX_SID_DIAGNOSTICS) \
	-DBMX_V3D_RENDER_TEST_KERNEL=$(BMX_V3D_RENDER_TEST_KERNEL)
CPPFLAGS += -DBMX_EMU_MULTICORE=$(BMX_EMU_MULTICORE) \
	-DBMX_SID_WORKER=$(BMX_SID_WORKER) \
	-DBMX_SID_DIAGNOSTICS=$(BMX_SID_DIAGNOSTICS) \
	-DBMX_V3D_RENDER_TEST_KERNEL=$(BMX_V3D_RENDER_TEST_KERNEL) \
	-DBMX_PI4_LEGACY_DISPLAY=$(if $(filter 3-32 4-32,$(RASPPI)-$(AARCH)),1,0)
CFLAGS += -DBMX_PI4_LEGACY_DISPLAY=$(if $(filter 3-32 4-32,$(RASPPI)-$(AARCH)),1,0)

# Build stamp for the About menu (re-evaluated every make invocation, so it
# is always fresh): "DD.MM.YYYY HH:MM <short-hash>".
CFLAGS += -DBMX_BUILD_STAMP="$(shell date +%d.%m.%Y\ %H:%M) $(shell git -C $(CURDIR) rev-parse --short HEAD 2>/dev/null || echo nogit)"

# ATARI800-specific include paths and compile flags
ifneq ($(MACHINE_CLASS),RASPI_ATARI800)
CFLAGS += $(addprefix -I ,$(wildcard $(VICE_INCLUDE_DIRS)))
else
# Feature selection for the atari800 core lives in exactly one place:
# third_party/atari800/src/config.h (mirrors upstream --target=libatari800).
# Only -DLIBATARI800 is passed here (needed before config.h is included).
CFLAGS += -I third_party/atari800/src \
          -I third_party/atari800/src/libatari800 \
          -I third_party/atari800/src/roms \
          -DLIBATARI800=1
endif

# Circle's freestanding operator new/new[] returns null on allocation failure.
# GCC may otherwise assume throwing-new semantics and remove the explicit null
# checks which protect the updater's low-memory failure paths.
CPPFLAGS += -fcheck-new

ifeq ($(BMC64_BUILD_PROFILE),debug)
CFLAGS += -DBMC64_DEBUG_PROFILE
CPPFLAGS += -DBMC64_DEBUG_PROFILE
else
CFLAGS += -DNDEBUG
CPPFLAGS += -DNDEBUG
endif

# A noncanonical updater repository is a deliberately conspicuous debug-only
# build property.  The C++ repository-policy header repeats the guard and
# validates the bounded identifier literals; this front-end check prevents a
# release-profile build from reaching the compiler with any test-channel
# input at all.
BMX_UPDATE_TEST_BUILD_VALUES := $(strip \
	$(BMX_UPDATE_TEST_CHANNEL) \
	$(BMX_UPDATE_TEST_REPOSITORY_OWNER) \
	$(BMX_UPDATE_TEST_REPOSITORY_NAME) \
	$(BMX_UPDATE_HARDWARE_TEST_MODE))
ifneq ($(BMX_UPDATE_TEST_BUILD_VALUES),)
ifneq ($(BMC64_BUILD_PROFILE),debug)
$(error BMX update test-channel variables require BMC64_BUILD_PROFILE=debug)
endif
ifneq ($(BMX_UPDATE_TEST_CHANNEL),1)
$(error BMX_UPDATE_TEST_CHANNEL must be exactly 1)
endif
ifeq ($(strip $(BMX_UPDATE_TEST_REPOSITORY_OWNER)),)
$(error BMX_UPDATE_TEST_REPOSITORY_OWNER is required for the test channel)
endif
ifeq ($(strip $(BMX_UPDATE_TEST_REPOSITORY_NAME)),)
$(error BMX_UPDATE_TEST_REPOSITORY_NAME is required for the test channel)
endif
ifneq ($(findstring ',$(BMX_UPDATE_TEST_REPOSITORY_OWNER)$(BMX_UPDATE_TEST_REPOSITORY_NAME)),)
$(error BMX update test repository identifiers must not contain a single quote)
endif
# GitHub owner/repository identity is case-insensitive.  Normalize without a
# parse-time shell so hostile build-variable contents cannot become commands.
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(strip $(BMX_UPDATE_TEST_REPOSITORY_OWNER))/$(strip $(BMX_UPDATE_TEST_REPOSITORY_NAME))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst A,a,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst B,b,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst C,c,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst D,d,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst E,e,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst F,f,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst G,g,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst H,h,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst I,i,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst J,j,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst K,k,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst L,l,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst M,m,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst N,n,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst O,o,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst P,p,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst Q,q,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst R,r,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst S,s,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst T,t,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst U,u,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst V,v,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst W,w,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst X,x,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst Y,y,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
override BMX_UPDATE_TEST_REPOSITORY_LOWER := $(subst Z,z,$(BMX_UPDATE_TEST_REPOSITORY_LOWER))
ifeq ($(BMX_UPDATE_TEST_REPOSITORY_LOWER),kdre/bmx)
$(error BMX update test channel must not target canonical kdre/bmx)
endif
CPPFLAGS += -DBMX_UPDATE_TEST_CHANNEL \
	-DBMX_UPDATE_TEST_REPOSITORY_OWNER='"$(BMX_UPDATE_TEST_REPOSITORY_OWNER)"' \
	-DBMX_UPDATE_TEST_REPOSITORY_NAME='"$(BMX_UPDATE_TEST_REPOSITORY_NAME)"'
ifneq ($(strip $(BMX_UPDATE_HARDWARE_TEST_MODE)),)
ifneq ($(BMX_UPDATE_HARDWARE_TEST_MODE),1)
$(error BMX_UPDATE_HARDWARE_TEST_MODE must be exactly 1)
endif
CPPFLAGS += -DBMX_UPDATE_HARDWARE_TEST_MODE=1
endif

endif
ifneq ($(BMC64_RS232_LOG_LEVEL),)
CFLAGS += -DBMC64_RS232_LOG_LEVEL=$(BMC64_RS232_LOG_LEVEL)
CPPFLAGS += -DBMC64_RS232_LOG_LEVEL=$(BMC64_RS232_LOG_LEVEL)
endif
ifneq ($(BMC64_ACIA_LOG_LEVEL),)
CFLAGS += -DBMC64_ACIA_LOG_LEVEL=$(BMC64_ACIA_LOG_LEVEL)
CPPFLAGS += -DBMC64_ACIA_LOG_LEVEL=$(BMC64_ACIA_LOG_LEVEL)
endif
ifneq ($(BMC64_TCP_LOG_LEVEL),)
CFLAGS += -DBMC64_TCP_LOG_LEVEL=$(BMC64_TCP_LOG_LEVEL)
CPPFLAGS += -DBMC64_TCP_LOG_LEVEL=$(BMC64_TCP_LOG_LEVEL)
endif
ifneq ($(BMC64_NET_LOG_LEVEL),)
CFLAGS += -DBMC64_NET_LOG_LEVEL=$(BMC64_NET_LOG_LEVEL)
CPPFLAGS += -DBMC64_NET_LOG_LEVEL=$(BMC64_NET_LOG_LEVEL)
endif

BMX_UPDATE_UPDATER_ABI ?= 2
CFLAGS += -DBMX_UPDATE_UPDATER_ABI=$(BMX_UPDATE_UPDATER_ABI)
CPPFLAGS += -DBMX_UPDATE_UPDATER_ABI=$(BMX_UPDATE_UPDATER_ABI)

ifneq ($(strip $(BMX_UPDATE_SIMPLE_PRODUCTION)),)
ifneq ($(BMX_UPDATE_SIMPLE_PRODUCTION),1)
$(error BMX_UPDATE_SIMPLE_PRODUCTION must be exactly 1)
endif
ifneq ($(BMC64_BUILD_PROFILE),release)
$(error BMX_UPDATE_SIMPLE_PRODUCTION requires BMC64_BUILD_PROFILE=release)
endif
ifneq ($(strip $(BMX_UPDATE_TEST_CHANNEL)),)
$(error BMX_UPDATE_SIMPLE_PRODUCTION is forbidden in an update-test-channel build)
endif
CFLAGS += -DBMX_UPDATE_SIMPLE_PRODUCTION=1
CPPFLAGS += -DBMX_UPDATE_SIMPLE_PRODUCTION=1
endif

ifneq ($(strip $(BMX_UPDATE_OWNER_DRAFT_TEST)),)
ifneq ($(BMX_UPDATE_OWNER_DRAFT_TEST),1)
$(error BMX_UPDATE_OWNER_DRAFT_TEST must be exactly 1)
endif
ifneq ($(BMC64_BUILD_PROFILE),release)
$(error BMX_UPDATE_OWNER_DRAFT_TEST requires BMC64_BUILD_PROFILE=release)
endif
ifneq ($(strip $(BMX_UPDATE_TEST_CHANNEL)),)
$(error BMX_UPDATE_OWNER_DRAFT_TEST is forbidden in an update-test-channel build)
endif
CFLAGS += -DBMX_UPDATE_OWNER_DRAFT_TEST=1
CPPFLAGS += -DBMX_UPDATE_OWNER_DRAFT_TEST=1
endif

UPDATE_TLS_LIBS := \
        $(CIRCLE_STDLIB_HOME)/src/circle-mbedtls/libcircle-mbedtls.a \
        $(CIRCLE_STDLIB_HOME)/libs/mbedtls/library/libmbedtls.a \
        $(CIRCLE_STDLIB_HOME)/libs/mbedtls/library/libmbedx509.a \
        $(CIRCLE_STDLIB_HOME)/libs/mbedtls/library/libmbedcrypto.a

ifeq ($(MACHINE_CLASS),RASPI_ATARI800)
ATARI800_IMAGECONTENTS_LIB :=
else ifeq ($(MACHINE_CLASS),RASPI_DOS)
ATARI800_IMAGECONTENTS_LIB :=
else ifeq ($(MACHINE_CLASS),RASPI_ATARIST)
ATARI800_IMAGECONTENTS_LIB :=
else ifeq ($(MACHINE_CLASS),RASPI_AMIGA)
ATARI800_IMAGECONTENTS_LIB :=
else ifeq ($(MACHINE_CLASS),RASPI_ZXSPECTRUM)
ATARI800_IMAGECONTENTS_LIB :=
else ifeq ($(MACHINE_CLASS),RASPI_CPC)
ATARI800_IMAGECONTENTS_LIB :=
else ifeq ($(MACHINE_CLASS),RASPI_MSX)
ATARI800_IMAGECONTENTS_LIB :=
else ifeq ($(MACHINE_CLASS),RASPI_A2600)
ATARI800_IMAGECONTENTS_LIB :=
else ifeq ($(MACHINE_CLASS),RASPI_MAC68K)
ATARI800_IMAGECONTENTS_LIB :=
else ifeq ($(MACHINE_CLASS),RASPI_MAC128K)
ATARI800_IMAGECONTENTS_LIB :=
else ifeq ($(MACHINE_CLASS),RASPI_APPLE2)
ATARI800_IMAGECONTENTS_LIB :=
else ifeq ($(MACHINE_CLASS),RASPI_BBC)
ATARI800_IMAGECONTENTS_LIB :=
else
ATARI800_IMAGECONTENTS_LIB := $(VICE)/imagecontents/libimagecontents.a
endif

LIBS := $(VICELIBS) \
        $(BMC64_COMMON_LIB) \
        $(ATARI800_IMAGECONTENTS_LIB) \
        $(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a \
        $(CIRCLEHOME)/addon/wlan/libwlan.a \
        $(UPDATE_TLS_LIBS) \
        $(CIRCLE_STDLIB_LIBS) \
        $(CIRCLEHOME)/addon/linux/liblinuxemu.a \
        $(CIRCLEHOME)/lib/sound/libsound.a

# AMIGA only: vAmiga's std::filesystem pulls newlib stat.o, whose _stat
# duplicates new_io.o's FatFs-backed _stat (other kernels never pull
# stat.o, so they don't need this). First object wins: new_io.o.
# (LD is raw ld here, so plain ld spelling, and LDFLAGS -- not LIBS --
# because every LIBS entry becomes a make prerequisite.)
ifeq ($(MACHINE_CLASS),RASPI_AMIGA)
LDFLAGS += --allow-multiple-definition
endif

# ZXSPECTRUM only: the ZEsarUX timer shim (zx_timer.o) defines init_timer,
# which duplicates linuxemu's timer.o once something else pulls that member
# in. First object wins: zx_timer.o (COMMON_LIB precedes liblinuxemu.a).
ifeq ($(MACHINE_CLASS),RASPI_ZXSPECTRUM)
LDFLAGS += --allow-multiple-definition
endif

ifeq ($(RASPPI)-$(AARCH),4-32)
LIBS += $(CIRCLEHOME)/addon/vc4/vchiq/libvchiq.a \
	$(CIRCLEHOME)/addon/vc4/interface/bcm_host/libbcm_host.a \
	$(CIRCLEHOME)/addon/vc4/interface/khronos/libkhrn_client.a \
	$(CIRCLEHOME)/addon/vc4/interface/vcos/libvcos.a \
	$(CIRCLEHOME)/addon/vc4/interface/vmcs_host/libvmcs_host.a
endif
ifeq ($(RASPPI)-$(AARCH),3-32)
LIBS += $(CIRCLEHOME)/addon/vc4/vchiq/libvchiq.a \
	$(CIRCLEHOME)/addon/vc4/interface/bcm_host/libbcm_host.a \
	$(CIRCLEHOME)/addon/vc4/interface/khronos/libkhrn_client.a \
	$(CIRCLEHOME)/addon/vc4/interface/vcos/libvcos.a \
	$(CIRCLEHOME)/addon/vc4/interface/vmcs_host/libvmcs_host.a
endif

# A machine clean deliberately keeps shared BMX objects.  Use the wrapper's
# explicit --clean operation to remove an entire board/configuration variant.
EXTRACLEAN += $(addprefix $(BUILD_DIR)/,$(BMC64_MACHINE_OBJS)) \
              $(addprefix $(BUILD_DIR)/,$(BMC64_MACHINE_OBJS:.o=.d)) \
              $(TARGET).elf $(TARGET).lst $(TARGET).img $(TARGET).hex $(TARGET).cir $(TARGET).map

.PHONY: update-path-policy update-path-policy-check

# sd-layout.toml is the only hand-maintained SD/update-path policy.  The
# generated header contains only its device-side projection; host-only source
# locations therefore do not force target rebuilds.  Release builds check the
# tracked header and never rewrite source state.
update-path-policy:
	@PYTHONDONTWRITEBYTECODE=1 python3 $(UPDATE_PATH_POLICY_GENERATOR) \
		--policy $(SD_LAYOUT_TOML) --output $(UPDATE_PATH_POLICY_HEADER)

update-path-policy-check:
	@PYTHONDONTWRITEBYTECODE=1 python3 $(UPDATE_PATH_POLICY_GENERATOR) \
		--policy $(SD_LAYOUT_TOML) --output $(UPDATE_PATH_POLICY_HEADER) \
		--check

# Circle's Rules.mk does not include this project's generated .d files.  Keep
# the header as an explicit prerequisite, and run the non-mutating consistency
# check even when the object itself is already current.
$(BMX_COMMON_BUILD_DIR)/platform/platform.o \
		$(BMX_COMMON_BUILD_DIR)/update/release_manifest.o: \
		$(UPDATE_PATH_POLICY_GENERATOR) $(UPDATE_PATH_POLICY_MODULE) \
		$(UPDATE_PATH_POLICY_HEADER) | update-path-policy-check

$(BUILD_DIR):
	@mkdir -p $@

$(BMX_COMMON_BUILD_DIR):
	@mkdir -p $@

ifneq ($(strip $(BMX_CONFIG_STAMP)),)
$(OBJS): $(BMX_CONFIG_STAMP)
endif

$(addprefix $(BUILD_DIR)/,$(BMC64_MACHINE_OBJS)): CFLAGS += -D$(MACHINE_CLASS)
$(addprefix $(BUILD_DIR)/,$(BMC64_MACHINE_OBJS)): CPPFLAGS += -D$(MACHINE_CLASS)

# ATARI800 source compile rules (machine dir)
# NOTE: ATARI800 -I dirs must precede $(CFLAGS)/$(CPPFLAGS) on the command
# line: both VICE and atari800 ship a `config.h`, and the VICE build tree is
# on the include path via VICE_INCLUDE_DIRS. Left-most -I wins.
$(BUILD_DIR)/$(ATARI800_SRC_DIR)/%.o: $(ATARI800_SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) -I$(ATARI800_SRC_DIR) -I$(ATARI800_SRC_DIR)/libatari800 -I$(ATARI800_SRC_DIR)/roms $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/$(SRC_DIR)/atari800/%.o: $(SRC_DIR)/atari800/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) -I$(ATARI800_SRC_DIR) -I$(ATARI800_SRC_DIR)/libatari800 -I$(ATARI800_SRC_DIR)/roms $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/$(SRC_DIR)/atari800/%.o: $(SRC_DIR)/atari800/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) -I$(ATARI800_SRC_DIR) -I$(ATARI800_SRC_DIR)/libatari800 -I$(ATARI800_SRC_DIR)/roms $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# ATARI800 objects live in BMC64_COMMON_OBJS (shared across machines), so they
# are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/$(ATARI800_SRC_DIR)/%.o: $(ATARI800_SRC_DIR)/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) -I$(ATARI800_SRC_DIR) -I$(ATARI800_SRC_DIR)/libatari800 -I$(ATARI800_SRC_DIR)/roms $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/$(SRC_DIR)/atari800/%.o: $(SRC_DIR)/atari800/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) -I$(ATARI800_SRC_DIR) -I$(ATARI800_SRC_DIR)/libatari800 -I$(ATARI800_SRC_DIR)/roms $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/$(SRC_DIR)/atari800/%.o: $(SRC_DIR)/atari800/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) -I$(ATARI800_SRC_DIR) -I$(ATARI800_SRC_DIR)/libatari800 -I$(ATARI800_SRC_DIR)/roms $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# DOS source compile rules (machine dir + vendored core)
# NOTE: DOS -I dirs must precede $(CFLAGS)/$(CPPFLAGS) on the command
# line: both VICE and dosbox-x ship a `config.h`, and the VICE build tree is
# on the include path via VICE_INCLUDE_DIRS. Left-most -I wins.
# The src/dos glue rules also carry DOS_INCLUDE_DIRS/FORCE_INCLUDE:
# the link-stub TUs (savestate/globals/ide_cdrom/deleted_owner) include
# vendored dosbox-x headers; the other glue TUs are unaffected in practice
# (no SDL/png/config.h names used).
# Vendored DOSBox-X MAME cores assume 64-bit int32_t == int. On AArch32
# int32_t is long, so pointer conversions in fmopl/ymf262 need -fpermissive
# (Pi3 and Pi4-32 only; 64-bit builds keep strict diagnostics).
ifeq ($(strip $(AARCH)),32)
DOS_CXX_EXTRA := -fpermissive
else
DOS_CXX_EXTRA :=
endif
$(BUILD_DIR)/third_party/dosbox-x/%.o: third_party/dosbox-x/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(DOS_CXX_EXTRA) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
# Vendored C sources (AdLib backends: cqm, esfm). Same include roots, no SDL
# stub wall (pure DSP code, no dosbox headers needing it).
$(BUILD_DIR)/third_party/dosbox-x/%.o: third_party/dosbox-x/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(DOS_INCLUDE_DIRS) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/dos/%.o: $(SRC_DIR)/dos/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# The four link-stub TUs include vendored dosbox-x headers (dosbox.h, ...):
# explicit rules with DOS includes beat the generic pattern rules above.
# All other dos glue keeps pristine BMX flags (VICE config.h, no SDL
# stub wall: bmx_sdl_stubs.h's extern "C" would break their C++ includes).
$(BUILD_DIR)/dos/dos_savestate_stubs.o: $(SRC_DIR)/dos/dos_savestate_stubs.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BUILD_DIR)/dos/dos_globals.o: $(SRC_DIR)/dos/dos_globals.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BUILD_DIR)/dos/dos_ide_cdrom_stubs.o: $(SRC_DIR)/dos/dos_ide_cdrom_stubs.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BUILD_DIR)/dos/dos_deleted_owner_stubs.o: $(SRC_DIR)/dos/dos_deleted_owner_stubs.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BUILD_DIR)/dos/dos_phase1_link2.o: $(SRC_DIR)/dos/dos_phase1_link2.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BUILD_DIR)/dos/dos_boot.o: $(SRC_DIR)/dos/dos_boot.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
# dos_sound.o touches live DOSBox-X sections for the sound menu
# (control.h/setup.h): needs the vendored include roots + the SDL stub wall
# (mem.h needs le16toh), like dos_boot.o.
$(BUILD_DIR)/dos/dos_sound.o: $(SRC_DIR)/dos/dos_sound.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_sound.o: $(SRC_DIR)/dos/dos_sound.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
# dos_perf.o (fast-with-fallback CPU controls): vendored roots + wall.
$(BUILD_DIR)/dos/dos_perf.o: $(SRC_DIR)/dos/dos_perf.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_perf.o: $(SRC_DIR)/dos/dos_perf.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
# dos_sound_oplboards.o implements the hardware-serial OPL board stubs:
# needs the vendored include roots for its stub headers, but no SDL wall.
$(BUILD_DIR)/dos/dos_sound_oplboards.o: $(SRC_DIR)/dos/dos_sound_oplboards.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
# dos_video.o (RENDER backend) and dos_input.o (keyboard map) include
# vendored dosbox-x headers, so they need DOS_INCLUDE_DIRS, but WITHOUT
# the SDL stub wall (bmx_sdl_stubs.h's extern "C" breaks their C++ includes).
$(BUILD_DIR)/dos/dos_video.o: $(SRC_DIR)/dos/dos_video.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BUILD_DIR)/dos/dos_input.o: $(SRC_DIR)/dos/dos_input.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
# dos_emulator_core.o drives DOS_GetLoop slices: needs Bitu/LoopHandler
# from vendored dosbox.h (Bitu is 64-bit on AArch64, so no local typedef).
$(BUILD_DIR)/dos/dos_emulator_core.o: $(SRC_DIR)/dos/dos_emulator_core.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
# dos_builtin_stubs.o (empty binary blobs + minizip failure stubs +
# deleted-backend helpers): needs vendored dosbox-x headers AND the SDL stub
# wall like every other dos stub (mapper.h/programs.h pull mem.h, which
# needs le16toh from bmx_sdl_stubs.h).
$(BUILD_DIR)/dos/dos_builtin_stubs.o: $(SRC_DIR)/dos/dos_builtin_stubs.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/$(SRC_DIR)/dos/%.o: $(SRC_DIR)/dos/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# DOS objects live in BMC64_COMMON_OBJS (shared across machines), so they
# are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/third_party/dosbox-x/%.o: third_party/dosbox-x/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(DOS_CXX_EXTRA) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/third_party/dosbox-x/%.o: third_party/dosbox-x/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(DOS_INCLUDE_DIRS) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/dos/%.o: $(SRC_DIR)/dos/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_savestate_stubs.o: $(SRC_DIR)/dos/dos_savestate_stubs.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_globals.o: $(SRC_DIR)/dos/dos_globals.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_ide_cdrom_stubs.o: $(SRC_DIR)/dos/dos_ide_cdrom_stubs.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_deleted_owner_stubs.o: $(SRC_DIR)/dos/dos_deleted_owner_stubs.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_phase1_link2.o: $(SRC_DIR)/dos/dos_phase1_link2.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_boot.o: $(SRC_DIR)/dos/dos_boot.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_sound_oplboards.o: $(SRC_DIR)/dos/dos_sound_oplboards.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_video.o: $(SRC_DIR)/dos/dos_video.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_input.o: $(SRC_DIR)/dos/dos_input.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_emulator_core.o: $(SRC_DIR)/dos/dos_emulator_core.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
$(BMX_COMMON_BUILD_DIR)/dos/dos_builtin_stubs.o: $(SRC_DIR)/dos/dos_builtin_stubs.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(DOS_INCLUDE_DIRS) $(DOS_FORCE_INCLUDE) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/$(SRC_DIR)/dos/%.o: $(SRC_DIR)/dos/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# ATARIST source compile rules (machine dir + vendored core in Phase 1).
# Phase 0 glue needs no extra -I; the vendored-core rules take
# ATARIST_INCLUDE_DIRS when third_party/hatari lands.
# Hatari glue (src/atarist) includes vendored Hatari headers, so it takes
# ATARIST_INCLUDE_DIRS too (config.h must win over VICE config.h).
$(BUILD_DIR)/atarist/%.o: $(SRC_DIR)/atarist/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(ATARIST_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# BMX Hatari backend providers are plain C (Hatari headers are C-only:
# cpu/sysdeps.h includes <string> under __cplusplus).
$(BUILD_DIR)/atarist/%.o: $(SRC_DIR)/atarist/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ATARIST_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# Vendored Hatari core objects. NOTE: ATARIST_INCLUDE_DIRS must precede
# $(CFLAGS)/$(CPPFLAGS): both VICE and Hatari ship a `config.h`, and the
# VICE build tree is on the include path. Left-most -I wins.
$(BUILD_DIR)/third_party/hatari/%.o: third_party/hatari/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ATARIST_INCLUDE_DIRS) $(ATARIST_DEFS) $(ATARIST_FORCE_INCLUDE) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# ATARIST objects live in BMC64_COMMON_OBJS (shared across machines), so they
# are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/atarist/%.o: $(SRC_DIR)/atarist/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(ATARIST_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/atarist/%.o: $(SRC_DIR)/atarist/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ATARIST_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/third_party/hatari/%.o: third_party/hatari/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ATARIST_INCLUDE_DIRS) $(ATARIST_DEFS) $(ATARIST_FORCE_INCLUDE) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# AMIGA build rules. BMX glue (src/amiga) takes VAMIGA_INCLUDE_DIRS so the
# upstream headers resolve; upstream vAmiga objects additionally take the
# force-include prefix + C++20. NOTE: VAMIGA flags must precede
# $(CPPFLAGS): both VICE and vAmiga ship config-ish headers and the older
# -std= from Circle Rules must be overridden by the later -std=c++20.
$(BUILD_DIR)/amiga/%.o: $(SRC_DIR)/amiga/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) $(VAMIGA_INCLUDE_DIRS) $(VAMIGA_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/vamiga-upstream/%.o: $(VAMIGA_CORE_DIR)/%.cpp | $(BUILD_DIR)
	@echo "  VAMIGA $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) $(VAMIGA_INCLUDE_DIRS) $(VAMIGA_FORCE_INCLUDE) $(VAMIGA_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/vamiga-upstream/%.o: $(VAMIGA_CORE_DIR)/%.c | $(BUILD_DIR)
	@echo "  VAMIGA-C $@"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(VAMIGA_INCLUDE_DIRS) -include third_party/vamiga/bmx-shim/vamiga_c_prefix.h $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# AMIGA objects live in BMC64_COMMON_OBJS (shared across machines), so they
# are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/amiga/%.o: $(SRC_DIR)/amiga/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) $(VAMIGA_INCLUDE_DIRS) $(VAMIGA_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/vamiga-upstream/%.o: $(VAMIGA_CORE_DIR)/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  VAMIGA $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) $(VAMIGA_INCLUDE_DIRS) $(VAMIGA_FORCE_INCLUDE) $(VAMIGA_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/vamiga-upstream/%.o: $(VAMIGA_CORE_DIR)/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  VAMIGA-C $@"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(VAMIGA_INCLUDE_DIRS) -include third_party/vamiga/bmx-shim/vamiga_c_prefix.h $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# ZXSPECTRUM source compile rules (machine dir + vendored ZEsarUX subset).
# The vendored C files include upstream headers by bare name; the BMX glue
# in src/zxspectrum includes them too, so both take ZXSPECTRUM_INCLUDE_DIRS.
$(BUILD_DIR)/zxspectrum/%.o: $(SRC_DIR)/zxspectrum/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(ZXSPECTRUM_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/zxspectrum/%.o: $(SRC_DIR)/zxspectrum/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ZXSPECTRUM_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/third_party/zesarux/%.o: third_party/zesarux/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ZXSPECTRUM_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# ZXSPECTRUM objects live in BMC64_COMMON_OBJS (shared across machines), so
# they are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/zxspectrum/%.o: $(SRC_DIR)/zxspectrum/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(ZXSPECTRUM_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/zxspectrum/%.o: $(SRC_DIR)/zxspectrum/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ZXSPECTRUM_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/third_party/zesarux/%.o: third_party/zesarux/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ZXSPECTRUM_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# CPC source compile rules (machine dir). Same vendored include dirs as the
# ZX build (bare-name upstream headers); the BMX glue lives in src/cpc.
$(BUILD_DIR)/cpc/%.o: $(SRC_DIR)/cpc/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(ZXSPECTRUM_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/cpc/%.o: $(SRC_DIR)/cpc/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ZXSPECTRUM_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# CPC objects live in BMC64_COMMON_OBJS (shared across machines), so they
# are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/cpc/%.o: $(SRC_DIR)/cpc/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(ZXSPECTRUM_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/cpc/%.o: $(SRC_DIR)/cpc/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ZXSPECTRUM_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# APPLE2 source compile rules (machine dir). KEGS headers are included by
# bare name, so third_party/apple2 is on the path for glue and core alike;
# the vendored core additionally gets -D__BMX__ (Circle clock branch).
$(BUILD_DIR)/apple2/%.o: $(SRC_DIR)/apple2/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(APPLE2_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/apple2/%.o: $(SRC_DIR)/apple2/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(APPLE2_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/third_party/apple2/%.o: third_party/apple2/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(APPLE2_INCLUDE_DIRS) $(APPLE2_DEFS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# APPLE2 objects live in BMC64_COMMON_OBJS (shared across machines), so they
# are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/apple2/%.o: $(SRC_DIR)/apple2/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(APPLE2_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/apple2/%.o: $(SRC_DIR)/apple2/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(APPLE2_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/third_party/apple2/%.o: third_party/apple2/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(APPLE2_INCLUDE_DIRS) $(APPLE2_DEFS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# BBC source compile rules (machine dir). CLK headers resolve by relative
# path; the vendored core additionally gets the BMX prefix (mutex/condvar
# stand-ins) and C++20. Mirrors the STELLA third_party C++ pattern.
$(BUILD_DIR)/bbc/%.o: $(SRC_DIR)/bbc/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(BBC_INCLUDE_DIRS) $(CPPFLAGS) $(BBC_FORCE_INCLUDE) $(BBC_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/third_party/bbc/%.o: third_party/bbc/%.cpp | $(BUILD_DIR)
	@echo "  BBC   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(BBC_INCLUDE_DIRS) $(BBC_DEFS) $(CPPFLAGS) $(BBC_FORCE_INCLUDE) $(BBC_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# BBC objects live in BMC64_COMMON_OBJS (shared across machines), so they
# are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/bbc/%.o: $(SRC_DIR)/bbc/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(BBC_INCLUDE_DIRS) $(CPPFLAGS) $(BBC_FORCE_INCLUDE) $(BBC_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/third_party/bbc/%.o: third_party/bbc/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  BBC   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(BBC_INCLUDE_DIRS) $(BBC_DEFS) $(CPPFLAGS) $(BBC_FORCE_INCLUDE) $(BBC_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# MSX source compile rules (machine dir). Same vendored include dirs as the
# ZX/CPC builds (bare-name upstream headers); the BMX glue lives in src/msx.
# The vendored machines/msx.c + video_chips/vdp_9918a.c compile through the
# generic third_party/zesarux rules above (listed explicitly in the
# RASPI_MSX BMC64_OBJS block).
$(BUILD_DIR)/msx/%.o: $(SRC_DIR)/msx/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(ZXSPECTRUM_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/msx/%.o: $(SRC_DIR)/msx/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ZXSPECTRUM_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# MSX objects live in BMC64_COMMON_OBJS (shared across machines), so they
# are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/msx/%.o: $(SRC_DIR)/msx/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(ZXSPECTRUM_INCLUDE_DIRS) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/msx/%.o: $(SRC_DIR)/msx/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(ZXSPECTRUM_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# A2600 source compile rules (machine dir + vendored Stella subset).
# Stella includes headers by bare name; the BMX backend header lives in
# src/a2600, so STELLA_INCLUDE_DIRS covers both.
$(BUILD_DIR)/a2600/%.o: $(SRC_DIR)/a2600/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(STELLA_INCLUDE_DIRS) $(STELLA_DEFS) $(CPPFLAGS) $(STELLA_FORCE_INCLUDE) $(STELLA_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/a2600/%.o: $(SRC_DIR)/a2600/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(STELLA_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/stella-upstream/%.o: $(STELLA_SRC_DIR)/%.cxx | $(BUILD_DIR)
	@echo "  STELLA $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(STELLA_INCLUDE_DIRS) $(STELLA_DEFS) $(CPPFLAGS) $(STELLA_FORCE_INCLUDE) $(STELLA_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# A2600 objects live in BMC64_COMMON_OBJS (shared across machines), so they
# are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/a2600/%.o: $(SRC_DIR)/a2600/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(STELLA_INCLUDE_DIRS) $(STELLA_DEFS) $(CPPFLAGS) $(STELLA_FORCE_INCLUDE) $(STELLA_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/a2600/%.o: $(SRC_DIR)/a2600/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(STELLA_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/stella-upstream/%.o: $(STELLA_SRC_DIR)/%.cxx | $(BMX_COMMON_BUILD_DIR)
	@echo "  STELLA $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(STELLA_INCLUDE_DIRS) $(STELLA_DEFS) $(CPPFLAGS) $(STELLA_FORCE_INCLUDE) $(STELLA_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# MAC68K source compile rules (machine dir + vendored Basilisk II subset).
# Basilisk II includes headers by bare name; the BMX glue header lives in
# src/mac68k, so BASILISK2_INCLUDE_DIRS covers both.
$(BUILD_DIR)/mac68k/%.o: $(SRC_DIR)/mac68k/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(BASILISK2_INCLUDE_DIRS) $(BASILISK2_DEFS) $(CPPFLAGS) $(BASILISK2_FORCE_INCLUDE) $(BASILISK2_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/mac68k/%.o: $(SRC_DIR)/mac68k/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(BASILISK2_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/basilisk-upstream/%.o: $(BASILISK2_ROOT)/%.cpp | $(BUILD_DIR)
	@echo "  BASILISK2 $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(BASILISK2_INCLUDE_DIRS) $(BASILISK2_DEFS) $(CPPFLAGS) $(BASILISK2_FORCE_INCLUDE) $(BASILISK2_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/basilisk-upstream/%.o: $(BASILISK2_ROOT)/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(BASILISK2_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# Basilisk II host-generated CPU tables (third_party/basilisk2/gen, built
# with host gcc/g++ per Unix/Makefile.in; never the ARM cross compiler).
$(BUILD_DIR)/basilisk-gen/%.o: $(BASILISK2_GEN_DIR)/%.cpp | $(BUILD_DIR)
	@echo "  BASILISK2 $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(BASILISK2_INCLUDE_DIRS) $(BASILISK2_DEFS) $(CPPFLAGS) $(BASILISK2_FORCE_INCLUDE) $(BASILISK2_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# MAC68K objects live in BMC64_COMMON_OBJS (shared across machines), so they
# are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/mac68k/%.o: $(SRC_DIR)/mac68k/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(BASILISK2_INCLUDE_DIRS) $(BASILISK2_DEFS) $(CPPFLAGS) $(BASILISK2_FORCE_INCLUDE) $(BASILISK2_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/mac68k/%.o: $(SRC_DIR)/mac68k/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(BASILISK2_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/basilisk-upstream/%.o: $(BASILISK2_ROOT)/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  BASILISK2 $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(BASILISK2_INCLUDE_DIRS) $(BASILISK2_DEFS) $(CPPFLAGS) $(BASILISK2_FORCE_INCLUDE) $(BASILISK2_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/basilisk-upstream/%.o: $(BASILISK2_ROOT)/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(BASILISK2_INCLUDE_DIRS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/basilisk-gen/%.o: $(BASILISK2_GEN_DIR)/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  BASILISK2 $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(BASILISK2_INCLUDE_DIRS) $(BASILISK2_DEFS) $(CPPFLAGS) $(BASILISK2_FORCE_INCLUDE) $(BASILISK2_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# MAC128K source compile rules (machine dir + vendored Mini vMac subset).
# Mini vMac includes headers by bare name; the BMX backend header lives in
# src/mac128k, so MINIVMAC_INCLUDE_DIRS covers bmx-shim, cfg, src and glue.
$(BUILD_DIR)/mac128k/%.o: $(SRC_DIR)/mac128k/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(MINIVMAC_INCLUDE_DIRS) $(MINIVMAC_DEFS) $(CPPFLAGS) $(MINIVMAC_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/mac128k/%.o: $(SRC_DIR)/mac128k/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(MINIVMAC_INCLUDE_DIRS) $(MINIVMAC_DEFS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/minivmac-upstream/%.o: $(MINIVMAC_SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "  MINIVMAC $@"
	@mkdir -p $(dir $@)
	@$(CC) $(MINIVMAC_INCLUDE_DIRS) $(MINIVMAC_DEFS) $(CFLAGS) $(C_STANDARD) $(MINIVMAC_CFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

# MAC128K objects live in BMC64_COMMON_OBJS (shared across machines), so they
# are prefixed with BMX_COMMON_BUILD_DIR at link time. Provide matching
# pattern rules for the shared object pool.
ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/mac128k/%.o: $(SRC_DIR)/mac128k/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(MINIVMAC_INCLUDE_DIRS) $(MINIVMAC_DEFS) $(CPPFLAGS) $(MINIVMAC_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/mac128k/%.o: $(SRC_DIR)/mac128k/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(MINIVMAC_INCLUDE_DIRS) $(MINIVMAC_DEFS) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/minivmac-upstream/%.o: $(MINIVMAC_SRC_DIR)/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  MINIVMAC $@"
	@mkdir -p $(dir $@)
	@$(CC) $(MINIVMAC_INCLUDE_DIRS) $(MINIVMAC_DEFS) $(CFLAGS) $(C_STANDARD) $(MINIVMAC_CFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

# Hatari 68k CPU tables: generated by host tools (build68k, gencpu) exactly
# like upstream's cross-compile path. Generated sources live in-tree under
# third_party/hatari/gen and are rebuilt whenever the generator inputs
# change. Host `cc` is used deliberately (never the cross compiler).
ATARIST_CPU_TABLE_SRCS = cpustbl.c cpuemu_0.c cpuemu_11.c cpuemu_13.c \
	cpuemu_20.c cpuemu_21.c cpuemu_22.c cpuemu_23.c cpuemu_24.c \
	cpuemu_31.c cpuemu_32.c cpuemu_33.c cpuemu_34.c cpuemu_35.c \
	cpuemu_40.c cpuemu_50.c

$(ATARIST_GEN_DIR)/build68k: $(ATARIST_X_SRC_DIR)/cpu/build68k.c $(ATARIST_X_SRC_DIR)/cpu/writelog.c
	@mkdir -p $(ATARIST_GEN_DIR)
	cc -I$(ATARIST_X_SRC_DIR)/cpu $(ATARIST_X_SRC_DIR)/cpu/build68k.c $(ATARIST_X_SRC_DIR)/cpu/writelog.c -o $@

$(ATARIST_GEN_DIR)/cpudefs.c: $(ATARIST_X_SRC_DIR)/cpu/table68k $(ATARIST_GEN_DIR)/build68k
	@mkdir -p $(ATARIST_GEN_DIR)
	$(ATARIST_GEN_DIR)/build68k < $(ATARIST_X_SRC_DIR)/cpu/table68k > $@

$(ATARIST_GEN_DIR)/gencpu: $(ATARIST_GEN_DIR)/cpudefs.c $(ATARIST_X_SRC_DIR)/cpu/gencpu.c $(ATARIST_X_SRC_DIR)/cpu/readcpu.c
	@mkdir -p $(ATARIST_GEN_DIR)
	cc -I$(ATARIST_X_SRC_DIR)/cpu -I$(ATARIST_GEN_DIR) $(ATARIST_GEN_DIR)/cpudefs.c $(ATARIST_X_SRC_DIR)/cpu/gencpu.c $(ATARIST_X_SRC_DIR)/cpu/readcpu.c -o $@

$(addprefix $(ATARIST_GEN_DIR)/,$(ATARIST_CPU_TABLE_SRCS)) &: $(ATARIST_GEN_DIR)/gencpu
	@mkdir -p $(ATARIST_GEN_DIR)
	cd $(ATARIST_GEN_DIR) && ./gencpu

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	@echo "  AS    $@"
	@mkdir -p $(dir $@)
	@$(AS) $(AFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

ifneq ($(abspath $(BMX_COMMON_BUILD_DIR)),$(abspath $(BUILD_DIR)))
$(BMX_COMMON_BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BMX_COMMON_BUILD_DIR)
	@echo "  AS    $@"
	@mkdir -p $(dir $@)
	@$(AS) $(AFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(C_STANDARD) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

$(BMX_COMMON_BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BMX_COMMON_BUILD_DIR)
	@echo "  CPP   $@"
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<
endif

$(MINIZ_TINFL_OBJ): third_party/miniz/miniz_tinfl.c \
		third_party/miniz/miniz.c third_party/miniz/miniz.h | $(BMX_COMMON_BUILD_DIR)
	@echo "  CC    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(C_STANDARD) -I third_party/miniz \
		-MMD -MP -MF $(@:.o=.d) -MT $@ -c -o $@ $<

-include $(OBJS:.o=.d)

# Policy maintenance must work in a fresh source checkout before the pinned
# Circle tree has been extracted.  Every kernel/build goal still requires the
# normal Circle rules; only these two self-contained explicit goals skip them.
ifneq ($(strip $(MAKECMDGOALS)),)
ifeq ($(strip $(filter-out update-path-policy update-path-policy-check,$(MAKECMDGOALS))),)
else
include $(CIRCLEHOME)/Rules.mk
endif
else
include $(CIRCLEHOME)/Rules.mk
endif

ifeq ($(strip $(BMX_GENERATE_LISTING)),0)
# Circle's link recipe always emits a listing.  Keep the expected empty file
# but skip the expensive full-kernel disassembly for normal developer builds.
override OBJDUMP := true
override CPPFILT := cat
endif
