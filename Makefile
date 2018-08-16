# GNU_TOOLCHAIN_PREFIX:   The perfix of gnu toolchain.
ifeq "$(ARCH)" "arm"
GNU_TOOLCHAIN_PREFIX:=arm-linux-gnueabihf-
endif
CC = $(GNU_TOOLCHAIN_PREFIX)gcc
C++ = $(GNU_TOOLCHAIN_PREFIX)g++
LINK = $(GNU_TOOLCHAIN_PREFIX)g++

LIBS = -lkompex-sqlite-wrapper -ldl -lpthread -lglib-2.0 -lgio-2.0 -lgobject-2.0 -lgmodule-2.0 -lffi -lpcre -lz -lmosquitto -lrt
#must add -fPIC option
CCFLAGS = $(COMPILER_FLAGS) -c -g -fPIC -std=gnu99 -D_GNU_SOURCE
C++FLAGS = $(COMPILER_FLAGS) -c -g -fPIC

TARGET=wlss

ifeq "$(ARCH)" "arm"
CCFLAGS += -D__ARM__
cross=/media/kevin/cdd2ed2c-f6f2-4c9c-aef4-4a16128687dd/kevin/AM335X/am335x/arm/armhf
LDFLAGS = -L$(cross)/sqlite/lib/ \
          -L$(cross)/glib/lib \
          -L$(cross)/libffi/lib \
          -L$(cross)/zlib/lib \
          -L$(cross)/pcre/lib
INCLUDES = -I. -I/media/kevin/cdd2ed2c-f6f2-4c9c-aef4-4a16128687dd/kevin/AM335X/am335x/arm/usr/include/ \
       -I$(cross)/sqlite/include/kompex/ \
       -I$(cross)/glib/include/glib-2.0/ \
       -I$(cross)/glib/glib-2.0/glib/ \
       -I$(cross)/glib/lib/glib-2.0/include/
else
INCLUDES = -I. -I/usr/include/ \
       -I/usr/local/include/glib-2.0 \
       -I/usr/local/lib/glib-2.0/include/ \
       -I/media/kevin/cdd2ed2c-f6f2-4c9c-aef4-4a16128687dd/kevin/AM335X/am335x/arm/x86/include/ \
       -I/media/kevin/cdd2ed2c-f6f2-4c9c-aef4-4a16128687dd/kevin/AM335X/am335x/arm/x86/include/kompex/ \
       -I/media/kevin/cdd2ed2c-f6f2-4c9c-aef4-4a16128687dd/kevin/AM335X/am335x/arm/x86/include/json-c/
endif
C++FILES = sqlite.cpp

CFILES = main.c pipe.c cJSON.c

OBJFILE = $(CFILES:.c=.o) $(C++FILES:.cpp=.o)

all:$(TARGET)

$(TARGET): $(OBJFILE)
	$(LINK) $^ $(LIBS) $(LDFLAGS) -Wall -fPIC -o $@

%.o:%.c
	$(CC) -o $@ $(CCFLAGS) $< $(INCLUDES)

%.o:%.cpp
	$(C++) -o $@ $(C++FLAGS) $< $(INCLUDES)

install:
	tsxs -i -o $(TARGET)

clean:
	rm -rf $(TARGET)
	rm -rf $(OBJFILE)

