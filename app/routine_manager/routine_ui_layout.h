/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/routine_ui_layout.h
 ****************************************************************************/

#ifndef __ROUTINE_UI_LAYOUT_H
#define __ROUTINE_UI_LAYOUT_H

#define ROUTINE_UI_SCREEN_WIDTH          480
#define ROUTINE_UI_SCREEN_HEIGHT         320
#define ROUTINE_UI_HEADER_HEIGHT          42
#define ROUTINE_UI_BODY_HEIGHT           278
#define ROUTINE_UI_NAV_BUTTON_Y            4
#define ROUTINE_UI_NAV_BUTTON_WIDTH       44
#define ROUTINE_UI_NAV_BUTTON_HEIGHT      32
#define ROUTINE_UI_NAV_PREV_X             270
#define ROUTINE_UI_NAV_NEXT_X             424
#define ROUTINE_UI_CARD_LEFT_X              6
#define ROUTINE_UI_CARD_RIGHT_X           242
#define ROUTINE_UI_CARD_FIRST_Y             4
#define ROUTINE_UI_CARD_SECOND_Y           99
#define ROUTINE_UI_CARD_WIDTH             228
#define ROUTINE_UI_CARD_HEIGHT             89
#define ROUTINE_UI_NAV_CARD_GAP             10
#define ROUTINE_UI_VOICE_Y                194
#define ROUTINE_UI_VOICE_HEIGHT            78
#define ROUTINE_UI_CHART_Y                  4
#define ROUTINE_UI_CHART_HEIGHT           136
#define ROUTINE_UI_SCHEDULE_Y             144
#define ROUTINE_UI_SCHEDULE_HEIGHT        128

_Static_assert(ROUTINE_UI_HEADER_HEIGHT + ROUTINE_UI_BODY_HEIGHT ==
               ROUTINE_UI_SCREEN_HEIGHT, "header and body must fill screen");
_Static_assert(ROUTINE_UI_NAV_BUTTON_WIDTH >= 44 &&
               ROUTINE_UI_NAV_BUTTON_HEIGHT >= 32,
               "navigation touch target is too small");
_Static_assert(ROUTINE_UI_HEADER_HEIGHT + ROUTINE_UI_CARD_FIRST_Y -
               (ROUTINE_UI_NAV_BUTTON_Y + ROUTINE_UI_NAV_BUTTON_HEIGHT) >=
               ROUTINE_UI_NAV_CARD_GAP,
               "navigation and first card are too close");
_Static_assert(ROUTINE_UI_VOICE_Y + ROUTINE_UI_VOICE_HEIGHT <=
               ROUTINE_UI_BODY_HEIGHT, "sensor page exceeds body");
_Static_assert(ROUTINE_UI_SCHEDULE_Y + ROUTINE_UI_SCHEDULE_HEIGHT <=
               ROUTINE_UI_BODY_HEIGHT, "history page exceeds body");

#endif /* __ROUTINE_UI_LAYOUT_H */
