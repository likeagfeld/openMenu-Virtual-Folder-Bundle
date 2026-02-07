/*
 * File: discross_main.c
 * Project: openmenu (Discross standalone)
 * File Created: 2025-02-XX
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos/init.h>
#include <arch/exec.h>
#include <dc/flashrom.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/keyboard.h>
#include <dc/pvr.h>
#include <dc/video.h>

#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include "dcnow/dcnow_net_init.h"
#include "texture/txr_manager.h"
#include "texture/simple_texture_allocator.h"
#include "ui/common.h"
#include "ui/dc/input.h"
#include "ui/draw_prototypes.h"
#include "ui/font_prototypes.h"
#include "ui/theme_manager.h"
#include "ui/ui_menu_credits.h"
#include "bloader.h"

/* Initialize KOS with network support - CRITICAL for socket() to work! */
KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NET);

extern image img_empty_boxart;
extern image img_dir_boxart;

static image txr_bg_left;
static image txr_bg_right;
static theme_color *current_theme_colors = NULL;
static enum draw_state draw_current = DRAW_DISCORD_CHAT;
static int navigate_timeout = 0;

static void
processInput(void) {
    inputs _input;
    unsigned int buttons;

    maple_device_t* cont;
    maple_device_t* kbd;
    cont_state_t* state;
    kbd_state_t* kbd_state;

    /*  Reset Everything */
    memset(&_input, 0, sizeof(inputs));

    /* Set neutral analog values */
    _input.axes_1 = 128; /* Neutral analog X */
    _input.axes_2 = 128; /* Neutral analog Y */

    cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    kbd = maple_enum_type(0, MAPLE_FUNC_KEYBOARD);

    if (!cont && !kbd) {
        /* No controller or keyboard - send neutral input */
        INPT_ReceiveFromHost(_input);
        return;
    }

    if (cont) {
        /* Controller found - also works with the controller portion of a light gun */
        state = (cont_state_t*)maple_dev_status(cont);
        buttons = state->buttons;

        /* DPAD */
        _input.dpad = (state->buttons >> 4) & ~240; // mrneo240 ;)

        /* BUTTONS */
        _input.btn_a = (uint8_t)!!(buttons & CONT_A);
        _input.btn_b = (uint8_t)!!(buttons & CONT_B);
        _input.btn_x = (uint8_t)!!(buttons & CONT_X);
        _input.btn_y = (uint8_t)!!(buttons & CONT_Y);
        _input.btn_start = (uint8_t)!!(buttons & CONT_START);

        /* ANALOG */
        _input.axes_1 = ((uint8_t)(state->joyx) + 128);
        _input.axes_2 = ((uint8_t)(state->joyy) + 128);

        /* TRIGGERS */
        _input.trg_left = (uint8_t)state->ltrig & 255;
        _input.trg_right = (uint8_t)state->rtrig & 255;
    }

    if (kbd) {
        /* Keyboard found - copy list of pressed key scancodes from cond.keys */
        kbd_state = (kbd_state_t*)maple_dev_status(kbd);
        _input.kbd_modifiers = kbd_state->shift_keys;
        for (int i = 0; i < INPT_MAX_KEYBOARD_KEYS; i++) {
            _input.kbd_buttons[i] = kbd_state->cond.keys[i];
        }
    }

    INPT_ReceiveFromHost(_input);
}

static int
translate_input(void) {
    processInput();

    /* Check for ABXY+Start reset combo - disconnect modem and reset console */
    if (INPT_ButtonEx(BTN_A, BTN_HELD) &&
        INPT_ButtonEx(BTN_B, BTN_HELD) &&
        INPT_ButtonEx(BTN_X, BTN_HELD) &&
        INPT_ButtonEx(BTN_Y, BTN_HELD) &&
        INPT_ButtonEx(BTN_START, BTN_HELD)) {
        printf("ABXY+Start detected - disconnecting and resetting...\n");

        /* Disconnect modem/PPP before reset */
        dcnow_net_disconnect();

        /* Reset console - same as exit to BIOS */
        arch_exec_at(bloader_data, bloader_size, 0xacf00000);
    }

    /* D-Pad directions */
    if (INPT_DPADDirection(DPAD_LEFT)) {
        return LEFT;
    }
    if (INPT_DPADDirection(DPAD_RIGHT)) {
        return RIGHT;
    }
    if (INPT_DPADDirection(DPAD_UP)) {
        return UP;
    }
    if (INPT_DPADDirection(DPAD_DOWN)) {
        return DOWN;
    }

    /* Analog stick */
    if (INPT_AnalogI(AXES_X) < 128 - 24) {
        return LEFT;
    }
    if (INPT_AnalogI(AXES_X) > 128 + 24) {
        return RIGHT;
    }

    if (INPT_AnalogI(AXES_Y) < 128 - 24) {
        return UP;
    }
    if (INPT_AnalogI(AXES_Y) > 128 + 24) {
        return DOWN;
    }

    /* Buttons */
    if (INPT_ButtonEx(BTN_A, BTN_PRESS)) {
        return A;
    }
    if (INPT_ButtonEx(BTN_B, BTN_PRESS)) {
        return B;
    }
    if (INPT_Button(BTN_X)) {
        return X;
    }
    if (INPT_ButtonEx(BTN_Y, BTN_PRESS)) {
        return Y;
    }
    if (INPT_ButtonEx(BTN_START, BTN_PRESS)) {
        return START;
    }

    /* Triggers */
    if (INPT_TriggerPressed(TRIGGER_L)) {
        return TRIG_L;
    }
    if (INPT_TriggerPressed(TRIGGER_R)) {
        return TRIG_R;
    }

    /* Keyboard support - skip if no keys pressed */
    if (!INPT_KeyboardNone()) {
        /* Arrow keys → D-Pad */
        if (INPT_KeyboardButton(KBD_KEY_LEFT)) {
            return LEFT;
        }
        if (INPT_KeyboardButton(KBD_KEY_RIGHT)) {
            return RIGHT;
        }
        if (INPT_KeyboardButton(KBD_KEY_UP)) {
            return UP;
        }
        if (INPT_KeyboardButton(KBD_KEY_DOWN)) {
            return DOWN;
        }

        /* Z or Space → A button (edge-detected) */
        if (INPT_KeyboardButtonPress(KBD_KEY_Z) || INPT_KeyboardButtonPress(KBD_KEY_SPACE)) {
            return A;
        }
        /* X or Escape → B button (edge-detected) */
        if (INPT_KeyboardButtonPress(KBD_KEY_X) || INPT_KeyboardButtonPress(KBD_KEY_ESCAPE)) {
            return B;
        }
        /* A → X button (hold-detected for grid artwork zoom) */
        if (INPT_KeyboardButton(KBD_KEY_A)) {
            return X;
        }
        /* S → Y button (edge-detected) */
        if (INPT_KeyboardButtonPress(KBD_KEY_S)) {
            return Y;
        }
        /* Enter → Start (edge-detected) */
        if (INPT_KeyboardButtonPress(KBD_KEY_ENTER)) {
            return START;
        }

        /* Q or Page Up → Left Trigger */
        if (INPT_KeyboardButton(KBD_KEY_Q) || INPT_KeyboardButton(KBD_KEY_PGUP)) {
            return TRIG_L;
        }
        /* W or Page Down → Right Trigger */
        if (INPT_KeyboardButton(KBD_KEY_W) || INPT_KeyboardButton(KBD_KEY_PGDOWN)) {
            return TRIG_R;
        }
    }

    return NONE;
}

static void
init_gfx_pvr(void) {
    int dc_region, ct;

    dc_region = flashrom_get_region();
    ct = vid_check_cable();

    if (dc_region == FLASHROM_REGION_EUROPE && ct != CT_VGA) {
        if (1 == 1) {
            vid_set_mode(DM_640x480_NTSC_IL, PM_RGB565);
        } else {
            vid_set_mode(DM_640x480_PAL_IL, PM_RGB565);
        }
    }

    pvr_init_params_t params = {
        {PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_0},
        256 * 1024,
        0,
        0,
        0,
        0};

    pvr_init(&params);
    draw_set_list(PVR_LIST_OP_POLY);
}

static void
load_theme_assets(void) {
    theme_region* region_themes = NULL;
    theme_custom* custom_themes = NULL;
    int num_default_themes = 0;
    int num_custom_themes = 0;
    region region_current = sf_region[0];

    theme_manager_load();
    region_themes = theme_get_default(sf_aspect[0], &num_default_themes);
    custom_themes = theme_get_custom(&num_custom_themes);

    if (sf_custom_theme[0]) {
        int custom_theme_num = sf_custom_theme_num[0];
        region_current = REGION_END + 1 + custom_theme_num;
    }

    unsigned int temp = texman_create();
    draw_load_texture_buffer("EMPTY.PVR", &img_empty_boxart, texman_get_tex_data(temp));
    texman_reserve_memory(img_empty_boxart.width, img_empty_boxart.height, 2 /* 16Bit */);

    temp = texman_create();
    draw_load_texture_buffer("DIR.PVR", &img_dir_boxart, texman_get_tex_data(temp));
    texman_reserve_memory(img_dir_boxart.width, img_dir_boxart.height, 2 /* 16Bit */);

    if ((int)region_current >= num_default_themes) {
        region_current -= num_default_themes;
        current_theme_colors = &custom_themes[region_current].colors;

        temp = texman_create();
        draw_load_texture_buffer(custom_themes[region_current].bg_left, &txr_bg_left, texman_get_tex_data(temp));
        texman_reserve_memory(txr_bg_left.width, txr_bg_left.height, 2 /* 16Bit */);

        temp = texman_create();
        draw_load_texture_buffer(custom_themes[region_current].bg_right, &txr_bg_right, texman_get_tex_data(temp));
        texman_reserve_memory(txr_bg_right.width, txr_bg_right.height, 2 /* 16Bit */);
    } else {
        current_theme_colors = &region_themes[region_current].colors;

        temp = texman_create();
        draw_load_texture_buffer(region_themes[region_current].bg_left, &txr_bg_left, texman_get_tex_data(temp));
        texman_reserve_memory(txr_bg_left.width, txr_bg_left.height, 2 /* 16Bit */);

        temp = texman_create();
        draw_load_texture_buffer(region_themes[region_current].bg_right, &txr_bg_right, texman_get_tex_data(temp));
        texman_reserve_memory(txr_bg_right.width, txr_bg_right.height, 2 /* 16Bit */);
    }

    font_bmf_init("FONT/BASILEA.FNT", "FONT/BASILEA_W.PVR", sf_aspect[0]);
}

static void
draw_background(void) {
    const dimen_RECT left = {.x = 0, .y = 0, .w = 512, .h = 480};
    const dimen_RECT right = {.x = 0, .y = 0, .w = 128, .h = 480};

    draw_draw_sub_image(0, 0, 512, 480, COLOR_WHITE, &txr_bg_left, &left);
    draw_draw_sub_image(512, 0, 128, 480, COLOR_WHITE, &txr_bg_right, &right);
}

static void
draw_frame(void) {
    pvr_wait_ready();
    pvr_scene_begin();

    draw_set_list(PVR_LIST_OP_POLY);
    pvr_list_begin(PVR_LIST_OP_POLY);
    draw_background();
    draw_discord_chat_op();
    pvr_list_finish();

    draw_set_list(PVR_LIST_TR_POLY);
    pvr_list_begin(PVR_LIST_TR_POLY);
    draw_discord_chat_tr();
    pvr_list_finish();

    pvr_scene_finish();
}

int
main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    fflush(stdout);
    setbuf(stdout, NULL);
    init_gfx_pvr();

    savefile_init();

    txr_create_small_pool();
    txr_create_large_pool();
    draw_init();
    load_theme_assets();

    discord_chat_setup(&draw_current, current_theme_colors, &navigate_timeout, current_theme_colors->menu_highlight_color);

    bool running = true;
    while (running) {
        z_reset();
        handle_input_discord_chat(translate_input());
        vid_waitvbl();

        if (draw_current != DRAW_DISCORD_CHAT) {
            running = false;
            continue;
        }

        draw_frame();
    }

    dcnow_net_disconnect();
    savefile_close();
    return 0;
}
