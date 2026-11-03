/*
 * main.c - program entry, initialisation, title/menu flow, shutdown.
 * (0097:FF16 main, EC3D init, E83E memory check, FD15 title loop,
 *  FC85 start game, FC9C shutdown)
 */
#include "dn1.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void init_globals_from_exe(void);
void load_graphics(void);
void load_screens(void);
void load_keys(const char *name);
void save_keys(const char *name);
void load_speed(const char *name);
void save_speed(const char *name);
void save_demos(void);
void load_high_scores(void);
void save_high_scores(void);
void ui_fade_out(int d);
void ui_fade_in(int d);
void ui_fade_grey(int d);
void ui_clear_page0(void);
void ui_show_story(void);
void ui_show_intro_a(void);
void ui_show_intro_b(void);
void ui_show_badguy(void);
void ui_show_duke(void);
void ui_exit_text(void);
int  main_menu(int x);                   /* 0097:049F */
int  restore_menu(int x);                /* 0097:0126 */
void save_game(uint8_t slot);

int  forced_menu_choice = -1;            /* test harness: bypass the menu */

static void set_all_hints(uint8_t v)
{
    hint_5a11 = hint_5c6b = hint_5554 = hint_637e = hint_5ec8 = hint_555a = v;
    hint_555c = hint_5553 = hint_555b = hint_68f7 = hint_5555 = hint_5c6a = v;
}

/* 0097:FC9C  restore the machine and exit */
static void save_on_exit(void)
{
    save_keys(ds_str(0x0B19));
    save_demos();
    save_speed(ds_str(0x0B3B));
    save_high_scores();
}
void shutdown_game(void)
{
    save_on_exit();
    ui_exit_text();
    pf_exit(0);
}

/* (port) main menu > Episode select: save what quitting saves, then start
 * the other episode's EXE (the platform layer restarts at its main menu) */
int port_start_at_menu;
void port_switch_episode(int e)
{
    save_on_exit();
    cfg.episode = e;
    cfg_save(game_path(PORT_CFG_FILE));
    pf_restart();
}

/* 0097:FD15  title screens + main menu until a game or demo is chosen */
static void title_menu(void)
{
    for (;;) {
        int choice;
        ui_clear_page0();
        if (forced_menu_choice == -2) pf_exit(0);          /* test harness: one run only */
        choice = (forced_menu_choice >= 0) ? forced_menu_choice : main_menu(7);
        if (forced_menu_choice >= 0) forced_menu_choice = -2;   /* only once */
        set_all_hints(0);
        if (choice == 1) {                                     /* new game */
            demo_play = 0;
            demo_available = 1;
            demo_pos = 0;
            demo_record = 1;                                   /* always records USERDEMO */
            restoring_game = 0;
            ui_fade_out(0x14);
            ui_show_badguy();
            ui_fade_in(0x14);
            if (!joystick_mode) SPIN_WHILE(!(last_scancode & 0x80));
            ui_show_intro_a();
            ui_fade_out(0x14);
            ui_show_duke();
            ui_fade_in(0x14);
            if (!joystick_mode) SPIN_WHILE(!(last_scancode & 0x80));
            ui_show_intro_b();
            demo_missing = 0;
            ui_fade_grey(0x14);
            if (hints_on == 1) set_all_hints(1);
            play_sound(0x1A);
            return;
        }
        if (choice == 2) {                                     /* play USERDEMO */
            if (!demo_available) continue;
            demo_play = 1;
            demo_record = 0; demo_pos = 0;
            restoring_game = 0;
            ui_fade_grey(0x14);
            return;
        }
        if (choice == 3 && restore_menu(7)) {                  /* restore */
            restoring_game = 1;
            demo_play = 0;
            demo_record = 0;
            if (hints_on == 1) set_all_hints(1);
            return;
        }
        if (choice == 4) {                                     /* record MY_DEMO */
            demo_play = 0;
            demo_available = 1;
            demo_pos = 0;
            demo_record = 2;
            restoring_game = 0;
            ui_fade_grey(0x14);
            return;
        }
        if (choice == 5) {                                     /* play MY_DEMO */
            if (!demo_available) continue;
            demo_play = 2;
            demo_record = 0; demo_pos = 0;
            restoring_game = 0;
            ui_fade_grey(0x14);
            return;
        }
        if (choice == 0) {                                     /* quit */
            ui_fade_out(0x14);
            shutdown_game();
        }
    }
}

/* 0097:FC85 */
static void start_game(void)
{
    new_game();
    if (!quit_flag) {
        save_game('T');
        port_in_play = 1;                /* (port) */
        play_level_loop();
        port_in_play = 0;
    }
}

/* 0097:EC3D */
static void init_game(void)
{
    {   /* 108E:25CD: "joystick present" = both button lines read high */
        int b = pf_joystick_buttons();
        joy_present = ((b & 0x10) && (b & 0x20)) ? 1 : 0;
    }
    set_draw_page(0);
    set_display_page(0);
    load_keys(ds_str(0x0AB2));
    load_high_scores();
    load_graphics();
    sound_load_bank(ds_str(0x0ABB), ds_str(0x0AC5));
    load_screens();
    sound_enabled = 1;                   /* 108E:1E97 also reprograms the PIT */
    if (cfg_loaded && cfg.volume == 0) sound_enabled = 0;   /* (port) saved volume OFF */
    timer_ticks = 0;
    load_speed(ds_str(0x0B00));
}

/* 0097:FF16 */
void game_main(int argc, char **argv)
{
    /* the initialised data first: in DOS it is in place before main() runs,
     * so the switches below must not be overwritten by it */
    init_globals_from_exe();
    if (argc > 1 && strncmp(argv[1], "asp", 3) == 0) {
        cmd_asp = 1;
        if (argc > 2 && argv[2][0] == 'g') cmd_chain_kbd = 1;
    }
    init_game();
    for (;;) {
        title_menu();
        start_game();
        ui_fade_out(0x0F);
        quit_flag = 0;
    }
}
