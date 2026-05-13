#pragma once

// Wiederverwendbarer Fullscreen-Eingabescreen mit LVGL-Tastatur.
//
// Pattern: Aufrufer registriert LV_EVENT_CLICKED auf seine Textarea
// und ruft daraus ui_keyboard_open() auf. Das Modul oeffnet einen
// eigenen Screen mit Titel, gespiegelter Textarea und Tastatur,
// uebernimmt bei "Fertig" den Wert in die uebergebene Textarea und
// laedt den vorher aktiven Screen wieder.
//
// Die Layout-Maps und der gesamte KB-State liegen als 'static' im
// Modul; der Screen wird beim Schliessen async geloescht. Es gibt
// immer hoechstens eine offene Tastatur.

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Oeffnet die Tastatur fuer 'target_ta'. Bei "Fertig" / LV_EVENT_READY
// wird der eingegebene Text in target_ta uebernommen und der zuvor
// aktive Screen wieder geladen. Bei Abbrechen (X) bleibt target_ta
// unveraendert.
//
//   target_ta : LVGL-Textarea, die das Ergebnis erhaelt (Pflicht).
//   title     : Ueberschrift im Eingabescreen.
//   password  : true -> Editor erlaubt mehr Zeichen (Password-Laenge),
//               Anzeige im Editor bleibt im Klartext.
//   max_len   : max. Zeichen im Editor; 0 = kein Limit.
//
// Mehrfachaufrufe waehrend die Tastatur schon offen ist sind no-ops.
void ui_keyboard_open(lv_obj_t *target_ta,
                      const char *title,
                      bool password,
                      uint32_t max_len);

// true, solange der KB-Screen aktiv ist.
bool ui_keyboard_is_open(void);

#ifdef __cplusplus
}
#endif
