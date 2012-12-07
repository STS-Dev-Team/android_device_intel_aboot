/*************************************************************************
 * Copyright(c) 2011 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * **************************************************************************/

#ifndef UI_H
#define UI_H

#include "logd.h"

#define MAX_COLS		60
#define MAX_ROWS		34

#define BLANK_SIZE		1
#define TITLE_TOP		0
#define TITLE_MAX		1
#define INFO_TOP		(TITLE_TOP + TITLE_MAX)
#define INFO_MAX		10
#define MENU_TOP		(INFO_TOP + INFO_MAX + BLANK_SIZE)
#define MENU_MAX		4
#define MSG_TOP			(MAX_ROWS - MSG_MAX)
#define MSG_MAX			1
#define LOG_TOP			(MSG_TOP - LOG_MAX)
#define LOG_MAX			10

#define CHAR_WIDTH		10
#define CHAR_HEIGHT		30

//bootloader mode
enum {
	MODE_COS,
	MODE_POS
};

//icons
enum {
  BACKGROUND_ICON_NONE,
  BACKGROUND_ICON_BACKGROUND,
  BACKGROUND_ICON_ERR,
  BACKGROUND_ICON_BAT00,
  BACKGROUND_ICON_BAT13,
  BACKGROUND_ICON_BAT25,
  BACKGROUND_ICON_BAT38,
  BACKGROUND_ICON_BAT50,
  BACKGROUND_ICON_BAT63,
  BACKGROUND_ICON_BAT75,
  BACKGROUND_ICON_BAT88,
  BACKGROUND_ICON_BAT100,
  BACKGROUND_ICON_FULL,
  NUM_BACKGROUND_ICONS
};

//UI block types
enum {
	TITLE,
	INFO,
	MENU,
	LOG,
	MSG,
	BLOCK_NUM
};

//Show or hidden UI block
enum {
	HIDDEN,
	VISIBLE
};

//UI messge types
enum {
	ALERT,
	TIPS
};

//UI messge alignment type
enum {
	LEFT,
	MIDDLE
};

struct color {
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
};

struct ui_block {
	int type;
	int top;
	int rows;
	int show;
	struct color** clr_table;
	char (*text_table) [MAX_COLS];
};

#if 1
#define LOGE(format, ...) \
    do { \
        ui_print("E:" format, ##__VA_ARGS__); \
        __libc_android_log_print(ANDROID_LOG_ERROR, "FASTBOOT", (format), ##__VA_ARGS__ ); \
    } while (0)
#define LOGW(format, ...) \
    __libc_android_log_print(ANDROID_LOG_WARN, "FASTBOOT", (format), ##__VA_ARGS__ )
#define LOGI(format, ...) \
    __libc_android_log_print(ANDROID_LOG_INFO, "FASTBOOT", (format), ##__VA_ARGS__ )
#define LOGV(format, ...) \
    __libc_android_log_print(ANDROID_LOG_VERBOSE, "FASTBOOT", (format), ##__VA_ARGS__ )
#define LOGD(format, ...) \
    __libc_android_log_print(ANDROID_LOG_DEBUG, "FASTBOOT", (format), ##__VA_ARGS__ )
#else
#define LOGE(...) ui_print("E:" __VA_ARGS__)
#define LOGW(...) fprintf(stdout, "W:" __VA_ARGS__)
#define LOGI(...) fprintf(stdout, "I:" __VA_ARGS__)
#define LOGV(...) do {} while (0)
#define LOGD(...) do {} while (0)
#endif

void ui_set_screen_state(int state);
int ui_get_screen_state(void);
void ui_set_screensaver_delay(int delay);
void ui_restore_screensaver_delay();
void ui_set_background(int icon);
void ui_block_init(int type, char **titles, struct color **clrs);
void ui_block_show(int type);
void ui_block_hide(int type);
int ui_block_visible(int type);
void ui_start_process_bar();
void ui_stop_process_bar();
void ui_show_process(int show);
void ui_print(const char *fmt, ...);
void ui_msg(int type, int align, const char *fmt, ...);
void ui_start_menu(char** items, int initial_selection);
int ui_menu_select(int sel);
int ui_wait_key();
int ui_key_pressed(int key);
void ui_clear_key_queue();
void ui_init();
void ui_exit();

#endif /* UI_H */
