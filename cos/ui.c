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
#include <limits.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <fcntl.h>

#include <lite/lite.h>
#include <lite/window.h>
#include <lite/util.h>
#include <dirent.h>

#include <lite/cursor.h>
#include <leck/textbutton.h>
#include <leck/list.h>
#include <leck/check.h>
#include <leck/image.h>
#include <leck/label.h>
#include <leck/slider.h>
#include <leck/progressbar.h>

#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/msg.h>

#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/reboot.h>

#include "recovery.h"

#define BATTERY_CAPACITY_FULL 100
#define BATTERY_CAPACITY "/sys/class/power_supply"

static int main_window_width = 0;
static int main_window_height = 0;

typedef struct _coordinates {
	int x;
	int y;
	int w;
	int h;
} coordinates;

static coordinates logo;
static coordinates title;
static coordinates info_label;
static coordinates title_user_info;
static coordinates capacity_buff;
static coordinates time_buff;
static coordinates children_win;
static coordinates progress;

static int capacity = 0;
static int charger_flag = 1;
static int charging_full = 0;
static char *labelText = "BKB Charging OS";
static char *labelText_user_info = "Keep pressing power button 3s to Android";
static LiteWindow *window = NULL;
static LiteWindow *children_window = NULL;
static LiteImage *image = NULL;
static LiteLabel *labelTitle = NULL;
static LiteLabel *labelInfo = NULL;
static LiteImage *imageBat = NULL;
static LiteLabel *labelBat = NULL;
static LiteLabel *labelTime = NULL;
static LiteProgressBar *progressbar = NULL;

static DFBColor red = { a: 255, r: 255, g: 0, b:0 };
static DFBColor green = { a: 255, r: 0, g: 255, b:0 };
static DFBColor black = { a: 0, r: 0, g: 0, b:0 };
static DFBColor white = { a: 255, r: 255, g: 255, b:255 };

static struct msgtype {
	long mtype;
	char buffer[100];
} msg;

static int init_ui()
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

	children_win.x = 40;
	children_win.y = 260;
	children_win.w = 500;
	children_win.h = 100;

	info_label.x = 50;
	info_label.y = 35;
	info_label.w = 400;
	info_label.h = 60;

	logo.x = 0.042 * main_window_width;
	logo.y = 0.125 * main_window_height;
	logo.w = 0.125 * main_window_width;
	logo.h = 0.075 * main_window_height;

	title.x = logo.x + logo.w + 0.05 * main_window_width;
	title.y = logo.y + 0.0125 * main_window_height;
	title.w = 0.83 * main_window_width;
	title.h = 0.125 * main_window_height;

	progress.x = (main_window_width * 0.5) / 2;
	progress.y = (main_window_height * 0.8) / 2;
	progress.w = main_window_width * 0.46;
	progress.h = main_window_height * 0.265;

	capacity_buff.x = progress.x + progress.w + main_window_width * 0.05;
	capacity_buff.y =
	    progress.y + progress.h / 2 - main_window_height * 0.00625;
	capacity_buff.w = main_window_width * 0.27;
	capacity_buff.h = main_window_height * 0.16;

	time_buff.x = 0.28 * main_window_width;
	time_buff.y = progress.y + progress.h + 0.08 * main_window_height;
	time_buff.w = 1.08 * main_window_width;
	time_buff.h = 0.08 * main_window_height;

	title_user_info.x = time_buff.x - 100;
	title_user_info.y = time_buff.y + time_buff.h + 20;
	title_user_info.w = 500;
	title_user_info.h = 0.08 * main_window_height;

	close(fd);
	return 1;
}

static void rect_init(DFBRectangle * rect, int x, int y, int w, int h)
{
	rect->x = x;
	rect->y = y;
	rect->w = w;
	rect->h = h;
}

int open_tip_win(char *info)
{
	lite_set_window_opacity(children_window, liteFullWindowOpacity);
	lite_set_label_text(labelInfo, info);
	return 0;
}

void user_info(char *buffer)
{
	char *info;

	if (strcmp(buffer, COS_BOOT_MOS_BUF) == 0)
		info = "    Release power button to enter into MOS     ";
	else if (strcmp(buffer, COS_CHRG_REMOVE) == 0)
		info = "   The phone will shut down after 5s";
	else if (strcmp(buffer, COS_VOL_LOW) == 0)
		info = "The battery low ,please insert charge";
	else if (strcmp(buffer, COS_VOL_HIGH) == 0)
		lite_close_window(children_window);
	else
		info = "       Get error userinfo        ";

	open_tip_win(info);
}

void get_now_time()
{
	time_t now;
	struct tm *info;
	char buf[256] = { '\0', };

	time(&now);
	info = localtime(&now);
	sprintf(buf, " %04d-%02d-%02d  %02d:%02d",
		(info->tm_year + 1900), (info->tm_mon + 1),
		(info->tm_mday), (info->tm_hour), (info->tm_min));
	lite_set_label_text(labelTime, buf);
}

void show_capacity()
{
	char buf[256] = { '\0', };
	sprintf(buf, "%d%s",
		((capacity >
		  BATTERY_CAPACITY_FULL) ? BATTERY_CAPACITY_FULL :
		 capacity), "%");
	if (charging_full)
		strcpy(buf, "FULL");
	lite_set_label_text(labelBat, buf);
}

volatile int capacity_update = 0;
void *event_listener(void *data)
{
	int msgid = *(int *)data;
	syslog(LOG_DEBUG, "event listener created ...\n");
	while (1) {
		msgrcv(msgid, &msg, sizeof(struct msgtype), 0, 0);
		switch (msg.mtype) {
		case COS_CHRG_EVENT:
			if (strcmp(msg.buffer, COS_CHRG_REMOVE) == 0) {
				labelText_user_info =
				    "USB charger removal or abnormal status...";
				lite_set_label_color(labelTitle, &red);
				lite_set_label_text(labelTitle,
						    labelText_user_info);
				charger_flag = 0;
			} else {
				labelText_user_info =
				    "Keep pressing power button 3s to Android";
				lite_set_label_color(labelTitle, &green);
				lite_set_label_text(labelTitle,
						    labelText_user_info);
				charger_flag = 1;
			}
			break;
		case COS_VOL_GATE:
			if (strcmp(msg.buffer, COS_VOL_LOW) == 0) {
				labelText_user_info =
				    "Power is too LOW to enter Android";
				lite_set_label_color(labelTitle, &red);
				lite_set_label_text(labelTitle,
						    labelText_user_info);
			} else {
				labelText_user_info =
				    "Keep pressing power button 3s to Android";
				lite_set_label_color(labelTitle, &green);
				lite_set_label_text(labelTitle,
						    labelText_user_info);
			}
			break;
		case COS_TEMP_EVENT:
			labelText_user_info =
				    "Temperature is too high, shutdown system!";
				lite_set_label_color(labelTitle, &red);
				lite_set_label_text(labelTitle,
						    labelText_user_info);
			break;
		case COS_BOOT_MOS:
			user_info(msg.buffer);
			break;
		case COS_BAT_STATUS:
			capacity_update = 1;
			if (strcmp(msg.buffer, "FULL")) {
				if (charging_full) {
					syslog(LOG_ERR, "This should never happen,"
							"back to charging mode from maintenance mode\n");
					charging_full = 0;
				}
				capacity = atoi(msg.buffer);
			} else
				charging_full = 1;
/*			labelText_user_info =
			    "Keep pressing power button 3s to Android";
			lite_set_label_color(labelTitle, &green);
			lite_set_label_text(labelTitle, labelText_user_info);*/
			break;
		default:
			syslog(LOG_WARNING, "Unknown msg type!\n");
		}
	}
}

void *battery_status_update(void *data)
{
	int cap = capacity;
	while (1) {
		get_now_time();
		show_capacity();
		if (charging_full)
			lite_set_progressbar_value(progressbar, 100 / 100.0f);
		else if (charger_flag) {
			if (cap > 100 || cap % 20)
				cap = capacity - capacity % 20;
			lite_set_progressbar_value(progressbar, cap / 100.0f);
			cap += 20;
		} else {
			cap = capacity;
			lite_set_progressbar_value(progressbar, cap / 100.0f);
		}
		sleep(1);
	}
	return NULL;
}

int cos_ui(int argc, char *argv[], int fd, int msgid)
{
	DFBRectangle rect;
	DFBResult res;
	LiteFont *font;
	IDirectFBFont *font_interface;

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
		syslog(LOG_ERR, "fail to init ui main window!\n");
		return res;
	}
	lite_hide_cursor();
	lite_set_window_background(window, &black);
	/* creat children window */
	rect_init(&rect, children_win.x, children_win.y, children_win.w,
		  children_win.h);
	res =
	    lite_new_window(NULL, &rect, DWCAPS_ALPHACHANNEL,
			    liteDefaultWindowTheme, "Notice", &children_window);
	if (res != DFB_OK) {
		syslog(LOG_ERR, "fail to init ui child window!\n");
		return res;
	}

	/* setup user_info_label */
	rect_init(&rect, info_label.x, info_label.y, info_label.w,
		  info_label.h);
	res =
	    lite_new_label(LITE_BOX(children_window), &rect, liteNoLabelTheme,
			   15, &labelInfo);
	lite_set_label_font(labelInfo, "DroidSans", LITE_FONT_BOLD, 15,
			    DFFA_OUTLINED);

	/* setup logo image */
	rect_init(&rect, logo.x, logo.y, logo.w, logo.h);
	res = lite_new_image(LITE_BOX(window), &rect, liteNoImageTheme, &image);
	char buf[256];
	char buf2[256];
	sprintf(buf, "%s/android.png", get_datadir());
	lite_load_image(image, buf);

	/* setup label */
	rect_init(&rect, title.x, title.y, title.w, title.h);
	res =
	    lite_new_label(LITE_BOX(window), &rect, liteNoLabelTheme, 40,
			   &labelTitle);
	lite_set_label_font(labelTitle, "DroidSans", LITE_FONT_BOLD, 40,
			    DFFA_OUTLINED);
	lite_set_label_color(labelTitle, &white);
	lite_set_label_text(labelTitle, labelText);

	/* setup capacity label */
	rect_init(&rect, capacity_buff.x, capacity_buff.y, capacity_buff.w,
		  capacity_buff.h);
	res =
	    lite_new_label(LITE_BOX(window), &rect, liteNoLabelTheme, 18,
			   &labelBat);
	lite_set_label_font(labelBat, "DroidSans", LITE_FONT_ITALIC, 18,
			    DFFA_OUTLINED);
	lite_set_label_color(labelBat, &white);

	/* setup time label */
	rect_init(&rect, time_buff.x, time_buff.y, time_buff.w, time_buff.h);
	res =
	    lite_new_label(LITE_BOX(window), &rect, liteNoLabelTheme, 30,
			   &labelTime);
	lite_set_label_font(labelTime, "DroidSans", LITE_FONT_PLAIN, 30,
			    DFFA_OUTLINED);
	lite_set_label_color(labelTime, &white);

	/* setup label */
	rect_init(&rect, title_user_info.x, title_user_info.y,
		  title_user_info.w, title_user_info.h);
	res =
	    lite_new_label(LITE_BOX(window), &rect, liteNoLabelTheme, 20,
			   &labelTitle);
	lite_set_label_font(labelTitle, "DroidSans", LITE_FONT_BOLD, 20,
			    DFFA_OUTLINED);

	/* add progress bar */
	rect_init(&rect, progress.x, progress.y, progress.w, progress.h);
	res =
	    lite_new_progressbar(LITE_BOX(window), &rect,
				 liteNoProgressBarTheme, &progressbar);
	sprintf(buf2, "%s/battery_charge_background_empty.png", get_datadir());
	sprintf(buf, "%s/battery_charge_foreground_full.png", get_datadir());
	lite_set_progressbar_images(progressbar, buf, buf2);
	lite_set_progressbar_value(progressbar, 0.0f);

	/* battery/charging status monitoring */
	{
		pthread_t thread_bat;
		pthread_attr_t attr_bat;

		pthread_attr_init(&attr_bat);
		pthread_create(&thread_bat, &attr_bat, event_listener, &msgid);
		while (capacity_update == 0)
			usleep(50000);
		pthread_create(&thread_bat, &attr_bat, battery_status_update,
			       (void *)imageBat);
	}

	/* show the window */
	lite_set_window_opacity(window, liteFullWindowOpacity);
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
