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
#ifndef POWER_H
#define POWER_H

#define IPC_DEVICE_NAME		    "/dev/mid_ipc"
#define IPC_READ_RR_FROM_OSNIB	0xC1
#define IPC_WRITE_RR_TO_OSNIB	0xC2
#define IPC_WRITE_ALARM_TO_OSNIB 0xC5
#define IPC_COLD_RESET		    0xC3
#define IPC_READ_VBATTCRIT	    0xC4
#define RR_SIGNED_MOS		    0x0
#define RR_SIGNED_COS           0xa
#define BATTERY_CAPACITY_FULL 100

typedef enum {
	CHARGE_STATUS_UNKOWN,
	CHARGE_STATUS_CHARGING,
	CHARGE_STATUS_DISCHARGING,
	CHARGE_STATUS_NOT_CHARGING,
	CHARGE_STATUS_FULL,
	CHARGE_STATUS_COUNT,
} charge_status;

struct power_supply_status
{
	char path[255];
	int present;
	int voltage_now;
	int current_now;
	int capacity;
	int temp;
	int charge_now;
	int charge_full;
	int charge_full_design;
	charge_status status;
};

int find_power_supply(struct power_supply_status *st, char *type);
void get_power_supply_status(struct power_supply_status *status);
int get_voltage_gate();
int can_boot_android();
void force_shutdown();
/* from modem.c */
void reset_modem(void);
void modem_airplane(void);

#endif
