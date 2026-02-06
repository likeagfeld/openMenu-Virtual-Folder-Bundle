/*
 * File: ui_marquee.c
 * Project: ui
 * -----
 * Shared marquee text scrolling state machine.
 */

#include <stdint.h>
#include "ui/ui_marquee.h"

static inline int
get_marquee_speed_frames(void) {
    extern uint8_t* sf_marquee_speed;
    switch (sf_marquee_speed[0]) {
        case 0: return 8;  /* Slow */
        case 1: return 6;  /* Medium */
        case 2: return 4;  /* Fast */
        default: return 6; /* Default to Medium */
    }
}

void
marquee_init(marquee_ctx* ctx, int display_width) {
    ctx->state = MARQUEE_STATE_INITIAL_PAUSE;
    ctx->offset = 0;
    ctx->timer = MARQUEE_INITIAL_PAUSE_FRAMES;
    ctx->max_offset = 0;
    ctx->last_selected = -1;
    ctx->display_width = display_width;
}

void
marquee_reset(marquee_ctx* ctx) {
    ctx->state = MARQUEE_STATE_INITIAL_PAUSE;
    ctx->offset = 0;
    ctx->timer = MARQUEE_INITIAL_PAUSE_FRAMES;
    ctx->max_offset = 0;
}

void
marquee_check_selection(marquee_ctx* ctx, int current_selected) {
    if (current_selected != ctx->last_selected) {
        marquee_reset(ctx);
        ctx->last_selected = current_selected;
    }
}

void
marquee_update(marquee_ctx* ctx, int name_length) {
    int max_offset = name_length - ctx->display_width;
    if (max_offset < 0) {
        max_offset = 0;
    }

    ctx->max_offset = max_offset;

    if (ctx->timer > 0) {
        ctx->timer--;
        return;
    }

    switch (ctx->state) {
        case MARQUEE_STATE_INITIAL_PAUSE:
            ctx->state = MARQUEE_STATE_SCROLL_LEFT;
            ctx->timer = get_marquee_speed_frames();
            break;

        case MARQUEE_STATE_SCROLL_LEFT:
            ctx->offset++;
            if (ctx->offset >= ctx->max_offset) {
                ctx->offset = ctx->max_offset;
                ctx->state = MARQUEE_STATE_END_PAUSE;
                ctx->timer = MARQUEE_END_PAUSE_FRAMES;
            } else {
                ctx->timer = get_marquee_speed_frames();
            }
            break;

        case MARQUEE_STATE_END_PAUSE:
            ctx->state = MARQUEE_STATE_SCROLL_RIGHT;
            ctx->timer = get_marquee_speed_frames();
            break;

        case MARQUEE_STATE_SCROLL_RIGHT:
            ctx->offset--;
            if (ctx->offset <= 0) {
                ctx->offset = 0;
                ctx->state = MARQUEE_STATE_INITIAL_PAUSE;
                ctx->timer = MARQUEE_INITIAL_PAUSE_FRAMES;
            } else {
                ctx->timer = get_marquee_speed_frames();
            }
            break;
    }
}
