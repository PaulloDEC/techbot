/*
 * input.c - keyboard interrupt handler and joystick support.
 *
 * The original hooks INT 9 and keeps one "held" flag per bound action plus a
 * few hard-wired keys (cheats, speed).  last_scancode holds the most recent
 * raw scancode (make or break, bit 7 = release) and is polled everywhere.
 */
#include "dn1.h"

/* 0097:15F4  INT 9 handler, called by the platform layer per scancode */
void keyboard_isr(uint8_t sc)
{
    last_scancode = sc;
    if (sc && (sc == cfg.key_qsave || sc == cfg.key_qload)) port_qs_request = sc;   /* (port) */
    if (boss_dead == 0 && exit_stage == 0 && !joystick_mode && demo_play == 0 &&
        tele_a_busy == 0 && tele_b_busy == 0) {
        uint8_t *flag[6];
        int i;
        flag[0] = &in_up; flag[1] = &in_down; flag[2] = &in_left; flag[3] = &in_right;
        flag[4] = &in_fire; flag[5] = &in_jump;
        for (i = 0; i < 6; i++) {
            if (sc == key_bind[i]) *flag[i] = 1;
            if (sc == (key_bind[i] | 0x80)) *flag[i] = 0;
        }
        /* (the original tests fire after jump; order is irrelevant) */
    }
    if (sc == 0x33) key_speed_down = 1;   /* ','  slower */
    if (sc == 0xB3) key_speed_down = 0;
    if (sc == 0x34) key_speed_up = 1;     /* '.'  faster */
    if (sc == 0xB4) key_speed_up = 0;
    if (sc == 0x0E) key_bksp = 1;
    if (sc == 0x8E) key_bksp = 0;
    if (sc == 0x51) key_pgdn = 1;
    if (sc == 0xD1) key_pgdn = 0;
    if (sc == 0x22) key_g = 1;
    if (sc == 0xA2) key_g = 0;
    if (sc == 0x18) key_o = 1;
    if (sc == 0x98) key_o = 0;
    if (sc == 0x20) key_d = 1;
    if (sc == 0xA0) key_d = 0;
}

/* ---------------------------------------------------------------------
 * Joystick (0097:0C52, 10A5, 1053, 1076; 108E:01F5, 1340, 25CD).
 * The original times the game-port RC circuits.  This port maps a modern
 * game controller through the platform layer: pf_joystick_buttons()
 * returns the port-201h style button bits; analogue directions are folded
 * into in_left/in_right/in_up/in_down here.
 * ------------------------------------------------------------------- */
extern int pf_joy_x, pf_joy_y;      /* -1 / 0 / +1 */
uint8_t joy_fire_swap;              /* (port option) swap joystick buttons */
void joystick_read(int calib)
{
    (void)calib;
    in_left  = pf_joy_x < 0;
    in_right = pf_joy_x > 0;
    in_up    = pf_joy_y < 0;
    in_down  = pf_joy_y > 0;
}

void joystick_buttons(void)
{
    int b = pf_joystick_buttons();
    /* button 1 / button 2; joy_fire_swap selects which one fires */
    uint8_t b1 = !(b & 0x10), b2 = !(b & 0x20);
    if (joy_fire_swap) { in_fire = b2; in_jump = b1; }
    else               { in_fire = b1; in_jump = b2; }
}
