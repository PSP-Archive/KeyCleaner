TARGET = KeyCleaner
OBJS = main.o IdStorage.o

INCDIR =
CFLAGS = -O2 -G0 -Wall
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1

LIBDIR = ./
LIBS = -lpng -lz -lm -lpsppower
LDFLAGS =

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Key Cleaner
PSP_EBOOT_SND0="snd0.at3"
PSP_EBOOT_ICON="icon0.png"
PSP_EBOOT_PIC1="pic1.png"

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
