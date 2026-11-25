#---------------------------------------------------------------------------------
# OpenSpinDrift_nx -- Nintendo Switch wrapper for the OpenSpinDrift SDL2 game
# library. Builds no game code and bundles no game data.
#
# Requires devkitA64 + devkitPro packages:
#   pacman -S switch-dev switch-sdl2 switch-mesa switch-libdrm_nouveau
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO in your environment. (export DEVKITPRO=/opt/devkitpro)")
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := OpenSpinDrift_nx
APP_TITLE   := OpenSpinDrift
APP_AUTHOR  := Mr. Matt & simplePsyduck
APP_VERSION := 1.0
APP_ICON    := $(TOPDIR)/icon.jpg
BUILD    := build

# switch_rules' %.nro rule only passes NROFLAGS to elf2nro, so the title,
# author, version and icon have to be handed over explicitly or the built
# .nro carries no NACP asset and hbmenu falls back to the filename.
NROFLAGS := --icon=$(APP_ICON) --nacp=$(TOPDIR)/$(TARGET).nacp
SOURCES  := source
INCLUDES := source

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES) \
           $(INCLUDE) -D__SWITCH__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
            -Wl,--allow-multiple-definition -Wl,-Map,$(notdir $*.map)

# SDL2 provides the window/HID/audio/GL-context backend the game expects;
# mesa GLES2 + EGL + nouveau are the rendering stack it imports.
LIBS := -lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau -lnx -lm

LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD := $(CXX)
export OFILES := $(addsuffix .o,$(SFILES)) $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(PORTLIBS)/include/SDL2 -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
$(BUILD):
	@mkdir -p $@
clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
DEPENDS := $(OFILES:.o=.d)
all : $(OUTPUT).nro
$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf : $(OFILES)
-include $(DEPENDS)
endif
