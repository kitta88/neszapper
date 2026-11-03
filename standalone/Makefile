.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set)
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := gyroduckhunt
BUILD       := build
SOURCES     := source
DATA        :=
ROMFS       := romfs
INCLUDES    := ../nestopia/libretro/libretro-common/include
APP_TITLE   := Gyro Zapper NES
APP_AUTHOR  := Kitta88
APP_VERSION := 0.1.0
NO_ICON     := 1

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
export APP_TITLE APP_AUTHOR APP_VERSION NO_ICON
export NROFLAGS += --romfsdir=$(TOPDIR)/$(ROMFS)

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
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif

