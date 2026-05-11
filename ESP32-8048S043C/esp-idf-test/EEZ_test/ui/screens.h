#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    _SCREEN_ID_LAST = 1
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *bar1;
    lv_obj_t *lbl_wetterstation;
    lv_obj_t *clock_wetter;
    lv_obj_t *bar_sec;
    lv_obj_t *weather_icon;
    lv_obj_t *lbl_clock;
    lv_obj_t *statussonne;
    lv_obj_t *lbl_datum_tag;
    lv_obj_t *lbl_temp;
} objects_t;

extern objects_t objects;

void create_screen_main();
void tick_screen_main();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/
