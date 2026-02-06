/*
 * File: ui_bg.c
 * Project: ui
 * -----
 * Shared background layer drawing for all UI modes.
 */

#include "ui/ui_bg.h"

static image* bg_left = NULL;
static image* bg_right = NULL;

void
ui_bg_set(image* left, image* right) {
    bg_left = left;
    bg_right = right;
}

void
ui_bg_draw(void) {
    {
        const dimen_RECT left = {.x = 0, .y = 0, .w = 512, .h = 480};
        draw_draw_sub_image(0, 0, 512, 480, COLOR_WHITE, bg_left, &left);
    }
    {
        const dimen_RECT right = {.x = 0, .y = 0, .w = 128, .h = 480};
        draw_draw_sub_image(512, 0, 128, 480, COLOR_WHITE, bg_right, &right);
    }
}
