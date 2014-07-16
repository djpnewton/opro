AR = $(PREFIX)ar
CC = $(PREFIX)gcc
CXX = $(PREFIX)g++
CFLAGS = -g -O0 -fPIC -fvisibility=hidden #--sysroot $(SYSROOT)

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

SO_LIBS = -lpthread -lunwind
ifdef ANDROID
SO_LIBS += -llog
endif
APP_LIBS = -lpthread -lunwind


%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $^

libopro.so: opro.o 
	$(CC) $(CFLAGS) -o $@ $^ $(SO_LIBS) -shared -Wl,--exclude-libs=ALL

libload.so: load.o 
	$(CC) $(CFLAGS) -o $@ $^ $(SO_LIBS) -shared -Wl,--exclude-libs=ALL

testapp: libopro.so libload.so testapp.o 
	$(CC) $(LDFLAGS) -o $@ $^ $(APP_LIBS) -lopro -lload -L. -Wl,-rpath=.

clean:
	rm -f $(OBJ) testapp libopro.so libload.so

.PHONY: clean
