/*
 * File: dcnow_menu.c
 * Project: dcnow
 * -----
 * DC Now (dreamcast.online/now) Player Status Popup Menu
 *
 * Extracted from ui_menu_credits.c — contains game code mapping,
 * state management, network connection/fetch lifecycle, input handling,
 * two rendering paths (bitmap + vector font), and background auto-refresh.
 */

#include <string.h>

#include <openmenu_settings.h>

#include "ui/draw_kos.h"
#include "ui/draw_prototypes.h"
#include "ui/font_prototypes.h"
#include "ui/dc/input.h"
#include "ui/ui_menu_credits.h"

#include "dcnow_api.h"
#include "dcnow_net_init.h"
#include "dcnow_vmu.h"
#include <arch/timer.h>
#include "../texture/txr_manager.h"

#ifdef DCNOW_ASYNC
#include "dcnow_worker.h"
#endif

extern image img_empty_boxart;  /* Defined in draw_kos.c */

/* File-local copies of state shared with ui_menu_credits.c */
static enum draw_state* dcnow_state_ptr = NULL;
static uint32_t dcnow_text_color;

/* Mapping from DC Now API game codes to openMenu product IDs for box art lookup */
typedef struct {
    const char* api_code;
    const char* product_id;
} dcnow_game_mapping_t;

static const dcnow_game_mapping_t game_code_map[] = {
    {"PSO", "PSO"},              /* Phantasy Star Online */
    {"Q3", "Q3"},                /* Quake III Arena */
    {"CHUCHU", "CHUCHU"},        /* ChuChu Rocket! */
    {"BROWSERS", "BROWSERS"},     /* Web Browsers */
    {"AFO", "AFO"},              /* Alien Front Online */
    {"4X4", "4X4"},              /* 4x4 Evolution */
    {"DAYTONA", "DAYTONA"},      /* Daytona USA */
    {"OUTTRIG", "OUTTRIG"},      /* Outtrigger */
    {"STARLNCR", "STARLNCR"},    /* Starlancer */
    {"WWP", "WWP"},              /* Worms World Party */
    {"DRIVSTRK", "DRIVSTRK"},    /* Driving Strikers */
    {"POWSMASH", "POWSMASH"},    /* Power Smash / Virtua Tennis */
    {"GUNDAM", "GUNDAM"},        /* Mobile Suit Gundam */
    {"MONACO", "MONACO"},        /* Monaco Grand Prix Online */
    {"POD", "POD"},              /* POD SpeedZone */
    {"SPEDEVIL", "SPEDEVIL"},    /* Speed Devils Online */
    {"NBA2K1", "NBA2K1"},        /* NBA 2K1 */
    {"NBA2K2", "NBA2K2"},        /* NBA 2K2 */
    {"NFL2K1", "NFL2K1"},        /* NFL 2K1 */
    {"NFL2K2", "NFL2K2"},        /* NFL 2K2 */
    {"NCAA2K2", "NCAA2K2"},      /* NCAA 2K2 */
    {"WSB2K2", "WSB2K2"},        /* World Series Baseball 2K2 */
    {"F355", "F355"},            /* Ferrari F355 Challenge */
    {"OOGABOOGA", "OOGABOOGA"},  /* Ooga Booga */
    {"TOYRACER", "TOYRACER"},    /* Toy Racer */
    {"GOLF2", "GOLF2"},          /* Golf Shiyouyo 2 */
    {"HUNDSWORD", "HUNDSWORD"},  /* Hundred Swords */
    {"MAXPOOL", "MAXPOOL"},      /* Maximum Pool */
    {"PBABOWL", "PBABOWL"},      /* PBA Tour Bowling 2001 */
    {"NEXTTET", "NEXTTET"},      /* The Next Tetris */
    {"SEGATET", "SEGATET"},      /* Sega Tetris */
    {"SEGASWRL", "SEGASWRL"},    /* Sega Swirl */
    {"PLANRING", "PLANRING"},    /* Planet Ring */
    {"IGPACK", "IGPACK"},        /* Internet Game Pack */
    {"DEEDEE", "DEEDEE"},        /* Dee Dee Planet */
    {"AEROFD", "AEROFD"},        /* Aero Dancing FSD */
    {"AEROI", "AEROI"},          /* Aero Dancing i */
    {"AEROISD", "AEROISD"},      /* Aero Dancing iSD */
    {"FLOIGAN", "FLOIGAN"},      /* Floigan Bros Episode 1 */
    {"SA", "SA"},                /* Sonic Adventure */
    {"SA2", "SA2"},              /* Sonic Adventure 2 */
    {"JSR", "JSR"},              /* Jet Grind Radio / Jet Set Radio */
    {"SHENMUE", "SHENMUE"},      /* Shenmue Passport */
    {"CRAZYT2", "CRAZYT2"},      /* Crazy Taxi 2 */
    {"MSR", "MSR"},              /* Metropolis Street Racer */
    {"SAMBA", "SAMBA"},          /* Samba de Amigo */
    {"SF2049", "SF2049"},        /* San Francisco Rush 2049 */
    {"SEGAGT", "SEGAGT"},        /* Sega GT */
    {"SWR", "SWR"},              /* Star Wars Episode I Racer */
    {"CLASSIC", "CLASSIC"},      /* ClassiCube */
    {NULL, NULL}                 /* Terminator */
};

/* Look up product ID from API game code */
static const char* get_product_id_from_api_code(const char* api_code) {
    if (!api_code || api_code[0] == '\0') {
        return NULL;
    }

    for (int i = 0; game_code_map[i].api_code != NULL; i++) {
        if (strcmp(game_code_map[i].api_code, api_code) == 0) {
            return game_code_map[i].product_id;
        }
    }

    /* If not found in map, try using the API code directly */
    return api_code;
}

typedef enum {
    DCNOW_VIEW_CONNECTION_SELECT,  /* Choosing connection method (Serial/Modem) */
    DCNOW_VIEW_GAMES,              /* Showing list of games */
    DCNOW_VIEW_PLAYERS             /* Showing list of players for selected game */
} dcnow_view_t;

static dcnow_data_t dcnow_data;
static dcnow_view_t dcnow_view = DCNOW_VIEW_GAMES;
static int dcnow_choice = 0;
static int dcnow_conn_choice = 0;  /* Connection method: 0=Serial, 1=Modem */
static int dcnow_scroll_offset = 0;  /* Scroll offset for viewing large game lists */
static int dcnow_selected_game = -1; /* Index of selected game for player view */
static bool dcnow_data_fetched = false;
static bool dcnow_is_loading = false;
static bool dcnow_needs_fetch = false;  /* Flag to trigger fetch on next frame */
static bool dcnow_shown_loading = false;  /* Track if we've displayed loading screen */
static bool dcnow_net_initialized = false;
static char connection_status[128] = "";
static int* dcnow_navigate_timeout = NULL;  /* Pointer to navigate timeout for input debouncing */

/* Timestamp (ms) of the last successful fetch — 0 until first fetch completes */
static uint64_t dcnow_last_fetch_ms = 0;
/* Scratch buffer for auto-refresh so old data survives a failed fetch */
static dcnow_data_t dcnow_temp_data;

static bool dcnow_is_connecting = false;
static bool dcnow_connect_cooldown_pending = false;
static dcnow_connection_method_t dcnow_pending_method = DCNOW_CONN_SERIAL;
static int dcnow_connect_anim_frame = 0;

#ifdef DCNOW_ASYNC
static dcnow_worker_context_t dcnow_worker_ctx;
static bool dcnow_worker_initialized = false;
static bool dcnow_bg_fetch_active = false;  /* Background auto-refresh in progress */
#endif

extern void (*current_ui_draw_OP)(void);
extern void (*current_ui_draw_TR)(void);

#define DCNOW_INPUT_TIMEOUT_INITIAL (10)
#define DCNOW_INPUT_TIMEOUT_REPEAT (4)
#define DCNOW_AUTO_REFRESH_MS       60000  /* 60 seconds between auto-refreshes */

static void dcnow_connection_status_callback(const char* message) {
    strncpy(connection_status, message, sizeof(connection_status) - 1);
    connection_status[sizeof(connection_status) - 1] = '\0';

    dcnow_vmu_show_status(message);

    pvr_wait_ready();
    pvr_scene_begin();

    draw_set_list(PVR_LIST_OP_POLY);
    pvr_list_begin(PVR_LIST_OP_POLY);
    (*current_ui_draw_OP)();
    pvr_list_finish();

    draw_set_list(PVR_LIST_TR_POLY);
    pvr_list_begin(PVR_LIST_TR_POLY);
    (*current_ui_draw_TR)();
    pvr_list_finish();

    pvr_scene_finish();
}

static void dcnow_start_connect_worker_or_sync(void) {
#ifdef DCNOW_ASYNC
    dcnow_worker_start_connect(&dcnow_worker_ctx, dcnow_pending_method);
#else
    dcnow_set_status_callback(dcnow_connection_status_callback);
    int net_result = dcnow_net_init_with_method(dcnow_pending_method);
    dcnow_set_status_callback(NULL);
    dcnow_is_connecting = false;
    connection_status[0] = '\0';

    if (net_result < 0) {
        DCNOW_DPRINTF("DC Now: Connection failed: %d\n", net_result);
        memset(&dcnow_data, 0, sizeof(dcnow_data));
        snprintf(dcnow_data.error_message, sizeof(dcnow_data.error_message),
                "Connection failed (error %d). Press A to retry", net_result);
        dcnow_data.data_valid = false;
    } else {
        DCNOW_DPRINTF("DC Now: Connection successful, starting fetch\n");
        dcnow_net_initialized = true;
        memset(&dcnow_data, 0, sizeof(dcnow_data));
        dcnow_data.data_valid = false;

        dcnow_data_fetched = false;
        dcnow_is_loading = true;
        dcnow_choice = 0;
        dcnow_scroll_offset = 0;
        dcnow_needs_fetch = true;
    }
#endif
}

void
dcnow_setup(enum draw_state* state, struct theme_color* _colors, int* timeout_ptr, uint32_t title_color) {
    /* Store state pointer and text color locally before calling popup_setup */
    dcnow_state_ptr = state;
    dcnow_text_color = _colors->menu_text_color;

    popup_setup(state, _colors, timeout_ptr, title_color);

#ifdef DCNOW_ASYNC
    if (!dcnow_worker_initialized) {
        dcnow_worker_init();
        dcnow_worker_initialized = true;
    }
#endif
    dcnow_choice = 0;
    dcnow_scroll_offset = 0;
    dcnow_view = DCNOW_VIEW_GAMES;
    dcnow_selected_game = -1;

    /* Store navigate timeout pointer for input debouncing */
    dcnow_navigate_timeout = timeout_ptr;

    /* Set the draw state to DRAW_DCNOW_PLAYERS to actually show the DC Now menu */
    *state = DRAW_DCNOW_PLAYERS;

    /* Network initialization is now done via menu option, not automatically */
    /* User can select "Connect to DreamPi" from the DC Now menu */

    /* If network is already initialized and we haven't fetched data yet, try to fetch */
    if (dcnow_net_initialized && !dcnow_data_fetched && !dcnow_is_loading) {
        dcnow_is_loading = true;

        /* Show VMU refresh indicator while we block on the fetch */
        dcnow_vmu_show_refreshing();

        /* Attempt to fetch fresh data from dreamcast.online/now */
        int result = dcnow_fetch_data(&dcnow_data, 5000);  /* 5 second timeout */

        if (result == 0) {
            dcnow_data_fetched = true;
            /* Update VMU display with games list */
            dcnow_vmu_update_display(&dcnow_data);
            dcnow_last_fetch_ms = timer_ms_gettime64();
        } else {
            /* Failed to fetch - try to use cached data */
            if (!dcnow_get_cached_data(&dcnow_data)) {
                /* No cached data available, show error */
                memset(&dcnow_data, 0, sizeof(dcnow_data));
                strcpy(dcnow_data.error_message, "Not connected - select Connect to begin");
                dcnow_data.data_valid = false;
            }
        }

        dcnow_is_loading = false;
    } else if (!dcnow_net_initialized) {
        /* Show message prompting user to connect */
        memset(&dcnow_data, 0, sizeof(dcnow_data));
        strcpy(dcnow_data.error_message, "Not connected");
        dcnow_data.data_valid = false;
    }
}

void
handle_input_dcnow(enum control input) {
    /* Check navigate timeout to prevent input skipping */
    if (dcnow_navigate_timeout && *dcnow_navigate_timeout > 0) {
        return;
    }

    switch (input) {
        case A: {
            /* A button: Show connection menu / Connect / Fetch data / Drill down */
            if (!dcnow_net_initialized && dcnow_view != DCNOW_VIEW_CONNECTION_SELECT) {
                /* Show connection method selection */
                dcnow_view = DCNOW_VIEW_CONNECTION_SELECT;
                dcnow_conn_choice = 0;  /* Default to Serial */
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
            } else if (dcnow_view == DCNOW_VIEW_CONNECTION_SELECT) {
                /* User selected a connection method - queue cooldown-aware connect */
                DCNOW_DPRINTF("DC Now: Starting connection with method %d...\n", dcnow_conn_choice);
                dcnow_is_connecting = true;
                dcnow_connect_cooldown_pending = true;
                dcnow_pending_method = dcnow_conn_choice == 0 ? DCNOW_CONN_SERIAL : DCNOW_CONN_MODEM;
                dcnow_connect_anim_frame = 0;
                connection_status[0] = '\0';
                dcnow_view = DCNOW_VIEW_GAMES;  /* switch to games view so popup shows status */
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
            } else if (!dcnow_data.data_valid) {
                /* Fetch initial data */
                DCNOW_DPRINTF("DC Now: Requesting initial fetch...\n");
                dcnow_data_fetched = false;
                dcnow_is_loading = true;
                dcnow_choice = 0;
                dcnow_scroll_offset = 0;
#ifdef DCNOW_ASYNC
                dcnow_worker_start_fetch(&dcnow_worker_ctx, 5000);
#else
                dcnow_needs_fetch = true;
#endif
            } else if (dcnow_view == DCNOW_VIEW_GAMES && dcnow_choice < dcnow_data.game_count) {
                /* Drill down into selected game to show players */
                dcnow_selected_game = dcnow_choice;
                dcnow_view = DCNOW_VIEW_PLAYERS;
                dcnow_choice = 0;
                dcnow_scroll_offset = 0;
                DCNOW_DPRINTF("DC Now: Drilling down - game_idx=%d, view now=%d (1=PLAYERS)\n",
                       dcnow_selected_game, dcnow_view);
                /* Set timeout after navigation */
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
            } else {
                DCNOW_DPRINTF("DC Now: A pressed but conditions not met - view=%d, choice=%d, game_count=%d, data_valid=%d\n",
                       dcnow_view, dcnow_choice, dcnow_data.game_count, dcnow_data.data_valid);
            }
        } break;
        case X: {
            /* X button: Refresh data */
            if (dcnow_net_initialized && dcnow_data.data_valid) {
                DCNOW_DPRINTF("DC Now: Requesting refresh...\n");
                dcnow_data_fetched = false;
                dcnow_data.data_valid = false;
                dcnow_is_loading = true;
                dcnow_shown_loading = false;  /* Reset flag so loading screen shows */
                dcnow_view = DCNOW_VIEW_GAMES;
                dcnow_choice = 0;
                dcnow_scroll_offset = 0;
#ifdef DCNOW_ASYNC
                dcnow_worker_start_fetch(&dcnow_worker_ctx, 5000);
#else
                dcnow_needs_fetch = true;
#endif
                /* Set timeout after refresh action */
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
            }
        } break;
        case B: {
            /* B button: Back or Close */
            DCNOW_DPRINTF("DC Now: B pressed, view=%d (0=CONN_SELECT, 1=GAMES, 2=PLAYERS)\n", dcnow_view);
            if (dcnow_connect_cooldown_pending) {
                dcnow_connect_cooldown_pending = false;
                dcnow_is_connecting = false;
                connection_status[0] = '\0';
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
                return;
            }
#ifdef DCNOW_ASYNC
            /* Cancel async operation if one is in progress */
            if (dcnow_is_connecting || (dcnow_is_loading && dcnow_worker_is_busy())) {
                DCNOW_DPRINTF("DC Now: Requesting cancellation of async operation\n");
                if (dcnow_worker_is_busy()) {
                    dcnow_worker_cancel(&dcnow_worker_ctx);
                }
                dcnow_connect_cooldown_pending = false;
                dcnow_is_connecting = false;
                connection_status[0] = '\0';
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
                return;
            }
#endif
            if (dcnow_view == DCNOW_VIEW_CONNECTION_SELECT) {
                /* Cancel connection selection, go back to games view */
                DCNOW_DPRINTF("DC Now: Canceling connection selection\n");
                dcnow_view = DCNOW_VIEW_GAMES;
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
                return;
            }
            if (dcnow_view == DCNOW_VIEW_PLAYERS) {
                /* Go back to game list */
                DCNOW_DPRINTF("DC Now: Going back to game list\n");
                dcnow_view = DCNOW_VIEW_GAMES;
                /* Restore previous selection, ensuring it's valid */
                if (dcnow_selected_game >= 0 && dcnow_selected_game < dcnow_data.game_count) {
                    dcnow_choice = dcnow_selected_game;
                } else {
                    dcnow_choice = 0;
                }
                dcnow_scroll_offset = 0;
                dcnow_selected_game = -1;
                /* Set timeout after back navigation */
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
                /* DO NOT close the menu - just return to games view */
                return;  /* Early return to prevent any further processing */
            }
            /* If we reach here, we're in games view, so close the menu */
            DCNOW_DPRINTF("DC Now: Closing DC Now menu\n");
            *dcnow_state_ptr = DRAW_UI;
            /* Set timeout after closing menu */
            if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
        } break;
        case START: {
            /* START button: Do nothing (let it close the menu naturally) */
        } break;
        case UP: {
            /* Scroll up / Change connection selection */
            if (dcnow_view == DCNOW_VIEW_CONNECTION_SELECT) {
                /* Toggle between Serial (0) and Modem (1) */
                dcnow_conn_choice = (dcnow_conn_choice == 0) ? 1 : 0;
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
            } else if (dcnow_choice > 0) {
                dcnow_choice--;
                /* Adjust scroll offset if selection goes above visible area */
                if (dcnow_choice < dcnow_scroll_offset) {
                    dcnow_scroll_offset = dcnow_choice;
                }
                DCNOW_DPRINTF("DC Now: UP - choice=%d, scroll_offset=%d\n", dcnow_choice, dcnow_scroll_offset);
                /* Set timeout after navigation to prevent skipping */
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
            }
        } break;
        case DOWN: {
            /* Scroll down / Change connection selection */
            if (dcnow_view == DCNOW_VIEW_CONNECTION_SELECT) {
                /* Toggle between Serial (0) and Modem (1) */
                dcnow_conn_choice = (dcnow_conn_choice == 0) ? 1 : 0;
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
            } else {
                int max_items = 0;
                int total_items = 0;
                if (dcnow_view == DCNOW_VIEW_GAMES && dcnow_data.data_valid) {
                    total_items = dcnow_data.game_count;
                    max_items = total_items - 1;
                } else if (dcnow_view == DCNOW_VIEW_PLAYERS && dcnow_selected_game >= 0) {
                    total_items = dcnow_data.games[dcnow_selected_game].player_count;
                    max_items = total_items - 1;
                }

                if (total_items > 0 && dcnow_choice < max_items) {
                    dcnow_choice++;
                    /* Adjust scroll offset if selection goes below visible area */
                    int max_visible = (sf_ui[0] == UI_SCROLL || sf_ui[0] == UI_FOLDERS) ? 10 : 8;
                    if (dcnow_choice >= dcnow_scroll_offset + max_visible) {
                        dcnow_scroll_offset = dcnow_choice - max_visible + 1;
                    }
                    /* Ensure scroll offset doesn't go negative */
                    if (dcnow_scroll_offset < 0) {
                        dcnow_scroll_offset = 0;
                    }
                    DCNOW_DPRINTF("DC Now: DOWN - choice=%d, scroll_offset=%d, max_items=%d\n",
                           dcnow_choice, dcnow_scroll_offset, max_items);
                    /* Set timeout after navigation to prevent skipping */
                    if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
                }
            }
        } break;
        case TRIG_L:
        case TRIG_R: {
            /* L+R pressed together: manual refresh (same flow as X) */
            if (INPT_TriggerPressed(TRIGGER_L) && INPT_TriggerPressed(TRIGGER_R)) {
                if (dcnow_net_initialized && dcnow_data.data_valid) {
                    DCNOW_DPRINTF("DC Now: L+R refresh requested\n");
                    dcnow_data_fetched = false;
                    dcnow_data.data_valid = false;
                    dcnow_is_loading = true;
                    dcnow_shown_loading = false;
#ifdef DCNOW_ASYNC
                    dcnow_worker_start_fetch(&dcnow_worker_ctx, 5000);
#else
                    dcnow_needs_fetch = true;
#endif
                    dcnow_view = DCNOW_VIEW_GAMES;
                    dcnow_choice = 0;
                    dcnow_scroll_offset = 0;
                    if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
                }
            }
        } break;
        case Y: {
            /* Y button: Disconnect from network */
            if (dcnow_net_initialized) {
                DCNOW_DPRINTF("DC Now: Disconnecting...\n");
                /* Disconnect modem/network - brief freeze (~700ms) is acceptable */
                dcnow_net_disconnect();
                dcnow_net_initialized = false;
                dcnow_data_fetched = false;
                dcnow_last_fetch_ms = 0;
                memset(&dcnow_data, 0, sizeof(dcnow_data));
                snprintf(dcnow_data.error_message, sizeof(dcnow_data.error_message),
                        "Disconnected. Press A to reconnect");
                dcnow_data.data_valid = false;
                dcnow_view = DCNOW_VIEW_GAMES;
                dcnow_choice = 0;
                dcnow_scroll_offset = 0;
                DCNOW_DPRINTF("DC Now: Disconnected successfully\n");
                if (dcnow_navigate_timeout) *dcnow_navigate_timeout = DCNOW_INPUT_TIMEOUT_INITIAL;
            }
        } break;
        default:
            break;
    }
}

void
draw_dcnow_op(void) { /* Opaque pass - nothing to draw */ }

void
draw_dcnow_tr(void) {
    z_set_cond(205.0f);

    if (dcnow_is_connecting && dcnow_connect_cooldown_pending) {
        unsigned int remaining_ms = dcnow_net_get_ppp_cooldown_remaining_ms();
        if (remaining_ms > 0) {
            unsigned int secs = (remaining_ms + 999) / 1000;
            snprintf(connection_status, sizeof(connection_status),
                     "Waiting for DreamPi reset (%us)...", secs);
        } else {
            dcnow_connect_cooldown_pending = false;
            dcnow_start_connect_worker_or_sync();
        }
    }

#ifdef DCNOW_ASYNC
    /* Poll async worker thread for connection progress */
    if (dcnow_is_connecting && dcnow_worker_is_busy()) {
        dcnow_worker_state_t state = dcnow_worker_poll(&dcnow_worker_ctx);
        strncpy(connection_status, dcnow_worker_get_status(&dcnow_worker_ctx),
                sizeof(connection_status) - 1);
        connection_status[sizeof(connection_status) - 1] = '\0';

        if (state == DCNOW_WORKER_DONE) {
            dcnow_is_connecting = false;
            dcnow_net_initialized = true;
            connection_status[0] = '\0';
            DCNOW_DPRINTF("DC Now: Async connection complete, starting fetch\n");

            /* Initialize the network subsystem (socket priming etc) */
            dcnow_init();

            /* Auto-start data fetch */
            dcnow_is_loading = true;
            dcnow_worker_start_fetch(&dcnow_worker_ctx, 10000);
        } else if (state == DCNOW_WORKER_ERROR) {
            dcnow_is_connecting = false;
            connection_status[0] = '\0';
            memset(&dcnow_data, 0, sizeof(dcnow_data));
            snprintf(dcnow_data.error_message, sizeof(dcnow_data.error_message),
                    "Connection failed (error %d). Press A to retry",
                    dcnow_worker_ctx.error_code);
            dcnow_data.data_valid = false;
            DCNOW_DPRINTF("DC Now: Async connection failed: %d\n", dcnow_worker_ctx.error_code);
        }
    } else if (dcnow_is_connecting && !dcnow_connect_cooldown_pending && !dcnow_worker_is_busy()) {
        /* Worker finished between frames - catch up */
        dcnow_worker_poll(&dcnow_worker_ctx);
        dcnow_is_connecting = false;
        connection_status[0] = '\0';
    }

    /* Poll async worker thread for fetch progress */
    if (dcnow_is_loading && dcnow_worker_is_busy()) {
        dcnow_worker_state_t state = dcnow_worker_poll(&dcnow_worker_ctx);
        dcnow_shown_loading = true;

        if (state == DCNOW_WORKER_DONE) {
            dcnow_is_loading = false;
            memcpy(&dcnow_data, &dcnow_worker_ctx.result_data, sizeof(dcnow_data));
            dcnow_data_fetched = true;
            dcnow_vmu_update_display(&dcnow_data);
            dcnow_last_fetch_ms = timer_ms_gettime64();
            DCNOW_DPRINTF("DC Now: Async fetch complete\n");
        } else if (state == DCNOW_WORKER_ERROR) {
            dcnow_is_loading = false;
            DCNOW_DPRINTF("DC Now: Async fetch failed: %d\n", dcnow_worker_ctx.error_code);
        }
    } else if (dcnow_is_loading && !dcnow_worker_is_busy() && !dcnow_needs_fetch) {
        /* Worker finished between frames - catch up */
        dcnow_worker_poll(&dcnow_worker_ctx);
    }
#endif

    /* Check if we need to fetch data (only after showing loading screen) - sync path */
#ifndef DCNOW_ASYNC
    if (dcnow_needs_fetch && dcnow_shown_loading) {
        dcnow_needs_fetch = false;
        DCNOW_DPRINTF("DC Now: Fetching data...\n");

        /* Show VMU refresh indicator while we block on the network */
        dcnow_vmu_show_refreshing();

        int result = dcnow_fetch_data(&dcnow_data, 5000);
        if (result == 0) {
            dcnow_data_fetched = true;
            dcnow_vmu_update_display(&dcnow_data);
            dcnow_last_fetch_ms = timer_ms_gettime64();
            DCNOW_DPRINTF("DC Now: Data refreshed successfully\n");
        } else {
            DCNOW_DPRINTF("DC Now: Data refresh failed: %d\n", result);
        }

        dcnow_is_loading = false;
    }

    /* Auto-refresh every 60 seconds while the popup is open with valid data */
    if (dcnow_net_initialized && dcnow_data.data_valid && !dcnow_is_loading && dcnow_last_fetch_ms > 0) {
        uint64_t now = timer_ms_gettime64();
        if ((now - dcnow_last_fetch_ms) >= DCNOW_AUTO_REFRESH_MS) {
            DCNOW_DPRINTF("DC Now: Auto-refresh triggered\n");
            dcnow_vmu_show_refreshing();

            int result = dcnow_fetch_data(&dcnow_temp_data, 5000);
            if (result == 0) {
                memcpy(&dcnow_data, &dcnow_temp_data, sizeof(dcnow_data));
                dcnow_vmu_update_display(&dcnow_data);
                DCNOW_DPRINTF("DC Now: Auto-refresh completed successfully\n");
            } else {
                /* Fetch failed — keep old data, restore old VMU display */
                dcnow_vmu_update_display(&dcnow_data);
                DCNOW_DPRINTF("DC Now: Auto-refresh failed: %d\n", result);
            }
            dcnow_last_fetch_ms = timer_ms_gettime64();
        }
    }
#endif /* !DCNOW_ASYNC */

    if (sf_ui[0] == UI_SCROLL || sf_ui[0] == UI_FOLDERS) {
        /* Scroll/Folders mode - use bitmap font */
        const int line_height = 20;
        const int title_gap = line_height;
        const int padding = 16;
        const int max_visible_games = 10;  /* Show at most 10 games at once */

        /* Calculate width based on content */
        int max_line_len = 30;  /* "Dreamcast NOW! - Online Now" */
        const int icon_space = 36;  /* Extra space for 28px icon + 8px gap */

        /* Check instruction text length - account for all buttons: A=Fetch Y=Disconnect X=Refresh B=Close */
        const char* instructions = "A=Fetch  Y=Disconnect  X=Refresh  B=Close";
        int instr_len = strlen(instructions) + 4;  /* Extra margin for colored buttons */
        if (instr_len > max_line_len) {
            max_line_len = instr_len;
        }

        if (dcnow_data.data_valid) {
            for (int i = 0; i < dcnow_data.game_count; i++) {
                int len = strlen(dcnow_data.games[i].game_name) + 15;  /* name + " - 999 players" + margin */
                if (len > max_line_len) {
                    max_line_len = len;
                }
            }
            /* Check player names and details in player view */
            if (dcnow_view == DCNOW_VIEW_PLAYERS && dcnow_selected_game >= 0 &&
                dcnow_selected_game < dcnow_data.game_count) {
                int player_count = dcnow_data.games[dcnow_selected_game].player_count;
                for (int i = 0; i < player_count && i < 64; i++) {
                    int len = strlen(dcnow_data.games[dcnow_selected_game].player_names[i]);
                    const json_player_details_t *details = &dcnow_data.games[dcnow_selected_game].player_details[i];
                    /* Add space for " [Level | Country]" if present */
                    if (details->level[0] != '\0' || details->country[0] != '\0') {
                        len += strlen(details->level) + strlen(details->country) + 8;  /* " [ | ]" + margin */
                    }
                    if (len > max_line_len) {
                        max_line_len = len;
                    }
                }
            }
        } else {
            int err_len = strlen(dcnow_data.error_message);
            if (err_len > max_line_len) {
                max_line_len = err_len;
            }
        }

        const int width = (max_line_len * 8) + padding + icon_space;

        int num_lines = 2;  /* Title + total players line */
        if (dcnow_data.data_valid) {
            if (dcnow_view == DCNOW_VIEW_PLAYERS && dcnow_selected_game >= 0) {
                /* Player list view */
                int player_count = dcnow_data.games[dcnow_selected_game].player_count;
                num_lines += 1;  /* Game title line */
                num_lines += (player_count < max_visible_games ? player_count : max_visible_games);
                if (player_count > max_visible_games) {
                    num_lines += 1;  /* Scroll indicator */
                }
                num_lines += 3;  /* Separator + spacing + instructions */
            } else {
                /* Game list view */
                num_lines += (dcnow_data.game_count < max_visible_games ? dcnow_data.game_count : max_visible_games);
                if (dcnow_data.game_count > max_visible_games) {
                    num_lines += 1;  /* Scroll indicator */
                }
                num_lines += 3;  /* Separator + spacing + instructions */
            }
        } else {
            num_lines += 3;  /* Error message + separator + instructions */
        }

        const int height = (int)((num_lines * line_height + title_gap) * 1.5);
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + (padding / 2);

        /* Draw stunning DC Now popup with enhanced visuals */
        draw_popup_menu(x, y, width, height);

        /* Add cyan accent border for DC Now popup */
        const int accent_offset = 3;
        draw_draw_quad(x - accent_offset, y - accent_offset, width + (2 * accent_offset), 2, 0xFF00DDFF);  /* Top */
        draw_draw_quad(x - accent_offset, y + height + accent_offset - 2, width + (2 * accent_offset), 2, 0xFF00DDFF);  /* Bottom */
        draw_draw_quad(x - accent_offset, y - accent_offset, 2, height + (2 * accent_offset), 0xFF00DDFF);  /* Left */
        draw_draw_quad(x + width + accent_offset - 2, y - accent_offset, 2, height + (2 * accent_offset), 0xFF00DDFF);  /* Right */

        /* Add Dreamcast button color corner accents */
        draw_draw_quad(x - 6, y - 6, 8, 8, 0xFFDD2222);  /* Top-left - RED (A button) */
        draw_draw_quad(x + width - 2, y - 6, 8, 8, 0xFF3399FF);  /* Top-right - BLUE (B button) */
        draw_draw_quad(x - 6, y + height - 2, 8, 8, 0xFF00DD00);  /* Bottom-left - GREEN (Y button) */
        draw_draw_quad(x + width - 2, y + height - 2, 8, 8, 0xFFFFCC00);  /* Bottom-right - YELLOW (X button) */

        int cur_y = y + 2;
        font_bmp_begin_draw();

        /* Title with debug view indicator */
        char title[64];
        if (dcnow_view == DCNOW_VIEW_PLAYERS) {
            snprintf(title, sizeof(title), "Dreamcast NOW! - Player List");
        } else {
            snprintf(title, sizeof(title), "Dreamcast NOW! - Online Now");
        }
        int title_x = x + (width / 2) - ((strlen(title) * 8) / 2);
        font_bmp_set_color(0xFF00DDFF);  /* Bright cyan for title */
        font_bmp_draw_main(title_x, cur_y, title);

        cur_y += title_gap;

        if (dcnow_is_connecting) {
            font_bmp_set_color(0xFFFFAA00);  /* Orange */
            if (connection_status[0] != '\0') {
                font_bmp_draw_main(x_item, cur_y, connection_status);
            } else {
                font_bmp_draw_main(x_item, cur_y, "Connecting...");
            }
            cur_y += line_height;
        } else if (dcnow_is_loading) {
            /* Show loading message */
            font_bmp_set_color(dcnow_text_color);
            font_bmp_draw_main(x_item, cur_y,
                dcnow_last_fetch_ms == 0 ? "Fetching initial data..." : "Refreshing... Please Wait");
            dcnow_shown_loading = true;  /* Mark that we've shown the loading screen */
            cur_y += line_height;
        } else if (dcnow_data.data_valid) {
            if (dcnow_view == DCNOW_VIEW_PLAYERS && dcnow_selected_game >= 0) {
                /* Show player list for selected game */
                char game_name_buf[80];
                char player_count_buf[20];
                snprintf(game_name_buf, sizeof(game_name_buf), "%s - ",
                         dcnow_data.games[dcnow_selected_game].game_name);
                snprintf(player_count_buf, sizeof(player_count_buf), "%d players",
                         dcnow_data.games[dcnow_selected_game].player_count);

                /* Draw game name in white */
                font_bmp_set_color(dcnow_text_color);
                int name_width = strlen(game_name_buf) * 8;
                font_bmp_draw_main(x_item, cur_y, game_name_buf);

                /* Draw player count in yellow-green */
                font_bmp_set_color(0xFFAAFF00);
                font_bmp_draw_main(x_item + name_width, cur_y, player_count_buf);
                cur_y += line_height;

                int player_count = dcnow_data.games[dcnow_selected_game].player_count;
                int visible_count = (player_count < max_visible_games) ? player_count : max_visible_games;

                for (int i = 0; i < visible_count; i++) {
                    int player_idx = dcnow_scroll_offset + i;
                    if (player_idx >= player_count) break;

                    font_bmp_set_color(player_idx == dcnow_choice ? 0xFFFF8800 : dcnow_text_color);  /* Bright orange for selection */
                    font_bmp_draw_main(x_item, cur_y, dcnow_data.games[dcnow_selected_game].player_names[player_idx]);

                    /* Show level and country for highlighted player */
                    if (player_idx == dcnow_choice) {
                        const json_player_details_t *details = &dcnow_data.games[dcnow_selected_game].player_details[player_idx];
                        if (details->level[0] != '\0' || details->country[0] != '\0') {
                            char info[64];
                            if (details->level[0] != '\0' && details->country[0] != '\0') {
                                snprintf(info, sizeof(info), " [%s | %s]", details->level, details->country);
                            } else if (details->level[0] != '\0') {
                                snprintf(info, sizeof(info), " [%s]", details->level);
                            } else {
                                snprintf(info, sizeof(info), " [%s]", details->country);
                            }
                            int name_width = strlen(dcnow_data.games[dcnow_selected_game].player_names[player_idx]) * 8;
                            font_bmp_set_color(0xFF88CCFF);  /* Light blue for details */
                            font_bmp_draw_main(x_item + name_width, cur_y, info);
                        }
                    }

                    cur_y += line_height;
                }

                /* Show scroll indicators if needed */
                if (player_count > max_visible_games) {
                    char scroll_info[32];
                    snprintf(scroll_info, sizeof(scroll_info), "(%d/%d)",
                             dcnow_choice + 1, player_count);
                    font_bmp_set_color(0xFFBBBBBB);  /* Light gray for scroll info */
                    font_bmp_draw_main(x_item, cur_y, scroll_info);
                    cur_y += line_height;
                }
            } else {
                /* Show total players with color coding */
                char total_label[40];
                char total_count[20];
                snprintf(total_label, sizeof(total_label), "Total Active Players: ");
                snprintf(total_count, sizeof(total_count), "%d", dcnow_data.total_players);

                /* Draw label in light blue */
                font_bmp_set_color(0xFF88CCFF);
                int label_width = strlen(total_label) * 8;
                font_bmp_draw_main(x_item, cur_y, total_label);

                /* Draw count in yellow-green */
                font_bmp_set_color(0xFFAAFF00);
                font_bmp_draw_main(x_item + label_width, cur_y, total_count);
                cur_y += line_height + 4;  /* Extra spacing after total */

                /* Show game list */
                if (dcnow_data.game_count == 0) {
                font_bmp_set_color(dcnow_text_color);
                font_bmp_draw_main(x_item, cur_y, "No active games");
                cur_y += line_height;
            } else {
                /* Show games with scrolling support */
                int visible_count = (dcnow_data.game_count < max_visible_games) ?
                                   dcnow_data.game_count : max_visible_games;

                for (int i = 0; i < visible_count; i++) {
                    int game_idx = dcnow_scroll_offset + i;
                    if (game_idx >= dcnow_data.game_count) break;

                    /* Try to load box art icon for this game */
                    image game_icon;
                    bool has_icon = false;
                    if (dcnow_data.games[game_idx].game_code[0] != '\0') {
                        /* Map API code to product ID */
                        const char* product_id = get_product_id_from_api_code(dcnow_data.games[game_idx].game_code);
                        DCNOW_DPRINTF("DC Now UI: API code '%s' -> product ID '%s'\n",
                               dcnow_data.games[game_idx].game_code, product_id);

                        if (product_id && txr_get_small(product_id, &game_icon) == 0) {
                            /* Check if we got a real texture or just the empty placeholder */
                            if (game_icon.texture != img_empty_boxart.texture) {
                                has_icon = true;
                                DCNOW_DPRINTF("DC Now UI: Found texture for '%s'\n", product_id);
                            } else {
                                DCNOW_DPRINTF("DC Now UI: No texture found for '%s'\n", product_id);
                            }
                        }
                    } else {
                        DCNOW_DPRINTF("DC Now UI: Game %d has empty code\n", game_idx);
                    }

                    /* Draw box art icon if available (28x28 pixels) */
                    int text_x = x_item;
                    if (has_icon) {
                        const int icon_size = 28;
                        draw_draw_image(x_item, cur_y - 4, icon_size, icon_size, COLOR_WHITE, &game_icon);
                        text_x = x_item + icon_size + 6;  /* Icon + small gap */
                    }

                    /* Format game name and player count separately for better color coding */
                    char game_name_buf[80];
                    char player_count_buf[30];
                    const char* status = dcnow_data.games[game_idx].is_active ? "" : " (offline)";

                    snprintf(game_name_buf, sizeof(game_name_buf), "%s - ", dcnow_data.games[game_idx].game_name);

                    if (dcnow_data.games[game_idx].player_count == 1) {
                        snprintf(player_count_buf, sizeof(player_count_buf), "%d player%s",
                                 dcnow_data.games[game_idx].player_count, status);
                    } else {
                        snprintf(player_count_buf, sizeof(player_count_buf), "%d players%s",
                                 dcnow_data.games[game_idx].player_count, status);
                    }

                    /* Draw game name - white or bright orange when selected */
                    font_bmp_set_color(game_idx == dcnow_choice ? 0xFFFF8800 : dcnow_text_color);
                    int name_width = strlen(game_name_buf) * 8;
                    font_bmp_draw_main(text_x, cur_y, game_name_buf);

                    /* Draw player count in yellow-green */
                    font_bmp_set_color(0xFFAAFF00);
                    font_bmp_draw_main(text_x + name_width, cur_y, player_count_buf);
                    cur_y += line_height;
                }

                /* Show scroll indicators if needed */
                if (dcnow_data.game_count > max_visible_games) {
                    char scroll_info[32];
                    snprintf(scroll_info, sizeof(scroll_info), "(%d/%d)",
                             dcnow_choice + 1, dcnow_data.game_count);
                    font_bmp_set_color(0xFFBBBBBB);  /* Light gray for scroll info */
                    font_bmp_draw_main(x_item, cur_y, scroll_info);
                    cur_y += line_height;
                }
                }
            }
        } else if (dcnow_view == DCNOW_VIEW_CONNECTION_SELECT) {
            /* Connection method selection menu */
            font_bmp_set_color(0xFF00DDFF);  /* Cyan header */
            font_bmp_draw_main(x_item, cur_y, "Select Connection Method:");
            cur_y += line_height + 4;

            /* Serial option */
            if (dcnow_conn_choice == 0) {
                font_bmp_set_color(0xFFFF8800);  /* Bright orange when selected */
                font_bmp_draw_main(x_item, cur_y, "> Serial (Coders Cable)");
            } else {
                font_bmp_set_color(dcnow_text_color);
                font_bmp_draw_main(x_item, cur_y, "  Serial (Coders Cable)");
            }
            cur_y += line_height;

            /* Modem option */
            if (dcnow_conn_choice == 1) {
                font_bmp_set_color(0xFFFF8800);  /* Bright orange when selected */
                font_bmp_draw_main(x_item, cur_y, "> Modem (Dial-up)");
            } else {
                font_bmp_set_color(dcnow_text_color);
                font_bmp_draw_main(x_item, cur_y, "  Modem (Dial-up)");
            }
            cur_y += line_height + 8;

            /* Instructions */
            font_bmp_set_color(0xFFBBBBBB);
            font_bmp_draw_main(x_item, cur_y, "UP/DOWN=Select  A=Connect  B=Cancel");
            cur_y += line_height;
        } else {
            /* Show error message or connection prompt */
            font_bmp_set_color(dcnow_text_color);
            font_bmp_draw_main(x_item, cur_y, dcnow_data.error_message);
            cur_y += line_height;
            if (!dcnow_net_initialized) {
                font_bmp_draw_main(x_item, cur_y, "Press A to connect");
            } else {
                font_bmp_draw_main(x_item, cur_y, "Press A to retry");
            }
            cur_y += line_height;
        }

        /* Separator line before instructions */
        cur_y += 4;
        font_bmp_set_color(0xFF00DDFF);  /* Cyan for separator */
        font_bmp_draw_main(x_item, cur_y, "----------------------------------------");
        cur_y += line_height;

        /* Instructions with stunning Dreamcast button color-coding */
        int instr_x = x_item;
        if (dcnow_view == DCNOW_VIEW_PLAYERS) {
            /* B button - BLUE */
            font_bmp_set_color(0xFF3399FF);
            font_bmp_draw_main(instr_x, cur_y, "B");
            instr_x += 8;
            font_bmp_set_color(0xFFCCCCCC);
            font_bmp_draw_main(instr_x, cur_y, "=Back");
        } else if (!dcnow_net_initialized) {
            /* A button - RED */
            font_bmp_set_color(0xFFDD2222);
            font_bmp_draw_main(instr_x, cur_y, "A");
            instr_x += 8;
            font_bmp_set_color(0xFFCCCCCC);
            font_bmp_draw_main(instr_x, cur_y, "=Connect  |  ");
            instr_x += 13 * 8;
            /* B button - BLUE */
            font_bmp_set_color(0xFF3399FF);
            font_bmp_draw_main(instr_x, cur_y, "B");
            instr_x += 8;
            font_bmp_set_color(0xFFCCCCCC);
            font_bmp_draw_main(instr_x, cur_y, "=Close");
        } else if (!dcnow_data.data_valid) {
            /* A button - RED */
            font_bmp_set_color(0xFFDD2222);
            font_bmp_draw_main(instr_x, cur_y, "A");
            instr_x += 8;
            font_bmp_set_color(0xFFCCCCCC);
            font_bmp_draw_main(instr_x, cur_y, "=Fetch");
            instr_x += 6 * 8 + 16;  /* text + 16px gap */
            /* Y button - GREEN */
            font_bmp_set_color(0xFF00DD00);
            font_bmp_draw_main(instr_x, cur_y, "Y");
            instr_x += 8;
            font_bmp_set_color(0xFFCCCCCC);
            font_bmp_draw_main(instr_x, cur_y, "=Disconnect");
            instr_x += 11 * 8 + 16;  /* text + 16px gap */
            /* B button - BLUE */
            font_bmp_set_color(0xFF3399FF);
            font_bmp_draw_main(instr_x, cur_y, "B");
            instr_x += 8;
            font_bmp_set_color(0xFFCCCCCC);
            font_bmp_draw_main(instr_x, cur_y, "=Close");
        } else {
            /* A button - RED */
            font_bmp_set_color(0xFFDD2222);
            font_bmp_draw_main(instr_x, cur_y, "A");
            instr_x += 8;
            font_bmp_set_color(0xFFCCCCCC);
            font_bmp_draw_main(instr_x, cur_y, "=Details");
            instr_x += 8 * 8 + 16;  /* text + 16px gap */
            /* X button - YELLOW */
            font_bmp_set_color(0xFFFFCC00);
            font_bmp_draw_main(instr_x, cur_y, "X");
            instr_x += 8;
            font_bmp_set_color(0xFFCCCCCC);
            font_bmp_draw_main(instr_x, cur_y, "=Refresh");
            instr_x += 8 * 8 + 16;  /* text + 16px gap */
            /* Y button - GREEN */
            font_bmp_set_color(0xFF00DD00);
            font_bmp_draw_main(instr_x, cur_y, "Y");
            instr_x += 8;
            font_bmp_set_color(0xFFCCCCCC);
            font_bmp_draw_main(instr_x, cur_y, "=Disconnect");
            instr_x += 11 * 8 + 16;  /* text + 16px gap */
            /* B button - BLUE */
            font_bmp_set_color(0xFF3399FF);
            font_bmp_draw_main(instr_x, cur_y, "B");
            instr_x += 8;
            font_bmp_set_color(0xFFCCCCCC);
            font_bmp_draw_main(instr_x, cur_y, "=Close");
        }
        cur_y += line_height;

    } else {
        /* LineDesc/Grid modes - use vector font */
        const int line_height = 28;
        const int title_gap = line_height / 2;
        const int padding = 20;
        const int max_visible_games = 8;
        const int icon_space = 44;  /* 36px icon + 8px gap */

        /* Calculate width based on content */
        int max_line_len = 35;  /* Base width for title */

        /* Check instruction text length (vector font is ~10 pixels per char) */
        /* Account for all buttons: A=Fetch Y=Disconnect X=Refresh B=Close */
        const char* instructions = "A=Fetch  Y=Disconnect  X=Refresh  B=Close";
        int instr_len = strlen(instructions) + 4;  /* Extra margin for colored buttons */
        if (instr_len > max_line_len) {
            max_line_len = instr_len;
        }

        if (dcnow_data.data_valid) {
            for (int i = 0; i < dcnow_data.game_count; i++) {
                /* Estimate character width for vector font (~10 pixels/char) */
                int len = strlen(dcnow_data.games[i].game_name) + 15;
                if (len > max_line_len) {
                    max_line_len = len;
                }
            }
            /* Check player names and details in player view */
            if (dcnow_view == DCNOW_VIEW_PLAYERS && dcnow_selected_game >= 0 &&
                dcnow_selected_game < dcnow_data.game_count) {
                int player_count = dcnow_data.games[dcnow_selected_game].player_count;
                for (int i = 0; i < player_count && i < 64; i++) {
                    int len = strlen(dcnow_data.games[dcnow_selected_game].player_names[i]);
                    const json_player_details_t *details = &dcnow_data.games[dcnow_selected_game].player_details[i];
                    /* Add space for " [Level | Country]" if present */
                    if (details->level[0] != '\0' || details->country[0] != '\0') {
                        len += strlen(details->level) + strlen(details->country) + 8;  /* " [ | ]" + margin */
                    }
                    if (len > max_line_len) {
                        max_line_len = len;
                    }
                }
            }
        }

        int num_lines = 2;  /* Title + total */
        if (dcnow_data.data_valid) {
            if (dcnow_view == DCNOW_VIEW_PLAYERS && dcnow_selected_game >= 0) {
                /* Player list view */
                int player_count = dcnow_data.games[dcnow_selected_game].player_count;
                num_lines += 1;  /* Game title line */
                num_lines += (player_count < max_visible_games ? player_count : max_visible_games);
                if (player_count > max_visible_games) {
                    num_lines += 1;  /* Scroll indicator */
                }
                num_lines += 3;  /* Separator + spacing + instructions */
            } else {
                /* Game list view */
                num_lines += (dcnow_data.game_count < max_visible_games ? dcnow_data.game_count : max_visible_games);
                if (dcnow_data.game_count > max_visible_games) {
                    num_lines += 1;  /* Scroll indicator */
                }
                num_lines += 3;  /* Separator + spacing + instructions */
            }
        } else {
            num_lines += 3;  /* Error message + separator + instructions */
        }

        const int width = (max_line_len * 10) + padding + icon_space;
        const int height = (int)((num_lines * line_height + title_gap) * 1.5);
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + 10;

        /* Draw stunning DC Now popup with enhanced visuals */
        draw_popup_menu(x, y, width, height);

        /* Add cyan accent border for DC Now popup */
        const int accent_offset = 3;
        draw_draw_quad(x - accent_offset, y - accent_offset, width + (2 * accent_offset), 2, 0xFF00DDFF);  /* Top */
        draw_draw_quad(x - accent_offset, y + height + accent_offset - 2, width + (2 * accent_offset), 2, 0xFF00DDFF);  /* Bottom */
        draw_draw_quad(x - accent_offset, y - accent_offset, 2, height + (2 * accent_offset), 0xFF00DDFF);  /* Left */
        draw_draw_quad(x + width + accent_offset - 2, y - accent_offset, 2, height + (2 * accent_offset), 0xFF00DDFF);  /* Right */

        /* Add Dreamcast button color corner accents */
        draw_draw_quad(x - 6, y - 6, 8, 8, 0xFFDD2222);  /* Top-left - RED (A button) */
        draw_draw_quad(x + width - 2, y - 6, 8, 8, 0xFF3399FF);  /* Top-right - BLUE (B button) */
        draw_draw_quad(x - 6, y + height - 2, 8, 8, 0xFF00DD00);  /* Bottom-left - GREEN (Y button) */
        draw_draw_quad(x + width - 2, y + height - 2, 8, 8, 0xFFFFCC00);  /* Bottom-right - YELLOW (X button) */

        int cur_y = y + 2;
        font_bmf_begin_draw();
        font_bmf_set_height_default();

        /* Title with bright cyan color */
        if (dcnow_view == DCNOW_VIEW_PLAYERS) {
            font_bmf_draw_centered(x + width / 2, cur_y, 0xFF00DDFF, "Dreamcast NOW! - Player List");
        } else {
            font_bmf_draw_centered(x + width / 2, cur_y, 0xFF00DDFF, "Dreamcast NOW! - Online Now");
        }
        cur_y += title_gap;

        if (dcnow_is_connecting) {
            if (connection_status[0] != '\0') {
                font_bmf_draw(x_item, cur_y, 0xFFFFAA00, connection_status);
            } else {
                font_bmf_draw(x_item, cur_y, 0xFFFFAA00, "Connecting...");
            }
            cur_y += line_height;
        } else if (dcnow_is_loading) {
            cur_y += line_height;
            font_bmf_draw(x_item, cur_y, dcnow_text_color,
                dcnow_last_fetch_ms == 0 ? "Fetching initial data..." : "Refreshing... Please Wait");
            dcnow_shown_loading = true;  /* Mark that we've shown the loading screen */
        } else if (dcnow_data.data_valid) {
            if (dcnow_view == DCNOW_VIEW_PLAYERS && dcnow_selected_game >= 0) {
                /* Show player list for selected game */
                cur_y += line_height;

                /* Format game name and player count with different colors */
                char game_name_buf[80];
                char player_count_buf[20];
                snprintf(game_name_buf, sizeof(game_name_buf), "%s - ",
                         dcnow_data.games[dcnow_selected_game].game_name);
                snprintf(player_count_buf, sizeof(player_count_buf), "%d players",
                         dcnow_data.games[dcnow_selected_game].player_count);

                /* Measure game name width for positioning */
                int name_x = x_item;
                font_bmf_draw(name_x, cur_y, dcnow_text_color, game_name_buf);

                /* Draw player count in yellow-green (estimate width) */
                int count_x = name_x + (strlen(game_name_buf) * 10);
                font_bmf_draw(count_x, cur_y, 0xFFAAFF00, player_count_buf);

                int player_count = dcnow_data.games[dcnow_selected_game].player_count;
                int visible_count = (player_count < max_visible_games) ? player_count : max_visible_games;

                for (int i = 0; i < visible_count; i++) {
                    int player_idx = dcnow_scroll_offset + i;
                    if (player_idx >= player_count) break;

                    cur_y += line_height;
                    uint32_t color = (player_idx == dcnow_choice) ? 0xFFFF8800 : dcnow_text_color;  /* Bright orange for selection */
                    font_bmf_draw(x_item, cur_y, color, dcnow_data.games[dcnow_selected_game].player_names[player_idx]);

                    /* Show level and country for highlighted player */
                    if (player_idx == dcnow_choice) {
                        const json_player_details_t *details = &dcnow_data.games[dcnow_selected_game].player_details[player_idx];
                        if (details->level[0] != '\0' || details->country[0] != '\0') {
                            char info[64];
                            if (details->level[0] != '\0' && details->country[0] != '\0') {
                                snprintf(info, sizeof(info), " [%s | %s]", details->level, details->country);
                            } else if (details->level[0] != '\0') {
                                snprintf(info, sizeof(info), " [%s]", details->level);
                            } else {
                                snprintf(info, sizeof(info), " [%s]", details->country);
                            }
                            int name_x = x_item + (strlen(dcnow_data.games[dcnow_selected_game].player_names[player_idx]) * 10);
                            font_bmf_draw(name_x, cur_y, 0xFF88CCFF, info);  /* Light blue for details */
                        }
                    }
                }

                /* Show scroll indicators if needed */
                if (player_count > max_visible_games) {
                    cur_y += line_height;
                    char scroll_info[32];
                    snprintf(scroll_info, sizeof(scroll_info), "(%d/%d)",
                             dcnow_choice + 1, player_count);
                    font_bmf_draw(x_item, cur_y, 0xFFBBBBBB, scroll_info);  /* Light gray */
                }
            } else {
                /* Total players with color coding */
                cur_y += line_height;
                char total_label[40];
                char total_count[20];
                snprintf(total_label, sizeof(total_label), "Total Active Players: ");
                snprintf(total_count, sizeof(total_count), "%d", dcnow_data.total_players);

                /* Draw label in light blue */
                font_bmf_draw(x_item, cur_y, 0xFF88CCFF, total_label);

                /* Draw count in yellow-green (estimate position) */
                int count_x = x_item + (strlen(total_label) * 10);
                font_bmf_draw(count_x, cur_y, 0xFFAAFF00, total_count);

                cur_y += 6;  /* Extra spacing after total */

                /* Game list */
                if (dcnow_data.game_count == 0) {
                cur_y += line_height;
                font_bmf_draw(x_item, cur_y, dcnow_text_color, "No active games");
            } else {
                /* Show games with scrolling support */
                int visible_count = (dcnow_data.game_count < max_visible_games) ?
                                   dcnow_data.game_count : max_visible_games;

                for (int i = 0; i < visible_count; i++) {
                    int game_idx = dcnow_scroll_offset + i;
                    if (game_idx >= dcnow_data.game_count) break;

                    cur_y += line_height;

                    /* Try to load box art icon for this game */
                    image game_icon;
                    bool has_icon = false;
                    if (dcnow_data.games[game_idx].game_code[0] != '\0') {
                        /* Map API code to product ID */
                        const char* product_id = get_product_id_from_api_code(dcnow_data.games[game_idx].game_code);

                        if (product_id && txr_get_small(product_id, &game_icon) == 0) {
                            /* Check if we got a real texture or just the empty placeholder */
                            if (game_icon.texture != img_empty_boxart.texture) {
                                has_icon = true;
                            }
                        }
                    }

                    /* Draw box art icon if available (36x36 pixels for vector font) */
                    int text_x = x_item;
                    if (has_icon) {
                        const int icon_size = 36;
                        draw_draw_image(x_item, cur_y - 6, icon_size, icon_size, COLOR_WHITE, &game_icon);
                        text_x = x_item + icon_size + 8;  /* Icon + small gap */
                    }

                    /* Format game name and player count separately for better color coding */
                    char game_name_buf[80];
                    char player_count_buf[30];
                    const char* status = dcnow_data.games[game_idx].is_active ? "" : " (offline)";

                    snprintf(game_name_buf, sizeof(game_name_buf), "%s - ", dcnow_data.games[game_idx].game_name);

                    if (dcnow_data.games[game_idx].player_count == 1) {
                        snprintf(player_count_buf, sizeof(player_count_buf), "%d player%s",
                                 dcnow_data.games[game_idx].player_count, status);
                    } else {
                        snprintf(player_count_buf, sizeof(player_count_buf), "%d players%s",
                                 dcnow_data.games[game_idx].player_count, status);
                    }

                    /* Draw game name - white or bright orange when selected */
                    uint32_t name_color = (game_idx == dcnow_choice) ? 0xFFFF8800 : dcnow_text_color;
                    font_bmf_draw_auto_size(text_x, cur_y, name_color, game_name_buf, width - (text_x - x_item) - 20);

                    /* Draw player count in yellow-green (estimate position) */
                    int count_x = text_x + (strlen(game_name_buf) * 10);
                    font_bmf_draw(count_x, cur_y, 0xFFAAFF00, player_count_buf);
                }

                /* Show scroll indicators if needed */
                if (dcnow_data.game_count > max_visible_games) {
                    cur_y += line_height;
                    char scroll_info[32];
                    snprintf(scroll_info, sizeof(scroll_info), "(%d/%d)",
                             dcnow_choice + 1, dcnow_data.game_count);
                    font_bmf_draw(x_item, cur_y, 0xFFBBBBBB, scroll_info);  /* Light gray */
                }
                }
            }
        } else if (dcnow_view == DCNOW_VIEW_CONNECTION_SELECT) {
            /* Connection method selection menu */
            cur_y += line_height;
            font_bmf_draw(x_item, cur_y, 0xFF00DDFF, "Select Connection Method:");  /* Cyan header */
            cur_y += line_height + 6;

            /* Serial option */
            if (dcnow_conn_choice == 0) {
                font_bmf_draw(x_item, cur_y, 0xFFFF8800, "> Serial (Coders Cable)");  /* Bright orange */
            } else {
                font_bmf_draw(x_item, cur_y, dcnow_text_color, "  Serial (Coders Cable)");
            }
            cur_y += line_height;

            /* Modem option */
            if (dcnow_conn_choice == 1) {
                font_bmf_draw(x_item, cur_y, 0xFFFF8800, "> Modem (Dial-up)");  /* Bright orange */
            } else {
                font_bmf_draw(x_item, cur_y, dcnow_text_color, "  Modem (Dial-up)");
            }
            cur_y += line_height + 10;

            /* Instructions */
            font_bmf_draw(x_item, cur_y, 0xFFBBBBBB, "UP/DOWN=Select  A=Connect  B=Cancel");
            cur_y += line_height;
        } else {
            /* Error or connection prompt */
            cur_y += line_height;
            font_bmf_draw(x_item, cur_y, dcnow_text_color, dcnow_data.error_message);
            cur_y += line_height;
            if (!dcnow_net_initialized) {
                font_bmf_draw(x_item, cur_y, dcnow_text_color, "Press A to connect");
            } else {
                font_bmf_draw(x_item, cur_y, dcnow_text_color, "Press A to retry");
            }
            cur_y += line_height;
        }

        /* Separator line before instructions */
        cur_y += 6;
        font_bmf_draw(x_item, cur_y, 0xFF00DDFF, "----------------------------------------");  /* Cyan separator */
        cur_y += line_height;

        /* Instructions with stunning Dreamcast button color-coding */
        int instr_x = x_item;
        if (dcnow_view == DCNOW_VIEW_CONNECTION_SELECT) {
            /* Connection selection - no additional instructions needed */
        } else if (dcnow_view == DCNOW_VIEW_PLAYERS) {
            /* B button - BLUE */
            font_bmf_draw(instr_x, cur_y, 0xFF3399FF, "B");
            instr_x += 12;
            font_bmf_draw(instr_x, cur_y, 0xFFCCCCCC, "=Back");
        } else if (!dcnow_net_initialized) {
            /* A button - RED */
            font_bmf_draw(instr_x, cur_y, 0xFFDD2222, "A");
            instr_x += 12;
            font_bmf_draw(instr_x, cur_y, 0xFFCCCCCC, "=Connect  |  ");
            instr_x += 130;
            /* B button - BLUE */
            font_bmf_draw(instr_x, cur_y, 0xFF3399FF, "B");
            instr_x += 12;
            font_bmf_draw(instr_x, cur_y, 0xFFCCCCCC, "=Close");
        } else if (!dcnow_data.data_valid) {
            /* A button - RED */
            font_bmf_draw(instr_x, cur_y, 0xFFDD2222, "A");
            instr_x += 12;
            font_bmf_draw(instr_x, cur_y, 0xFFCCCCCC, "=Fetch");
            instr_x += 60 + 20;  /* text + 20px gap */
            /* Y button - GREEN */
            font_bmf_draw(instr_x, cur_y, 0xFF00DD00, "Y");
            instr_x += 12;
            font_bmf_draw(instr_x, cur_y, 0xFFCCCCCC, "=Disconnect");
            instr_x += 110 + 20;  /* text + 20px gap */
            /* B button - BLUE */
            font_bmf_draw(instr_x, cur_y, 0xFF3399FF, "B");
            instr_x += 12;
            font_bmf_draw(instr_x, cur_y, 0xFFCCCCCC, "=Close");
        } else {
            /* A button - RED */
            font_bmf_draw(instr_x, cur_y, 0xFFDD2222, "A");
            instr_x += 12;
            font_bmf_draw(instr_x, cur_y, 0xFFCCCCCC, "=Details");
            instr_x += 80 + 20;  /* text + 20px gap */
            /* X button - YELLOW */
            font_bmf_draw(instr_x, cur_y, 0xFFFFCC00, "X");
            instr_x += 12;
            font_bmf_draw(instr_x, cur_y, 0xFFCCCCCC, "=Refresh");
            instr_x += 80 + 20;  /* text + 20px gap */
            /* Y button - GREEN */
            font_bmf_draw(instr_x, cur_y, 0xFF00DD00, "Y");
            instr_x += 12;
            font_bmf_draw(instr_x, cur_y, 0xFFCCCCCC, "=Disconnect");
            instr_x += 110 + 20;  /* text + 20px gap */
            /* B button - BLUE */
            font_bmf_draw(instr_x, cur_y, 0xFF3399FF, "B");
            instr_x += 12;
            font_bmf_draw(instr_x, cur_y, 0xFFCCCCCC, "=Close");
        }
        cur_y += line_height;
    }
}

/* Background auto-refresh for DC Now data (called from main loop)
 * This ensures data is refreshed every 60 seconds even when popup is closed */
void
dcnow_background_tick(void) {
#ifdef DCNOW_ASYNC
    /* Async: check if a background fetch completed */
    if (dcnow_bg_fetch_active && !dcnow_worker_is_busy()) {
        dcnow_worker_state_t state = dcnow_worker_poll(&dcnow_worker_ctx);
        if (state == DCNOW_WORKER_DONE) {
            memcpy(&dcnow_data, &dcnow_worker_ctx.result_data, sizeof(dcnow_data));
            dcnow_vmu_update_display(&dcnow_data);
            DCNOW_DPRINTF("DC Now: Async background refresh completed\n");
        } else if (state == DCNOW_WORKER_ERROR) {
            /* Keep old data, restore old VMU display */
            dcnow_vmu_update_display(&dcnow_data);
            DCNOW_DPRINTF("DC Now: Async background refresh failed: %d\n", dcnow_worker_ctx.error_code);
        }
        dcnow_bg_fetch_active = false;
        dcnow_last_fetch_ms = timer_ms_gettime64();
    }
#endif

    /* Only refresh if network is initialized and we have valid data */
    if (!dcnow_net_initialized || !dcnow_data.data_valid || dcnow_is_loading) {
        return;
    }

#ifdef DCNOW_ASYNC
    /* Don't start a new background fetch if one is already running */
    if (dcnow_bg_fetch_active || dcnow_worker_is_busy()) {
        return;
    }
#endif

    /* Check if we have a valid last fetch timestamp */
    if (dcnow_last_fetch_ms == 0) {
        return;
    }

    /* Check if 60 seconds have passed since last refresh */
    uint64_t now = timer_ms_gettime64();
    if ((now - dcnow_last_fetch_ms) < DCNOW_AUTO_REFRESH_MS) {
        return;
    }

    /* Time to refresh! */
    DCNOW_DPRINTF("DC Now: Background auto-refresh triggered\n");
    dcnow_vmu_show_refreshing();

#ifdef DCNOW_ASYNC
    /* Async: start background fetch without blocking */
    if (dcnow_worker_start_fetch(&dcnow_worker_ctx, 5000) == 0) {
        dcnow_bg_fetch_active = true;
    } else {
        /* Worker busy, try again next tick */
        DCNOW_DPRINTF("DC Now: Background refresh deferred - worker busy\n");
    }
#else
    int result = dcnow_fetch_data(&dcnow_temp_data, 5000);
    if (result == 0) {
        memcpy(&dcnow_data, &dcnow_temp_data, sizeof(dcnow_data));
        dcnow_vmu_update_display(&dcnow_data);
        DCNOW_DPRINTF("DC Now: Background auto-refresh completed successfully\n");
    } else {
        /* Fetch failed — keep old data, restore old VMU display */
        dcnow_vmu_update_display(&dcnow_data);
        DCNOW_DPRINTF("DC Now: Background auto-refresh failed: %d\n", result);
    }
    dcnow_last_fetch_ms = timer_ms_gettime64();
#endif
}
