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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/reboot.h>
#include <cutils/properties.h>

#include "common.h"
#include "roots.h"
#include "ufdisk.h"
#include "power.h"
#include "firmware.h"

#define BOOT_LOADER_VERSION		"V0.1"

#define CONSOLE_DEBUG

#define PROP_FILE					"/default.prop"
#define PRODUCT_NAME_ATTR		"ro.product.name"
#define PRODUCT_DEVICE_ATTR		"ro.product.device"
#define MAX_NAME_SIZE			128
#define BUF_SIZE					256

enum {
	ITEM_BOOTLOADER,
	ITEM_REBOOT,
	ITEM_RECOVERY,
	ITEM_POWEROFF
};

enum {
	HIGHLIGHT_DOWN,
	HIGHLIGHT_UP,
	SELECT_ITEM,
	NO_ACTION
};

extern struct color white;
extern struct color green;
extern struct color brown;

static char* title[] = {"                     INTEL FASTBOOT MODE", NULL};
static struct color* title_clr[] = {&brown, NULL};

static char **info;
#if 0
static char *info[] = {"SCU FW:        IFWI:",
									 "MODEM:    ",
									 " ",
									 " ",
									 " ",
									 " ",
									 " ",
									 " ",
									 "SELECT - VOL_UP OR VOL_DOWN",
									 "EXCUTE - CAMERA", NULL};
#endif
static struct color* info_clr[] = {&white, &white, &white, &white, &white, &white, &white, &green, &green, &green, NULL};

static char* menu[] = {"REBOOT FASTBOOT",
								   "REBOOT",
								   "RECOVERY",
								   "POWER OFF", NULL};


extern int pos_main();
extern int cos_main(int mode);
extern int ufdisk_ensure_partition_created(void);

static int table_init(char** table, int width, int height)
{
	int i;

	if ((info = malloc(height * sizeof(char*))) == NULL)
		return -1;
	for (i = 0; i < height; i++) {
		if ((info[i] = malloc(width * sizeof(char))) == NULL)
			return -1;
		memset(info[i], 0, width);
	}
	return 0;
}

static void table_exit(char ** table, int height)
{
	int i;

	if (table) {
		for (i = 0; i < height; i++)
			if (table[i])   free(table[i]);
	}
}

static void goto_recovery()
{
	//ui_exit();
	table_exit(info, INFO_MAX);
#ifdef CONSOLE_DEBUG
	property_set("ro.debuggable", "0");
#endif
	sleep(2);
	if (execv("/sbin/recovery", NULL))
		ui_msg(ALERT, LEFT, "SWITCH TO RECOVERY FAILED!");
}

static char* strupr(char *str)
{
	char *p = str;
	while (*p != '\0') {
		*p = toupper(*p);
		p++;
	}
	return str;
}

static int read_from_file(char* file, char *attr, char *value)
{
	char *p;
	char buf[BUF_SIZE];
	FILE *f;

	if ((f = fopen(file, "r")) == NULL) {
		LOGE("open %s error!\n", file);
		return -1;
	}
	while(fgets(buf, BUF_SIZE, f)) {
		if ((p = strstr(buf, attr)) != NULL) {
			p += strlen(attr)+1;
			strncpy(value, p, MAX_NAME_SIZE);
			value[MAX_NAME_SIZE-1] = '\0';
			strupr(value);
			break;
		}
	}

	fclose(f);
	return 0;
}

static int get_info()
{
	struct scu_ipc_version version;
	char pro_dev[MAX_NAME_SIZE], pro_name[MAX_NAME_SIZE];
	int i, ret = 0;

	memset(&version, 0, sizeof(struct scu_ipc_version));
	memset(&pro_dev, 0, MAX_NAME_SIZE);
	memset(&pro_name, 0, MAX_NAME_SIZE);
	if (get_fw_info(&version) != 0) {
		LOGE("get_fw_info error!\n");
		ret = -1;
	}
	if (read_from_file(PROP_FILE, PRODUCT_NAME_ATTR, pro_name) != 0) {
		LOGE("read %s error!\n", PRODUCT_NAME_ATTR);
		ret = -1;
	}
	if (read_from_file(PROP_FILE, PRODUCT_DEVICE_ATTR, pro_dev) != 0) {
		LOGE("read %s error!\n", PRODUCT_DEVICE_ATTR);
		ret = -1;
	}

	for(i = 0; i < MAX_COLS-1; i++) {
		info[0][i] = '_';
		info[5][i] = '_';
	}
	snprintf(info[1], MAX_COLS-1, "IA32 FW:    CPU-V %02X.%02X     SUPP-V %02X.%02X     VH-V %02X.%02X",
	          version.data[IA32_CPU_OFFSET], version.data[IA32_CPU_OFFSET-1],
	          version.data[IA32_SUPP_OFFSET], version.data[IA32_SUPP_OFFSET-1],
	          version.data[IA32_VH_OFFSET], version.data[IA32_VH_OFFSET-1]);
	snprintf(info[2], MAX_COLS-1, "SCU FW:     ROM-V %02X.%02X     RT-V %02X.%02X",
	          version.data[SCU_ROM_OFFSET], version.data[SCU_ROM_OFFSET-1],
	          version.data[SCU_RT_OFFSET], version.data[SCU_RT_OFFSET-1]);
	snprintf(info[3], MAX_COLS-1, "PUNIT FW:   V %02X.%02X         IFWI:   V %02X.%02X",
	          version.data[PUNIT_OFFSET], version.data[PUNIT_OFFSET-1],
	          version.data[IFWI_OFFSET], version.data[IFWI_OFFSET-1]);
	snprintf(info[4], MAX_COLS-1, "PRODUCT_NAME:%s      PRODUCT_DEVICE:%s", pro_name, pro_dev);
	snprintf(info[7], MAX_COLS-1, "                   FASTBOOT APP %s", BOOT_LOADER_VERSION);
	snprintf(info[8], MAX_COLS-1, "SELECT - VOL_UP OR VOL_DOWN");
	snprintf(info[9], MAX_COLS-1, "EXCUTE - CAMERA");
	for (i = 0; i < INFO_MAX; i++)
		info[i][MAX_COLS-1] = '\0';

	return ret;
}

int device_handle_key(int key_code, int visible) {
	/* a key press will ensure screen state to 1 */
	if (visible == VISIBLE) {
		switch (key_code) {
			case KEY_DOWN:
			case KEY_VOLUMEDOWN:
				return HIGHLIGHT_DOWN;

			case KEY_UP:
			case KEY_VOLUMEUP:
				return HIGHLIGHT_UP;

			case KEY_ENTER:
			case KEY_CAMERA:
			case BTN_MOUSE:              // trackball button
				return SELECT_ITEM;
		}
	}

    return NO_ACTION;
}


static int get_menu_selection(char** items, int initial_selection) {
	ui_clear_key_queue();
	ui_start_menu(items, initial_selection);
	int selected = initial_selection;
	int chosen_item = -1;

	while (chosen_item < 0) {
		int key = ui_wait_key();
		int visible = ui_block_visible(MENU);
		int action = device_handle_key(key, visible);

		if (ui_get_screen_state() == 0)
			ui_set_screen_state(1);
		else
			switch (action) {
				case HIGHLIGHT_UP:
					--selected;
					selected = ui_menu_select(selected);
					break;
				case HIGHLIGHT_DOWN:
					++selected;
					selected = ui_menu_select(selected);
					break;
				case SELECT_ITEM:
					chosen_item = selected;
					break;
				case NO_ACTION:
					break;
			}
	}
	return chosen_item;
}


static void prompt_and_wait()
{
	for (;;) {
		int chosen_item = get_menu_selection(menu, 0);
		switch (chosen_item) {
			case ITEM_BOOTLOADER:
				sync();
				__reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, "bootloader");
				break;
			case ITEM_RECOVERY:
                                goto_recovery();
				break;
			case ITEM_REBOOT:
				sync();
				 __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, "android");
				break;
			case ITEM_POWEROFF:
				force_shutdown();
				break;
		}
	}
}

int main(void)
{
	//Init operation
#ifdef CONSOLE_DEBUG
	freopen("/dev/console", "a", stdout);
	freopen("/dev/console", "a", stderr);
	property_set("ro.debuggable", "1");
#endif
	ui_init();
	ui_block_show(MSG);
	load_volume_table();

	if (ufdisk_need_create_partition()) {
		ui_msg(TIPS, LEFT, "PARTITON EMMC...");
		ufdisk_create_partition();
		ui_msg(TIPS, LEFT, " ");
	}

	if (ensure_path_mounted("/logs") != 0)
		LOGE("unable to mount the log partition\n");
	else
		property_set("service.apk_logfs.enable", "1");

	switch (get_poweron_reason()) {
	  case RR_SIGNED_COS:
		cos_main(MODE_COS);
		break;
	  case RR_SIGNED_RECOVERY:
#ifdef CONSOLE_DEBUG
		property_set("ro.debuggable", "0");
#endif
		sleep(2);
		if (execv("/sbin/recovery", NULL))
			ui_msg(ALERT, LEFT, "SWITCH TO RECOVERY FAILED!");
		break;
	  default:
		break;
	}

	if (can_boot_android() != 0) {
		ui_msg(ALERT, MIDDLE, "LOW POWER, CHARGING...");
		if (cos_main(MODE_POS)) {
			ui_msg(ALERT, MIDDLE, "CHARGING ERROR, SHUTDOWN!");
			sleep(5);
			force_shutdown();
		}
	}
	//Setup default UI interface
	ui_set_background(BACKGROUND_ICON_BACKGROUND);
	ui_block_init(TITLE, title, title_clr);
	if (table_init(info, MAX_COLS, INFO_MAX) == 0) {
		if(get_info() < 0)
			LOGE("get_info error!\n");
		ui_block_init(INFO, (char**)info, info_clr);
	} else {
		LOGE("Init info table error!\n");
	}
	ui_block_show(LOG);

	//Init fastboot
	pos_main();

	ui_show_process(VISIBLE);

	//wait for user's choice
	prompt_and_wait();
	return 0;
}
