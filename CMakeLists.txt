cmake_minimum_required(VERSION 3.16)
project(techbot C)   # Techbot - An Enhanced PC Port of Duke Nukem

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
    src/episode_map.c src/port_art.c src/port_log.c src/port_hud.c)

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
        add_executable(techbot ${DN1_CORE} src/pf_sdl.c ${TECHBOT_RES})
        target_compile_definitions(techbot PRIVATE TECHBOT_CONSOLE)
    else()
        add_executable(techbot WIN32 ${DN1_CORE} src/pf_sdl.c ${TECHBOT_RES})
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
    target_link_libraries(techbot PRIVATE SDL2::SDL2)
    if(UNIX)
        target_link_libraries(techbot PRIVATE m)
    endif()
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
else()
    message(WARNING "SDL2 not found: only the headless test build is configured. "
                    "Pass -DCMAKE_PREFIX_PATH=<SDL2 folder> (see BUILDING.md).")
endif()

# Deterministic headless build used by the verification tools (POSIX only:
# uses dirent for case-insensitive file lookup).
if(NOT WIN32)
    add_executable(dn1_headless ${DN1_CORE} src/pf_headless.c)
    target_link_libraries(dn1_headless PRIVATE m)
endif()
