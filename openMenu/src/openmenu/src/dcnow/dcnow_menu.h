/*
 * File: dcnow_menu.h
 * Project: dcnow
 * -----
 * DC Now (dreamcast.online/now) Player Status Popup Menu
 */

#pragma once

#include "ui/common.h"

struct theme_color;

void dcnow_setup(enum draw_state* state, struct theme_color* _colors, int* timeout_ptr, uint32_t title_color);
void handle_input_dcnow(enum control input);
void draw_dcnow_op(void);
void draw_dcnow_tr(void);
void dcnow_background_tick(void);
