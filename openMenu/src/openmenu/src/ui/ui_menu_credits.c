/*
 * File: ui_menu_credits.c
 * Project: ui
 * File Created: Monday, 12th July 2021 11:34:23 pm
 * Author: Hayden Kowalchuk
 * -----
 * Copyright (c) 2021 Hayden Kowalchuk, Hayden Kowalchuk
 * License: BSD 3-clause "New" or "Revised" License,
 * http://www.opensource.org/licenses/BSD-3-Clause
 */

#include <stdio.h>
#include <string.h>

#include <backend/db_item.h>
#include <backend/gd_item.h>
#include <backend/gd_list.h>
#include <backend/gdemu_sdk.h>
#include <crayon_savefile/savefile.h>
#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include "ui/draw_kos.h"
#include "ui/draw_prototypes.h"
#include "ui/font_prototypes.h"
#include "ui/ui_common.h"
#include "ui/dc/input.h"

#include "ui/ui_menu_credits.h"

/* Include dcnow_vmu.h early for VMU setting changes */
#include "../dcnow/dcnow_vmu.h"

/* DC Now menu logic lives in its own module */
#include "../dcnow/dcnow_menu.h"

/* External declaration for VM2/VMUPro/USB4Maple detection */
#include <dc/maple.h>
#include "vm2/vm2_api.h"
extern int vm2_device_count;
extern void vm2_rescan(void);

#pragma region Exit_Menu

/* Exit to BIOS menu option strings */
static const char* exit_option_text[] = {
    "Send Game ID + Mount disc + Exit to BIOS",
    "Send Game ID + Exit to BIOS",
    "Mount disc + Exit to BIOS",
    "Exit to BIOS",
    "Close"
};

static int exit_menu_choice = 0;
static int exit_menu_num_options = 0;
static int exit_menu_is_folder = 0;

/* Exit menu option indices - set dynamically based on context */
typedef enum EXIT_OPTION {
    EXIT_OPT_SENDID_MOUNT = 0,
    EXIT_OPT_SENDID_ONLY,
    EXIT_OPT_MOUNT_ONLY,
    EXIT_OPT_EXIT_ONLY,
    EXIT_OPT_CLOSE,
    EXIT_OPT_MAX
} EXIT_OPTION;

/* Dynamic option list for current context */
static EXIT_OPTION exit_options[EXIT_OPT_MAX];

/* Build exit options list based on context
 * - is_folder: true if a folder is selected (only Exit to BIOS + Close)
 * - has_vm2: true if VM2/VMUPro/USB4MAPLE is detected
 * - is_game: true if type != "other" (game, psx, etc.)
 * - Also checks if VMU Game ID transmission is enabled (not set to Off)
 */
static void
exit_menu_build_options(int is_folder, int has_vm2, int is_game) {
    exit_menu_num_options = 0;
    exit_menu_is_folder = is_folder;

    if (is_folder) {
        /* Folder selected: only Exit to BIOS and Close */
        exit_options[exit_menu_num_options++] = EXIT_OPT_EXIT_ONLY;
        exit_options[exit_menu_num_options++] = EXIT_OPT_CLOSE;
    } else if (has_vm2 && is_game && sf_vm2_send_all[0] != VM2_SEND_OFF) {
        /* VM2 detected + type != "other" + transmission enabled: all options */
        exit_options[exit_menu_num_options++] = EXIT_OPT_SENDID_MOUNT;
        exit_options[exit_menu_num_options++] = EXIT_OPT_SENDID_ONLY;
        exit_options[exit_menu_num_options++] = EXIT_OPT_MOUNT_ONLY;
        exit_options[exit_menu_num_options++] = EXIT_OPT_EXIT_ONLY;
        exit_options[exit_menu_num_options++] = EXIT_OPT_CLOSE;
    } else {
        /* No VM2 or type == "other": mount, exit, close */
        exit_options[exit_menu_num_options++] = EXIT_OPT_MOUNT_ONLY;
        exit_options[exit_menu_num_options++] = EXIT_OPT_EXIT_ONLY;
        exit_options[exit_menu_num_options++] = EXIT_OPT_CLOSE;
    }
}

#pragma endregion Exit_Menu

#pragma region CodeBreaker_Menu

/* CodeBreaker menu option strings */
static const char* cb_option_text[] = {
    "Launch selected disc with CodeBreaker",
    "Close"
};

static int cb_menu_choice = 0;
#define CB_MENU_NUM_OPTIONS 2

typedef enum CB_OPTION {
    CB_OPT_LAUNCH = 0,
    CB_OPT_CLOSE
} CB_OPTION;

#pragma endregion CodeBreaker_Menu

#pragma region Settings_Menu

static const char* menu_choice_text[] = {"Style", "Theme", "Aspect", "Beep", "Exit to 3D BIOS", "Sort", "Filter", "Multi-Disc", "Multi-Disc Grouping", "Artwork", "Display Index Numbers", "Disc Details", "Artwork", "Item Details", "Clock", "Marquee Speed", "VMU Game ID", "Boot Mode", "DC NOW! VMU", "Deflicker Filter"};
static const char* theme_choice_text[] = {"LineDesc", "Grid3", "Scroll", "Folders"};
static const char* region_choice_text[] = {"NTSC-U", "NTSC-J", "PAL"};
static const char* region_choice_text_scroll[] = {"GDMENU"};
static const char* region_choice_text_folders[] = {"FoldersDefault"};
static const char* aspect_choice_text[] = {"4:3", "16:9"};
static const char* beep_choice_text[] = {"Off", "On"}; /* Hidden from UI but kept for array sizing */
static const char* bios_3d_choice_text[] = {"Off", "On"};
static const char* sort_choice_text[] = {"Alphabetical", "Name", "Region", "Genre", "SD Card Order"};
static const char* sort_choice_text_folders[] = {"Alphabetical", "SD Card Order"};
#define SORT_CHOICES_FOLDERS 2
static const char* filter_choice_text[] = {"All",      "Action",   "Racing",   "Simulation", "Sports",     "Lightgun",
                                           "Fighting", "Shooter",  "Survival", "Adventure",  "Platformer", "RPG",
                                           "Shmup",    "Strategy", "Puzzle",   "Arcade",     "Music"};
static const char* multidisc_choice_text[] = {"Show All", "Compact"};
static const char* multidisc_grouping_choice_text[] = {"Anywhere", "Same Folder Only"};
static const char* scroll_art_choice_text[] = {"Off", "On"};
static const char* scroll_index_choice_text[] = {"Off", "On"};
static const char* disc_details_choice_text[] = {"Show", "Hide"};
static const char* folders_art_choice_text[] = {"Off", "On"};
static const char* folders_item_details_choice_text[] = {"Off", "On"};
static const char* marquee_speed_choice_text[] = {"Slow", "Medium", "Fast"};
static const char* clock_choice_text[] = {"On (12-Hour)", "On (24-Hour)", "Off"};
static const char* vm2_send_all_choice_text[] = {"Send to All", "Send to First", "Off"};
static const char* boot_mode_choice_text[] = {"Full Boot", "License Only", "Animation Only", "Fast Boot"};
static const char* dcnow_vmu_choice_text[] = {"On", "Off"};
static const char* deflicker_disable_choice_text[] = {"On", "Light", "Medium", "Strong", "Off"};
static const char* save_choice_text[] = {"Save/Load", "Apply"};
static const char* credits_text[] = {"Credits"};

const char* custom_theme_text[10] = {0};
static theme_custom* custom_themes;
static theme_scroll* custom_scroll;
static int num_custom_themes;
int cb_multidisc = 0;
int start_cb = 0;
static int psx_launcher_choice = 0;  /* 0 = Bleem!, 1 = Bloom */
static const gd_item* cur_game_item = NULL;

#define MENU_OPTIONS  ((int)(sizeof(menu_choice_text) / sizeof(menu_choice_text)[0]))
#define MENU_CHOICES  (MENU_OPTIONS)
#define THEME_CHOICES (sizeof(theme_choice_text) / sizeof(theme_choice_text)[0])
static int REGION_CHOICES = (sizeof(region_choice_text) / sizeof(region_choice_text)[0]);
#define ASPECT_CHOICES     (sizeof(aspect_choice_text) / sizeof(aspect_choice_text)[0])
#define BEEP_CHOICES       (sizeof(beep_choice_text) / sizeof(beep_choice_text)[0]) /* Hidden from UI */
#define BIOS_3D_CHOICES    (sizeof(bios_3d_choice_text) / sizeof(bios_3d_choice_text)[0])
#define SORT_CHOICES       (sizeof(sort_choice_text) / sizeof(sort_choice_text)[0])
#define FILTER_CHOICES     (sizeof(filter_choice_text) / sizeof(filter_choice_text)[0])
#define MULTIDISC_CHOICES  (sizeof(multidisc_choice_text) / sizeof(multidisc_choice_text)[0])
#define MULTIDISC_GROUPING_CHOICES (sizeof(multidisc_grouping_choice_text) / sizeof(multidisc_grouping_choice_text)[0])
#define SCROLL_ART_CHOICES (sizeof(scroll_art_choice_text) / sizeof(scroll_art_choice_text)[0])
#define SCROLL_INDEX_CHOICES (sizeof(scroll_index_choice_text) / sizeof(scroll_index_choice_text)[0])
#define DISC_DETAILS_CHOICES (sizeof(disc_details_choice_text) / sizeof(disc_details_choice_text)[0])
#define FOLDERS_ART_CHOICES (sizeof(folders_art_choice_text) / sizeof(folders_art_choice_text)[0])
#define FOLDERS_ITEM_DETAILS_CHOICES (sizeof(folders_item_details_choice_text) / sizeof(folders_item_details_choice_text)[0])
#define MARQUEE_SPEED_CHOICES (sizeof(marquee_speed_choice_text) / sizeof(marquee_speed_choice_text)[0])
#define CLOCK_CHOICES (sizeof(clock_choice_text) / sizeof(clock_choice_text)[0])
#define VM2_SEND_ALL_CHOICES (sizeof(vm2_send_all_choice_text) / sizeof(vm2_send_all_choice_text)[0])
#define BOOT_MODE_CHOICES (sizeof(boot_mode_choice_text) / sizeof(boot_mode_choice_text)[0])
#define DCNOW_VMU_CHOICES (sizeof(dcnow_vmu_choice_text) / sizeof(dcnow_vmu_choice_text)[0])
#define DEFLICKER_DISABLE_CHOICES (sizeof(deflicker_disable_choice_text) / sizeof(deflicker_disable_choice_text)[0])

typedef enum MENU_CHOICE {
    CHOICE_START,
    CHOICE_THEME = CHOICE_START,
    CHOICE_REGION,
    CHOICE_ASPECT,
    CHOICE_BEEP,
    CHOICE_BIOS_3D,
    CHOICE_SORT,
    CHOICE_FILTER,
    CHOICE_MULTIDISC,
    CHOICE_MULTIDISC_GROUPING,
    CHOICE_SCROLL_ART,
    CHOICE_SCROLL_INDEX,
    CHOICE_DISC_DETAILS,
    CHOICE_FOLDERS_ART,
    CHOICE_FOLDERS_ITEM_DETAILS,
    CHOICE_CLOCK,
    CHOICE_MARQUEE_SPEED,
    CHOICE_VM2_SEND_ALL,
    CHOICE_BOOT_MODE,
    CHOICE_DCNOW_VMU,
    CHOICE_DEFLICKER_DISABLE,
    CHOICE_SAVE,
    CHOICE_DCNOW,
    CHOICE_CREDITS,
    CHOICE_END = CHOICE_CREDITS
} MENU_CHOICE;

#define INPUT_TIMEOUT (10)

static int choices[MENU_CHOICES + 1];
static int choices_max[MENU_CHOICES + 1] = {
    THEME_CHOICES,     3, ASPECT_CHOICES, BEEP_CHOICES, BIOS_3D_CHOICES, SORT_CHOICES, FILTER_CHOICES,
    MULTIDISC_CHOICES, MULTIDISC_GROUPING_CHOICES, SCROLL_ART_CHOICES, SCROLL_INDEX_CHOICES, DISC_DETAILS_CHOICES, FOLDERS_ART_CHOICES, FOLDERS_ITEM_DETAILS_CHOICES, CLOCK_CHOICES, MARQUEE_SPEED_CHOICES, VM2_SEND_ALL_CHOICES, BOOT_MODE_CHOICES, DCNOW_VMU_CHOICES, DEFLICKER_DISABLE_CHOICES, 2 /* Apply/Save */};
static const char** menu_choice_array[MENU_CHOICES] = {theme_choice_text,       region_choice_text,   aspect_choice_text,
                                                       beep_choice_text,        bios_3d_choice_text,  sort_choice_text,
                                                       filter_choice_text,      multidisc_choice_text, multidisc_grouping_choice_text,
                                                       scroll_art_choice_text,  scroll_index_choice_text, disc_details_choice_text,
                                                       folders_art_choice_text, folders_item_details_choice_text, clock_choice_text, marquee_speed_choice_text, vm2_send_all_choice_text,
                                                       boot_mode_choice_text, dcnow_vmu_choice_text, deflicker_disable_choice_text};
static int current_choice = CHOICE_START;
static int* input_timeout_ptr = NULL;

#pragma endregion Settings_Menu

#pragma region Credits_Menu

typedef struct credit_pair {
    const char* contributor;
    const char* role;
} credit_pair;

static const credit_pair credits[] = {
    (credit_pair){"ateam", "Folders, Updates/Fixes"},
    (credit_pair){"megavolt85", "gdemu sdk, coder"},
    (credit_pair){"u/westhinksdifferent/", "UI Mockups"},
    (credit_pair){"FlorreW", "Metadata DB"},
    (credit_pair){"hasnopants", "Metadata DB"},
    (credit_pair){"Roareye", "Metadata DB"},
    (credit_pair){"sonik-br", "GDMENUCardManager"},
    (credit_pair){"protofall", "Crayon_VMU"},
    (credit_pair){"TheLegendOfXela", "Boxart (Customs)"},
    (credit_pair){"marky-b-1986", "Theming Ideas"},
    (credit_pair){"Various Testers", "Breaking Things"},
    (credit_pair){"Kofi Supporters", "Coffee+Hardware"},
    (credit_pair){"mrneo240", "Author"},
};
static const int num_credits = sizeof(credits) / sizeof(credit_pair);

#pragma endregion Credits_Menu

static enum draw_state* state_ptr = NULL;
static theme_color* stored_colors = NULL;
static uint32_t text_color;
static uint32_t highlight_color;
static uint32_t menu_bkg_color;
static uint32_t menu_bkg_border_color;
static uint32_t menu_title_color;

/* Build version string (compiled in from VERSION.TXT at build time) */
#ifndef OPENMENU_BUILD_VERSION
#define OPENMENU_BUILD_VERSION "Unknown"
#endif

void
set_cur_game_item(const gd_item* id) {
    cur_game_item = id;
}

const gd_item*
get_cur_game_item() {
    return cur_game_item;
}

static void
common_setup(enum draw_state* state, theme_color* _colors, int* timeout_ptr) {
    /* Ensure color themeing is consistent */
    text_color = _colors->menu_text_color;
    highlight_color = _colors->menu_highlight_color;
    menu_bkg_color = _colors->menu_bkg_color;
    menu_bkg_border_color = _colors->menu_bkg_border_color;

    /* So we can modify the shared state and input timeout */
    state_ptr = state;
    stored_colors = _colors;
    input_timeout_ptr = timeout_ptr;
    *input_timeout_ptr = (30 * 1) /* half a second */;
}

void
menu_setup(enum draw_state* state, theme_color* _colors, int* timeout_ptr, uint32_t title_color) {
    common_setup(state, _colors, timeout_ptr);
    menu_title_color = title_color;

    /* Rescan for VM2 devices (detect hot-swapped devices) */
    vm2_rescan();

    choices[CHOICE_THEME] = sf_ui[0];
    choices[CHOICE_REGION] = sf_region[0];
    choices[CHOICE_ASPECT] = sf_aspect[0];
    choices[CHOICE_SORT] = sf_sort[0];
    /* In Folders mode, clamp Sort to valid range (0-1) */
    if (sf_ui[0] == UI_FOLDERS && choices[CHOICE_SORT] >= SORT_CHOICES_FOLDERS) {
        choices[CHOICE_SORT] = 0;  /* Default to Alphabetical */
    }
    choices[CHOICE_FILTER] = sf_filter[0];
    choices[CHOICE_BEEP] = sf_beep[0]; /* Hidden from UI */
    choices[CHOICE_BIOS_3D] = sf_bios_3d[0];
    choices[CHOICE_MULTIDISC] = sf_multidisc[0];
    choices[CHOICE_MULTIDISC_GROUPING] = sf_multidisc_grouping[0];
    choices[CHOICE_SCROLL_ART] = sf_scroll_art[0];
    choices[CHOICE_SCROLL_INDEX] = sf_scroll_index[0];
    choices[CHOICE_DISC_DETAILS] = sf_disc_details[0];
    choices[CHOICE_FOLDERS_ART] = sf_folders_art[0];
    choices[CHOICE_FOLDERS_ITEM_DETAILS] = sf_folders_item_details[0];
    choices[CHOICE_MARQUEE_SPEED] = sf_marquee_speed[0];
    choices[CHOICE_CLOCK] = sf_clock[0];
    choices[CHOICE_VM2_SEND_ALL] = sf_vm2_send_all[0];
    choices[CHOICE_BOOT_MODE] = sf_boot_mode[0];
    choices[CHOICE_DCNOW_VMU] = sf_dcnow_vmu[0];
    choices[CHOICE_DEFLICKER_DISABLE] = sf_deflicker_disable[0];

    if (choices[CHOICE_THEME] != UI_SCROLL && choices[CHOICE_THEME] != UI_FOLDERS) {
        menu_choice_array[CHOICE_REGION] = region_choice_text;
        REGION_CHOICES = (sizeof(region_choice_text) / sizeof(region_choice_text)[0]);
        choices_max[CHOICE_REGION] = REGION_CHOICES;
        /* Grab custom themes if we have them */
        custom_themes = theme_get_custom(&num_custom_themes);
        if (num_custom_themes > 0) {
            for (int i = 0; i < num_custom_themes; i++) {
                choices_max[CHOICE_REGION]++;
                custom_theme_text[i] = custom_themes[i].name;
            }
        }
    } else {
        /* Assign appropriate default theme name based on UI mode */
        if (sf_ui[0] == UI_FOLDERS) {
            menu_choice_array[CHOICE_REGION] = region_choice_text_folders;
        } else {
            menu_choice_array[CHOICE_REGION] = region_choice_text_scroll;
        }
        REGION_CHOICES = 1;
        choices_max[CHOICE_REGION] = 1;
        /* Load appropriate themes based on UI mode */
        if (sf_ui[0] == UI_FOLDERS) {
            custom_scroll = theme_get_folder(&num_custom_themes);
        } else {
            custom_scroll = theme_get_scroll(&num_custom_themes);
        }
        if (num_custom_themes > 0) {
            for (int i = 0; i < num_custom_themes; i++) {
                choices_max[CHOICE_REGION]++;
                custom_theme_text[i] = custom_scroll[i].name;
            }
            if (sf_custom_theme[0] == THEME_ON) {
                choices[CHOICE_REGION] = sf_custom_theme_num[0] + 1;
            }
        }
    }

    if (choices[CHOICE_REGION] >= choices_max[CHOICE_REGION]) {
        choices[CHOICE_REGION] = choices_max[CHOICE_REGION] - 1;
    }
}

void
popup_setup(enum draw_state* state, theme_color* _colors, int* timeout_ptr, uint32_t title_color) {
    common_setup(state, _colors, timeout_ptr);
    menu_title_color = title_color;

    current_choice = CHOICE_START;
    psx_launcher_choice = 0;  /* Reset to Bleem! as default */
}

void
exit_menu_setup(enum draw_state* state, theme_color* _colors, int* timeout_ptr, uint32_t title_color, int is_folder) {
    common_setup(state, _colors, timeout_ptr);
    menu_title_color = title_color;

    /* Rescan for VM2 devices (detect hot-swapped devices) */
    vm2_rescan();

    /* Reset selection to first option */
    exit_menu_choice = 0;

    /* Determine if VM2 is present */
    int has_vm2 = (vm2_device_count > 0);

    /* Determine if current item is a game (type != "other") */
    int is_game = 0;
    if (!is_folder && cur_game_item != NULL && cur_game_item->type[0] != '\0') {
        is_game = (strcmp(cur_game_item->type, "other") != 0);
    }

    /* Build the options list */
    exit_menu_build_options(is_folder, has_vm2, is_game);
}

static void
menu_leave(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    *state_ptr = DRAW_UI;
    *input_timeout_ptr = (30 * 1) /* half a second */;
}

static void
credits_leave(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    *state_ptr = DRAW_MENU;
    *input_timeout_ptr = (20);
}

static void
menu_accept(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    if (current_choice == CHOICE_SAVE) {
        /* Save/Load opens VMU browser without applying settings yet */
        if (choices[CHOICE_SAVE] == 0 /* Save/Load */) {
            saveload_setup(state_ptr, stored_colors, input_timeout_ptr, menu_title_color);
            return;
        }

        /* Apply: update Global Settings */
        sf_ui[0] = choices[CHOICE_THEME];
        sf_region[0] = choices[CHOICE_REGION];
        sf_aspect[0] = choices[CHOICE_ASPECT];
        sf_sort[0] = choices[CHOICE_SORT];
        sf_filter[0] = choices[CHOICE_FILTER];
        sf_beep[0] = choices[CHOICE_BEEP]; /* Hidden from UI */
        sf_bios_3d[0] = choices[CHOICE_BIOS_3D];
        sf_multidisc[0] = choices[CHOICE_MULTIDISC];
        sf_multidisc_grouping[0] = choices[CHOICE_MULTIDISC_GROUPING];
        sf_scroll_art[0] = choices[CHOICE_SCROLL_ART];
        sf_scroll_index[0] = choices[CHOICE_SCROLL_INDEX];
        sf_disc_details[0] = choices[CHOICE_DISC_DETAILS];
        sf_folders_art[0] = choices[CHOICE_FOLDERS_ART];
        sf_folders_item_details[0] = choices[CHOICE_FOLDERS_ITEM_DETAILS];
        sf_marquee_speed[0] = choices[CHOICE_MARQUEE_SPEED];
        sf_clock[0] = choices[CHOICE_CLOCK];
        sf_vm2_send_all[0] = choices[CHOICE_VM2_SEND_ALL];
        sf_boot_mode[0] = choices[CHOICE_BOOT_MODE];
        sf_dcnow_vmu[0] = choices[CHOICE_DCNOW_VMU];
        sf_deflicker_disable[0] = choices[CHOICE_DEFLICKER_DISABLE];

        /* Immediately apply DC Now VMU setting change */
        if (sf_dcnow_vmu[0] == DCNOW_VMU_OFF) {
            /* Setting turned OFF - restore OpenMenu logo if DC Now display is active */
            if (dcnow_vmu_is_active()) {
                dcnow_vmu_restore_logo();
            }
        }
        /* When turned ON, the VMU will be updated next time dcnow_vmu_update_display is called
         * (e.g., when opening DC Now popup or on next data refresh) */

        if (choices[CHOICE_THEME] != UI_SCROLL && choices[CHOICE_THEME] != UI_FOLDERS && sf_region[0] > REGION_END) {
            sf_custom_theme[0] = THEME_ON;
            int num_default_themes = 0;
            theme_get_default(sf_aspect[0], &num_default_themes);
            sf_custom_theme_num[0] = sf_region[0] - num_default_themes;
        } else if ((choices[CHOICE_THEME] == UI_SCROLL || choices[CHOICE_THEME] == UI_FOLDERS) && sf_region[0] > 0) {
            sf_custom_theme[0] = THEME_ON;
            sf_custom_theme_num[0] = sf_region[0] - 1;
        } else {
            sf_custom_theme[0] = THEME_OFF;
        }

        /* If not filtering, then plain sort */
        if (!choices[CHOICE_FILTER]) {
            switch ((CFG_SORT)choices[CHOICE_SORT]) {
                case SORT_NAME: list_set_sort_name(); break;
                case SORT_DATE: list_set_sort_region(); break;
                case SORT_PRODUCT: list_set_sort_genre(); break;
                case SORT_SD_CARD: list_set_sort_default(); break;
                default:
                case SORT_DEFAULT: list_set_sort_alphabetical(); break;
            }
        } else {
            /* If filtering, filter down to only genre then sort */
            list_set_genre_sort((FLAGS_GENRE)choices[CHOICE_FILTER] - 1, choices[CHOICE_SORT]);
        }

        extern void reload_ui(void);
        reload_ui();
    }
    if (current_choice == CHOICE_DCNOW) {
        /* Call dcnow_setup() to initialize the DC Now popup */
        dcnow_setup(state_ptr, stored_colors, input_timeout_ptr, menu_title_color);
    }
    if (current_choice == CHOICE_CREDITS) {
        *state_ptr = DRAW_CREDITS;
        *input_timeout_ptr = (20 * 1) /* 1/3 second */;
    }
}

static void
menu_choice_prev(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    current_choice--;
    /* Wrap around if we go below start */
    if (current_choice < CHOICE_START) {
        current_choice = CHOICE_END;
    }
    /* Keep skipping until we land on a valid option */
    int attempts = 0;
    while (attempts < CHOICE_END - CHOICE_START + 1) {
        int skip = 0;
        /* Skip SCROLL_ART option in non-Scroll modes */
        if (current_choice == CHOICE_SCROLL_ART && sf_ui[0] != UI_SCROLL) {
            skip = 1;
        }
        /* Skip SCROLL_INDEX option in non-Scroll modes */
        if (current_choice == CHOICE_SCROLL_INDEX && sf_ui[0] != UI_SCROLL) {
            skip = 1;
        }
        /* Skip DISC_DETAILS option in non-Scroll modes */
        if (current_choice == CHOICE_DISC_DETAILS && sf_ui[0] != UI_SCROLL) {
            skip = 1;
        }
        /* Skip MULTIDISC_GROUPING option in non-Folders modes or when Multi-Disc is "Show All" */
        if (current_choice == CHOICE_MULTIDISC_GROUPING && (sf_ui[0] != UI_FOLDERS || choices[CHOICE_MULTIDISC] == MULTIDISC_SHOW)) {
            skip = 1;
        }
        /* Skip FOLDERS_ART option in non-Folders modes */
        if (current_choice == CHOICE_FOLDERS_ART && sf_ui[0] != UI_FOLDERS) {
            skip = 1;
        }
        /* Skip FOLDERS_ITEM_DETAILS option in non-Folders modes */
        if (current_choice == CHOICE_FOLDERS_ITEM_DETAILS && sf_ui[0] != UI_FOLDERS) {
            skip = 1;
        }
        /* Skip MARQUEE_SPEED option in non-Scroll/Folders modes */
        if (current_choice == CHOICE_MARQUEE_SPEED && sf_ui[0] != UI_SCROLL && sf_ui[0] != UI_FOLDERS) {
            skip = 1;
        }
        /* Skip CLOCK option in non-Folders modes */
        if (current_choice == CHOICE_CLOCK && sf_ui[0] != UI_FOLDERS) {
            skip = 1;
        }
        /* Skip VM2_SEND_ALL option when no VM2 devices detected */
        if (current_choice == CHOICE_VM2_SEND_ALL && vm2_device_count == 0) {
            skip = 1;
        }
        /* Skip Aspect in Scroll mode (not used) */
        if (current_choice == CHOICE_ASPECT && sf_ui[0] == UI_SCROLL) {
            skip = 1;
        }
        /* Skip Aspect/Filter in Folders mode */
        if (sf_ui[0] == UI_FOLDERS && (current_choice == CHOICE_ASPECT || current_choice == CHOICE_FILTER)) {
            skip = 1;
        }
        /* Skip BEEP option (disabled/commented out) */
        if (current_choice == CHOICE_BEEP) {
            skip = 1;
        }
        /* Skip DCNOW in up/down navigation (reached via left/right from Save/Apply) */
        if (current_choice == CHOICE_DCNOW) {
            skip = 1;
        }
        /* Skip CREDITS in up/down navigation (reached via left/right from Save/Apply) */
        if (current_choice == CHOICE_CREDITS) {
            skip = 1;
        }
        if (!skip) {
            break;  /* Found a valid option */
        }
        current_choice--;
        if (current_choice < CHOICE_START) {
            current_choice = CHOICE_END;
        }
        attempts++;
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_choice_next(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    current_choice++;
    /* Wrap around if we go past end */
    if (current_choice > CHOICE_END) {
        current_choice = CHOICE_START;
    }
    /* Keep skipping until we land on a valid option */
    int attempts = 0;
    while (attempts < CHOICE_END - CHOICE_START + 1) {
        int skip = 0;
        /* Skip SCROLL_ART option in non-Scroll modes */
        if (current_choice == CHOICE_SCROLL_ART && sf_ui[0] != UI_SCROLL) {
            skip = 1;
        }
        /* Skip SCROLL_INDEX option in non-Scroll modes */
        if (current_choice == CHOICE_SCROLL_INDEX && sf_ui[0] != UI_SCROLL) {
            skip = 1;
        }
        /* Skip DISC_DETAILS option in non-Scroll modes */
        if (current_choice == CHOICE_DISC_DETAILS && sf_ui[0] != UI_SCROLL) {
            skip = 1;
        }
        /* Skip MULTIDISC_GROUPING option in non-Folders modes or when Multi-Disc is "Show All" */
        if (current_choice == CHOICE_MULTIDISC_GROUPING && (sf_ui[0] != UI_FOLDERS || choices[CHOICE_MULTIDISC] == MULTIDISC_SHOW)) {
            skip = 1;
        }
        /* Skip FOLDERS_ART option in non-Folders modes */
        if (current_choice == CHOICE_FOLDERS_ART && sf_ui[0] != UI_FOLDERS) {
            skip = 1;
        }
        /* Skip FOLDERS_ITEM_DETAILS option in non-Folders modes */
        if (current_choice == CHOICE_FOLDERS_ITEM_DETAILS && sf_ui[0] != UI_FOLDERS) {
            skip = 1;
        }
        /* Skip MARQUEE_SPEED option in non-Scroll/Folders modes */
        if (current_choice == CHOICE_MARQUEE_SPEED && sf_ui[0] != UI_SCROLL && sf_ui[0] != UI_FOLDERS) {
            skip = 1;
        }
        /* Skip CLOCK option in non-Folders modes */
        if (current_choice == CHOICE_CLOCK && sf_ui[0] != UI_FOLDERS) {
            skip = 1;
        }
        /* Skip VM2_SEND_ALL option when no VM2 devices detected */
        if (current_choice == CHOICE_VM2_SEND_ALL && vm2_device_count == 0) {
            skip = 1;
        }
        /* Skip Aspect in Scroll mode (not used) */
        if (current_choice == CHOICE_ASPECT && sf_ui[0] == UI_SCROLL) {
            skip = 1;
        }
        /* Skip Aspect/Filter in Folders mode */
        if (sf_ui[0] == UI_FOLDERS && (current_choice == CHOICE_ASPECT || current_choice == CHOICE_FILTER)) {
            skip = 1;
        }
        /* Skip BEEP option (disabled/commented out) */
        if (current_choice == CHOICE_BEEP) {
            skip = 1;
        }
        /* Skip DCNOW in up/down navigation (reached via left/right from Save/Apply) */
        if (current_choice == CHOICE_DCNOW) {
            skip = 1;
        }
        /* Skip CREDITS in up/down navigation (reached via left/right from Save/Apply) */
        if (current_choice == CHOICE_CREDITS) {
            skip = 1;
        }
        if (!skip) {
            break;  /* Found a valid option */
        }
        current_choice++;
        if (current_choice > CHOICE_END) {
            current_choice = CHOICE_START;
        }
        attempts++;
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_region_adj(void) {
    if (choices[CHOICE_THEME] != UI_SCROLL && choices[CHOICE_THEME] != UI_FOLDERS) {
        menu_choice_array[CHOICE_REGION] = region_choice_text;
        REGION_CHOICES = (sizeof(region_choice_text) / sizeof(region_choice_text)[0]);
        choices_max[CHOICE_REGION] = REGION_CHOICES;
        /* Grab custom themes if we have them */
        custom_themes = theme_get_custom(&num_custom_themes);
        if (num_custom_themes > 0) {
            for (int i = 0; i < num_custom_themes; i++) {
                choices_max[CHOICE_REGION]++;
                custom_theme_text[i] = custom_themes[i].name;
            }
        }
    } else {
        /* Assign appropriate default theme name based on current Style selection */
        if (choices[CHOICE_THEME] == UI_FOLDERS) {
            menu_choice_array[CHOICE_REGION] = region_choice_text_folders;
            REGION_CHOICES = (sizeof(region_choice_text_folders) / sizeof(region_choice_text_folders)[0]);
        } else {
            menu_choice_array[CHOICE_REGION] = region_choice_text_scroll;
            REGION_CHOICES = (sizeof(region_choice_text_scroll) / sizeof(region_choice_text_scroll)[0]);
        }
        choices_max[CHOICE_REGION] = REGION_CHOICES;
        /* Load appropriate themes based on UI mode */
        if (choices[CHOICE_THEME] == UI_FOLDERS) {
            custom_scroll = theme_get_folder(&num_custom_themes);
        } else {
            custom_scroll = theme_get_scroll(&num_custom_themes);
        }
        if (num_custom_themes > 0) {
            for (int i = 0; i < num_custom_themes; i++) {
                choices_max[CHOICE_REGION]++;
                custom_theme_text[i] = custom_scroll[i].name;
            }
        }
    }

    if (choices[CHOICE_REGION] >= choices_max[CHOICE_REGION]) {
        choices[CHOICE_REGION] = choices_max[CHOICE_REGION] - 1;
    }
}

static void
menu_choice_left(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    /* Handle Save/Apply/DC Now/Credits row navigation */
    if (current_choice == CHOICE_CREDITS) {
        /* Move left from Credits to DC Now */
        current_choice = CHOICE_DCNOW;
        *input_timeout_ptr = INPUT_TIMEOUT;
        return;
    }
    if (current_choice == CHOICE_DCNOW) {
        /* Move left from DC Now to Apply */
        current_choice = CHOICE_SAVE;
        choices[CHOICE_SAVE] = 1;  /* Select Apply */
        *input_timeout_ptr = INPUT_TIMEOUT;
        return;
    }
    if (current_choice == CHOICE_SAVE && choices[CHOICE_SAVE] == 0) {
        /* Already on Save (leftmost), do nothing */
        return;
    }
    choices[current_choice]--;
    if (choices[current_choice] < 0) {
        choices[current_choice] = 0;
    }
    if (current_choice == CHOICE_THEME) {
        menu_region_adj();
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_choice_right(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    /* Handle Save/Apply/DC Now/Credits row navigation */
    if (current_choice == CHOICE_CREDITS) {
        /* Already on Credits (rightmost), do nothing */
        return;
    }
    if (current_choice == CHOICE_DCNOW) {
        /* Move right from DC Now to Credits */
        current_choice = CHOICE_CREDITS;
        *input_timeout_ptr = INPUT_TIMEOUT;
        return;
    }
    if (current_choice == CHOICE_SAVE && choices[CHOICE_SAVE] == 1) {
        /* On Apply, move right to DC Now */
        current_choice = CHOICE_DCNOW;
        *input_timeout_ptr = INPUT_TIMEOUT;
        return;
    }
    choices[current_choice]++;
    /* In Folders mode, limit Sort to 2 options */
    int max_choice = choices_max[current_choice];
    if (current_choice == CHOICE_SORT && sf_ui[0] == UI_FOLDERS) {
        max_choice = SORT_CHOICES_FOLDERS;
    }
    if (choices[current_choice] >= max_choice) {
        choices[current_choice]--;
    }
    if (current_choice == CHOICE_THEME) {
        menu_region_adj();
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_multidisc_prev(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    current_choice--;
    if (current_choice < 0) {
        current_choice = 0;
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_multidisc_next(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    current_choice++;
    int multidisc_len = list_multidisc_length();
    /* Allow one extra option for Close */
    if (current_choice > multidisc_len) {
        current_choice = multidisc_len;
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_accept_multidisc(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    const gd_item** list_multidisc = list_get_multidisc();
    int multidisc_len = list_multidisc_length();

    /* Close option is at index multidisc_len */
    if (current_choice == multidisc_len) {
        menu_leave();
        return;
    }

    if (!cb_multidisc) {
        dreamcast_launch_disc(list_multidisc[current_choice]);
    } else {
        dreamcast_launch_cb(list_multidisc[current_choice]);
    }
}

static void
menu_exit_prev(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    exit_menu_choice--;
    if (exit_menu_choice < 0) {
        exit_menu_choice = 0;
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_exit_next(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    exit_menu_choice++;
    if (exit_menu_choice >= exit_menu_num_options) {
        exit_menu_choice = exit_menu_num_options - 1;
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_exit_accept(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }

    EXIT_OPTION selected = exit_options[exit_menu_choice];

    switch (selected) {
        case EXIT_OPT_CLOSE:
            /* Just close the popup */
            menu_leave();
            break;

        case EXIT_OPT_EXIT_ONLY:
            /* Exit to BIOS without mounting disc or sending ID */
            exit_to_bios_ex(0, 0);
            break;

        case EXIT_OPT_MOUNT_ONLY:
            /* Mount disc and exit to BIOS (no ID sending) */
            exit_to_bios_ex(1, 0);
            break;

        case EXIT_OPT_SENDID_ONLY:
            /* Send game ID and exit to BIOS (no disc mounting) */
            exit_to_bios_ex(0, 1);
            break;

        case EXIT_OPT_SENDID_MOUNT:
            /* Send game ID + mount disc + exit to BIOS */
            exit_to_bios_ex(1, 1);
            break;

        default:
            break;
    }
}

void
cb_menu_setup(enum draw_state* state, theme_color* _colors, int* timeout_ptr, uint32_t title_color) {
    common_setup(state, _colors, timeout_ptr);
    menu_title_color = title_color;

    /* Reset selection to first option (Launch) */
    cb_menu_choice = 0;
}

static void
menu_cb_prev(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    cb_menu_choice--;
    if (cb_menu_choice < 0) {
        cb_menu_choice = 0;
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_cb_next(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    cb_menu_choice++;
    if (cb_menu_choice >= CB_MENU_NUM_OPTIONS) {
        cb_menu_choice = CB_MENU_NUM_OPTIONS - 1;
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_cb_accept(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }

    CB_OPTION selected = (CB_OPTION)cb_menu_choice;

    switch (selected) {
        case CB_OPT_LAUNCH:
            start_cb = 1;
            break;

        case CB_OPT_CLOSE:
            menu_leave();
            break;

        default:
            break;
    }
}

void
handle_input_menu(enum control input) {
    switch (input) {
        case LEFT: menu_choice_left(); break;
        case RIGHT: menu_choice_right(); break;
        case UP: menu_choice_prev(); break;
        case DOWN: menu_choice_next(); break;
        case START:
        case B: menu_leave(); break;
        case A: menu_accept(); break;
        default: break;
    }
}

void
handle_input_credits(enum control input) {
    switch (input) {
        case A:
        case B:
        case START: credits_leave(); break;
        default: break;
    }
}

void
handle_input_multidisc(enum control input) {
    switch (input) {
        case UP: menu_multidisc_prev(); break;
        case DOWN: menu_multidisc_next(); break;
        case B: menu_leave(); break;
        case A: menu_accept_multidisc(); break;
        default: break;
    }
}

void
handle_input_exit(enum control input) {
    /* All modes use navigable menu */
    switch (input) {
        case UP: menu_exit_prev(); break;
        case DOWN: menu_exit_next(); break;
        case B: menu_leave(); break;
        case A: menu_exit_accept(); break;
        default: break;
    }
}

void
handle_input_codebreaker(enum control input) {
    switch (input) {
        case UP: menu_cb_prev(); break;
        case DOWN: menu_cb_next(); break;
        case B: menu_leave(); break;
        case A: menu_cb_accept(); break;
        default: break;
    }
}

void
draw_menu_op(void) { /* might be useless */ }

static void
string_outer_concat(char* out, const char* left, const char* right, int len) {
    const int input_len = strlen(left) + strlen(right);
    strcpy(out, left);
    for (int i = 0; i < len - input_len; i++) {
        strcat(out, " ");
    }
    strcat(out, right);
}

static void
draw_popup_menu_ex(int x, int y, int width, int height, int ui_mode) {
    const int border_width = 2;
    draw_draw_quad(x - border_width, y - border_width, width + (2 * border_width), height + (2 * border_width),
                   menu_bkg_border_color);
    draw_draw_quad(x, y, width, height, menu_bkg_color);

    if (ui_mode == UI_SCROLL || ui_mode == UI_FOLDERS) {
        /* Top header */
        draw_draw_quad(x, y, width, 20, menu_bkg_border_color);
    }
}

void
draw_popup_menu(int x, int y, int width, int height) {
    draw_popup_menu_ex(x, y, width, height, sf_ui[0]);
}

void
draw_menu_tr(void) {
    z_set_cond(205.0f);
    if (sf_ui[0] == UI_SCROLL || sf_ui[0] == UI_FOLDERS) {
        /* Menu size and placement */
        const int line_height = 24;
        const int width = 320;
        /* Calculate visible options for height.
         * Extra rows after options: Save/Apply/Credits, spacing, GDEMU version, Build version = 4 rows
         * The +4 in the height formula accounts for these rows */
        int visible_options = MENU_OPTIONS - 1;  /* Hide BEEP */
        if (sf_ui[0] == UI_SCROLL) {
            visible_options -= 4;  /* Hide Aspect, FOLDERS_ART, FOLDERS_ITEM_DETAILS, CLOCK, MULTIDISC_GROUPING (5 items, -1 for padding) */
        } else if (sf_ui[0] == UI_FOLDERS) {
            visible_options -= 4;  /* Hide Aspect, Filter, SCROLL_ART, SCROLL_INDEX, DISC_DETAILS (5 items, -1 for padding) */
            /* Dynamically hide MULTIDISC_GROUPING when Multi-Disc is "Show All" */
            if (choices[CHOICE_MULTIDISC] == MULTIDISC_SHOW) {
                visible_options -= 1;
            }
        }
        /* Dynamically hide VM2_SEND_ALL when no VM2 devices detected */
        if (vm2_device_count == 0) {
            visible_options -= 1;
        }
        const int height = (visible_options + 4) * line_height + (line_height * 11 / 12);
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + 8;  /* 8px left margin */

        char line_buf[70];

        /* Draw a popup in the middle of the screen */
        draw_popup_menu(x, y, width, height);

        /* overlay our text on top with options */
        int cur_y = y + 2;
        font_bmp_begin_draw();
        font_bmp_set_color(menu_title_color);

        font_bmp_draw_main(width - (8 * 8 / 2), cur_y, "Settings");

        cur_y += line_height / 2;
        for (int i = 0; i < MENU_CHOICES; i++) {
            /* Skip SCROLL_ART option in non-Scroll modes */
            if (i == CHOICE_SCROLL_ART && sf_ui[0] != UI_SCROLL) {
                continue;
            }
            /* Skip SCROLL_INDEX option in non-Scroll modes */
            if (i == CHOICE_SCROLL_INDEX && sf_ui[0] != UI_SCROLL) {
                continue;
            }
            /* Skip DISC_DETAILS option in non-Scroll modes */
            if (i == CHOICE_DISC_DETAILS && sf_ui[0] != UI_SCROLL) {
                continue;
            }
            /* Skip MULTIDISC_GROUPING option in non-Folders modes or when Multi-Disc is "Show All" */
            if (i == CHOICE_MULTIDISC_GROUPING && (sf_ui[0] != UI_FOLDERS || choices[CHOICE_MULTIDISC] == MULTIDISC_SHOW)) {
                continue;
            }
            /* Skip FOLDERS_ART option in non-Folders modes */
            if (i == CHOICE_FOLDERS_ART && sf_ui[0] != UI_FOLDERS) {
                continue;
            }
            /* Skip FOLDERS_ITEM_DETAILS option in non-Folders modes */
            if (i == CHOICE_FOLDERS_ITEM_DETAILS && sf_ui[0] != UI_FOLDERS) {
                continue;
            }
            /* Skip MARQUEE_SPEED option in non-Scroll/Folders modes */
            if (i == CHOICE_MARQUEE_SPEED && sf_ui[0] != UI_SCROLL && sf_ui[0] != UI_FOLDERS) {
                continue;
            }
            /* Skip CLOCK option in non-Folders modes */
            if (i == CHOICE_CLOCK && sf_ui[0] != UI_FOLDERS) {
                continue;
            }
            /* Skip VM2_SEND_ALL option when no VM2 devices detected */
            if (i == CHOICE_VM2_SEND_ALL && vm2_device_count == 0) {
                continue;
            }
            /* Skip Aspect in Scroll mode (not used) */
            if (i == CHOICE_ASPECT && sf_ui[0] == UI_SCROLL) {
                continue;
            }
            /* Skip Aspect/Filter in Folders mode */
            if (sf_ui[0] == UI_FOLDERS && (i == CHOICE_ASPECT || i == CHOICE_FILTER)) {
                continue;
            }
            /* Skip BEEP option (disabled/commented out) */
            if (i == CHOICE_BEEP) {
                continue;
            }
            cur_y += line_height;
            if (i == current_choice) {
                font_bmp_set_color(highlight_color);
            } else {
                font_bmp_set_color(text_color);
            }
            if (i == CHOICE_REGION && (choices[i] >= REGION_CHOICES)) {
                string_outer_concat(line_buf, menu_choice_text[i], custom_theme_text[(int)choices[i] - REGION_CHOICES],
                                    38);
            } else if (i == CHOICE_SORT && sf_ui[0] == UI_FOLDERS) {
                /* In Folders mode, use Folders-specific sort text and clamp value */
                int sort_idx = choices[i] < SORT_CHOICES_FOLDERS ? choices[i] : 0;
                string_outer_concat(line_buf, menu_choice_text[i], sort_choice_text_folders[sort_idx], 38);
            } else {
                string_outer_concat(line_buf, menu_choice_text[i], menu_choice_array[i][(int)choices[i]], 38);
            }
            font_bmp_draw_main(x_item, cur_y, line_buf);
        }

        /* Draw Save/Apply/DC Now/Credits on one line */
        uint32_t save_color =
            ((current_choice == CHOICE_SAVE) && (choices[CHOICE_SAVE] == 0) ? highlight_color : text_color);
        uint32_t apply_color =
            ((current_choice == CHOICE_SAVE) && (choices[CHOICE_SAVE] == 1) ? highlight_color : text_color);
        uint32_t dcnow_color = (current_choice == CHOICE_DCNOW ? highlight_color : text_color);
        uint32_t credits_color = (current_choice == CHOICE_CREDITS ? highlight_color : text_color);
        cur_y += line_height;
        /* Save, Apply, DC NOW!, Credits across the bottom */
        font_bmp_set_color(save_color);
        font_bmp_draw_main(640 / 2 - (8 * 18), cur_y, save_choice_text[0]);
        font_bmp_set_color(apply_color);
        font_bmp_draw_main(640 / 2 - (8 * 7), cur_y, save_choice_text[1]);
        font_bmp_set_color(dcnow_color);
        font_bmp_draw_main(640 / 2 + (8 * 1), cur_y, "DC NOW!");
        font_bmp_set_color(credits_color);
        font_bmp_draw_main(640 / 2 + (8 * 11), cur_y, credits_text[0]);

        /* Add empty line for spacing */
        cur_y += line_height;

        /* Draw GDEMU firmware version (non-selectable) */
        uint8_t version_buffer[8] = {0};
        uint32_t version_size = 8;
        char version_str[40];
        if (gdemu_get_version(version_buffer, &version_size) == 0) {
            snprintf(version_str, sizeof(version_str), "GDEMU Firmware: %d.%02x.%d",
                     version_buffer[7], version_buffer[6], version_buffer[5]);
        } else {
            snprintf(version_str, sizeof(version_str), "GDEMU Firmware: N/A");
        }
        font_bmp_set_color(text_color);
        cur_y += line_height;
        /* Center based on actual string length (8 pixels per character) */
        int str_pixel_width = strlen(version_str) * 8;
        font_bmp_draw_main(640 / 2 - (str_pixel_width / 2), cur_y, version_str);

        /* Draw openMenu build version (non-selectable) */
        char build_str[80];
        snprintf(build_str, sizeof(build_str), "openMenu Build: %s", OPENMENU_BUILD_VERSION);
        cur_y += line_height;
        str_pixel_width = strlen(build_str) * 8;
        font_bmp_draw_main(640 / 2 - (str_pixel_width / 2), cur_y, build_str);

    } else {
        /* Menu size and placement (many options not shown in LineDesc/Grid3) */
        const int line_height = 32;
        const int width = 400;
        /* Exclude: BEEP, SCROLL_ART, SCROLL_INDEX, DISC_DETAILS, FOLDERS_ART, FOLDERS_ITEM_DETAILS, MARQUEE_SPEED, CLOCK, MULTIDISC_GROUPING (9 items) */
        int visible_options = MENU_OPTIONS - 9;
        /* Dynamically hide VM2_SEND_ALL when no VM2 devices detected */
        if (vm2_device_count == 0) {
            visible_options -= 1;
        }
        const int height = (visible_options + 3) * line_height - line_height / 4 + line_height; /* Add space for version strings */
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2); /* Vertically centered */
        const int x_item = x + 4;
        const int x_choice = 344 + 24 + 20 + 25; /* magic :( */

        /* Draw a popup in the middle of the screen */
        draw_popup_menu(x, y, width, height);

        /* overlay our text on top with options */
        int cur_y = y + 2;
        font_bmf_begin_draw();
        font_bmf_set_height_default();

        font_bmf_draw(x_item, cur_y, text_color, "Settings");

        cur_y += line_height / 4;
        for (int i = 0; i < MENU_CHOICES; i++) {
            /* Skip SCROLL_ART option in non-Scroll modes */
            if (i == CHOICE_SCROLL_ART && sf_ui[0] != UI_SCROLL) {
                continue;
            }
            /* Skip SCROLL_INDEX option in non-Scroll modes */
            if (i == CHOICE_SCROLL_INDEX && sf_ui[0] != UI_SCROLL) {
                continue;
            }
            /* Skip DISC_DETAILS option in non-Scroll modes */
            if (i == CHOICE_DISC_DETAILS && sf_ui[0] != UI_SCROLL) {
                continue;
            }
            /* Skip FOLDERS_ART option in non-Folders modes */
            if (i == CHOICE_FOLDERS_ART && sf_ui[0] != UI_FOLDERS) {
                continue;
            }
            /* Skip FOLDERS_ITEM_DETAILS option in non-Folders modes */
            if (i == CHOICE_FOLDERS_ITEM_DETAILS && sf_ui[0] != UI_FOLDERS) {
                continue;
            }
            /* Skip MARQUEE_SPEED option in non-Scroll/Folders modes */
            if (i == CHOICE_MARQUEE_SPEED && sf_ui[0] != UI_SCROLL && sf_ui[0] != UI_FOLDERS) {
                continue;
            }
            /* Skip CLOCK option in non-Folders modes */
            if (i == CHOICE_CLOCK && sf_ui[0] != UI_FOLDERS) {
                continue;
            }
            /* Skip VM2_SEND_ALL option when no VM2 devices detected */
            if (i == CHOICE_VM2_SEND_ALL && vm2_device_count == 0) {
                continue;
            }
            /* Skip MULTIDISC_GROUPING option in non-Folders modes or when Multi-Disc is "Show All" */
            if (i == CHOICE_MULTIDISC_GROUPING && (sf_ui[0] != UI_FOLDERS || choices[CHOICE_MULTIDISC] == MULTIDISC_SHOW)) {
                continue;
            }
            /* Skip Aspect/Sort/Filter in Folders mode */
            if (sf_ui[0] == UI_FOLDERS && (i == CHOICE_ASPECT || i == CHOICE_SORT || i == CHOICE_FILTER)) {
                continue;
            }
            /* Skip BEEP option (disabled/commented out) */
            if (i == CHOICE_BEEP) {
                continue;
            }
            cur_y += line_height;
            uint32_t temp_color = text_color;
            if (i == current_choice) {
                temp_color = highlight_color;
            }
            font_bmf_draw(x_item, cur_y, temp_color, menu_choice_text[i]);

            if (i == CHOICE_REGION && (choices[i] >= REGION_CHOICES)) {
                font_bmf_draw_centered(x_choice, cur_y, temp_color,
                                       custom_theme_text[(int)choices[i] - REGION_CHOICES]);
            } else {
                font_bmf_draw_centered(x_choice, cur_y, temp_color, menu_choice_array[i][(int)choices[i]]);
            }
        }

        /* Draw Save/Apply/DC Now/Credits on one line */
        uint32_t save_color =
            ((current_choice == CHOICE_SAVE) && (choices[CHOICE_SAVE] == 0) ? highlight_color : text_color);
        uint32_t apply_color =
            ((current_choice == CHOICE_SAVE) && (choices[CHOICE_SAVE] == 1) ? highlight_color : text_color);
        uint32_t dcnow_color = ((current_choice == CHOICE_DCNOW) ? highlight_color : text_color);
        uint32_t credits_color = ((current_choice == CHOICE_CREDITS) ? highlight_color : text_color);
        cur_y += line_height;
        font_bmf_draw_centered(640 / 2 - (width / 2) + 50, cur_y, save_color, save_choice_text[0]);
        font_bmf_draw_centered(640 / 2 - (width / 6), cur_y, apply_color, save_choice_text[1]);
        font_bmf_draw_centered(640 / 2 + (width / 6), cur_y, dcnow_color, "DC NOW!");
        font_bmf_draw_centered(640 / 2 + (width / 2) - 50, cur_y, credits_color, credits_text[0]);

        /* Add empty line for spacing */
        cur_y += line_height;

        /* Draw GDEMU firmware version (non-selectable, smaller font) */
        uint8_t version_buffer[8] = {0};
        uint32_t version_size = 8;
        char version_str[40];
        if (gdemu_get_version(version_buffer, &version_size) == 0) {
            snprintf(version_str, sizeof(version_str), "GDEMU  Firmware:  %d.%02x.%d",
                     version_buffer[7], version_buffer[6], version_buffer[5]);
        } else {
            snprintf(version_str, sizeof(version_str), "GDEMU  Firmware:  N/A");
        }
        cur_y += line_height / 2;
        font_bmf_set_height(20.0f);
        font_bmf_draw_centered(640 / 2, cur_y, text_color, version_str);

        /* Draw openMenu build version (non-selectable, smaller font) */
        char build_str[80];
        snprintf(build_str, sizeof(build_str), "openMenu  Build :  %s", OPENMENU_BUILD_VERSION);
        cur_y += line_height * 3 / 4;
        font_bmf_draw_centered(640 / 2, cur_y, text_color, build_str);

        font_bmf_set_height_default();
    }
}

void
draw_credits_op(void) { /* Again nothing... */ }

void
draw_credits_tr(void) {
    z_set_cond(205.0f);

    if (sf_ui[0] == UI_SCROLL || sf_ui[0] == UI_FOLDERS) {
        /* Menu size and placement */
        const int line_height = 24;
        const int width = 320;
        const int height = (num_credits + 1) * line_height + (line_height * 13 / 12);
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + 8;  /* 8px left margin */

        char line_buf[65];

        /* Draw a popup in the middle of the screen */
        draw_popup_menu(x, y, width, height);

        /* overlay our text on top with options */
        int cur_y = y + 4;
        font_bmp_begin_draw();
        font_bmp_set_color(sf_ui[0] == UI_FOLDERS ? menu_title_color : text_color);

        font_bmp_draw_main(width - (8 * 8 / 2), cur_y, "Credits");
        font_bmp_set_color(sf_ui[0] == UI_FOLDERS ? text_color : highlight_color);

        cur_y += line_height / 2;
        for (int i = 0; i < num_credits; i++) {
            cur_y += line_height;
            string_outer_concat(line_buf, credits[i].contributor, credits[i].role, 38);
            font_bmp_draw_main(x_item, cur_y, line_buf);
        }

    } else {
        /* Menu size and placement */
        const int line_height = 26;
        const int width = 560;
        const int height = (num_credits + 2) * line_height;
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_choice = 344 + 24 + 60; /* magic :( */
        const int x_item = x + 4;

        /* Draw a popup in the middle of the screen */
        draw_popup_menu(x, y, width, height);

        /* overlay our text on top with options */
        int cur_y = y + 2;
        font_bmf_begin_draw();
        font_bmf_set_height(24.0f);

        font_bmf_draw(x_item, cur_y, text_color, "Credits");

        cur_y += line_height / 4;
        for (int i = 0; i < num_credits; i++) {
            cur_y += line_height;
            font_bmf_draw(x_item, cur_y, highlight_color, credits[i].contributor);
            font_bmf_draw_centered(x_choice, cur_y, highlight_color, credits[i].role);
        }
    }
}

void
draw_multidisc_op(void) { /* Again nothing...Still... */ }

void
draw_multidisc_tr(void) {
    const gd_item** list_multidisc = list_get_multidisc();
    int multidisc_len = list_multidisc_length();

    z_set_cond(205.0f);
    if (sf_ui[0] == UI_SCROLL || sf_ui[0] == UI_FOLDERS) {
        /* Menu size and placement - width auto-sized based on disc labels */
        const int line_height = 24;
        const int title_gap = line_height / 2;
        const int title_width = 10 * 8;  /* "Multi-Disc" = 10 chars */
        const int padding = 16;  /* 8px margin on each side */
        const int max_name_chars = 35;  /* Maximum characters for game name */
        char line_buf[48];
        char temp_game_name[36];

        /* Find the longest disc label to determine popup width */
        int max_label_len = 0;
        for (int i = 0; i < multidisc_len; i++) {
            int name_len = strlen(list_multidisc[i]->name);
            if (name_len > max_name_chars) {
                name_len = max_name_chars;
            }
            /* Account for " #N" or " #NN" suffix (space + # + 1-2 digits) */
            int disc_num = gd_item_disc_num(list_multidisc[i]->disc);
            int suffix_len = (disc_num >= 10) ? 4 : 3;
            int label_len = name_len + suffix_len;
            if (label_len > max_label_len) {
                max_label_len = label_len;
            }
        }

        /* Width is the larger of title or max label, plus padding */
        const int content_width = max_label_len * 8;
        const int width = (content_width > title_width ? content_width : title_width) + padding;
        const int height = (multidisc_len + 2) * line_height + (line_height / 2) + title_gap;
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + (padding / 2);

        /* Draw a popup in the middle of the screen */
        draw_popup_menu(x, y, width, height);

        /* overlay our text on top with options */
        int cur_y = y + 2;
        font_bmp_begin_draw();
        font_bmp_set_color(sf_ui[0] == UI_FOLDERS ? menu_title_color : text_color);

        font_bmp_draw_main(x + width / 2 - (10 * 8 / 2), cur_y, "Multi-Disc");

        cur_y += title_gap;  /* Add spacing after title */
        for (int i = 0; i < multidisc_len; i++) {
            cur_y += line_height;
            if (i == current_choice) {
                font_bmp_set_color(highlight_color);
            } else {
                font_bmp_set_color(text_color);
            }
            const int disc_num = gd_item_disc_num(list_multidisc[i]->disc);
            strncpy(temp_game_name, list_multidisc[i]->name, sizeof(temp_game_name) - 1);
            temp_game_name[sizeof(temp_game_name) - 1] = '\0';
            /* Add ellipsis if name was truncated */
            if (strlen(list_multidisc[i]->name) >= sizeof(temp_game_name)) {
                strcpy(&temp_game_name[sizeof(temp_game_name) - 4], "...");
            }
            /* Format as "GameName #N" without fixed-width padding */
            snprintf(line_buf, sizeof(line_buf), "%s #%d", temp_game_name, disc_num);
            font_bmp_draw_main(x_item, cur_y, line_buf);
        }

        /* Close option */
        cur_y += line_height;
        font_bmp_set_color(current_choice == multidisc_len ? highlight_color : text_color);
        font_bmp_draw_main(x_item, cur_y, "Close");
    } else {
        /* Menu size and placement */
        const int line_height = 32;
        const int width = 300;
        const int height = (multidisc_len + 2) * line_height + (line_height / 2);
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + 4;
        char line_buf[65];
        char temp_game_name[62];

        /* Draw a popup in the middle of the screen */
        draw_popup_menu(x, y, width, height);

        /* overlay our text on top with options */
        int cur_y = y + 2;
        font_bmf_begin_draw();
        font_bmf_set_height_default();

        font_bmf_draw_centered(x + width / 2, cur_y, text_color, "Multi-Disc");

        cur_y += line_height / 4;

        for (int i = 0; i < multidisc_len; i++) {
            cur_y += line_height;
            uint32_t temp_color = text_color;
            if (i == current_choice) {
                temp_color = highlight_color;
            }
            const int disc_num = gd_item_disc_num(list_multidisc[i]->disc);
            strncpy(temp_game_name, list_multidisc[i]->name, sizeof(temp_game_name) - 1);
            temp_game_name[sizeof(temp_game_name) - 1] = '\0';
            /* Add ellipsis if name was truncated */
            if (strlen(list_multidisc[i]->name) >= sizeof(temp_game_name)) {
                strcpy(&temp_game_name[sizeof(temp_game_name) - 4], "...");
            }
            snprintf(line_buf, 69, "%s #%d", temp_game_name, disc_num);
            font_bmf_draw_auto_size(x_item, cur_y, temp_color, line_buf, width - 4);
        }

        /* Close option */
        cur_y += line_height;
        font_bmf_draw(x_item, cur_y, current_choice == multidisc_len ? highlight_color : text_color, "Close");
    }
}

void
draw_exit_op(void) { /* Again nothing...Still...Ugh... */ }

void
draw_exit_tr(void) {
    z_set_cond(205.0f);

    if (sf_ui[0] == UI_SCROLL || sf_ui[0] == UI_FOLDERS) {
        /* Menu size and placement - width calculated based on actual options */
        const int line_height = 24;
        const int title_gap = line_height / 2;
        const int padding = 16;  /* 8px margin on each side */
        const int title_width = 12 * 8;  /* "Exit to BIOS" = 12 chars */

        /* Find the longest option text in the current menu */
        int max_option_len = 0;
        for (int i = 0; i < exit_menu_num_options; i++) {
            int len = strlen(exit_option_text[exit_options[i]]);
            if (len > max_option_len) {
                max_option_len = len;
            }
        }

        /* Width is the larger of title or max option, plus padding */
        const int content_width = max_option_len * 8;
        const int width = (content_width > title_width ? content_width : title_width) + padding;
        const int height = (exit_menu_num_options + 1) * line_height + (line_height / 2) + title_gap;
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + (padding / 2);

        draw_popup_menu(x, y, width, height);

        int cur_y = y + 2;
        font_bmp_begin_draw();
        font_bmp_set_color(menu_title_color);

        font_bmp_draw_main(x + width / 2 - (12 * 8 / 2), cur_y, "Exit to BIOS");

        cur_y += title_gap;
        for (int i = 0; i < exit_menu_num_options; i++) {
            cur_y += line_height;
            if (i == exit_menu_choice) {
                font_bmp_set_color(highlight_color);
            } else {
                font_bmp_set_color(text_color);
            }
            font_bmp_draw_main(x_item, cur_y, exit_option_text[exit_options[i]]);
        }
    } else {
        /* LineDesc/Grid modes - dynamic menu with larger font */
        const int line_height = 32;
        const int title_gap = line_height / 4;
        const int padding = 20;

        /* Find the longest option text in the current menu */
        int max_option_len = 0;
        for (int i = 0; i < exit_menu_num_options; i++) {
            int len = strlen(exit_option_text[exit_options[i]]);
            if (len > max_option_len) {
                max_option_len = len;
            }
        }

        /* Estimate width based on font (roughly 10-12px per char for bmf font) */
        const int content_width = max_option_len * 10;
        const int title_width = 12 * 10;  /* "Exit to BIOS" */
        const int width = (content_width > title_width ? content_width : title_width) + padding;
        const int height = (exit_menu_num_options + 1) * line_height + (line_height / 2);
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + 10;

        draw_popup_menu(x, y, width, height);

        int cur_y = y + 2;
        font_bmf_begin_draw();
        font_bmf_set_height_default();

        font_bmf_draw_centered(x + width / 2, cur_y, text_color, "Exit to BIOS");

        cur_y += title_gap;
        for (int i = 0; i < exit_menu_num_options; i++) {
            cur_y += line_height;
            uint32_t temp_color = text_color;
            if (i == exit_menu_choice) {
                temp_color = highlight_color;
            }
            font_bmf_draw_auto_size(x_item, cur_y, temp_color, exit_option_text[exit_options[i]], width - 20);
        }
    }
}

void
draw_codebreaker_op(void) { /* Again nothing...Still...Ugh... */ }

void
draw_codebreaker_tr(void) {
    z_set_cond(205.0f);

    if (sf_ui[0] == UI_SCROLL || sf_ui[0] == UI_FOLDERS) {
        /* Menu size and placement - width calculated based on actual options */
        const int line_height = 24;
        const int title_gap = line_height / 2;
        const int padding = 16;  /* 8px margin on each side */
        const int title_width = 10 * 8;  /* "Use Cheats" = 10 chars */

        /* Find the longest option text */
        int max_option_len = 0;
        for (int i = 0; i < CB_MENU_NUM_OPTIONS; i++) {
            int len = strlen(cb_option_text[i]);
            if (len > max_option_len) {
                max_option_len = len;
            }
        }

        /* Width is the larger of title or max option, plus padding */
        const int content_width = max_option_len * 8;
        const int width = (content_width > title_width ? content_width : title_width) + padding;
        const int height = (CB_MENU_NUM_OPTIONS + 1) * line_height + (line_height / 2) + title_gap;
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + (padding / 2);

        draw_popup_menu(x, y, width, height);

        int cur_y = y + 2;
        font_bmp_begin_draw();
        font_bmp_set_color(menu_title_color);

        font_bmp_draw_main(x + width / 2 - (10 * 8 / 2), cur_y, "Use Cheats");

        cur_y += title_gap;
        for (int i = 0; i < CB_MENU_NUM_OPTIONS; i++) {
            cur_y += line_height;
            if (i == cb_menu_choice) {
                font_bmp_set_color(highlight_color);
            } else {
                font_bmp_set_color(text_color);
            }
            font_bmp_draw_main(x_item, cur_y, cb_option_text[i]);
        }
    } else {
        /* LineDesc/Grid modes - dynamic menu with larger font */
        const int line_height = 32;
        const int title_gap = line_height / 4;
        const int padding = 20;

        /* Find the longest option text */
        int max_option_len = 0;
        for (int i = 0; i < CB_MENU_NUM_OPTIONS; i++) {
            int len = strlen(cb_option_text[i]);
            if (len > max_option_len) {
                max_option_len = len;
            }
        }

        /* Estimate width based on font (roughly 10-12px per char for bmf font) */
        const int content_width = max_option_len * 10;
        const int title_width = 10 * 10;  /* "Use Cheats" */
        const int width = (content_width > title_width ? content_width : title_width) + padding;
        const int height = (CB_MENU_NUM_OPTIONS + 1) * line_height + (line_height / 2);
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + 10;

        draw_popup_menu(x, y, width, height);

        int cur_y = y + 2;
        font_bmf_begin_draw();
        font_bmf_set_height_default();

        font_bmf_draw_centered(x + width / 2, cur_y, text_color, "Use Cheats");

        cur_y += title_gap;
        for (int i = 0; i < CB_MENU_NUM_OPTIONS; i++) {
            cur_y += line_height;
            uint32_t temp_color = text_color;
            if (i == cb_menu_choice) {
                temp_color = highlight_color;
            }
            font_bmf_draw_auto_size(x_item, cur_y, temp_color, cb_option_text[i], width - 20);
        }
    }
}

/* PSX Launcher popup functions */
static void
menu_psx_launcher_prev(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    psx_launcher_choice--;
    if (psx_launcher_choice < 0) {
        psx_launcher_choice = 0;
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_psx_launcher_next(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    psx_launcher_choice++;
    if (psx_launcher_choice > 2) {
        psx_launcher_choice = 2;
    }
    *input_timeout_ptr = INPUT_TIMEOUT;
}

static void
menu_accept_psx_launcher(void) {
    if (*input_timeout_ptr > 0) {
        return;
    }
    if (psx_launcher_choice == 0) {
        bleem_launch(cur_game_item);
    } else if (psx_launcher_choice == 1) {
        bloom_launch(cur_game_item);
    } else {
        /* Close */
        menu_leave();
    }
}

void
handle_input_psx_launcher(enum control input) {
    switch (input) {
        case UP: menu_psx_launcher_prev(); break;
        case DOWN: menu_psx_launcher_next(); break;
        case B: menu_leave(); break;
        case A: menu_accept_psx_launcher(); break;
        default: break;
    }
}

void
draw_psx_launcher_op(void) { /* Nothing needed */ }

void
draw_psx_launcher_tr(void) {
    z_set_cond(205.0f);

    if (sf_ui[0] == UI_SCROLL || sf_ui[0] == UI_FOLDERS) {
        /* Menu size and placement - width based on title "PlayStation Launcher" (20 chars) */
        const int line_height = 24;
        const int title_gap = line_height / 2;
        const int padding = 16;  /* 8px margin on each side */
        const int width = 20 * 8 + padding;  /* 176 */
        const int height = 4 * line_height + (line_height / 2) + title_gap;
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + (padding / 2);

        draw_popup_menu(x, y, width, height);

        int cur_y = y + 2;
        font_bmp_begin_draw();
        font_bmp_set_color(sf_ui[0] == UI_FOLDERS ? menu_title_color : text_color);

        font_bmp_draw_main(x + width / 2 - (20 * 8 / 2), cur_y, "PlayStation Launcher");

        cur_y += title_gap;
        cur_y += line_height;
        font_bmp_set_color(psx_launcher_choice == 0 ? highlight_color : text_color);
        font_bmp_draw_main(x_item, cur_y, "Bleemcast!");

        cur_y += line_height;
        font_bmp_set_color(psx_launcher_choice == 1 ? highlight_color : text_color);
        font_bmp_draw_main(x_item, cur_y, "Bloom");

        cur_y += line_height;
        font_bmp_set_color(psx_launcher_choice == 2 ? highlight_color : text_color);
        font_bmp_draw_main(x_item, cur_y, "Close");
    } else {
        /* LineDesc/Grid modes - keep original sizing */
        const int line_height = 32;
        const int width = 200;
        const int height = 4 * line_height + (line_height / 2);
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + 10;

        draw_popup_menu(x, y, width, height);

        int cur_y = y + 2;
        font_bmf_begin_draw();
        font_bmf_set_height_default();

        font_bmf_draw_centered(x + width / 2, cur_y, text_color, "PlayStation Launcher");

        cur_y += line_height;
        font_bmf_draw(x_item, cur_y, psx_launcher_choice == 0 ? highlight_color : text_color, "Bleemcast!");

        cur_y += line_height;
        font_bmf_draw(x_item, cur_y, psx_launcher_choice == 1 ? highlight_color : text_color, "Bloom");

        cur_y += line_height;
        font_bmf_draw(x_item, cur_y, psx_launcher_choice == 2 ? highlight_color : text_color, "Close");
    }
}

#pragma region SaveLoad_Menu

/* ===== Save/Load Settings Window ===== */

/* Save/Load sub-states */
typedef enum SAVELOAD_STATE {
    SAVELOAD_BROWSE = 0,    /* Browsing device list */
    SAVELOAD_CONFIRM,       /* Confirming overwrite */
    SAVELOAD_BUSY,          /* Operation in progress */
    SAVELOAD_RESULT         /* Showing result message */
} SAVELOAD_STATE;

/* Save status for each device */
typedef enum SAVE_STATUS {
    SAVE_NONE = 0,          /* No save file, can create */
    SAVE_CURRENT,           /* Up-to-date save file */
    SAVE_OLD,               /* Older version, will upgrade */
    SAVE_INVALID,           /* Corrupt/invalid, must overwrite */
    SAVE_NO_SPACE,          /* No save, not enough space */
    SAVE_FUTURE             /* Save from newer program version */
} SAVE_STATUS;

/* Information about a VMU slot */
typedef struct vmu_slot_info {
    int8_t device_id;           /* Crayon device ID (0-7) */
    int8_t crayon_status;       /* Raw CRAYON_SF_STATUS_* value */
    SAVE_STATUS save_status;    /* Friendly status enum */
    int has_device;             /* 1 if device present */
    char type_name[12];         /* "VMU", "VM2", "VMUPro", "USB4MAPLE", "None" */
    int is_startup_source;      /* 1 if this is where settings were loaded at boot */
} vmu_slot_info;

/* Save/Load window state */
static vmu_slot_info saveload_slots[8];         /* All 8 VMU slots */
static int saveload_cursor = 0;                 /* Current cursor position in full list */
static int saveload_selected_device = -1;       /* Index of selected device for actions (-1 = none) */
static SAVELOAD_STATE saveload_substate = SAVELOAD_BROWSE;
static const char* saveload_msg_line1 = NULL;
static const char* saveload_msg_line2 = NULL;
static int saveload_pending_action = 0;         /* 0 = save, 1 = load (for confirm dialog) */
static int saveload_confirm_choice = 0;         /* 0 = Yes, 1 = No */
static int saveload_pending_upgrade = 0;        /* 1 if load will trigger upgrade */
static int saveload_original_ui_mode = -1;      /* UI mode when window opened (for consistent rendering) */

#define SAVELOAD_ACTION_SAVE 0
#define SAVELOAD_ACTION_LOAD 1
#define SAVELOAD_ACTION_CLOSE 2

/* Count number of selectable items (devices with VMU + 3 action buttons) */
static int saveload_get_selectable_count(void) {
    int count = 0;
    for (int i = 0; i < 8; i++) {
        if (saveload_slots[i].has_device) {
            count++;
        }
    }
    return count + 3;  /* +3 for Save/Load/Close buttons */
}

/* Get the device index for a cursor position, or -1 if cursor is on action buttons */
static int saveload_cursor_to_device_index(int cursor) {
    int device_count = 0;
    for (int i = 0; i < 8; i++) {
        if (saveload_slots[i].has_device) {
            if (device_count == cursor) {
                return i;
            }
            device_count++;
        }
    }
    return -1;  /* Cursor is on action buttons */
}

/* Get the action index for a cursor position (0=Save, 1=Load, 2=Close), or -1 if on device */
static int saveload_cursor_to_action(int cursor) {
    int device_count = 0;
    for (int i = 0; i < 8; i++) {
        if (saveload_slots[i].has_device) {
            device_count++;
        }
    }
    if (cursor >= device_count) {
        return cursor - device_count;
    }
    return -1;
}

/* Check if cursor is on a device (vs action button) */
static int saveload_cursor_on_device(void) {
    return saveload_cursor_to_device_index(saveload_cursor) >= 0;
}

/* Scan all VMU slots and update saveload_slots array */
static void saveload_scan_devices(void) {
    savefile_refresh_device_info();
    int8_t startup_dev = savefile_get_startup_device_id();

    for (int8_t i = 0; i < 8; i++) {
        vmu_slot_info* slot = &saveload_slots[i];
        slot->device_id = i;
        slot->is_startup_source = (i == startup_dev);

        int8_t status = savefile_get_device_status(i);
        slot->crayon_status = status;

        if (status == CRAYON_SF_STATUS_NO_DEVICE) {
            slot->has_device = 0;
            slot->save_status = SAVE_NONE;
            strcpy(slot->type_name, "None");
        } else {
            slot->has_device = 1;

            /* Get device type name via maple */
            int port = i / 2;
            int unit = (i % 2 == 0) ? 1 : 2;
            maple_device_t* dev = maple_enum_dev(port, unit);
            if (dev) {
                const char* type = get_vmu_type_name(dev);
                strncpy(slot->type_name, type, sizeof(slot->type_name) - 1);
                slot->type_name[sizeof(slot->type_name) - 1] = '\0';
            } else {
                strcpy(slot->type_name, "VMU");
            }

            /* Map crayon status to friendly status */
            switch (status) {
                case CRAYON_SF_STATUS_NO_SF_ROOM:
                    slot->save_status = SAVE_NONE;
                    break;
                case CRAYON_SF_STATUS_NO_SF_FULL:
                    slot->save_status = SAVE_NO_SPACE;
                    break;
                case CRAYON_SF_STATUS_CURRENT_SF:
                    slot->save_status = SAVE_CURRENT;
                    break;
                case CRAYON_SF_STATUS_OLD_SF_ROOM:
                case CRAYON_SF_STATUS_OLD_SF_FULL:
                    slot->save_status = SAVE_OLD;
                    break;
                case CRAYON_SF_STATUS_FUTURE_SF:
                    slot->save_status = SAVE_FUTURE;
                    break;
                case CRAYON_SF_STATUS_INVALID_SF:
                default:
                    slot->save_status = SAVE_INVALID;
                    break;
            }
        }
    }

    /* Adjust selected device if it's no longer valid */
    if (saveload_selected_device >= 0) {
        int idx = saveload_cursor_to_device_index(saveload_selected_device);
        if (idx < 0 || !saveload_slots[idx].has_device) {
            saveload_selected_device = -1;
        }
    }
}

/* Initialize saveload state - called from menu_accept when colors are already set */
static void saveload_init_state(void) {
    /* Save current UI mode for consistent rendering until window closes */
    saveload_original_ui_mode = sf_ui[0];

    /* Reset state */
    saveload_substate = SAVELOAD_BROWSE;
    saveload_cursor = 0;
    saveload_selected_device = -1;
    saveload_msg_line1 = NULL;
    saveload_msg_line2 = NULL;
    saveload_confirm_choice = 0;
    saveload_pending_action = 0;
    saveload_pending_upgrade = 0;

    saveload_scan_devices();

    /* Find first selectable device and set cursor there */
    for (int i = 0; i < 8; i++) {
        if (saveload_slots[i].has_device) {
            saveload_cursor = 0;
            break;
        }
    }
}

/* Apply current menu choices to sf_* settings variables */
static void saveload_apply_choices_to_settings(void) {
    sf_ui[0] = choices[CHOICE_THEME];
    sf_region[0] = choices[CHOICE_REGION];
    sf_aspect[0] = choices[CHOICE_ASPECT];
    sf_sort[0] = choices[CHOICE_SORT];
    sf_filter[0] = choices[CHOICE_FILTER];
    sf_beep[0] = choices[CHOICE_BEEP];
    sf_bios_3d[0] = choices[CHOICE_BIOS_3D];
    sf_multidisc[0] = choices[CHOICE_MULTIDISC];
    sf_multidisc_grouping[0] = choices[CHOICE_MULTIDISC_GROUPING];
    sf_scroll_art[0] = choices[CHOICE_SCROLL_ART];
    sf_scroll_index[0] = choices[CHOICE_SCROLL_INDEX];
    sf_disc_details[0] = choices[CHOICE_DISC_DETAILS];
    sf_folders_art[0] = choices[CHOICE_FOLDERS_ART];
    sf_folders_item_details[0] = choices[CHOICE_FOLDERS_ITEM_DETAILS];
    sf_marquee_speed[0] = choices[CHOICE_MARQUEE_SPEED];
    sf_clock[0] = choices[CHOICE_CLOCK];
    sf_vm2_send_all[0] = choices[CHOICE_VM2_SEND_ALL];
    sf_boot_mode[0] = choices[CHOICE_BOOT_MODE];
    sf_dcnow_vmu[0] = choices[CHOICE_DCNOW_VMU];

    /* Handle custom theme encoding */
    if (choices[CHOICE_THEME] != UI_SCROLL && choices[CHOICE_THEME] != UI_FOLDERS && sf_region[0] > REGION_END) {
        sf_custom_theme[0] = THEME_ON;
        int num_default_themes = 0;
        theme_get_default(sf_aspect[0], &num_default_themes);
        sf_custom_theme_num[0] = sf_region[0] - num_default_themes;
    } else if ((choices[CHOICE_THEME] == UI_SCROLL || choices[CHOICE_THEME] == UI_FOLDERS) && sf_region[0] > 0) {
        sf_custom_theme[0] = THEME_ON;
        sf_custom_theme_num[0] = sf_region[0] - 1;
    } else {
        sf_custom_theme[0] = THEME_OFF;
    }
}

/* Execute save operation */
static void saveload_do_save(void) {
    if (saveload_selected_device < 0) return;
    int dev_idx = saveload_cursor_to_device_index(saveload_selected_device);
    if (dev_idx < 0) return;

    vmu_slot_info* slot = &saveload_slots[dev_idx];

    saveload_substate = SAVELOAD_BUSY;
    saveload_msg_line1 = "Saving...";
    saveload_msg_line2 = NULL;

    /* Apply current menu choices to settings */
    saveload_apply_choices_to_settings();

    /* Perform save */
    int8_t result = savefile_save_to_device(slot->device_id);

    saveload_substate = SAVELOAD_RESULT;
    if (result == 0) {
        saveload_msg_line1 = "Settings saved successfully.";
        saveload_msg_line2 = NULL;
    } else {
        /* Check if it was a space issue */
        uint32_t needed = savefile_get_save_size_blocks();
        uint32_t available = savefile_get_device_free_blocks(slot->device_id);
        if (needed > available) {
            static char space_msg[48];
            snprintf(space_msg, sizeof(space_msg), "Need %lu blocks, only %lu available.", (unsigned long)needed, (unsigned long)available);
            saveload_msg_line1 = "Error: Not enough space on VMU.";
            saveload_msg_line2 = space_msg;
        } else {
            saveload_msg_line1 = "Error: Failed to save settings.";
            saveload_msg_line2 = NULL;
        }
    }

    /* Refresh device info */
    saveload_scan_devices();
}

/* Execute load operation */
static void saveload_do_load(void) {
    if (saveload_selected_device < 0) return;
    int dev_idx = saveload_cursor_to_device_index(saveload_selected_device);
    if (dev_idx < 0) return;

    vmu_slot_info* slot = &saveload_slots[dev_idx];

    saveload_substate = SAVELOAD_BUSY;
    saveload_msg_line1 = "Loading...";
    saveload_msg_line2 = NULL;

    int was_old = (slot->save_status == SAVE_OLD);

    /* Perform load */
    int8_t result = savefile_load_from_device(slot->device_id);
    printf("saveload_do_load: result=%d device=%d\n", result, slot->device_id);

    saveload_substate = SAVELOAD_RESULT;
    if (result == 0) {
        /* Success */
        if (was_old) {
            /* Auto-upgrade: save back to VMU */
            savefile_save_to_device(slot->device_id);
            saveload_msg_line1 = "Settings loaded and upgraded.";
        } else {
            saveload_msg_line1 = "Settings loaded successfully.";
        }
        saveload_msg_line2 = NULL;

        /* Show success icon on the target VMU */
        savefile_show_success_icon(slot->device_id);
    } else {
        if (slot->save_status == SAVE_INVALID) {
            saveload_msg_line1 = "Error: Save file is corrupt.";
            saveload_msg_line2 = "Save new settings to replace it.";
        } else if (slot->save_status == SAVE_FUTURE) {
            saveload_msg_line1 = "Error: Save from newer version.";
            saveload_msg_line2 = "Please update openMenu.";
        } else {
            saveload_msg_line1 = "Error: Failed to load settings.";
            saveload_msg_line2 = NULL;
        }
    }

    /* Refresh device info */
    saveload_scan_devices();
}

/* Close the Save/Load window and return to main UI */
static void saveload_close_all(int do_reload) {
    if (do_reload) {
        /* Apply loaded settings to sort/filter */
        if (!sf_filter[0]) {
            switch ((CFG_SORT)sf_sort[0]) {
                case SORT_NAME: list_set_sort_name(); break;
                case SORT_DATE: list_set_sort_region(); break;
                case SORT_PRODUCT: list_set_sort_genre(); break;
                case SORT_SD_CARD: list_set_sort_default(); break;
                default:
                case SORT_DEFAULT: list_set_sort_alphabetical(); break;
            }
        } else {
            list_set_genre_sort((FLAGS_GENRE)sf_filter[0] - 1, sf_sort[0]);
        }

        extern void reload_ui(void);
        reload_ui();
    }
    *state_ptr = DRAW_UI;
    *input_timeout_ptr = 3;
}

void
saveload_setup(enum draw_state* state, theme_color* _colors, int* timeout_ptr, uint32_t title_color) {
    common_setup(state, _colors, timeout_ptr);
    menu_title_color = title_color;

    /* Save current UI mode for consistent rendering until window closes */
    saveload_original_ui_mode = sf_ui[0];

    /* Reset state */
    saveload_substate = SAVELOAD_BROWSE;
    saveload_cursor = 0;
    saveload_selected_device = -1;
    saveload_msg_line1 = NULL;
    saveload_msg_line2 = NULL;
    saveload_confirm_choice = 0;
    saveload_pending_action = 0;
    saveload_pending_upgrade = 0;

    saveload_scan_devices();

    /* Find first selectable device and set cursor there */
    for (int i = 0; i < 8; i++) {
        if (saveload_slots[i].has_device) {
            saveload_cursor = 0;
            break;
        }
    }

    *state = DRAW_SAVELOAD;
}


void
handle_input_saveload(enum control input) {
    /* Handle based on sub-state */
    switch (saveload_substate) {
        case SAVELOAD_BUSY:
            /* No input during operation */
            return;

        case SAVELOAD_RESULT:
            /* Only A button to continue */
            if (input == A) {
                /* Check if this was a successful load - if so, close and reload UI */
                if (saveload_msg_line1 != NULL &&
                    (strstr(saveload_msg_line1, "loaded") != NULL)) {
                    saveload_close_all(1);  /* Close with UI reload */
                } else if (saveload_msg_line1 != NULL &&
                           strstr(saveload_msg_line1, "saved") != NULL) {
                    saveload_close_all(1);  /* Close with reload so saved settings take effect */
                } else {
                    /* Error - return to browse */
                    saveload_substate = SAVELOAD_BROWSE;
                    saveload_msg_line1 = NULL;
                    saveload_msg_line2 = NULL;
                }
                *input_timeout_ptr = INPUT_TIMEOUT;
            }
            return;

        case SAVELOAD_CONFIRM:
            /* Confirm dialog */
            switch (input) {
                case UP:
                case DOWN:
                    if (*input_timeout_ptr > 0) break;
                    saveload_confirm_choice = !saveload_confirm_choice;
                    *input_timeout_ptr = INPUT_TIMEOUT;
                    break;
                case A:
                    if (saveload_confirm_choice == 0) {
                        /* Yes - proceed with action */
                        if (saveload_pending_action == SAVELOAD_ACTION_SAVE) {
                            saveload_do_save();
                        } else {
                            saveload_do_load();
                        }
                    } else {
                        /* No - cancel and return to browse */
                        saveload_substate = SAVELOAD_BROWSE;
                    }
                    *input_timeout_ptr = INPUT_TIMEOUT;
                    break;
                case B:
                    /* Cancel */
                    saveload_substate = SAVELOAD_BROWSE;
                    *input_timeout_ptr = INPUT_TIMEOUT;
                    break;
                default:
                    break;
            }
            return;

        case SAVELOAD_BROWSE:
            /* Normal browsing */
            break;
    }

    /* Browse state input handling */
    int total_selectable = saveload_get_selectable_count();
    int device_count = total_selectable - 3;
    int close_idx = device_count + 2;  /* Close button index */

    switch (input) {
        case UP:
            if (*input_timeout_ptr > 0) break;
            if (saveload_cursor > 0) {
                int new_cursor = saveload_cursor - 1;
                /* Skip Save/Load buttons if no device selected */
                if (saveload_selected_device < 0 && new_cursor >= device_count && new_cursor < close_idx) {
                    new_cursor = device_count - 1;  /* Jump to last device */
                    if (new_cursor < 0) new_cursor = 0;
                }
                saveload_cursor = new_cursor;
                /* Do NOT auto-select device - user must press A to select */
            }
            *input_timeout_ptr = INPUT_TIMEOUT;
            break;

        case DOWN:
            if (*input_timeout_ptr > 0) break;
            if (saveload_cursor < total_selectable - 1) {
                int new_cursor = saveload_cursor + 1;
                /* Skip Save/Load buttons if no device selected */
                if (saveload_selected_device < 0 && new_cursor >= device_count && new_cursor < close_idx) {
                    new_cursor = close_idx;  /* Jump to Close */
                }
                saveload_cursor = new_cursor;
                /* Do NOT auto-select device - user must press A to select */
            }
            *input_timeout_ptr = INPUT_TIMEOUT;
            break;

        case A: {
            int action = saveload_cursor_to_action(saveload_cursor);
            if (action == SAVELOAD_ACTION_CLOSE) {
                /* Close */
                *state_ptr = DRAW_MENU;
                *input_timeout_ptr = 3;
            } else if (action == SAVELOAD_ACTION_SAVE) {
                /* Save - check if we need confirmation */
                if (saveload_selected_device < 0) {
                    /* No device selected - do nothing */
                    break;
                }
                int dev_idx = saveload_cursor_to_device_index(saveload_selected_device);
                if (dev_idx >= 0) {
                    vmu_slot_info* slot = &saveload_slots[dev_idx];
                    if (slot->save_status == SAVE_CURRENT || slot->save_status == SAVE_OLD ||
                        slot->save_status == SAVE_INVALID) {
                        /* Need confirmation to overwrite */
                        saveload_substate = SAVELOAD_CONFIRM;
                        saveload_pending_action = SAVELOAD_ACTION_SAVE;
                        saveload_confirm_choice = 0;
                        saveload_pending_upgrade = 0;
                    } else {
                        /* No existing save - proceed directly */
                        saveload_do_save();
                    }
                }
            } else if (action == SAVELOAD_ACTION_LOAD) {
                /* Load - check if we can load and need confirmation */
                if (saveload_selected_device < 0) {
                    /* No device selected - do nothing */
                    break;
                }
                int dev_idx = saveload_cursor_to_device_index(saveload_selected_device);
                if (dev_idx >= 0) {
                    vmu_slot_info* slot = &saveload_slots[dev_idx];
                    if (slot->save_status == SAVE_NONE || slot->save_status == SAVE_NO_SPACE) {
                        /* No save to load */
                        saveload_substate = SAVELOAD_RESULT;
                        saveload_msg_line1 = "Error: No save file on this VMU.";
                        saveload_msg_line2 = NULL;
                    } else if (slot->save_status == SAVE_FUTURE) {
                        saveload_substate = SAVELOAD_RESULT;
                        saveload_msg_line1 = "Error: Save from newer version.";
                        saveload_msg_line2 = "Please update openMenu.";
                    } else if (slot->save_status == SAVE_INVALID) {
                        saveload_substate = SAVELOAD_RESULT;
                        saveload_msg_line1 = "Error: Save file is corrupt.";
                        saveload_msg_line2 = "Save new settings to replace it.";
                    } else if (slot->save_status == SAVE_OLD) {
                        /* Old save - need confirmation for upgrade */
                        saveload_substate = SAVELOAD_CONFIRM;
                        saveload_pending_action = SAVELOAD_ACTION_LOAD;
                        saveload_confirm_choice = 0;
                        saveload_pending_upgrade = 1;
                    } else {
                        /* Current save - load directly */
                        saveload_do_load();
                    }
                }
            } else if (saveload_cursor_on_device()) {
                /* On a device - select it and move to Save button */
                saveload_selected_device = saveload_cursor;
                saveload_cursor = device_count;  /* Move to Save button */
            }
            *input_timeout_ptr = INPUT_TIMEOUT;
            break;
        }

        case B:
        case START:
            /* Return to Settings menu */
            *state_ptr = DRAW_MENU;
            *input_timeout_ptr = 3;
            break;

        default:
            break;
    }
}

void
draw_saveload_op(void) {
    /* Nothing to draw in opaque pass */
}

void
draw_saveload_tr(void) {
    z_set_cond(205.0f);

    /* Use the UI mode that was active when the window opened, not the current sf_ui[0].
     * This ensures consistent rendering even if a load operation changes the settings. */
    int ui_mode = (saveload_original_ui_mode >= 0) ? saveload_original_ui_mode : sf_ui[0];

    if (ui_mode == UI_SCROLL || ui_mode == UI_FOLDERS) {
        /* Scroll/Folders mode - bitmap font */
        const int line_height = 24;
        const int padding = 16;

        /* Calculate height based on content - matching Credits window formula */
        /* 4 ports * (header + 2 sockets) = 12 lines + 4 action area lines = 16 content lines */
        int content_lines = 0;
        for (int p = 0; p < 4; p++) {
            content_lines++;  /* Port header */
            content_lines += 2;  /* Two sockets */
        }

        /* Action area: all states use 4 lines for consistent window height */
        content_lines += 4;

        const int width = 304;
        /* Match Credits formula: (content + 1) * line_height + extra padding */
        const int height = (content_lines + 1) * line_height + (line_height * 13 / 12);
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + (padding / 2);

        draw_popup_menu_ex(x, y, width, height, ui_mode);

        int cur_y = y + 2;  /* Match Settings title position */
        font_bmp_begin_draw();
        font_bmp_set_color(menu_title_color);

        /* Title - centered */
        const char* title = "Save and Load Settings";
        font_bmp_draw_main(x + width / 2 - ((int)strlen(title) * 8 / 2), cur_y, title);

        cur_y += line_height / 2;

        /* Track cursor position for highlighting */
        int cursor_idx = 0;
        int device_count = 0;

        /* Count devices first */
        for (int i = 0; i < 8; i++) {
            if (saveload_slots[i].has_device) {
                device_count++;
            }
        }

        /* Draw ports and sockets */
        for (int p = 0; p < 4; p++) {
            cur_y += line_height;
            font_bmp_set_color(text_color);
            char port_label[16];
            snprintf(port_label, sizeof(port_label), "Port %c", 'A' + p);
            font_bmp_draw_main(x_item, cur_y, port_label);

            /* Socket 1 */
            cur_y += line_height;
            int slot_idx = p * 2;
            vmu_slot_info* slot = &saveload_slots[slot_idx];

            if (slot->has_device) {
                /* Determine if this is highlighted or selected */
                int is_cursor = (saveload_substate == SAVELOAD_BROWSE && cursor_idx == saveload_cursor);
                int is_selected = (!saveload_cursor_on_device() && cursor_idx == saveload_selected_device);

                if (is_cursor) {
                    font_bmp_set_color(highlight_color);
                } else {
                    font_bmp_set_color(text_color);
                }

                /* Build status string */
                char status_str[20];
                if (slot->is_startup_source && slot->save_status == SAVE_CURRENT) {
                    strcpy(status_str, "(loaded)");
                } else {
                    switch (slot->save_status) {
                        case SAVE_NONE: strcpy(status_str, "(no save)"); break;
                        case SAVE_CURRENT: strcpy(status_str, "(saved)"); break;
                        case SAVE_OLD: {
                            uint32_t ver = savefile_get_device_version(slot->device_id);
                            snprintf(status_str, sizeof(status_str), "(old v%lu)", (unsigned long)ver);
                            break;
                        }
                        case SAVE_INVALID: strcpy(status_str, "(invalid)"); break;
                        case SAVE_NO_SPACE: strcpy(status_str, "(full)"); break;
                        case SAVE_FUTURE: strcpy(status_str, "(future)"); break;
                        default: strcpy(status_str, ""); break;
                    }
                }

                char line[48];
                if (is_selected) {
                    snprintf(line, sizeof(line), "> Socket 1: %s %s", slot->type_name, status_str);
                } else {
                    snprintf(line, sizeof(line), "  Socket 1: %s %s", slot->type_name, status_str);
                }
                font_bmp_draw_main(x_item, cur_y, line);
                cursor_idx++;
            } else {
                font_bmp_set_color(text_color);
                font_bmp_draw_main(x_item, cur_y, "  Socket 1: None");
            }

            /* Socket 2 */
            cur_y += line_height;
            slot_idx = p * 2 + 1;
            slot = &saveload_slots[slot_idx];

            if (slot->has_device) {
                int is_cursor = (saveload_substate == SAVELOAD_BROWSE && cursor_idx == saveload_cursor);
                int is_selected = (!saveload_cursor_on_device() && cursor_idx == saveload_selected_device);

                if (is_cursor) {
                    font_bmp_set_color(highlight_color);
                } else {
                    font_bmp_set_color(text_color);
                }

                char status_str[20];
                if (slot->is_startup_source && slot->save_status == SAVE_CURRENT) {
                    strcpy(status_str, "(loaded)");
                } else {
                    switch (slot->save_status) {
                        case SAVE_NONE: strcpy(status_str, "(no save)"); break;
                        case SAVE_CURRENT: strcpy(status_str, "(saved)"); break;
                        case SAVE_OLD: {
                            uint32_t ver = savefile_get_device_version(slot->device_id);
                            snprintf(status_str, sizeof(status_str), "(old v%lu)", (unsigned long)ver);
                            break;
                        }
                        case SAVE_INVALID: strcpy(status_str, "(invalid)"); break;
                        case SAVE_NO_SPACE: strcpy(status_str, "(full)"); break;
                        case SAVE_FUTURE: strcpy(status_str, "(future)"); break;
                        default: strcpy(status_str, ""); break;
                    }
                }

                char line[48];
                if (is_selected) {
                    snprintf(line, sizeof(line), "> Socket 2: %s %s", slot->type_name, status_str);
                } else {
                    snprintf(line, sizeof(line), "  Socket 2: %s %s", slot->type_name, status_str);
                }
                font_bmp_draw_main(x_item, cur_y, line);
                cursor_idx++;
            } else {
                font_bmp_set_color(text_color);
                font_bmp_draw_main(x_item, cur_y, "  Socket 2: None");
            }
        }

        /* Spacing before action area */
        cur_y += line_height;

        /* Action area - all states use exactly 4 lines for consistent window height */
        if (saveload_substate == SAVELOAD_BUSY || saveload_substate == SAVELOAD_RESULT) {
            font_bmp_set_color(text_color);

            /* Line 1: Main message */
            cur_y += line_height;
            if (saveload_msg_line1) {
                font_bmp_draw_main(x_item, cur_y, saveload_msg_line1);
            }

            if (saveload_msg_line2) {
                /* With context: msg1 / msg2 / empty / Press A */
                cur_y += line_height;
                font_bmp_draw_main(x_item, cur_y, saveload_msg_line2);
                cur_y += line_height;  /* Empty separator */
                cur_y += line_height;
                if (saveload_substate == SAVELOAD_RESULT) {
                    font_bmp_draw_main(x_item, cur_y, "Press A to continue.");
                }
            } else {
                /* No context: msg1 / empty / Press A / empty */
                cur_y += line_height;  /* Empty separator */
                cur_y += line_height;
                if (saveload_substate == SAVELOAD_RESULT) {
                    font_bmp_draw_main(x_item, cur_y, "Press A to continue.");
                }
                cur_y += line_height;  /* Empty for consistent height */
            }
        } else if (saveload_substate == SAVELOAD_CONFIRM) {
            /* Layout: prompt / Yes / No / empty */
            font_bmp_set_color(text_color);

            /* Line 1: Prompt message */
            cur_y += line_height;
            if (saveload_pending_upgrade) {
                uint32_t old_ver = 0;
                if (saveload_selected_device >= 0) {
                    int dev_idx = saveload_cursor_to_device_index(saveload_selected_device);
                    if (dev_idx >= 0) {
                        old_ver = savefile_get_device_version(saveload_slots[dev_idx].device_id);
                    }
                }
                char upgrade_msg[48];
                snprintf(upgrade_msg, sizeof(upgrade_msg), "Load will upgrade old save (v%lu).", (unsigned long)old_ver);
                font_bmp_draw_main(x_item, cur_y, upgrade_msg);
            } else {
                font_bmp_draw_main(x_item, cur_y, "Overwrite existing save?");
            }

            /* Line 2: Yes */
            cur_y += line_height;
            font_bmp_set_color(saveload_confirm_choice == 0 ? highlight_color : text_color);
            font_bmp_draw_main(x_item, cur_y, "Yes");

            /* Line 3: No */
            cur_y += line_height;
            font_bmp_set_color(saveload_confirm_choice == 1 ? highlight_color : text_color);
            font_bmp_draw_main(x_item, cur_y, "No");

            /* Line 4: Empty for consistent height */
            cur_y += line_height;
        } else {
            /* BROWSE state - Layout: Save / Load / Close / empty */
            int action_start_idx = device_count;

            /* Line 1: Save to selected */
            cur_y += line_height;
            int is_save_cursor = (saveload_cursor == action_start_idx);
            int save_disabled = (saveload_selected_device < 0);
            if (is_save_cursor && !save_disabled) {
                font_bmp_set_color(highlight_color);
            } else {
                font_bmp_set_color(text_color);
            }
            font_bmp_draw_main(x_item, cur_y, "Save to selected");

            /* Line 2: Load from selected */
            cur_y += line_height;
            int is_load_cursor = (saveload_cursor == action_start_idx + 1);
            int load_disabled = (saveload_selected_device < 0);
            if (is_load_cursor && !load_disabled) {
                font_bmp_set_color(highlight_color);
            } else {
                font_bmp_set_color(text_color);
            }
            font_bmp_draw_main(x_item, cur_y, "Load from selected");

            /* Line 3: Close */
            cur_y += line_height;
            int is_close_cursor = (saveload_cursor == action_start_idx + 2);
            if (is_close_cursor) {
                font_bmp_set_color(highlight_color);
            } else {
                font_bmp_set_color(text_color);
            }
            font_bmp_draw_main(x_item, cur_y, "Close");

            /* Line 4: Empty for consistent height */
            cur_y += line_height;
        }
    } else {
        /* LineDesc/Grid mode - proportional font */
        const int line_height = 26;
        const int padding = 16;

        /* Calculate height based on content */
        /* 4 ports * (header + 2 sockets) = 12 lines + 4 action area lines = 16 content lines */
        int content_lines = 0;
        for (int p = 0; p < 4; p++) {
            content_lines++;  /* Port header */
            content_lines += 2;  /* Two sockets */
        }

        /* Action area: all states use 4 lines for consistent window height */
        content_lines += 4;

        const int width = 400;
        const int height = (content_lines + 2) * line_height;
        const int x = (640 / 2) - (width / 2);
        const int y = (480 / 2) - (height / 2);
        const int x_item = x + (padding / 2);

        draw_popup_menu_ex(x, y, width, height, ui_mode);

        int cur_y = y + 2;  /* Match Settings title position */
        font_bmf_begin_draw();
        font_bmf_set_height(24.0f);

        /* Title */
        font_bmf_draw(x_item, cur_y, text_color, "Save and Load Settings");

        cur_y += line_height / 4;

        /* Track cursor position for highlighting */
        int cursor_idx = 0;
        int device_count = 0;

        /* Count devices first */
        for (int i = 0; i < 8; i++) {
            if (saveload_slots[i].has_device) {
                device_count++;
            }
        }

        /* Draw ports and sockets */
        for (int p = 0; p < 4; p++) {
            cur_y += line_height;
            char port_label[16];
            snprintf(port_label, sizeof(port_label), "Port %c", 'A' + p);
            font_bmf_draw(x_item, cur_y, text_color, port_label);

            /* Socket 1 */
            cur_y += line_height;
            int slot_idx = p * 2;
            vmu_slot_info* slot = &saveload_slots[slot_idx];

            if (slot->has_device) {
                /* Determine if this is highlighted or selected */
                int is_cursor = (saveload_substate == SAVELOAD_BROWSE && cursor_idx == saveload_cursor);
                int is_selected = (!saveload_cursor_on_device() && cursor_idx == saveload_selected_device);

                uint32_t slot_color = is_cursor ? highlight_color : text_color;

                /* Build status string */
                char status_str[20];
                if (slot->is_startup_source && slot->save_status == SAVE_CURRENT) {
                    strcpy(status_str, "(loaded)");
                } else {
                    switch (slot->save_status) {
                        case SAVE_NONE: strcpy(status_str, "(no save)"); break;
                        case SAVE_CURRENT: strcpy(status_str, "(saved)"); break;
                        case SAVE_OLD: {
                            uint32_t ver = savefile_get_device_version(slot->device_id);
                            snprintf(status_str, sizeof(status_str), "(old v%lu)", (unsigned long)ver);
                            break;
                        }
                        case SAVE_INVALID: strcpy(status_str, "(invalid)"); break;
                        case SAVE_NO_SPACE: strcpy(status_str, "(full)"); break;
                        case SAVE_FUTURE: strcpy(status_str, "(future)"); break;
                        default: strcpy(status_str, ""); break;
                    }
                }

                char line[48];
                if (is_selected) {
                    snprintf(line, sizeof(line), "> Socket 1: %s %s", slot->type_name, status_str);
                } else {
                    snprintf(line, sizeof(line), "   Socket 1: %s %s", slot->type_name, status_str);
                }
                font_bmf_draw(x_item, cur_y, slot_color, line);
                cursor_idx++;
            } else {
                font_bmf_draw(x_item, cur_y, text_color, "   Socket 1: None");
            }

            /* Socket 2 */
            cur_y += line_height;
            slot_idx = p * 2 + 1;
            slot = &saveload_slots[slot_idx];

            if (slot->has_device) {
                int is_cursor = (saveload_substate == SAVELOAD_BROWSE && cursor_idx == saveload_cursor);
                int is_selected = (!saveload_cursor_on_device() && cursor_idx == saveload_selected_device);

                uint32_t slot_color = is_cursor ? highlight_color : text_color;

                char status_str[20];
                if (slot->is_startup_source && slot->save_status == SAVE_CURRENT) {
                    strcpy(status_str, "(loaded)");
                } else {
                    switch (slot->save_status) {
                        case SAVE_NONE: strcpy(status_str, "(no save)"); break;
                        case SAVE_CURRENT: strcpy(status_str, "(saved)"); break;
                        case SAVE_OLD: {
                            uint32_t ver = savefile_get_device_version(slot->device_id);
                            snprintf(status_str, sizeof(status_str), "(old v%lu)", (unsigned long)ver);
                            break;
                        }
                        case SAVE_INVALID: strcpy(status_str, "(invalid)"); break;
                        case SAVE_NO_SPACE: strcpy(status_str, "(full)"); break;
                        case SAVE_FUTURE: strcpy(status_str, "(future)"); break;
                        default: strcpy(status_str, ""); break;
                    }
                }

                char line[48];
                if (is_selected) {
                    snprintf(line, sizeof(line), "> Socket 2: %s %s", slot->type_name, status_str);
                } else {
                    snprintf(line, sizeof(line), "   Socket 2: %s %s", slot->type_name, status_str);
                }
                font_bmf_draw(x_item, cur_y, slot_color, line);
                cursor_idx++;
            } else {
                font_bmf_draw(x_item, cur_y, text_color, "   Socket 2: None");
            }
        }

        /* Spacing before action area */
        cur_y += line_height;

        /* Action area - all states use exactly 4 lines for consistent window height */
        if (saveload_substate == SAVELOAD_BUSY || saveload_substate == SAVELOAD_RESULT) {
            /* Line 1: Main message */
            cur_y += line_height;
            if (saveload_msg_line1) {
                font_bmf_draw(x_item, cur_y, text_color, saveload_msg_line1);
            }

            if (saveload_msg_line2) {
                /* With context: msg1 / msg2 / empty / Press A */
                cur_y += line_height;
                font_bmf_draw(x_item, cur_y, text_color, saveload_msg_line2);
                cur_y += line_height;  /* Empty separator */
                cur_y += line_height;
                if (saveload_substate == SAVELOAD_RESULT) {
                    font_bmf_draw(x_item, cur_y, text_color, "Press A to continue.");
                }
            } else {
                /* No context: msg1 / empty / Press A / empty */
                cur_y += line_height;  /* Empty separator */
                cur_y += line_height;
                if (saveload_substate == SAVELOAD_RESULT) {
                    font_bmf_draw(x_item, cur_y, text_color, "Press A to continue.");
                }
                cur_y += line_height;  /* Empty for consistent height */
            }
        } else if (saveload_substate == SAVELOAD_CONFIRM) {
            /* Layout: prompt / Yes / No / empty */
            /* Line 1: Prompt message */
            cur_y += line_height;
            if (saveload_pending_upgrade) {
                uint32_t old_ver = 0;
                if (saveload_selected_device >= 0) {
                    int dev_idx = saveload_cursor_to_device_index(saveload_selected_device);
                    if (dev_idx >= 0) {
                        old_ver = savefile_get_device_version(saveload_slots[dev_idx].device_id);
                    }
                }
                char upgrade_msg[48];
                snprintf(upgrade_msg, sizeof(upgrade_msg), "Load will upgrade old save (v%lu).", (unsigned long)old_ver);
                font_bmf_draw(x_item, cur_y, text_color, upgrade_msg);
            } else {
                font_bmf_draw(x_item, cur_y, text_color, "Overwrite existing save?");
            }

            /* Line 2: Yes */
            cur_y += line_height;
            font_bmf_draw(x_item, cur_y, saveload_confirm_choice == 0 ? highlight_color : text_color, "Yes");

            /* Line 3: No */
            cur_y += line_height;
            font_bmf_draw(x_item, cur_y, saveload_confirm_choice == 1 ? highlight_color : text_color, "No");

            /* Line 4: Empty for consistent height */
            cur_y += line_height;
        } else {
            /* BROWSE state - Layout: Save / Load / Close / empty */
            int action_start_idx = device_count;

            /* Line 1: Save to selected */
            cur_y += line_height;
            int is_save_cursor = (saveload_cursor == action_start_idx);
            int save_disabled = (saveload_selected_device < 0);
            uint32_t save_color = (is_save_cursor && !save_disabled) ? highlight_color : text_color;
            font_bmf_draw(x_item, cur_y, save_color, "Save to selected");

            /* Line 2: Load from selected */
            cur_y += line_height;
            int is_load_cursor = (saveload_cursor == action_start_idx + 1);
            int load_disabled = (saveload_selected_device < 0);
            uint32_t load_color = (is_load_cursor && !load_disabled) ? highlight_color : text_color;
            font_bmf_draw(x_item, cur_y, load_color, "Load from selected");

            /* Line 3: Close */
            cur_y += line_height;
            int is_close_cursor = (saveload_cursor == action_start_idx + 2);
            uint32_t close_color = is_close_cursor ? highlight_color : text_color;
            font_bmf_draw(x_item, cur_y, close_color, "Close");

            /* Line 4: Empty for consistent height */
            cur_y += line_height;
        }
    }
}

#pragma endregion SaveLoad_Menu
