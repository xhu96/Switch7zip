#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Project settings
#---------------------------------------------------------------------------------
TARGET      := Switch7zip
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include

APP_TITLE   := Switch 7zip
APP_AUTHOR  := Xhulio
APP_VERSION := 0.9.11-pre
APP_ICON    := icon.jpg

#---------------------------------------------------------------------------------
# Compiler and linker settings
#---------------------------------------------------------------------------------
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
DEFINES  := -D__SWITCH__ -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
CFLAGS   := -g -Wall -Wextra -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS   += $(INCLUDE) `sdl2-config --cflags`
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# libarchive must appear before its dependencies; libnx should stay last.
# SDL2_image is used by the modern image viewer. switch-pkg-config keeps
# dependency ordering correct when available; the fallback works on typical
# devkitPro Switch SDL2_image installs.
SDL2_IMAGE_LIBS := $(shell switch-pkg-config --static --libs SDL2_image 2>/dev/null || echo -lSDL2_image -lpng -ljpeg -lwebp)
LIBS := -larchive -lzstd -llzma -llz4 -lbz2 -lz -lexpat $(SDL2_IMAGE_LIBS) `sdl2-config --libs` -lm
LIBDIRS := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# Build rules based on the standard libnx application template
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(NO_NACP)),)
export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(strip $(APP_ICON)),)
export NROFLAGS += --icon=$(CURDIR)/$(APP_ICON)
endif

.PHONY: all clean $(BUILD)

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS := $(OFILES:.o=.d)

.PHONY: all

all: $(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
else
$(OUTPUT).nro: $(OUTPUT).elf
endif

$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC): $(HFILES_BIN)

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
