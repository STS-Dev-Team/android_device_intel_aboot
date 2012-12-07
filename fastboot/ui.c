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

#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>

#include "minui.h"
#include "common.h"

//color table, BGRA format
struct color white = {223, 215, 200, 255};
struct color black = {0, 0, 0, 255};
struct color black_tr = {0, 0, 0, 160};
struct color red = {255, 30, 0, 255};
struct color green = {0, 191, 255, 255};
struct color yellow = {255, 215, 0, 255};
struct color brown = {128, 42, 42, 255};
struct color gray = {150, 150, 150, 255};

static struct color* title_clr[TITLE_MAX];
static struct color* info_clr[INFO_MAX];
static struct color* menu_clr[MENU_MAX];
static struct color* log_clr[LOG_MAX];
static struct color* msg_clr[MSG_MAX];

static char title[TITLE_MAX][MAX_COLS] = {{'\0'}};
static char info[INFO_MAX][MAX_COLS] = {{'\0'}};
static char menu[MENU_MAX][MAX_COLS] = {{'\0'}};
static char log[LOG_MAX][MAX_COLS] = {{'\0'}};
static char msg[MSG_MAX][MAX_COLS] = {{'\0'}};

static struct ui_block UI_BLOCK[BLOCK_NUM] = {
	[TITLE] = {
		.type       = TITLE,
		.top        = TITLE_TOP,
		.rows       = TITLE_MAX,
		.show       = HIDDEN,
		.clr_table  = title_clr,
		.text_table = title,
	},
	[INFO] = {
		.type       = INFO,
		.top        = INFO_TOP,
		.rows       = INFO_MAX,
		.show       = HIDDEN,
		.clr_table  = info_clr,
		.text_table = info,
	},
	[MENU] = {
		.type       = MENU,
		.top        = MENU_TOP,
		.rows       = MENU_MAX,
		.show       = HIDDEN,
		.clr_table  = menu_clr,
		.text_table = menu,
	},
	[LOG] = {
		.type       = LOG,
		.top        = LOG_TOP,
		.rows       = LOG_MAX,
		.show       = HIDDEN,
		.clr_table  = log_clr,
		.text_table = log,
	},
	[MSG] = {
		.type       = MSG,
		.top        = MSG_TOP,
		.rows       = MSG_MAX,
		.show       = HIDDEN,
		.clr_table  = msg_clr,
		.text_table = msg,
	}
};

#define PROGRESSBAR_INDETERMINATE_STATES 6

static int fb_width, fb_height;
static int log_row, log_col, log_top;
static int menu_items = 0, menu_sel = 0;

static struct color* title_dclr = &brown;
static struct color* info_dclr = &white;
static struct color* menu_dclr = &green;
static struct color* log_dclr = &gray;
static struct color* msg_dclr = &green;
static struct color* menu_sclr = &yellow;

static gr_surface gCurrentIcon = NULL;
static pthread_mutex_t gUpdateMutex = PTHREAD_MUTEX_INITIALIZER;
static gr_surface gBackgroundIcon[NUM_BACKGROUND_ICONS];
static gr_surface gProgressBarIndeterminate[PROGRESSBAR_INDETERMINATE_STATES];
static int show_process = 0;
static int process_frame = 0;
static volatile char process_update = 0;

static const struct { gr_surface* surface; const char *name; } BITMAPS[] = {
    { &gBackgroundIcon[BACKGROUND_ICON_BACKGROUND], "background" },
    { &gBackgroundIcon[BACKGROUND_ICON_ERR],        "bat_err" },
    { &gBackgroundIcon[BACKGROUND_ICON_BAT00],      "bat_00" },
    { &gBackgroundIcon[BACKGROUND_ICON_BAT13],      "bat_13" },
    { &gBackgroundIcon[BACKGROUND_ICON_BAT25],      "bat_25" },
    { &gBackgroundIcon[BACKGROUND_ICON_BAT38],      "bat_38" },
    { &gBackgroundIcon[BACKGROUND_ICON_BAT50],      "bat_50" },
    { &gBackgroundIcon[BACKGROUND_ICON_BAT63],      "bat_63" },
    { &gBackgroundIcon[BACKGROUND_ICON_BAT75],      "bat_75" },
    { &gBackgroundIcon[BACKGROUND_ICON_BAT88],      "bat_88" },
    { &gBackgroundIcon[BACKGROUND_ICON_BAT100],     "bat_100" },
    { &gBackgroundIcon[BACKGROUND_ICON_FULL],       "bat_full" },
    { &gProgressBarIndeterminate[0],    "indeterminate01" },
    { &gProgressBarIndeterminate[1],    "indeterminate02" },
    { &gProgressBarIndeterminate[2],    "indeterminate03" },
    { &gProgressBarIndeterminate[3],    "indeterminate04" },
    { &gProgressBarIndeterminate[4],    "indeterminate05" },
    { &gProgressBarIndeterminate[5],    "indeterminate06" },
    { NULL,                             				NULL },
};

// Key event input queue
static pthread_mutex_t key_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t key_queue_cond = PTHREAD_COND_INITIALIZER;
static int key_queue[256], key_queue_len = 0;
static volatile char key_pressed[KEY_MAX + 1];

#define SCREENSAVER_DELAY 30000
#define BRIGHTNESS_DELAY 100
#define TARGET_BRIGHTNESS 40
#define SYSFS_BACKLIGHT "/sys/devices/virtual/backlight"

static ui_timer_t *gScreenSaverTimer;
static ui_timer_t *gBrightnessTimer;
static ui_timer_t *gProgressTimer;
static int gCurBrightness = TARGET_BRIGHTNESS;
static char gBrightnessPath[255];
static int gScreenState = 1;
static int gTargetScreenState = 1;
static int default_screensaver_delay = SCREENSAVER_DELAY;

/**** screen state, and screen saver   *****/

extern int set_screen_state(int);
extern int acquire_wake_lock(int, const char*);

static int set_back_brightness_timer(void*data)
{
	int ret = TIMER_STOP;
	char buf[32];
	FILE *file;
	struct dirent *entry;
	DIR *dir;
	int val;
	const char *name;
	char *path = gBrightnessPath;
	if(path[0]==0) {
		dir = opendir(SYSFS_BACKLIGHT);
		if (dir == NULL) {
			ui_print("Could not open %s\n", SYSFS_BACKLIGHT);
			return TIMER_STOP;
		}
		while ((entry = readdir(dir))) {
			name = entry->d_name;
			snprintf(path, 255, "%s/%s/brightness", SYSFS_BACKLIGHT, name);
			if (access(path, 0) == 0) {
				break;
			}
		}
		closedir(dir);
	}
	val = gCurBrightness;
	if(gTargetScreenState && gCurBrightness < TARGET_BRIGHTNESS)
		val += 10;
	if(gTargetScreenState == 0 && gCurBrightness > 0)
		val -= 10;

	if (val != gCurBrightness &&
		(file = fopen(path, "w+")) != NULL) {
		snprintf(buf, 32, "%d", val);
		fwrite(buf, strlen(buf), 1, file);
		gCurBrightness = val;
		fclose(file);
		ret = TIMER_AGAIN;
		if(val == 0)
			set_screen_state(0);
	}

	return ret;
}

void ui_set_screen_state(int state)
{
	/* force restart the screen saver */
	ui_start_timer(gScreenSaverTimer, default_screensaver_delay);
	if (gTargetScreenState == state)
		return;
	gTargetScreenState = state;
	if(state)
		set_screen_state(state);
	ui_start_timer(gBrightnessTimer, BRIGHTNESS_DELAY);
}

int ui_get_screen_state(void)
{
	return gScreenState;
}

void ui_set_screensaver_delay(int delay)
{
	ui_set_screen_state(1);
	default_screensaver_delay = delay;
	ui_stop_timer(gScreenSaverTimer);
	ui_start_timer(gScreenSaverTimer, default_screensaver_delay);
}

void ui_restore_screensaver_delay()
{
	ui_set_screen_state(1);
	default_screensaver_delay = SCREENSAVER_DELAY;
	ui_stop_timer(gScreenSaverTimer);
	ui_start_timer(gScreenSaverTimer, default_screensaver_delay);
}

static void *screen_state_thread(void *cookie)
{
	int fd, err;
	char buf;
	while(1)
	{
		fd = open("/sys/power/wait_for_fb_sleep", O_RDONLY, 0);
		do {
			err = read(fd, &buf, 1);
			fprintf(stderr,"wait for sleep %d %d\n", err, errno);
		} while (err < 0 && errno == EINTR);
		pthread_mutex_lock(&gUpdateMutex);
		gScreenState = 0;
		pthread_mutex_unlock(&gUpdateMutex);
				ui_stop_timer(gScreenSaverTimer);
		close(fd);
		fd = open("/sys/power/wait_for_fb_wake", O_RDONLY, 0);
		do {
			err = read(fd, &buf, 1);
			fprintf(stderr,"wait for wake %d %d\n", err, errno);
		} while (err < 0 && errno == EINTR);
		pthread_mutex_lock(&gUpdateMutex);
		gScreenState = 1;
		pthread_mutex_unlock(&gUpdateMutex);
		close(fd);
				ui_start_timer(gScreenSaverTimer, default_screensaver_delay);

	}
	return NULL;
}

static int screen_saver_timer_cb(void *data)
{
	ui_set_screen_state(0);
	return TIMER_STOP;
}

static void ui_gr_color(struct color* clr)
{
	gr_color(clr->r, clr->g, clr->b, clr->a);
}

static void ui_gr_color_fill(struct color* clr)
{
	ui_gr_color(clr);
	gr_fill(0, 0, fb_width, fb_height);
}

// Draw the progress bar (if any) on the screen.  Does not flip pages.
// Should only be called with gUpdateMutex locked.
static void draw_progress_locked()
{
	int width = gr_get_width(gProgressBarIndeterminate[0]);
	int height = gr_get_height(gProgressBarIndeterminate[0]);

	int dx = (fb_width - width)/2;
	int dy = fb_height - 1.5 * CHAR_HEIGHT;

	// Erase behind the progress bar (in case this was a progress-only update)
	gr_color(0, 0, 0, 255);
	gr_fill(dx, dy, width, height);

	gr_blit(gProgressBarIndeterminate[process_frame], 0, 0, width, height, dx, dy);
}

static void update_progress_locked(void)
{
	draw_progress_locked();  // Draw only the progress bar
	gr_flip();
}

static int progress_timer_cb(void *data)
{
	if (process_update) {
		if (show_process) {
			pthread_mutex_lock(&gUpdateMutex);
			update_progress_locked();
			process_frame = (process_frame + 1) % PROGRESSBAR_INDETERMINATE_STATES;
			pthread_mutex_unlock(&gUpdateMutex);
		}
		return TIMER_AGAIN;
	} else
		return TIMER_STOP;
}


// Clear the screen and draw the currently selected background icon (if any).
// Should only be called with gUpdateMutex locked.
static void draw_background_locked(gr_surface icon)
{
	ui_gr_color_fill(&black);

	if (icon) {
		int iconWidth = gr_get_width(icon);
		int iconHeight = gr_get_height(icon);
		int iconX = (fb_width - iconWidth) / 2;
		int iconY = (fb_height - iconHeight) / 2;
		gr_blit(icon, 0, 0, iconWidth, iconHeight, iconX, iconY);
	}
	if (show_process)
		draw_progress_locked();
}

static void draw_text_line(int row, const char* t) {
  if (t[0] != '\0') {
	gr_text(0, (row+1)*CHAR_HEIGHT-1, t);
  }
}
// Redraw everything on the screen.  Does not flip pages.
// Should only be called with gUpdateMutex locked.
static void draw_screen_locked()
{
	int i, j;

	draw_background_locked(gCurrentIcon);
	ui_gr_color_fill(&black_tr);
	for (i = 0; i < BLOCK_NUM; i++)
		if (UI_BLOCK[i].show == VISIBLE) {
			if (i == LOG) {
				ui_gr_color(UI_BLOCK[i].clr_table[0]);
				for (j = 0; j < UI_BLOCK[i].rows; j++)
					draw_text_line(UI_BLOCK[i].top+j, UI_BLOCK[i].text_table[(j+log_top) % UI_BLOCK[i].rows]);
			} else {
				for (j = 0; j < UI_BLOCK[i].rows; j++) {
					ui_gr_color(UI_BLOCK[i].clr_table[j]);
					draw_text_line(UI_BLOCK[i].top+j, UI_BLOCK[i].text_table[j]);
				}
			}
		}
}

// Redraw everything on the screen and flip the screen (make it visible).
// Should only be called with gUpdateMutex locked.
static void update_screen_locked(void)
{
	draw_screen_locked();
	gr_flip();
}

static void set_block_text(int type, char **text)
{
	int i;
	for (i = 0; i < UI_BLOCK[type].rows; i++)
		UI_BLOCK[type].text_table[i][0] = '\0';
	for (i = 0; i < UI_BLOCK[type].rows; i++) {
		if (text[i] == NULL) break;
		strncpy(UI_BLOCK[type].text_table[i], text[i], strlen(text[i]) < MAX_COLS-1 ? strlen(text[i]) : MAX_COLS - 1);
	}
}

static void set_block_clr(int type, struct color *clr)
{
	int i;

	for (i = 0; i < UI_BLOCK[type].rows; i++)
		UI_BLOCK[type].clr_table[i] = clr;
}

static void update_block(int type, int visible)
{
	UI_BLOCK[type].show = visible;
	pthread_mutex_lock(&gUpdateMutex);
	update_screen_locked();
	pthread_mutex_unlock(&gUpdateMutex);
}

void ui_set_background(int icon)
{
	pthread_mutex_lock(&gUpdateMutex);
	gCurrentIcon = gBackgroundIcon[icon];
	update_screen_locked();
	pthread_mutex_unlock(&gUpdateMutex);
}

void ui_start_process_bar()
{
	process_update = 1;
	ui_start_timer(gProgressTimer, 500);
}

void ui_show_process(int show)
{
	show_process = show;
}

void ui_stop_process_bar()
{
	process_update = 0;
}

void ui_block_init(int type, char **titles, struct color **clrs)
{
	int i;

	for (i = 0; i < UI_BLOCK[type].rows; i++) {
		if (clrs[i] == NULL) break;
		UI_BLOCK[type].clr_table[i] = clrs[i];
	}
	set_block_text(type, titles);
	update_block(type, VISIBLE);
}

void ui_block_show(int type)
{
	UI_BLOCK[type].show = VISIBLE;
}

void ui_block_hide(int type)
{
	UI_BLOCK[type].show = HIDDEN;
}

int ui_block_visible(int type)
{
	return UI_BLOCK[type].show;
}

void ui_print(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, 256, fmt, ap);
	va_end(ap);

	fputs(buf, stdout);
	pthread_mutex_lock(&gUpdateMutex);
	char *ptr = buf;
	for (; *ptr != '\0'; ++ptr) {
		if (*ptr == '\r') {
			log[log_row][log_col] = '\0';
			log_col = 0;
		}
		if (*ptr == '\n' || log_col >= MAX_COLS) {
			log[log_row][log_col] = '\0';
			log_col = 0;
			log_row = (log_row + 1) % LOG_MAX;
			if (log_row == log_top) log_top = (log_top + 1) % LOG_MAX;
		}
		if ((*ptr != '\n') && (*ptr != '\r')) {
			log[log_row][log_col] = *ptr;
			log_col++;
		}
	}
	log[log_row][log_col] = '\0';
	update_screen_locked();
	pthread_mutex_unlock(&gUpdateMutex);
}

void ui_msg(int type, int align, const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, 256, fmt, ap);
	va_end(ap);

	switch (align) {
	  case MIDDLE:
		msg[0][0] = '\0';
		int k = (MAX_COLS - strlen(buf)) / 2;
		if (k > 0) {
			do {
				strcat(msg[0], " ");
				k--;
			} while (k > 0);
			strcat(msg[0], buf);
		} else
			strncpy(msg[0], buf, MAX_COLS);
		break;
	  default:
		strncpy(msg[0], buf, MAX_COLS);
		break;
	}

	switch (type) {
	  case ALERT:
		msg_clr[0] = &red;
		break;
	  default:
		msg_clr[0] = msg_dclr;
		break;
	}

	msg[0][MAX_COLS-1] = '\0';
	pthread_mutex_lock(&gUpdateMutex);
	update_screen_locked();
	pthread_mutex_unlock(&gUpdateMutex);
}

void ui_start_menu(char** items, int initial_selection)
{
	int k=0;

	while (k < MENU_MAX && items[k] != NULL) ++k;
	menu_items = k;
	menu_sel = initial_selection;
	set_block_text(MENU, items);
	set_block_clr(MENU, menu_dclr);
	menu_clr[menu_sel] = menu_sclr;
	update_block(MENU, VISIBLE);
}

int ui_menu_select(int sel)
{
	int old_sel;

	if (UI_BLOCK[MENU].show == VISIBLE) {
		old_sel = menu_sel;
		menu_sel = sel;
		if (menu_sel < 0) menu_sel = 0;
		if (menu_sel >= menu_items) menu_sel = menu_items-1;
		sel = menu_sel;
		if (menu_sel != old_sel){
			set_block_clr(MENU, menu_dclr);
			menu_clr[menu_sel] = menu_sclr;
			update_block(MENU, VISIBLE);
		}
	}
	return sel;
}

int ui_wait_key()
{
	pthread_mutex_lock(&key_queue_mutex);
	while (key_queue_len == 0) {
		pthread_cond_wait(&key_queue_cond, &key_queue_mutex);
	}

	int key = key_queue[0];
	memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
	pthread_mutex_unlock(&key_queue_mutex);
	return key;
}

int ui_key_pressed(int key)
{
	// This is a volatile static array, don't bother locking
	return key_pressed[key];
}

void ui_clear_key_queue()
{
	pthread_mutex_lock(&key_queue_mutex);
	key_queue_len = 0;
	pthread_mutex_unlock(&key_queue_mutex);
}


static int input_callback(int fd, short revents, void *data)
{
	struct input_event ev;
	int ret;

	ret = ev_get_input(fd, revents, &ev);
	if (ret)
		return -1;

	if (ev.type == EV_SYN || ev.type != EV_KEY || ev.code > KEY_MAX)
		return 0;
	key_pressed[ev.code] = ev.value;

	pthread_mutex_lock(&key_queue_mutex);
	const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
	if (ev.value > 0 && key_queue_len < queue_max) {
		key_queue[key_queue_len++] = ev.code;
		pthread_cond_signal(&key_queue_cond);
	}
	pthread_mutex_unlock(&key_queue_mutex);

	return 0;
}

// Reads input events, handles special hot keys, and adds to the key queue.
static void *input_thread(void *cookie)
{
    for (;;) {
        if (!ev_wait(ui_get_next_timer_ms()))
            ev_dispatch();
    }
    return NULL;
}

void ui_init(void)
{
	gr_init();
	ev_init(input_callback, NULL);

	set_block_clr(TITLE, title_dclr);
	set_block_clr(INFO, info_dclr);
	set_block_clr(MENU, menu_dclr);
	set_block_clr(LOG, log_dclr);
	set_block_clr(MSG, msg_dclr);

	fb_width = gr_fb_width();
	fb_height = gr_fb_height();
	printf("fb_width = %d, fb_height= %d\n", fb_width, fb_height);
	log_row = log_col = 0;
	log_top = 1;

	acquire_wake_lock(1, "fastboot");
	gScreenSaverTimer = ui_alloc_timer(screen_saver_timer_cb, 1, NULL);
	ui_start_timer(gScreenSaverTimer, default_screensaver_delay);
	gBrightnessTimer = ui_alloc_timer(set_back_brightness_timer, 1, NULL);
	gProgressTimer = ui_alloc_timer(progress_timer_cb, 1, NULL);

	int i;
	for (i = 0; BITMAPS[i].name != NULL; ++i) {
		int result = res_create_surface(BITMAPS[i].name, BITMAPS[i].surface);
		if (result < 0) {
			if (result == -2) {
				printf("Bitmap %s missing header\n", BITMAPS[i].name);
			} else {
				printf("Missing bitmap %s\n(Code %d)\n", BITMAPS[i].name, result);
			}
			*BITMAPS[i].surface = NULL;
		}
	}
	pthread_t t;
	pthread_create(&t, NULL, input_thread, NULL);
	pthread_create(&t, NULL, screen_state_thread, NULL);
}

void ui_exit(void)
{
	gr_exit();
}
