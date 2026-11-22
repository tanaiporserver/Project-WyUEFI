// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "lvgl.h"

void piano_bootmenu_sort(char **names);
// Takes ownership of the optional activity string returned by confd.
void piano_bootmenu_enter(void *activity);
void piano_bootmenu_style_screen(lv_obj_t *scr, lv_obj_t *title, lv_obj_t *page, lv_obj_t *time, lv_obj_t *btn);
void piano_bootmenu_style_item(lv_obj_t *btn, lv_obj_t *title, lv_obj_t *chk);
