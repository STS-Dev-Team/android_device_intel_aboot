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
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/reboot.h>
#include "common.h"
#include "power.h"
#include "firmware.h"

#define SYSFS_POWER_SUPPLY_INT	"/sys/class/power_supply"
#define SYSFS_FORCE_SHUTDOWN	"/sys/module/intel_mid_osip/parameters/force_shutdown_occured"

static char *charge_status_text[] = {
	"unknown", "Charging", "Discharging", "Not charging", "Full"
};

static int readFromFile(const char *path, char *buf, size_t size)
{
	if (!path)
		return -1;
	int fd = open(path, O_RDONLY, 0);
	if (fd == -1) {
		return -1;
	}
	size_t count = read(fd, buf, size);
	if (count > 0) {
		count = (count < size) ? count : size - 1;
		while (count > 0 && buf[count - 1] == '\n')
			count--;
		buf[count] = '\0';
	} else {
		buf[0] = '\0';
	}
	close(fd);
	return count;
}

/* find a power_supply that is of given type in sysfs
   retry for 4 sec until found.
 */
int find_power_supply(struct power_supply_status *st, char *type)
{
	DIR *pDir;
	struct dirent *ent = NULL;
	char path[255], buf[255];
	int i;
	for (i = 0; i < 40; i++) {
		pDir = opendir(SYSFS_POWER_SUPPLY_INT);
		while ((ent = readdir(pDir)) != NULL) {
			snprintf(path, 255, SYSFS_POWER_SUPPLY_INT"/%s/type", ent->d_name);
			if (readFromFile(path, buf, 255)>0 &&
			    strstr(buf, type) != NULL) {
				snprintf(st->path, 255, SYSFS_POWER_SUPPLY_INT"/%s/uevent", ent->d_name);
				return 0;
			}
		}
		usleep(100000);
	}
	LOGE("did not find power_supply %s\n", type);
	return -ENOENT;
}
void try_read_property(char *src, char *prop_name, int *value)
{
	char *res = strstr(src, prop_name);
	if (res) {
		/*skip prop_name and '='*/
		res += strlen(prop_name) + 1;
		*value = atoi(res);
	}
}
void get_power_supply_status(struct power_supply_status *status)
{
	FILE *f = fopen(status->path, "r");
	char buf[256];
	int i;

	if(f == NULL) {
		LOGE("unable to open status file");
		return;
	}
	while(fgets(buf, 256, f)) {
		try_read_property(buf, "PRESENT", &status->present);
		try_read_property(buf, "VOLTAGE_NOW", &status->voltage_now);
		try_read_property(buf, "CURRENT_NOW", &status->current_now);
		try_read_property(buf, "CAPACITY", &status->capacity);
		try_read_property(buf, "TEMP", &status->temp);
		try_read_property(buf, "CHARGE_NOW", &status->charge_now);
		try_read_property(buf, "CHARGE_FULL", &status->charge_full);
		try_read_property(buf, "CHARGE_FULL_DESIGN", &status->charge_full_design);
		char *res = strstr(buf, "STATUS");
		if (res) {
			status->status = CHARGE_STATUS_UNKOWN;
			res += strlen("STATUS") + 1;
			for (i = 0; i < CHARGE_STATUS_COUNT; i++) {
				if (strncmp(res, charge_status_text[i], strlen(charge_status_text[i])) == 0) {
					status->status = i;
					break;
				}
			}
		}
	}
	fclose(f);
}

/* Get the IA_APPS_RUN in uV from the SMIP */
int get_voltage_gate()
{
	int devfd, errNo;
	unsigned int read_buf;
	unsigned int voltage_gate, ret=0;

	if ((devfd = open(IPC_DEVICE_NAME, O_RDWR)) < 0) {
		LOGE("unable to open the DEVICE %s\n",
			IPC_DEVICE_NAME);
		ret = 0;
		goto err1;
	}

	if ((errNo = ioctl(devfd, IPC_READ_VBATTCRIT, &read_buf)) < 0) {
		LOGE("ioctl for DEVICE %s, returns error-%d\n",
			IPC_DEVICE_NAME, errNo);
		ret = 0;
		goto err2;
	}

	/*The VBATTCRIT value is the 2nd 2-bytes of this 4-bytes */
	voltage_gate = read_buf >> 16;
	/* Change mV to uV to align with the current voltage read from sysfs interface */
	ret = voltage_gate * 1000;
err2:
	close(devfd);
err1:
	return ret;
}

int can_boot_android()
{
	struct power_supply_status battery;

	find_power_supply(&battery, "Battery");
	get_power_supply_status(&battery);
	if (battery.voltage_now < get_voltage_gate())
		return -1;
	else
		return 0;
}

void force_shutdown()
{
	int fd;
	char c = '1';

	LOGI("[SHTDWN] %s, force shutdown", __func__);
	if ((fd = open(SYSFS_FORCE_SHUTDOWN, O_WRONLY)) < 0) {
		write(fd, &c, 1);
		close(fd);
	} else
		LOGI("[SHUTDOWN] Open %s error!\n", SYSFS_FORCE_SHUTDOWN);
	sync();
	reboot(LINUX_REBOOT_CMD_POWER_OFF);
}
