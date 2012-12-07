/*
 * Copyright (c) 2011 Borqs Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Borqs Ltd. nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <lite/lite.h>
#include <lite/window.h>
#include <lite/util.h>

#include <lite/cursor.h>
#include <leck/textbutton.h>
#include <leck/list.h>
#include <leck/check.h>
#include <leck/image.h>
#include <leck/label.h>
#include <leck/slider.h>
#include <leck/progressbar.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/msg.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include "recovery.h"
#include "textedit.h"
#include "ui.h"

#define BATTERY_CAPACITY_FULL 100

static LiteWindow *window = NULL;
static char *labelText = "BKB Provisioning OS";
static char *consoleTitle = "Description of update:";

static LiteImage *image = NULL;
static LiteLabel *labelTitle = NULL;
static LiteLabel *labelConsole = NULL;
static LiteTextEdit *console = NULL;
static LiteImage *imageBat = NULL;

static int main_window_width = 0;
static int main_window_height = 0;

typedef struct _coordinates {
	int x;
	int y;
	int w;
	int h;
} coordinates;

typedef struct _logthread {
	LiteTextEdit *console;
	int fd;
} logthread;

static struct msgtype {
	long mtype;
	char buffer[100];
} msg;

static int msgID;
static coordinates logo;
static coordinates title;
static coordinates battery;
static coordinates console_title;
static coordinates console_log;
static coordinates progress;

static DFBColor black = {a:0, r:0, g:0, b:0};
static DFBColor white = {a:255, r:255, g:255, b:255};
static int capacity = 10;
static int usb_present;

static void rect_init(DFBRectangle * rect, int x, int y, int w, int h)
{
	rect->x = x;
	rect->y = y;
	rect->w = w;
	rect->h = h;
}

int init_ui()
{
	int fd;
	struct fb_fix_screeninfo fi;
	struct fb_var_screeninfo vi;

	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0) {
		perror("cannot open fb0");
		return -1;
	}

	if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
		perror("failed to get fb0 info");
		close(fd);
		return -1;
	}

	if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
		perror("failed to get fb0 info");
		close(fd);
		return -1;
	}

	if (vi.xres == 800)
		main_window_width = vi.xres - 200;
	else
		main_window_width = vi.xres;

	main_window_height = vi.yres;

	logo.x = 0.042 * main_window_width;
	logo.y = 0.025 * main_window_height;
	logo.w = 0.125 * main_window_width;
	logo.h = 0.075 * main_window_height;

	title.x = logo.x + logo.w + 0.042 * main_window_width;
	title.y = logo.y + 0.0125 * main_window_height;
	title.w = 0.83 * main_window_width;
	title.h = 0.125 * main_window_height;

	console_log.x = 0.01 * main_window_width;

	console_title.x = 0.02 * main_window_width;
	console_title.y = 0.125 * main_window_height;
	console_title.w = main_window_width - 2 * console_log.x;
	console_title.h = 0.025 * main_window_height;

	console_log.y =
	    console_title.y + console_title.h + 0.01 * main_window_height;
	console_log.w = main_window_width - 2 * console_log.x;
	console_log.h = 0.5 * main_window_height + 0.225 * main_window_height;

	progress.x = 0.01 * main_window_width;
	progress.y = console_log.y + console_log.h + 0.065 * main_window_height;
	progress.w = console_log.w;
	progress.h = 0.03 * main_window_height;

	battery.x = main_window_width - 80;
	battery.y = 40;
	battery.w = 65;
	battery.h = 30;

	close(fd);
	return 1;
}

void *get_console_event(void *arg)
{
	int n = 0;
	char line[MAX_SIZE];
	logthread *r = (logthread *) arg;
	while ((n = read(r->fd, line, MAX_SIZE)) > 0) {
		lite_set_textedit_text(r->console, line);
		memset(line, 0, strlen(line));
	}

	return NULL;
}

int progress_file_read(char *line)
{
	int fd;
	fd = open("/tmp/progress.txt", O_RDONLY);
	if (fd < 0) {
		return 0;
	}
	read(fd, line, 1);
	close(fd);
	return 1;
}

void *change_pic_event(void *arg)
{
	char buf[256] = { '\0', };
	LiteImage *image = (LiteImage *) arg;
	char line;
	int i;

	while (1) {
		if (progress_file_read(&line)) {
			switch (line) {
			case BAR_START:
				for (i = 0; i < 6; i++) {
					sprintf(buf, "%s/indeterminate%d.png",
						get_datadir(), i + 1);
					lite_load_image(image, buf);
					usleep(50000);
				}
				break;
			case BAR_FINISH:
				usleep(50000);
				sprintf(buf, "%s/indeterminate1.png",
					get_datadir());
				lite_load_image(image, buf);
				break;
			}
		}
	}
}

void *pos_battery_status_update(void *data)
{
	int pic_no = 0;
	char *battery_pic_path;

	while (1) {
		char buf[256] = { '\0', };

		if (usb_present) {
			battery_pic_path = "battery_pos_charge";

			if (pic_no == 0 || pic_no > 100)
				pic_no = capacity - capacity % 20;
			switch (capacity) {
			case (0)...(BATTERY_CAPACITY_FULL - 1):
				if (pic_no == 100)
					sprintf(buf, "%s/%s/stat_sys_battery_full.png",
						get_datadir(), battery_pic_path);
				else
					sprintf(buf, "%s/%s/stat_sys_battery_%d.png",
						get_datadir(), battery_pic_path,
						pic_no);
				break;
			case BATTERY_CAPACITY_FULL...INT_MAX:
				sprintf(buf, "%s/%s/stat_sys_battery_full.png",
					get_datadir(), battery_pic_path);
				break;
			default:
				sprintf(buf,
					"%s/%s/stat_sys_battery_unknown.png",
					get_datadir(), battery_pic_path);
				break;
			}
			pic_no += 20;
		} else {
			battery_pic_path = "battery_pos_discharge";
			switch (capacity) {
			case (0)...(BATTERY_CAPACITY_FULL - 1):
				sprintf(buf, "%s/%s/stat_sys_battery_%d.png",
					get_datadir(), battery_pic_path,
					capacity - capacity % 5);
				break;
			case BATTERY_CAPACITY_FULL...INT_MAX:
				sprintf(buf, "%s/%s/stat_sys_battery_full.png",
					get_datadir(), battery_pic_path);
				break;
			default:
				sprintf(buf,
					"%s/%s/stat_sys_battery_unknown.png",
					get_datadir(), battery_pic_path);
				break;
			}
		}
		lite_load_image(imageBat, buf);
		sleep(1);
	}
	return NULL;
}

static int create_console_thread(LiteTextEdit * console, int fd)
{
	pthread_t thr;
	pthread_attr_t atr;

	logthread *r = malloc(sizeof(logthread));

	r->console = console;
	r->fd = fd;

	if (pthread_attr_init(&atr) != 0) {
		syslog(LOG_ERR, "ERROR: Unable to set thread attribute.\n");
	} else {
		if (pthread_create(&thr, &atr, get_console_event, (void *)r) !=
		    0)
			syslog(LOG_ERR, "ERROR: Unable to create get_console_event thread.\n");
	}
	return 0;
}

static int create_change_pic_thread(LiteImage * image)
{
	pthread_t thr;
	pthread_attr_t atr;

	if (pthread_attr_init(&atr) != 0) {
		syslog(LOG_ERR, "ERROR: Unable to set thread attribute.\n");
	} else {
		if (pthread_create(&thr, &atr, change_pic_event, (void *)image)
		    != 0)
			syslog(LOG_ERR, "ERROR: Unable to create change_pic_event thread.\n");
	}
	return 0;
}

static void *event_listener(void *data)
{
	while (1) {
		msgrcv(msgID, &msg, sizeof(struct msgtype), 0, 0);
		switch (msg.mtype) {
		case CHRG_EVENT:
			if (strcmp(msg.buffer, CHRG_INSERT) == 0)
				usb_present = 1;
			else
				usb_present = 0;
			break;
		case BAT_STATUS:
			capacity = atoi(msg.buffer);
			break;
		default:
			syslog(LOG_WARNING, "Unknown msg type!\n");
		}
	}
	return NULL;
}

int pos_ui(int argc, char *argv[], int fd, int msgid)
{
	DFBRectangle rect;
	DFBResult res;
	LiteFont *font;
	IDirectFBFont *font_interface;
	char buf[256] = { '\0', };

	msgID = msgid;
	/* initialize ui size */
	if (init_ui() < 0)
		return 1;

	/* initialize */
	if (lite_open(&argc, &argv))
		return 1;

	lite_get_font("default", LITE_FONT_PLAIN, 16, DEFAULT_FONT_ATTRIBUTE,
		      &font);
	lite_font(font, &font_interface);

	/* create a window */
	rect_init(&rect, 0, 0, main_window_width, main_window_height);
	res = lite_new_window(NULL,
			      &rect,
			      DWCAPS_ALPHACHANNEL,
			      liteDefaultWindowTheme,
			      "BKB Provisioning OS", &window);
	if (res != DFB_OK) {
		syslog(LOG_ERR, "fail to init pos ui main window!\n");
		return res;
	}
	lite_hide_cursor();
	lite_set_window_background(window, &black);

	/* setup logo image */
	rect_init(&rect, logo.x, logo.y, logo.w, logo.h);
	res = lite_new_image(LITE_BOX(window), &rect, liteNoImageTheme, &image);
	sprintf(buf, "%s/android.png", get_datadir());
	lite_load_image(image, buf);

	/* setup label */
	rect_init(&rect, title.x, title.y, title.w, title.h);
	res =
	    lite_new_label(LITE_BOX(window), &rect, liteNoLabelTheme, 30,
			   &labelTitle);
	lite_set_label_font(labelTitle, "DroidSans", LITE_FONT_BOLD, 30,
			    DFFA_OUTLINED);
	lite_set_label_color(labelTitle, &white);
	lite_set_label_text(labelTitle, labelText);

	/* setup console label */
	rect_init(&rect, console_title.x, console_title.y, console_title.w,
		  console_title.h);
	res =
	    lite_new_label(LITE_BOX(window), &rect, liteNoLabelTheme, 16,
			   &labelConsole);
	lite_set_label_font(labelConsole, "DroidSans", LITE_FONT_PLAIN, 16,
			    DFFA_OUTLINED);
	lite_set_label_color(labelConsole, &white);
	lite_set_label_text(labelConsole, consoleTitle);

	/* setup console */
	rect_init(&rect, console_log.x, console_log.y, console_log.w,
		  console_log.h);
	res =
	    lite_new_textedit(LITE_BOX(window), &rect, liteNoTextEditTheme, 16,
			      &console);
	lite_enable_textedit_cursor(console, false);
	lite_set_textedit_focus(console, false);

	/* setup battery logo */
	rect_init(&rect, battery.x, battery.y, battery.w, battery.h);
	res =
	    lite_new_image(LITE_BOX(window), &rect, liteNoImageTheme,
			   &imageBat);
	/* mesage queue status monitoring */
	{
		pthread_t thrd;
		pthread_attr_t attr;

		pthread_attr_init(&attr);
		pthread_create(&thrd, &attr, event_listener, NULL);
	}

	/* battery/charging status monitoring */
	{
		pthread_t thread_bat;
		pthread_attr_t attr_bat;

		pthread_attr_init(&attr_bat);
		pthread_create(&thread_bat, &attr_bat,
			       pos_battery_status_update, (void *)imageBat);
	}

	/* show the window */
	lite_set_window_opacity(window, liteFullWindowOpacity);

	/* setup the progress_bar */
	rect_init(&rect, progress.x, progress.y, progress.w, progress.h);
	res = lite_new_image(LITE_BOX(window), &rect, liteNoImageTheme, &image);
	sprintf(buf, "%s/indeterminate1.png", get_datadir());
	lite_load_image(image, buf);

	/* create log thread */
	create_console_thread(console, fd);

	/* create progress_bar thread */
	create_change_pic_thread(image);

	/* run the default event loop */
	lite_window_event_loop(window, 0);

	/* destory font */
	lite_release_font(font);

	/* destroy the window with all this children and resources */
	lite_destroy_window(window);

	/* deinitialize */
	lite_close();

	return 0;
}
