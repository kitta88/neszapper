.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set)
endif

TOPDIR ?= $(CURDIR)
ROMFS := romfs
TARGET := gyroduckhunt
APP_TITLE := ZAPP-con
APP_AUTHOR := Kitta88
APP_VERSION := 0.2.0
ICON := icon.jpg
include $(DEVKITPRO)/libnx/switch_rules

BUILD       := build
SOURCES     := source
DATA        :=
INCLUDES    := ../nestopia/libretro/libretro-common/include

ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
LIBDIRS     := $(DEVKITPRO)/portlibs/switch $(DEVKITPRO)/libnx
CFLAGS      := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS      += $(INCLUDE) -D__SWITCH__
CXXFLAGS    := $(CFLAGS) $(foreach dir,$(INCLUDES),-I$(TOPDIR)/$(dir)) \
               $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(DEVKITPRO)/portlibs/switch/include/SDL2 \
               -D_REENTRANT -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS     := -g $(ARCH)
LDFLAGS     = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH)
LIBS        := $(shell $(DEVKITPRO)/portlibs/switch/bin/sdl2-config --libs) -lSDL2_ttf -lfreetype -lharfbuzz -lpng -lnestopia \
               -lbz2 -lz -llzma -lm -lstdc++ -lpthread -lEGL -lGLESv2 \
               -lglapi -ldrm_nouveau -lnx
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
OFILES_SRC := $(CPPFILES:.cpp=.o)
OFILES := $(OFILES_SRC)

export LD := $(CXX)
export OFILES
export OFILES_SRC
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) -L$(TOPDIR)
export APP_TITLE APP_AUTHOR APP_VERSION

.PHONY: all clean $(BUILD)
all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

DEPENDS := $(OFILES:.o=.d)
.PHONY: all
all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ --nacp=$(OUTPUT).nacp --icon=$(TOPDIR)/$(ICON) --romfsdir=$(TOPDIR)/$(ROMFS)
	@echo "built ... $(notdir $@)"
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif

