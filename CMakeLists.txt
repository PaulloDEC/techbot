cmake_minimum_required(VERSION 3.16)
project(techbot C CXX)   # Techbot - An Enhanced PC Port of Duke Nukem (+ Shrapnel, its level editor)

set(CMAKE_C_STANDARD 99)
if(MSVC)
    set(CMAKE_C_STANDARD 11)          # MSVC has no C99 switch; /std:c11 is a superset here
endif()
set(CMAKE_C_STANDARD_REQUIRED ON)

set(DN1_CORE
    src/exe.c src/ega.c src/globals.c src/core.c src/player.c src/render.c
    src/actors1.c src/actors2.c src/actors3.c src/actors4.c src/actors5.c
    src/actors6.c src/actors7.c src/actors8.c src/game.c src/loop.c
    src/sound.c src/input.c src/files.c src/ui.c src/ui_dialogs.c
    src/frontend.c src/main.c src/port_cfg.c src/port_menu.c
    src/port_options.c src/port_qsave.c src/port_labels.c
    src/port_render.c src/port_sound.c
    src/episode_map.c src/port_art.c src/port_log.c src/port_hud.c
    src/port_state.c src/port_editor.c src/port_reach.c src/port_pack.c src/port_dir.c
    shrapnel/third_party/miniz/miniz.c)

# The game itself, built once and shared: Techbot, the headless test build
# and the Shrapnel level editor each add a platform layer (the pf_* functions
# and game_path, declared in src/dn1.h and src/port.h) and link this.
add_library(techbot_core STATIC ${DN1_CORE})
target_include_directories(techbot_core PUBLIC src shrapnel/third_party/miniz)
if(UNIX)
    target_link_libraries(techbot_core PUBLIC m)
endif()
if(MSVC)
    target_compile_definitions(techbot_core PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_options(techbot_core PRIVATE $<$<CONFIG:Release>:/Zi>)
endif()

# Playable build (SDL2): techbot(.exe).  (Not "dn1": on Windows that would be
# the same file name as the original DN1.EXE, which the port reads at
# startup.)  See BUILDING.md for how to point CMake at SDL2.
find_package(SDL2 CONFIG)
if(SDL2_FOUND)
    # WIN32: a windowed application (no console window).  Configure with
    # -DTECHBOT_CONSOLE=ON to get the console back, e.g. for printf debugging.
    option(TECHBOT_CONSOLE "Windows: build techbot.exe as a console program" OFF)
    set(TECHBOT_RES "")
    if(WIN32)
        set(TECHBOT_RES resources/techbot.rc)   # icon + version details
    endif()
    if(TECHBOT_CONSOLE)
        add_executable(techbot src/pf_sdl.c ${TECHBOT_RES})
        target_compile_definitions(techbot PRIVATE TECHBOT_CONSOLE)
    else()
        add_executable(techbot WIN32 src/pf_sdl.c ${TECHBOT_RES})
        # say it to the linker as well (WIN32 alone has been seen not to stick)
        if(MSVC)
            target_link_options(techbot PRIVATE /SUBSYSTEM:WINDOWS)
        elseif(WIN32)
            target_link_options(techbot PRIVATE -Wl,--subsystem,windows)
        endif()
    endif()
    if(TARGET SDL2::SDL2main)
        target_link_libraries(techbot PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(techbot PRIVATE techbot_core SDL2::SDL2)
    if(MSVC)
        target_compile_definitions(techbot PRIVATE _CRT_SECURE_NO_WARNINGS)
        # debug information for Release too (techbot.pdb next to techbot.exe):
        # with it, a crash dump (TECHBOT.DMP) opens in Visual Studio at the
        # exact line.  It does not slow the program down.
        target_compile_options(techbot PRIVATE $<$<CONFIG:Release>:/Zi>)
        target_link_options(techbot PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
    endif()
    if(WIN32)
        # put SDL2.dll next to techbot.exe so it runs straight from the build folder
        add_custom_command(TARGET techbot POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:techbot>)
    endif()

    # Shrapnel, the level editor (C++ / Dear ImGui), linking the same core
    set(IMGUI_DIR shrapnel/third_party/imgui)
    add_executable(shrapnel WIN32
        shrapnel/src/main.cpp shrapnel/src/episode.cpp shrapnel/src/tile_names.cpp shrapnel/src/selftest.cpp shrapnel/src/catalog.cpp shrapnel/src/picture.cpp shrapnel/src/theme.cpp shrapnel/src/keys.cpp shrapnel/src/prefabs.cpp shrapnel/src/prefab_builtin.cpp
        shrapnel/src/tools.cpp shrapnel/src/doc.cpp shrapnel/src/filedlg.cpp shrapnel/src/project.cpp shrapnel/src/checks.cpp
        shrapnel/src/pf_shrapnel.c shrapnel/src/pf_sdl_embed.c
        ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
    if(WIN32)
        target_sources(shrapnel PRIVATE shrapnel/resources/shrapnel.rc)   # icon + version details
    endif()
    target_include_directories(shrapnel PRIVATE ${IMGUI_DIR} shrapnel/third_party/miniz)
    target_compile_features(shrapnel PRIVATE cxx_std_17)   # std::filesystem (project files)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(shrapnel PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(shrapnel PRIVATE techbot_core SDL2::SDL2)
    if(WIN32)
        target_link_libraries(shrapnel PRIVATE comdlg32 shell32 ole32)   # Open / Save As / folder dialogs
    endif()
    if(MSVC)
        target_link_options(shrapnel PRIVATE /SUBSYSTEM:WINDOWS)
    elseif(WIN32)
        target_link_options(shrapnel PRIVATE -Wl,--subsystem,windows)
    endif()
    if(WIN32)
        add_custom_command(TARGET shrapnel POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:shrapnel>)
    endif()
else()
    message(WARNING "SDL2 not found: only the headless test build is configured. "
                    "Pass -DCMAKE_PREFIX_PATH=<SDL2 folder> (see BUILDING.md).")
endif()

# Deterministic headless build used by the verification tools (POSIX only:
# uses dirent for case-insensitive file lookup).
if(NOT WIN32)
    add_executable(dn1_headless src/pf_headless.c)
    target_link_libraries(dn1_headless PRIVATE techbot_core)
endif()
