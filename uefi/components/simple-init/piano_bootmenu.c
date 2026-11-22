// SPDX-License-Identifier: LGPL-3.0-or-later
// Product menu presentation; OS/reset actions stay with the resident core.
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "confd.h"
#include "gui.h"
#include "gui/activity.h"
#include "gui/filepicker.h"
#include "gui/msgbox.h"
#include "piano_bootmenu.h"

static int compare_entries(const void *left, const void *right) {
    const char *a = *(const char *const *)left;
    const char *b = *(const char *const *)right;
    int64_t a_order = confd_get_integer_dict("boot.configs", a, "order", 1000);
    int64_t b_order = confd_get_integer_dict("boot.configs", b, "order", 1000);
    if (a_order != b_order) return a_order < b_order ? -1 : 1;
    return strcmp(a, b);
}

void piano_bootmenu_sort(char **names) {
    size_t count = 0;
    if (!names) return;
    while (names[count]) ++count;
    qsort(names, count, sizeof(*names), compare_entries);
}

static char selected_file[PATH_MAX];
static bool boot_file_pending;

static void start_selected_file(void *unused) {
    (void)unused;
    // The picker queues its back task after its callback. Defer one more
    // async slice so the new confirmation isn't mistaken for the picker.
    if (boot_file_pending) {
        boot_file_pending = false;
        lv_async_call(start_selected_file, NULL);
        return;
    }
    guiact_start_activity_by_name("uefi-app-start", selected_file);
}

static bool select_boot_file(bool ok, const char **paths, uint16_t count, void *unused) {
    (void)unused;
    bool selected = ok && count == 1 && paths && paths[0];
    if (selected) {
        size_t bytes = strlen(paths[0]);
        if (bytes < 4 || bytes >= sizeof(selected_file) || strcasecmp(paths[0] + bytes - 4, ".efi")) {
#ifdef SUNUEFI_NP05J
            msgbox_alert("Please select an EFI boot file (.efi).");
#else
            msgbox_alert("请选择 EFI 启动文件（.efi）。");
#endif
            free((void *)paths);
            return true;
        }
        memcpy(selected_file, paths[0], bytes + 1);
    }
    free((void *)paths);
    // Start on the next GUI slice, after the picker has scheduled its close.
    if (selected) { boot_file_pending = true; lv_async_call(start_selected_file, NULL); }
    return false;
}

static void pick_boot_file(void *unused) {
    (void)unused;
#ifdef SUNUEFI_NP05J
    struct filepicker *picker = filepicker_create(select_boot_file, "Boot From File");
#else
    struct filepicker *picker = filepicker_create(select_boot_file, "从文件启动");
#endif
    filepicker_set_max_item(picker, 1);
}

void piano_bootmenu_style_screen(lv_obj_t *scr, lv_obj_t *title, lv_obj_t *page, lv_obj_t *time, lv_obj_t *btn) {
#ifdef SUNUEFI_NP05J
    // ธีม Dark OLED สำหรับ REDMAGIC Astra: พื้นหลังดำลึก การ์ดคอนทราสต์สูง และปุ่ม Boot ขนาดเหมาะมือ
    lv_obj_t *root = lv_scr_act();
    if (root) {
        lv_obj_set_style_bg_color(root, lv_color_hex(0x0B0E14), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    }
    if (scr) {
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B0E14), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(scr, 36, 0);
        lv_obj_set_style_pad_row(scr, 16, 0);
    }
    if (title) {
        lv_obj_set_style_text_color(title, lv_color_hex(0xF5F7FA), 0);
        lv_obj_set_style_pad_ver(title, 4, 0);
        lv_obj_set_style_pad_hor(title, 8, 0);
        if (scr) {
            uint32_t idx = lv_obj_get_index(title);
            lv_obj_t *bar = lv_obj_create(scr);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(bar, 96, 6);
            lv_obj_set_style_bg_color(bar, lv_color_hex(0xE63946), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(bar, 0, 0);
            lv_obj_set_style_pad_all(bar, 0, 0);
            lv_obj_move_to_index(bar, (int32_t)(idx + 1));

            lv_obj_t *sub = lv_label_create(scr);
            lv_label_set_text(sub, "SM8750P  |  Tap an entry to select, tap again or press Boot");
            lv_obj_set_style_text_color(sub, lv_color_hex(0x8B949E), 0);
            if (gui_font_small) lv_obj_set_style_text_font(sub, gui_font_small, 0);
            lv_obj_set_style_pad_bottom(sub, 8, 0);
            lv_obj_move_to_index(sub, (int32_t)(idx + 2));
        }
    }
    if (page) {
        lv_obj_set_style_pad_all(page, 12, 0);
        lv_obj_set_style_pad_row(page, 20, 0);
        lv_obj_set_style_pad_column(page, 20, 0);
    }
    if (time) {
        lv_obj_set_style_text_color(time, lv_color_hex(0xFF6B6B), 0);
    }
    if (btn) {
        lv_obj_set_style_radius(btn, 24, 0);
        lv_obj_set_style_pad_ver(btn, 22, 0);
        lv_obj_set_style_pad_hor(btn, 36, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xE63946), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xC82333), LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A3140), LV_STATE_DISABLED);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0x6E7681), LV_STATE_DISABLED);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
    }
#else
    (void)scr; (void)title; (void)page; (void)time; (void)btn;
#endif
}

void piano_bootmenu_style_item(lv_obj_t *btn, lv_obj_t *title, lv_obj_t *chk) {
#ifdef SUNUEFI_NP05J
    if (btn) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x151B26), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x263042), 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_radius(btn, 22, 0);
        lv_obj_set_style_pad_ver(btn, 24, 0);
        lv_obj_set_style_pad_hor(btn, 22, 0);
        lv_obj_set_style_pad_column(btn, 18, 0);
        lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1D2638), LV_STATE_CHECKED);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xFF4D4F), LV_STATE_CHECKED);
        lv_obj_set_style_border_width(btn, 3, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1D2638), LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xFF4D4F), LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(btn, 3, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x253147), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xFF6B6B), LV_STATE_PRESSED);
        // ตัด software shadow blur เพื่อลดภาระ CPU ขณะวาดการ์ดและเลื่อนหน้าจอ
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, LV_STATE_PRESSED);
    }
    if (title) {
        lv_obj_set_style_text_color(title, lv_color_hex(0xEEF2F7), 0);
    }
    if (chk) {
        lv_obj_set_style_text_color(chk, lv_color_hex(0xFF4D4F), 0);
        lv_obj_set_style_img_recolor(chk, lv_color_hex(0xFF4D4F), 0);
        lv_obj_set_style_img_recolor_opa(chk, LV_OPA_COVER, 0);
    }
#else
    (void)btn; (void)title; (void)chk;
#endif
}

void piano_bootmenu_enter(void *data) {
    char *activity = data;
    gui_screen_init();
    gui_draw();
    if (activity && *activity) {
        if (!strcmp(activity, "boot-file")) lv_async_call(pick_boot_file, NULL);
        else guiact_start_activity_by_name(activity, NULL);
    }
    free(activity);
}
