/*
 * File: ui_draw_state_dispatch.h
 * Project: ui
 * -----
 * Shared macros for draw_state switch dispatch.
 * Each UI module uses the same set of popup draw/input handlers.
 * Header-only: no .c file needed.
 */

#pragma once

/*
 * DISPATCH_DRAW_OP(draw_current)
 * Dispatches opaque-polygon drawing for the current popup state.
 * Called from each UI module's drawOP function after drawing the background.
 */
#define DISPATCH_DRAW_OP(draw_current)                         \
    switch (draw_current) {                                    \
        case DRAW_MENU:          draw_menu_op();          break; \
        case DRAW_CREDITS:       draw_credits_op();       break; \
        case DRAW_MULTIDISC:     draw_multidisc_op();     break; \
        case DRAW_EXIT:          draw_exit_op();          break; \
        case DRAW_CODEBREAKER:   draw_codebreaker_op();   break; \
        case DRAW_PSX_LAUNCHER:  draw_psx_launcher_op();  break; \
        case DRAW_SAVELOAD:      draw_saveload_op();      break; \
        case DRAW_DCNOW_PLAYERS: draw_dcnow_op();        break; \
        default:                                               \
        case DRAW_UI:            /* always drawn */        break; \
    }

/*
 * DISPATCH_DRAW_TR(draw_current)
 * Dispatches translucent-polygon drawing for the current popup state.
 * Called from each UI module's drawTR function after drawing the game list.
 */
#define DISPATCH_DRAW_TR(draw_current)                         \
    switch (draw_current) {                                    \
        case DRAW_MENU:          draw_menu_tr();          break; \
        case DRAW_CREDITS:       draw_credits_tr();       break; \
        case DRAW_MULTIDISC:     draw_multidisc_tr();     break; \
        case DRAW_EXIT:          draw_exit_tr();          break; \
        case DRAW_CODEBREAKER:   draw_codebreaker_tr();   break; \
        case DRAW_PSX_LAUNCHER:  draw_psx_launcher_tr();  break; \
        case DRAW_SAVELOAD:      draw_saveload_tr();      break; \
        case DRAW_DCNOW_PLAYERS: draw_dcnow_tr();        break; \
        default:                                               \
        case DRAW_UI:            /* always drawn */        break; \
    }

/*
 * DISPATCH_INPUT(draw_current, input_current, run_cb)
 * Dispatches input handling for the current popup state.
 * run_cb is the name of the module's local run_cb function,
 * called after CodeBreaker input if start_cb is set.
 */
#define DISPATCH_INPUT(draw_current, input_current, run_cb_fn) \
    switch (draw_current) {                                    \
        case DRAW_MENU:                                        \
            handle_input_menu(input_current);              break; \
        case DRAW_CREDITS:                                     \
            handle_input_credits(input_current);           break; \
        case DRAW_MULTIDISC:                                   \
            handle_input_multidisc(input_current);         break; \
        case DRAW_EXIT:                                        \
            handle_input_exit(input_current);              break; \
        case DRAW_CODEBREAKER:                                 \
            handle_input_codebreaker(input_current);            \
            if (start_cb) { run_cb_fn(); }                break; \
        case DRAW_PSX_LAUNCHER:                                \
            handle_input_psx_launcher(input_current);      break; \
        case DRAW_SAVELOAD:                                    \
            handle_input_saveload(input_current);          break; \
        case DRAW_DCNOW_PLAYERS:                               \
            handle_input_dcnow(input_current);             break; \
        default:                                               \
        case DRAW_UI:                                          \
            handle_input_ui(input_current);                break; \
    }
