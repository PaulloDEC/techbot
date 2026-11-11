# Techbot - simple GNU make build (Linux / WSL / MSYS2).  CMake is the main build on Windows.
# Outputs go to build/, where the verification tools expect them.
#
# The game is built once into build/libtechbot_core.a; each program adds its
# platform layer (the pf_* functions and game_path): src/pf_sdl.c for Techbot,
# src/pf_headless.c for the headless test build.
CC      ?= gcc
CFLAGS  ?= -std=c99 -O2 -Wall -Wno-unused-variable -Wno-unused-function -Wno-unused-label
CORE     = $(filter-out src/pf_sdl.c src/pf_headless.c,$(wildcard src/*.c))
OBJS     = $(patsubst src/%.c,build/obj/%.o,$(CORE)) build/obj/miniz.o
HDRS     = $(wildcard src/*.h) src/globals.def
LIB      = build/libtechbot_core.a

# Shrapnel, the level editor (C++ / Dear ImGui on SDL2): shrapnel/
CXX     ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall
IMGUI    = shrapnel/third_party/imgui
SH_CXX   = shrapnel/src/main.cpp shrapnel/src/episode.cpp shrapnel/src/tile_names.cpp shrapnel/src/selftest.cpp shrapnel/src/catalog.cpp shrapnel/src/picture.cpp shrapnel/src/theme.cpp shrapnel/src/tools.cpp shrapnel/src/doc.cpp shrapnel/src/filedlg.cpp shrapnel/src/project.cpp shrapnel/src/checks.cpp $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp \
           $(IMGUI)/imgui_widgets.cpp $(IMGUI)/backends/imgui_impl_sdl2.cpp $(IMGUI)/backends/imgui_impl_sdlrenderer2.cpp
SH_OBJS  = $(patsubst %.cpp,build/obj/sh/%.o,$(notdir $(SH_CXX))) build/obj/sh/pf_shrapnel.o build/obj/sh/pf_sdl_embed.o

all: build/techbot build/dn1_headless build/shrapnel

techbot: build/techbot
dn1_headless: build/dn1_headless
core: $(LIB)

build/obj/%.o: src/%.c $(HDRS)
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -Ishrapnel/third_party/miniz -c $< -o $@
# zip reading (packs) - shared with Shrapnel
build/obj/miniz.o: shrapnel/third_party/miniz/miniz.c
	@mkdir -p build/obj
	$(CC) -O2 -c $< -o $@

$(LIB): $(OBJS)
	rm -f $@
	ar rcs $@ $(OBJS)

build/techbot: $(LIB) src/pf_sdl.c $(HDRS)
	$(CC) $(CFLAGS) $(shell sdl2-config --cflags) -o $@ src/pf_sdl.c $(LIB) $(shell sdl2-config --libs) -lm

build/dn1_headless: $(LIB) src/pf_headless.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ src/pf_headless.c $(LIB) -lm

shrapnel: build/shrapnel

build/obj/sh/%.o: shrapnel/src/%.cpp shrapnel/src/shrapnel.h shrapnel/src/icon_data.h $(HDRS)
	@mkdir -p build/obj/sh
	$(CXX) $(CXXFLAGS) -Isrc -I$(IMGUI) -Ishrapnel/third_party/miniz $(shell sdl2-config --cflags) -c $< -o $@
build/obj/sh/%.o: $(IMGUI)/%.cpp
	@mkdir -p build/obj/sh
	$(CXX) $(CXXFLAGS) -I$(IMGUI) $(shell sdl2-config --cflags) -c $< -o $@
build/obj/sh/%.o: $(IMGUI)/backends/%.cpp
	@mkdir -p build/obj/sh
	$(CXX) $(CXXFLAGS) -I$(IMGUI) $(shell sdl2-config --cflags) -c $< -o $@
build/obj/sh/pf_shrapnel.o: shrapnel/src/pf_shrapnel.c $(HDRS)
	@mkdir -p build/obj/sh
	$(CC) $(CFLAGS) -Isrc -c $< -o $@
build/obj/sh/pf_sdl_embed.o: shrapnel/src/pf_sdl_embed.c src/pf_sdl.c $(HDRS)
	@mkdir -p build/obj/sh
	$(CC) $(CFLAGS) -Isrc $(shell sdl2-config --cflags) -c $< -o $@

build/shrapnel: $(SH_OBJS) $(LIB)
	$(CXX) -o $@ $(SH_OBJS) $(LIB) $(shell sdl2-config --libs) -lm

clean:
	rm -rf build/obj $(LIB) build/techbot build/techbot.exe build/dn1_headless build/shrapnel

.PHONY: all clean techbot dn1_headless core shrapnel
