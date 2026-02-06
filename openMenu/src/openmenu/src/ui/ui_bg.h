/*
 * File: ui_bg.h
 * Project: ui
 * -----
 * Shared background layer drawing for all UI modes.
 * All 4 UI modules use the same 512+128 px split background.
 */

#pragma once

#include "draw_prototypes.h"

void ui_bg_set(image* left, image* right);
void ui_bg_draw(void);
