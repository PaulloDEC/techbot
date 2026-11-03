# Techbot - simple GNU make build (Linux / WSL / MSYS2).  CMake is the main build on Windows.
# Outputs go to build/, where the verification tools expect them.
CC      ?= gcc
CFLAGS  ?= -std=c99 -O2 -Wall -Wno-unused-variable -Wno-unused-function -Wno-unused-label
CORE     = $(filter-out src/pf_sdl.c src/pf_headless.c,$(wildcard src/*.c))
DEPS     = $(CORE) src/*.h src/globals.def

all: build/techbot build/dn1_headless

techbot: build/techbot
dn1_headless: build/dn1_headless

build/techbot: $(DEPS) src/pf_sdl.c
	mkdir -p build
	$(CC) $(CFLAGS) $(shell sdl2-config --cflags) -o $@ $(CORE) src/pf_sdl.c $(shell sdl2-config --libs) -lm

build/dn1_headless: $(DEPS) src/pf_headless.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(CORE) src/pf_headless.c -lm

clean:
	rm -f build/techbot build/techbot.exe build/dn1_headless

.PHONY: all clean techbot dn1_headless
