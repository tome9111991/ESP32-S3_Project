// Wiederverwendbare Fullscreen-Tastatur.
// Maps liegen als static const im Flash (kein RAM-Footprint).
// Screen + Widget werden beim Oeffnen erzeugt und beim Schliessen
// async geloescht - kein Dauer-Heap.

#include "ui_keyboard.h"

#include "app_state.h"
#include "i18n.h"

#include "lvgl.h"

#include <stddef.h>

#define KB_X 0
#define KB_Y 120
#define KB_H 360

#define KB_CHAR(width) (LV_BUTTONMATRIX_CTRL_POPOVER | (width))
#define KB_CTRL(width) (LV_KEYBOARD_CTRL_BUTTON_FLAGS | (width))

static const char * const kb_map_lc[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_OK, "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_CLOSE, ""
};

static const lv_buttonmatrix_ctrl_t kb_ctrl_lc[] = {
    KB_CTRL(5), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CTRL(7),
    KB_CTRL(6), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CTRL(7),
    KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1),
    KB_CTRL(2), 10, KB_CTRL(2)
};

static const char * const kb_map_uc[] = {
    "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_OK, "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    LV_SYMBOL_CLOSE, " ", LV_SYMBOL_CLOSE, ""
};

static const lv_buttonmatrix_ctrl_t kb_ctrl_uc[] = {
    KB_CTRL(5), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CTRL(7),
    KB_CTRL(6), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CTRL(7),
    KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1),
    KB_CTRL(2), 10, KB_CTRL(2)
};

static const char * const kb_map_spec[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
    "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
    LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_CLOSE, ""
};

static const lv_buttonmatrix_ctrl_t kb_ctrl_spec[] = {
    KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CTRL(2),
    KB_CTRL(2), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1),
    KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1),
    KB_CTRL(2), 10, KB_CTRL(2)
};

static lv_obj_t *s_kb_screen     = NULL;
static lv_obj_t *s_kb_screen_ta  = NULL;
static lv_obj_t *s_kb_widget     = NULL;
static lv_obj_t *s_kb_target_ta  = NULL;
static lv_obj_t *s_kb_return_screen = NULL;

static void close_keyboard_screen(bool commit)
{
    if (!s_kb_screen) return;

    if (commit && s_kb_target_ta && s_kb_screen_ta) {
        const char *txt = lv_textarea_get_text(s_kb_screen_ta);
        lv_textarea_set_text(s_kb_target_ta, txt ? txt : "");
    }

    if (s_kb_return_screen) {
        lv_screen_load(s_kb_return_screen);
    }
    lv_obj_t *to_delete = s_kb_screen;
    s_kb_screen        = NULL;
    s_kb_screen_ta     = NULL;
    s_kb_widget        = NULL;
    s_kb_target_ta     = NULL;
    s_kb_return_screen = NULL;
    lv_obj_delete_async(to_delete);
}

static void on_kb_done_clicked(lv_event_t *e)
{
    (void)e;
    close_keyboard_screen(true);
}

static void on_kb_widget_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        close_keyboard_screen(true);
    } else if (code == LV_EVENT_CANCEL) {
        close_keyboard_screen(false);
    }
}

bool ui_keyboard_is_open(void)
{
    return s_kb_screen != NULL;
}

void ui_keyboard_open(lv_obj_t *target_ta,
                      const char *title,
                      bool password,
                      uint32_t max_len)
{
    if (s_kb_screen || !target_ta) return;

    s_kb_target_ta     = target_ta;
    s_kb_return_screen = lv_screen_active();

    s_kb_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_kb_screen);
    lv_obj_set_style_bg_color(s_kb_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_kb_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_kb_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Header: Titel links, Fertig-Button rechts.
    lv_obj_t *title_lbl = lv_label_create(s_kb_screen);
    lv_obj_remove_style_all(title_lbl);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_size(title_lbl, 500, 32);
    lv_obj_set_pos(title_lbl, 20, 12);
    lv_label_set_text(title_lbl, title ? title : "");

    lv_obj_t *done = lv_obj_create(s_kb_screen);
    lv_obj_remove_style_all(done);
    lv_obj_set_style_bg_color(done, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_set_style_bg_opa(done, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(done, 8, 0);
    lv_obj_remove_flag(done, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(done, 130, 36);
    lv_obj_set_pos(done, LCD_H_RES - 130 - 16, 10);
    lv_obj_add_flag(done, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(done, on_kb_done_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *done_lbl = lv_label_create(done);
    lv_obj_remove_style_all(done_lbl);
    lv_obj_set_style_text_font(done_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(done_lbl, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_text_align(done_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(done_lbl, 130, 32);
    lv_obj_set_pos(done_lbl, 0, 4);
    lv_label_set_text(done_lbl, T(KEYBOARD_DONE));
    lv_obj_add_flag(done_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Live-Editor unter dem Header.
    s_kb_screen_ta = lv_textarea_create(s_kb_screen);
    lv_obj_set_size(s_kb_screen_ta, LCD_H_RES - 40, 42);
    lv_obj_set_pos(s_kb_screen_ta, 20, 52);
    lv_obj_set_style_bg_color(s_kb_screen_ta, lv_color_hex(0x151b24), 0);
    lv_obj_set_style_bg_opa(s_kb_screen_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_kb_screen_ta, 2, 0);
    lv_obj_set_style_border_color(s_kb_screen_ta, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_set_style_radius(s_kb_screen_ta, 6, 0);
    lv_obj_set_style_pad_left(s_kb_screen_ta, 14, 0);
    lv_obj_set_style_pad_right(s_kb_screen_ta, 14, 0);
    lv_obj_set_style_text_font(s_kb_screen_ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_kb_screen_ta, lv_color_hex(COLOR_TEXT), 0);
    lv_textarea_set_one_line(s_kb_screen_ta, true);
    if (max_len > 0) {
        lv_textarea_set_max_length(s_kb_screen_ta, max_len);
    }
    (void)password; // Editor zeigt waehrend Eingabe immer Klartext.
    lv_textarea_set_password_mode(s_kb_screen_ta, false);
    lv_textarea_set_text(s_kb_screen_ta, lv_textarea_get_text(target_ta));
    lv_obj_add_state(s_kb_screen_ta, LV_STATE_FOCUSED);

    // Tastatur.
    s_kb_widget = lv_keyboard_create(s_kb_screen);
    lv_obj_remove_style_all(s_kb_widget);
    lv_obj_set_size(s_kb_widget, LCD_H_RES - (KB_X * 2), KB_H);
    lv_obj_align(s_kb_widget, LV_ALIGN_TOP_LEFT, KB_X, KB_Y);
    lv_keyboard_set_map(s_kb_widget, LV_KEYBOARD_MODE_TEXT_LOWER,
                        kb_map_lc, kb_ctrl_lc);
    lv_keyboard_set_map(s_kb_widget, LV_KEYBOARD_MODE_TEXT_UPPER,
                        kb_map_uc, kb_ctrl_uc);
    lv_keyboard_set_map(s_kb_widget, LV_KEYBOARD_MODE_SPECIAL,
                        kb_map_spec, kb_ctrl_spec);
    lv_keyboard_set_mode(s_kb_widget, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_popovers(s_kb_widget, false);
    lv_obj_set_style_bg_color(s_kb_widget, lv_color_hex(0x101720), 0);
    lv_obj_set_style_bg_opa(s_kb_widget, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_kb_widget, 0, 0);
    lv_obj_set_style_pad_top(s_kb_widget, 0, 0);
    lv_obj_set_style_pad_bottom(s_kb_widget, 0, 0);
    lv_obj_set_style_pad_left(s_kb_widget, 2, 0);
    lv_obj_set_style_pad_right(s_kb_widget, 2, 0);
    lv_obj_set_style_pad_row(s_kb_widget, 1, 0);
    lv_obj_set_style_pad_column(s_kb_widget, 2, 0);
    lv_obj_remove_flag(s_kb_widget, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(s_kb_widget, &lv_font_montserrat_30, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb_widget, lv_color_hex(0x18202b), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_kb_widget, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_kb_widget, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_kb_widget, lv_color_hex(COLOR_DIM), LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb_widget, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_opa(s_kb_widget, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_kb_widget, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(s_kb_widget, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_kb_widget, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_kb_widget, 1, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_kb_widget, 1, LV_PART_ITEMS);
    lv_keyboard_set_textarea(s_kb_widget, s_kb_screen_ta);
    lv_obj_add_event_cb(s_kb_widget, on_kb_widget_event, LV_EVENT_ALL, NULL);

    lv_screen_load(s_kb_screen);
}
