CFLAGS =
LDFLAGS =

ANDROID = 1
X86 = 
ifdef ANDROID
	ifdef X86
		#TODO
	else
		NDK_TOOLCHAIN = /opt/android-ndk-r9b/toolchains/arm-linux-androideabi-4.8/prebuilt/linux-x86_64
		PREFIX = $(NDK_TOOLCHAIN)/bin/arm-linux-androideabi-
		SYSROOT = /opt/android-ndk-r9b/platforms/android-9/arch-arm
		CFLAGS += --sysroot=$(SYSROOT)
		CFLAGS += -I$(NDK_TOOLCHAIN)/user/include
		CFLAGS += -L$(NDK_TOOLCHAIN)/user/lib
		CFLAGS += -DANDROID
	endif
endif

AR = $(PREFIX)ar
CC = $(PREFIX)gcc
CXX = $(PREFIX)g++
CFLAGS += -g -O0 -fPIC -fvisibility=hidden

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

SO_LIBS = -Wl,-Bstatic -lunwind -Wl,-Bdynamic
APP_LIBS =
ifdef ANDROID
#SO_LIBS += -llog
else
SO_LIBS += -lpthread
APP_LIBS += -lpthread
endif


%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $^

libopro.so: opro.o 
	$(CC) $(CFLAGS) -o $@ $^ $(SO_LIBS) -shared -Wl,--exclude-libs=ALL

libload.so: load.o 
	$(CC) $(CFLAGS) -o $@ $^ $(SO_LIBS) -shared -Wl,--exclude-libs=ALL

testapp: libopro.so libload.so testapp.o 
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(APP_LIBS) -lopro -lload -L. -Wl,-rpath=.

clean:
	rm -f $(OBJ) testapp libopro.so libload.so

.PHONY: clean
