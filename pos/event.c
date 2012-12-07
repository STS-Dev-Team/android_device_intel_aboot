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

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <fcntl.h>
#include <signal.h>
#include <input.h>
#include <dirent.h>

#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/reboot.h>

#include <linux/netlink.h>
#include <linux/reboot.h>
#include <sys/msg.h>

#include "recovery.h"

#define SYSFS_POWER_SUPPLY_INT  "/sys/class/power_supply"
#define BRIGHT_ON               "60"
#define BRIGHT_OFF              "0"

typedef enum {
	POWER_SUPPLY_STATUS_UNKNOWN,
	POWER_SUPPLY_STATUS_CHARGING,
	POWER_SUPPLY_STATUS_DISCHARGING,
	POWER_SUPPLY_STATUS_NOT_CHARGING,
	POWER_SUPPLY_STATUS_FULL,
	POWER_SUPPLY_STATUS_COUNT,
} POWER_SUPPLY_STATUS;

/* Extract from Kernel's power_supply_sysfs.c */
static char *status_text[] = {
	"Unknown", "Charging", "Discharging", "Not charging", "Full"
};

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
	POWER_SUPPLY_STATUS charging_status;
};

static struct msgtype {
	long mtype;
	char buffer[100];
} msg;

static struct power_supply_status charger;
static struct power_supply_status battery;
static int power_low = 0;
static int msgID;

extern int event_init(void);
extern int event_wait(struct input_event *ev, unsigned dont_wait, __u16 type,
		__u16 code, __s32 value);
extern char *get_brightness_path(void);
extern int get_back_brightness(void);

int is_power_low(void)
{
	return power_low;
}

static void set_back_brightness()
{
	char *str;
	FILE *file;
	char *bright;

	if (get_back_brightness())
		bright = BRIGHT_OFF;
	else
		bright = BRIGHT_ON;

	str = get_brightness_path();

	if ((file = fopen(str, "w+")) != NULL) {
		fwrite(bright, (strlen(bright) + 1), 1, file);
		fclose(file);
	}
}

static void *powerbtn_event(void *arg)
{
	int bright_val = 0;
	struct input_event ev;

	event_init();
	signal(SIGALRM, set_back_brightness);
	alarm(5);

	do {
		event_wait(&ev, 0, EV_KEY, KEY_POWER, 1);

		bright_val = get_back_brightness();
		if (bright_val < 0)
			continue;

		if (bright_val == 0) {
			set_back_brightness();
			signal(SIGALRM, set_back_brightness);
			alarm(5);
		} else {
			alarm(0);
			set_back_brightness();
		}
	} while (1);

	return NULL;
}

static int readFromFile(const char *path, char *buf, size_t size)
{
	if (!path)
		return -1;
	int fd = open(path, O_RDONLY, 0);
	if (fd == -1) {
		syslog(LOG_ERR, "Could not open '%s'", path);
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
			syslog(LOG_INFO, "non matching power_supply %s != %s\n", buf, type);
		}
		usleep(100000);
	}
	syslog(LOG_ERR, "did not find power_supply %s\n", type);
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

int get_power_supply_status(struct power_supply_status *status)
{
	FILE *f = fopen(status->path, "r");
	char buf[256];
	char *res = NULL;
	int i = 0;

	if(f == NULL) {
		syslog(LOG_ERR, "unable to open status file\n");
		return -1;
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

		res = strstr(buf, "STATUS");
                if (res) {
			status->charging_status = POWER_SUPPLY_STATUS_UNKNOWN;
			res += strlen("STATUS") + 1;
			for (i = 0; i < POWER_SUPPLY_STATUS_COUNT; i++) {
				if (strncmp(res, status_text[i], strlen(status_text[i])) == 0)
				{
					status->charging_status = i;
					break;
				}
			}
			if (i >= POWER_SUPPLY_STATUS_COUNT) {
				syslog(LOG_ERR, "Abnormal status: '%s'\n", res);
			}
		}
	}
	fclose(f);
	return 0;
}

/* Get the IA_APPS_RUN in uV from the SMIP */
static int get_voltage_gate()
{
	int devfd, errNo;
	unsigned int read_buf;
	unsigned int voltage_gate, ret=0;

	if ((devfd = open(IPC_DEVICE_NAME, O_RDWR)) < 0) {
		syslog(LOG_ERR, "unable to open the DEVICE %s\n",
			IPC_DEVICE_NAME);
		ret = 0;
		goto err1;
	}

	if ((errNo = ioctl(devfd, IPC_READ_VBATTCRIT, &read_buf)) < 0) {
		syslog(LOG_ERR, "ioctl for DEVICE %s, returns error-%d\n",
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

static void *power_supply_event(void *arg)
{
	struct sockaddr_nl nls;
	struct pollfd pfd;
	char buf[512];
	int voltage_gate;

	syslog(LOG_DEBUG, "power_supply_event thread created ...\n");
	if ((voltage_gate = get_voltage_gate()) < 0) {
		syslog(LOG_ERR, "Get voltage gate error!\n");
		return NULL;
	}

	memset(&nls, 0, sizeof(struct sockaddr_nl));
	nls.nl_family = AF_NETLINK;
	nls.nl_pid = getpid();
	nls.nl_groups = -1;

	pfd.events = POLLIN;
	pfd.fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);

	if (bind(pfd.fd, (void *)&nls, sizeof(struct sockaddr_nl))) {
		syslog(LOG_ERR, "Bind failed\n");
		return NULL;
	}
	get_power_supply_status(&battery);
	msg.mtype = BAT_STATUS;
	sprintf(msg.buffer,"%d", battery.capacity);
	msgsnd(msgID, &msg, sizeof(struct msgtype), 0);

	while (-1 != poll(&pfd, 1, -1)) {
		int i, len = recv(pfd.fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (len == -1) {
			syslog(LOG_ERR, "receive failed \n");
			break;
		}
		i = 0;
		while (i < len) {
			i += strlen(buf + i) + 1;
			if (!strncmp(buf + i, "SUBSYSTEM=power_supply", strlen("SUBSYSTEM=power_supply"))) {
				get_power_supply_status(&charger);
				get_power_supply_status(&battery);
				syslog(LOG_INFO, "\rcapacity = %d%% V= %.2fV->%.2fV I= %.2fmA T=%.1fC\n",
					battery.capacity,
					battery.voltage_now/1000000.,
					voltage_gate/1000000.,
					battery.current_now/1000.,
					battery.temp/10.
					);
				if (charger.present == 0) {
					msg.mtype = CHRG_EVENT;
					strcpy(msg.buffer, CHRG_REMOVE);
					msgsnd(msgID, &msg, sizeof(struct msgtype), 0);
				} else {
					msg.mtype = CHRG_EVENT;
					strcpy(msg.buffer, CHRG_INSERT);
					msgsnd(msgID, &msg, sizeof(struct msgtype), 0);
				}

				/* TODO: This will give repeated notices to user, but we should
				alert user by the popup window, this GUI api was not completed. */
				if (battery.capacity == 0 &&
					(!charger.present || (battery.charging_status != POWER_SUPPLY_STATUS_CHARGING))) {
					/* Need to do the test again in 5sec */
					printf("WARNING:Power too low, auto shutdown (1st attempt)\n");
					syslog(LOG_WARNING, "WARNING:Power too low, auto shutdown (1st attempt)\n");
					sleep(5);
					get_power_supply_status(&battery);
					if (battery.capacity == 0 &&
						(!charger.present || (battery.charging_status != POWER_SUPPLY_STATUS_CHARGING))) {
						printf("WARNING:Power too low, auto shutdown (1st attempt)\n");
						syslog(LOG_WARNING, "WARNING:Power too low, auto shutdown in 5sec!\n");
						sleep(5);
						reboot(LINUX_REBOOT_CMD_POWER_OFF);
					}
					break;
				} else if (battery.capacity <= 10 &&
						(!charger.present || (battery.charging_status != POWER_SUPPLY_STATUS_CHARGING))) {
					printf("WARNING:Low power! please plug in the power.\n");
					syslog(LOG_WARNING, "WARNING:Low power! please plug in the power.\n");
				}
				msg.mtype = BAT_STATUS;
				sprintf(msg.buffer,"%d", battery.capacity);
				msgsnd(msgID, &msg, sizeof(struct msgtype), 0);

				if (battery.voltage_now < voltage_gate) {
					power_low = 1;
				} else {
					power_low = 0;
				}

				break;
			}
		}
	}
	return NULL;
}

int pos_event_main(int argc, char *argv[], int msgid)
{
	int err;
	pthread_t thr;
	pthread_attr_t atr;

	msgID = msgid;
	err = find_power_supply(&battery, "Battery");
	err |= find_power_supply(&charger, "USB");
	if (err) {
		syslog(LOG_ERR, "There are issues with battery or charger driver. shutting down\n");
		sleep(5);
		reboot(LINUX_REBOOT_CMD_POWER_OFF);
	}

	if (pthread_attr_init(&atr) != 0) {
		syslog(LOG_ERR, "ERROR: Unable to set event thread attribute.\n");
	} else {
		if (pthread_create(&thr, &atr, powerbtn_event, NULL) != 0)
			syslog(LOG_ERR, "ERROR: Unable to create prower_btn_press thread.\n");
		if (pthread_create(&thr, &atr, power_supply_event, NULL) != 0)
			syslog(LOG_ERR, "ERROR: Unable to create power supply thread.\n");
	}
	return 1;
}
