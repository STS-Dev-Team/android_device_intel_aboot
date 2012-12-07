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
#include <unistd.h>
#include <linux/input.h>
#include <sys/reboot.h>

#include "minui.h"
#include "common.h"
#include "power.h"

#define REBOOTTIMER_DELAY 2000

//COS or POS mode
static int charge_mode;
static int voltage_gate;
static int voltage_good_pipe[2];

static struct power_supply_status charger;
static struct power_supply_status battery;

static ui_timer_t *battery_update_timer;
static ui_timer_t *power_poll_timer;
static ui_timer_t *reboot_timer;
static int battery_update_icon = BACKGROUND_ICON_BAT100;

static int battery_status_update_timer(void *data)
{
	static int current_icon;

	if (battery_update_icon > BACKGROUND_ICON_BAT100) {
		get_power_supply_status(&battery);
		get_power_supply_status(&charger);
		switch (battery.capacity) {
		case 0 ... 12:
			battery_update_icon = BACKGROUND_ICON_BAT00;
			break;
		case 13 ... 24:
			battery_update_icon = BACKGROUND_ICON_BAT13;
			break;
		case 25 ... 37:
			battery_update_icon = BACKGROUND_ICON_BAT25;
			break;
		case 38 ... 49:
			battery_update_icon = BACKGROUND_ICON_BAT38;
			break;
		case 50 ... 62:
			battery_update_icon = BACKGROUND_ICON_BAT50;
			break;
		case 63 ... 74:
			battery_update_icon = BACKGROUND_ICON_BAT63;
			break;
		case 75 ... 87:
			battery_update_icon = BACKGROUND_ICON_BAT75;
			break;
		case 88 ... 99:
			battery_update_icon = BACKGROUND_ICON_BAT88;
			break;
		case 100:
			battery_update_icon = BACKGROUND_ICON_BAT100;
			break;
		default:
			battery_update_icon = BACKGROUND_ICON_BAT00;
			break;
		}
		current_icon = battery_update_icon;
	}
	if (battery.status == CHARGE_STATUS_CHARGING)
		ui_set_background(battery_update_icon);
	else if (battery.status == CHARGE_STATUS_FULL)
		ui_set_background(BACKGROUND_ICON_FULL);
	else if (charger.present)
		ui_set_background(BACKGROUND_ICON_ERR);
	else
		ui_set_background(current_icon);

	battery_update_icon++;
	return TIMER_AGAIN;
}

static int power_poll_timer_cb(void *data)
{
	static int power_loss_counter = 0;
	char status_buf[256];

	get_power_supply_status(&charger);
	get_power_supply_status(&battery);

	if (!charger.present) {
		if (power_loss_counter == 0) {
			ui_set_screen_state(1);
			ui_msg(ALERT, MIDDLE, "CHARGER REMOVE");
		}
		if (power_loss_counter++ > 4) {
			LOGI("[SHTDWN] %s, shutdown due to power loss", __func__);
			force_shutdown();
		}
	} else if(power_loss_counter != 0) {
		ui_msg(TIPS, MIDDLE, " ");
		power_loss_counter = 0;
	}

	if (battery.status == CHARGE_STATUS_FULL && power_loss_counter == 0) {
		ui_set_screen_state(1);
		ui_msg(TIPS, MIDDLE, "BATTERY CHARGE FULL");
	}

	if (charge_mode == MODE_POS && battery.voltage_now > voltage_gate) {
		write(voltage_good_pipe[1],"OK",2);
	}

	return TIMER_AGAIN;
}

static int reboot_timer_cb(void *data)
{
	if (ui_key_pressed(KEY_POWER) && can_boot_android() == 0)
		 __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, "android");
	return TIMER_STOP;
}

static int charge_in_pos()
{
	int ret = -1;
	char buf[2];

	voltage_gate = get_voltage_gate();
	if (pipe(voltage_good_pipe) != 0) {
		LOGE("pipe error!\n");
		goto err;
	}
	do {
		read(voltage_good_pipe[0], buf, 2);
	} while (strncmp(buf, "OK", 2));
	ret = 0;
err:
	ui_stop_timer(battery_update_timer);
	ui_stop_timer(power_poll_timer);
	ui_restore_screensaver_delay();
	return ret;
}

int cos_main(int mode)
{
	int ret;

	charge_mode = mode;
	ret = find_power_supply(&battery, "Battery");
	ret |= find_power_supply(&charger, "USB");
	if (ret != 0) {
		ui_set_background(BACKGROUND_ICON_ERR);
		ui_msg(ALERT, LEFT, "Bad charger! Force shutdown...");
		sleep(5);
		force_shutdown();
	}
	ui_set_screensaver_delay(15000);
	battery_update_timer = ui_alloc_timer(battery_status_update_timer, 1, NULL);
	ui_start_timer(battery_update_timer,1000);
	power_poll_timer = ui_alloc_timer(power_poll_timer_cb, 0, NULL);
	ui_start_timer(power_poll_timer,1000);

	if (mode == MODE_POS)
		return charge_in_pos();

	//charge in COS mode
	modem_airplane();
	reboot_timer = ui_alloc_timer(reboot_timer_cb, 0, NULL);
	for (;;) {
		int key = ui_wait_key();
		ui_set_screen_state(1);
		if (key == KEY_POWER) {
			if (ui_key_pressed(KEY_POWER))
				ui_start_timer(reboot_timer, REBOOTTIMER_DELAY);
			else
				ui_stop_timer(reboot_timer);
		}
	}
	return 0;
}
