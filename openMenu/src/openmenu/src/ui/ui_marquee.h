/*
 * File: ui_marquee.h
 * Project: ui
 * -----
 * Shared marquee text scrolling state machine.
 * Used by ui_scroll.c and ui_folders.c for long game names.
 */

#pragma once

#define MARQUEE_INITIAL_PAUSE_FRAMES 60
#define MARQUEE_END_PAUSE_FRAMES 90

typedef enum {
    MARQUEE_STATE_INITIAL_PAUSE,
    MARQUEE_STATE_SCROLL_LEFT,
    MARQUEE_STATE_END_PAUSE,
    MARQUEE_STATE_SCROLL_RIGHT
} marquee_state_t;

typedef struct marquee_ctx {
    marquee_state_t state;
    int offset;
    int timer;
    int max_offset;
    int last_selected;
    int display_width;
} marquee_ctx;

void marquee_init(marquee_ctx* ctx, int display_width);
void marquee_reset(marquee_ctx* ctx);
void marquee_update(marquee_ctx* ctx, int name_length);
void marquee_check_selection(marquee_ctx* ctx, int current_selected);
