TARGET      := gyroduckhunt
BUILD       := build
SOURCES     := source
DATA        :=
INCLUDES    := -I../nestopia/libretro/libretro-common/include

APP_TITLE   := Gyro Zapper NES
APP_AUTHOR  := Kitta88
APP_VERSION := 0.1.0

SDL2_CONFIG ?= $(DEVKITPRO)/portlibs/switch/bin/sdl2-config
COMMONFLAGS := -O2 -Wall -Wextra -D__SWITCH__ $(INCLUDES) $(shell $(SDL2_CONFIG) --cflags)
CFLAGS      += $(COMMONFLAGS)
CXXFLAGS    += $(COMMONFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
LIBS        += $(shell $(SDL2_CONFIG) --libs) -lnestopia -lstdc++ -lbz2 -lz -llzma \
               -lm -lpthread -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lnx
LIBDIRS     += $(DEVKITPRO)/portlibs/switch/lib $(DEVKITPRO)/libnx/lib $(CURDIR)

include $(DEVKITPRO)/libnx/switch_rules

