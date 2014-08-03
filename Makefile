CFLAGS =
LDFLAGS =

# install variables
prefix = /usr/local
bindir = $(prefix)/bin
libdir = $(prefix)/lib
incdir = $(prefix)/include

LIBUNWIND =
LIBCORKSCREW = 1
ANDROID = 1
X86 = 
ifdef ANDROID
	ifdef X86
		#TODO
	else
		NDK_TOOLCHAIN = /opt/android-ndk-r9b/toolchains/arm-linux-androideabi-4.8/prebuilt/linux-x86_64
		TOOLCHAIN_PREFIX = $(NDK_TOOLCHAIN)/bin/arm-linux-androideabi-
		prefix = $(NDK_TOOLCHAIN)/user
		SYSROOT = /opt/android-ndk-r9b/platforms/android-18/arch-arm
		CFLAGS += --sysroot=$(SYSROOT)
		CFLAGS += -I$(NDK_TOOLCHAIN)/user/include
		CFLAGS += -L$(NDK_TOOLCHAIN)/user/lib
		CFLAGS += -DANDROID
		CFLAGS += -funwind-tables -Wl,--no-merge-exidx-entries
	endif
endif

AR = $(TOOLCHAIN_PREFIX)ar
CC = $(TOOLCHAIN_PREFIX)gcc
CXX = $(TOOLCHAIN_PREFIX)g++
CFLAGS += -g -O0 -fPIC -fvisibility=hidden

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

SO_LIBS =
APP_LIBS = libopro.a
TESTAPP_LIBS = libload.so
ifdef LIBUNWIND
	SO_LIBS += -Wl,-Bstatic -lunwind-ptrace -lunwind-generic -lunwind
	APP_LIBS += -Wl,-Bstatic -lunwind-ptrace -lunwind-generic -lunwind
	CFLAGS += -DLIBUNWIND
else
ifdef LIBCORKSCREW
	SO_LIBS += -Wl,-Bstatic -lcorkscrew
	APP_LIBS += -Wl,-Bstatic -lcorkscrew
	CFLAGS += -DLIBCORKSCREW
endif
endif
SO_LIBS += -Wl,-Bdynamic
APP_LIBS += -Wl,-Bdynamic
ifdef ANDROID
SO_LIBS += -llog
APP_LIBS += -llog
else
SO_LIBS += -lpthread -llzma
APP_LIBS += -lpthread -llzma
endif


%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $^

libopro.a: opro.o 
	$(AR) rcs $@ $^

libopro.so: opro.o 
	$(CC) $(CFLAGS) -o $@ $^ $(SO_LIBS) -shared -Wl,--exclude-libs=ALL

libload.so: load.o 
	$(CC) $(CFLAGS) -o $@ $^ $(SO_LIBS) -shared -Wl,--exclude-libs=ALL

opro: libopro.a opro_main.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(APP_LIBS) -L. -Wl,-rpath=.

testapp: libopro.a libload.so testapp.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(TESTAPP_LIBS) $(APP_LIBS) -L. -Wl,-rpath=.

install: libopro.a libopro.so
	mkdir -p $(libdir)
	mkdir -p $(incdir)
	cp libopro.a $(libdir)
	cp libopro.so $(libdir)
	cp opro.h $(incdir)

clean:
	rm -f $(OBJ) testapp libopro.so libload.so

.PHONY: clean
