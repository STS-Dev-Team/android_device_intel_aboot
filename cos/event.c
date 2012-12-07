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

#include <dirent.h>
#include <input.h>

#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/reboot.h>
#include <sys/msg.h>

#include <linux/netlink.h>
#include <linux/reboot.h>

#include <linux/rtc.h>
#include "recovery.h"

#define SYSFS_POWER_SUPPLY_INT	"/sys/class/power_supply"
#define SYSFS_VBATT_INT		"/sys/class/power_supply/%s/voltage_now"
#define SYSFS_CHGER_PRESENT_INT	"/sys/class/power_supply/%s/present"
#define SYSFS_BACKLIGHT_INT     "/sys/devices/virtual/backlight"
#define SYSFS_BACKLIGHT_VAL_INT "/sys/devices/virtual/backlight/%s/brightness"
#define BRIGHT_ON               "60"
#define BRIGHT_OFF              "0"
#define MAX_DEVICES             16
#define WAKE_LOCK               "/sys/power/wake_lock"
#define SCREEN_STATE            "/sys/power/state"
#define WAKE_UNLOCK             "/sys/power/wake_unlock"
#define RTC_FILE	"/dev/rtc0"
#define SYS_TEMP_INT				"/sys/class/thermal/thermal_zone0/temp"
#define MAX_TEMP					73000

typedef enum {
	POWER_SUPPLY_STATUS_CHARGING,
	POWER_SUPPLY_STATUS_FULL,
	POWER_SUPPLY_STATUS_COUNT,
} POWER_SUPPLY_STATUS;

static char *acceptable_status_text[] = {
	"Charging", "Full"
};

struct power_supply_status {
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

static struct power_supply_status charger;
static struct power_supply_status battery;
static struct pollfd ev_fds[MAX_DEVICES];
static unsigned ev_count = 0;
static int power_low = 0;

static int msgID;
static int boot_mos_flag;

static struct msgtype {
	long mtype;
	char buffer[100];
} msg;

static int is_power_low(void)
{
	return power_low;
}

int event_init(void)
{
	DIR *dir;
	struct dirent *de;
	int fd;
	dir = opendir("/dev");
	if (dir != 0) {
		while ((de = readdir(dir))) {
			if (strncmp(de->d_name, "event", 5))
				continue;
			fd = openat(dirfd(dir), de->d_name, O_RDONLY);
			if (fd < 0)
				continue;

			ev_fds[ev_count].fd = fd;
			ev_fds[ev_count].events = POLLIN;
			ev_count++;
			if (ev_count == MAX_DEVICES)
				break;
		}
	}
	return 0;
}

int event_wait(struct input_event *ev, unsigned dont_wait, __u16 type,
	       __u16 code, __s32 value)
{
	int r = 0;
	unsigned n;

	do {
		r = poll(ev_fds, ev_count, dont_wait ? 0 : -1);
		if (r > 0) {
			for (n = 0; n < ev_count; n++) {
				if (ev_fds[n].revents & POLLIN) {
					r = read(ev_fds[n].fd, ev, sizeof(*ev));
					if (r == sizeof(*ev) && ev->type == type
					    && ev->code == code
					    && ev->value == value)
						return 0;
				}
			}
		}
	} while (dont_wait == 0);

	return -1;
}

char *get_brightness_path(void)
{
	struct dirent *entry;
	DIR *dir;
	const char *name;
	char *path;

	dir = opendir(SYSFS_BACKLIGHT_INT);
	if (dir == NULL) {
		syslog(LOG_ERR, "Could not open %s\n", SYSFS_BACKLIGHT_INT);
		return NULL;
	}
	while ((entry = readdir(dir))) {
		name = entry->d_name;
		path =
		    malloc(strlen(SYSFS_BACKLIGHT_VAL_INT) + strlen(name) + 1);
		sprintf(path, SYSFS_BACKLIGHT_VAL_INT, name);

		if (access(path, 0) == 0) {
			closedir(dir);
			return path;
		}
	}
	closedir(dir);
	return NULL;
}

int get_back_brightness(void)
{
	FILE *file;
	int bright = -1;
	char *str;

	str = get_brightness_path();

	if ((file = fopen(str, "r+")) != NULL) {
		fscanf(file, "%d", &bright);
		fclose(file);
	} else {
		syslog(LOG_ERR, "Can't open brightness file!\n");
	}

	return bright;
}

static int set_back_brightness(char *bright_val)
{
	int ret = 0;
	char *str;
	FILE *file;

	str = get_brightness_path();

	if ((file = fopen(str, "w+")) != NULL) {
		fwrite(bright_val, (strlen(bright_val) + 1), 1, file);
		fclose(file);
		ret = 1;
	}

	return ret;
}

int turn_off_screen()
{
	FILE *file;
	int ret = 0;

	if ((file = fopen(WAKE_LOCK, "w")) != NULL) {
		fwrite("mywakelock\n", (strlen("mywakelock") + 1), 1, file);
		fclose(file);
		ret = 1;
	} else {
		syslog(LOG_ERR, "wake lock open error\n");
		ret = -1;
		goto err;
	}

	if ((file = fopen(SCREEN_STATE, "w")) != NULL) {
		fwrite("mem\n", (strlen("mem") + 1), 1, file);
		fclose(file);
		ret = 1;
	} else {
		syslog(LOG_ERR, "screen state open error\n");
		ret = -1;
	}

err:
	return ret;
}

int turn_on_screen()
{
	FILE *file;
	int ret = 0;

	if ((file = fopen(SCREEN_STATE, "w")) != NULL) {
		fwrite("on\n", (strlen("on") + 1), 1, file);
		fclose(file);
		ret = 1;
	} else {
		syslog(LOG_ERR, "screen state open error\n");
		ret = -1;
		goto err;
	}

	if ((file = fopen(WAKE_UNLOCK, "w")) != NULL) {
		fwrite("mywakelock\n", (strlen("mywakelock") + 1), 1, file);
		fclose(file);
		ret = 1;
	} else {
		syslog(LOG_ERR, "wake unlock open error\n");
		ret = -1;
	}
err:
	return ret;
}

int set_power_on_reason(int alarm_rang)
{
	unsigned char rbt_reason;
	int devfd = 0, errNo = 0, ret;

	rbt_reason = RR_SIGNED_MOS;

	if ((devfd = open(IPC_DEVICE_NAME, O_RDWR)) < 0) {
		syslog(LOG_ERR, "unable to open the DEVICE %s\n",
			IPC_DEVICE_NAME);
		ret = 0;
		goto err1;
	}

	if ((errNo = ioctl(devfd, IPC_WRITE_RR_TO_OSNIB, &rbt_reason)) < 0) {
		syslog(LOG_ERR,
			"ioctl for DEVICE %s, returns error-%d\n",
			IPC_DEVICE_NAME, errNo);
		ret = 0;
		goto err2;
	}

	if (alarm_rang)
		syslog(LOG_NOTICE, "RTC Alarm Fired ...\n");
	else
		syslog(LOG_NOTICE, "Clear the alarm flag here ...\n");
	if ((errNo = ioctl(devfd, IPC_WRITE_ALARM_TO_OSNIB, &alarm_rang)) < 0) {
		syslog(LOG_ERR,
			"ioctl for DEVICE %s, returns error-%d\n",
			IPC_DEVICE_NAME, errNo);
		ret = 0;
		goto err2;
	}
	ret = 1;
err2:
	close(devfd);
err1:
	return ret;
}

void on_display()
{
	/* Commented this out due to RUNTIME_PM still cause random reboot.
	   Will uncomment this after enabling RUNTIME_PM.
	   turn_on_screen(); */
	set_back_brightness(BRIGHT_ON);
}

void off_display()
{
	set_back_brightness(BRIGHT_OFF);
	/* Commented this out due to RUNTIME_PM still cause random reboot.
	   Will uncomment this after enabling RUNTIME_PM.
	   turn_off_screen(); */
}

void boot_android()
{
	if (!is_power_low() && set_power_on_reason(0)) {
		syslog(LOG_NOTICE, "release power button reboot to MOS\n");
		msg.mtype = COS_BOOT_MOS;
		strcpy(msg.buffer, COS_BOOT_MOS_BUF);
		msgsnd(msgID, &msg, sizeof(struct msgtype), 0);
		boot_mos_flag = 1;
		on_display();
	}
}

static void try_read_property(char *src, char *prop_name, int *value)
{
	char *res = strstr(src, prop_name);
	if (res) {
		/*skip prop_name and '=' */
		res += strlen(prop_name) + 1;
		*value = atoi(res);
	}
}

static int get_power_supply_status(struct power_supply_status *status)
{
	FILE *f = fopen(status->path, "r");
	char buf[256];
	char *res;
	int ret = 0;
	POWER_SUPPLY_STATUS i;

	if (f == NULL) {
		syslog(LOG_ERR, "unable to open status file\n");
		return -1;
	}
	while (fgets(buf, 256, f)) {
		try_read_property(buf, "PRESENT", &status->present);
		try_read_property(buf, "VOLTAGE_NOW", &status->voltage_now);
		try_read_property(buf, "CURRENT_NOW", &status->current_now);
		try_read_property(buf, "CAPACITY", &status->capacity);
		try_read_property(buf, "TEMP", &status->temp);
		try_read_property(buf, "CHARGE_NOW", &status->charge_now);
		try_read_property(buf, "CHARGE_FULL", &status->charge_full);
		try_read_property(buf, "CHARGE_FULL_DESIGN",
				  &status->charge_full_design);
		res = strstr(buf, "STATUS");
                if (res && charger.present) {
			res += strlen("STATUS") + 1;
			for (i = 0; i < POWER_SUPPLY_STATUS_COUNT; i++) {
				if (strncmp(res, acceptable_status_text[i],
						strlen(acceptable_status_text[i])) == 0) {
					status->charging_status = i;
					break;
				}
			}
			if (i >= POWER_SUPPLY_STATUS_COUNT) {
				syslog(LOG_ERR, "Abnormal status: %s\n", res);
				ret = -1;
			}
		}

	}
	fclose(f);
	return ret;
}

/* Get the IA_APPS_RUN in uV from the SMIP */
static int get_voltage_gate()
{
	int devfd, errNo;
	unsigned int read_buf;
	unsigned int voltage_gate, ret = 0;

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
static int find_power_supply(struct power_supply_status *st, char *type)
{
	DIR *pDir;
	struct dirent *ent = NULL;
	char path[255], buf[255];
	int i;
	for (i = 0; i < 40; i++) {
		pDir = opendir(SYSFS_POWER_SUPPLY_INT);
		while ((ent = readdir(pDir)) != NULL) {
			snprintf(path, 255, SYSFS_POWER_SUPPLY_INT "/%s/type",
				 ent->d_name);
			if (readFromFile(path, buf, 255) > 0
			    && strstr(buf, type) != NULL) {
				snprintf(st->path, 255,
					 SYSFS_POWER_SUPPLY_INT "/%s/uevent",
					 ent->d_name);
				return 0;
			}
			syslog(LOG_ERR, "non matching power_supply %s != %s\n",
				buf, type);
		}
		usleep(100000);
	}
	syslog(LOG_ERR, "did not find power_supply %s\n", type);
	return -ENOENT;
}

static void *power_off_task(void *arg)
{
	syslog(LOG_NOTICE, "Charger removal or abnormal status detected ...\n");
	syslog(LOG_NOTICE, "It will shutdown in 5s ...\n");
	sleep(5);
	reboot(LINUX_REBOOT_CMD_POWER_OFF);
	return NULL;
}

static void update_status(int voltage_gate)
{
	msg.mtype = COS_BAT_STATUS;
	if (battery.charging_status == POWER_SUPPLY_STATUS_FULL)
		sprintf(msg.buffer, "%s", "FULL");
	else
		sprintf(msg.buffer, "%d", battery.capacity);
	msgsnd(msgID, &msg, sizeof(struct msgtype), 0);

	if (battery.voltage_now < voltage_gate) {
		power_low = 1;
		msg.mtype = COS_VOL_GATE;
		strcpy(msg.buffer, COS_VOL_LOW);
		msgsnd(msgID, &msg,
			sizeof(struct msgtype), 0);
	} else {
		power_low = 0;
		msg.mtype = COS_VOL_GATE;
		strcpy(msg.buffer, COS_VOL_HIGH);
		msgsnd(msgID, &msg,
			sizeof(struct msgtype), 0);
	}

}

/* thread to monitor the power supply */
void *power_monitor_event(void *arg)
{
	struct sockaddr_nl nls;
	struct pollfd pfd;
	char buf[512];
	int voltage_gate;
	pthread_t thr;
	pthread_attr_t atr;
	int schedule_flag = 0;

	syslog(LOG_DEBUG, "power_monitor_event thread created ...\n");
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

	if (pthread_attr_init(&atr) != 0) {
		syslog(LOG_ERR, "ERROR: Unable to set event thread attribute.\n");
		return NULL;
	}

	get_power_supply_status(&charger);
	if ((get_power_supply_status(&battery) < 0 || !charger.present) && !schedule_flag) {
		if (pthread_create(&thr, &atr, power_off_task, NULL) != 0)
			syslog(LOG_ERR, "ERROR: Unable to create power_off_task thread.\n");
		else {
			schedule_flag = 1;
			msg.mtype = COS_CHRG_EVENT;
			strcpy(msg.buffer, COS_CHRG_REMOVE);
			msgsnd(msgID, &msg, sizeof(struct msgtype), 0);

			msg.mtype = COS_BAT_STATUS;
			if (battery.charging_status == POWER_SUPPLY_STATUS_FULL)
				sprintf(msg.buffer, "%s", "FULL");
			else
				sprintf(msg.buffer, "%d", battery.capacity);
			msgsnd(msgID, &msg, sizeof(struct msgtype), 0);

		}
	}

	if (!schedule_flag)
		update_status(voltage_gate);

	while (-1 != poll(&pfd, 1, -1)) {
		int i, len = recv(pfd.fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (len == -1) {
			syslog(LOG_ERR, "receive failed \n");
			break;
		}
		i = 0;

		while (i < len) {
			i += strlen(buf + i) + 1;
			if (!strncmp
			    (buf + i, "SUBSYSTEM=power_supply",
			     strlen("SUBSYSTEM=power_supply"))) {

				get_power_supply_status(&charger);
				if ((get_power_supply_status(&battery) < 0 || !charger.present) && !schedule_flag) {
					if (pthread_create(&thr, &atr, power_off_task, NULL) != 0)
						syslog(LOG_ERR, "ERROR: Unable to create power_off_press thread.\n");
					else {
						schedule_flag = 1;
						msg.mtype = COS_CHRG_EVENT;
						strcpy(msg.buffer, COS_CHRG_REMOVE);
						msgsnd(msgID, &msg, sizeof(struct msgtype), 0);
					}

				}
				if (!get_power_supply_status(&battery) && charger.present && schedule_flag) {
					syslog(LOG_NOTICE, "Back to charging status ...\n");
					pthread_cancel(thr);
					schedule_flag = 0;
					msg.mtype = COS_CHRG_EVENT;
					strcpy(msg.buffer, COS_CHRG_INSERT);
					msgsnd(msgID, &msg, sizeof(struct msgtype), 0);
				}

				if (!schedule_flag)
					update_status(voltage_gate);

				break;
			}
		}
	}
	return NULL;
}

void *powerbtn_event(void *arg)
{
	int bright_val = 0;
	struct input_event ev;

	syslog(LOG_DEBUG, "powerbtn_event thread created ...\n");

	event_init();
	boot_mos_flag = 0;
	signal(SIGALRM, off_display);
	alarm(5);

	do {
		event_wait(&ev, 0, EV_KEY, KEY_POWER, 1);

		alarm(0);
		bright_val = get_back_brightness();
		if (bright_val < 0)
			continue;

		if (bright_val == 0)
			on_display();

		signal(SIGALRM, boot_android);
		alarm(3);
		event_wait(&ev, 0, EV_KEY, KEY_POWER, 0);
		alarm(0);
		if (boot_mos_flag) {
			syslog(LOG_INFO, "Now reboot to MOS ...\n");
			boot_mos_flag = 0;
			reboot(LINUX_REBOOT_CMD_RESTART);
		} else if (bright_val != 0)
			off_display();
		else {
			signal(SIGALRM, off_display);
			alarm(5);
		}
	} while (1);

	return NULL;
}

void *rtc_alarm_event(void *arg)
{
	unsigned long data;
	int rtc_fd, ret;

	rtc_fd = open(RTC_FILE, O_RDONLY, 0);
	if (rtc_fd < 0) {
		syslog(LOG_ERR, "unable to open the DEVICE %s\n", RTC_FILE);
		goto err1;
	}
	/* Enable alarm interrupts */
	ret = ioctl(rtc_fd, RTC_AIE_ON, 0);
	if (ret == -1) {
		syslog(LOG_ERR, "rtc ioctl RTC_AIE_ON error\n");
		goto err2;
	}

	/* This blocks until the alarm ring causes an interrupt */
	ret = read(rtc_fd, &data, sizeof(unsigned long));
	if (ret < 0) {
		syslog(LOG_ERR, "rtc read error\n");
		goto err2;
	}
	syslog(LOG_NOTICE, "RTC Alarm rang.Now reboot to MOS ...\n");
	if (!is_power_low() && set_power_on_reason(1))
		reboot(LINUX_REBOOT_CMD_RESTART);

err2:
	close(rtc_fd);
err1:
	return NULL;
}

void *temp_monitor_event(void *arg)
{
	int temp;
	FILE *temp_fd;

	temp_fd = fopen(SYS_TEMP_INT, "r");
	if (temp_fd == NULL) {
		syslog(LOG_ERR, "unable to open file %s\n", SYS_TEMP_INT);
		return NULL;
	}
	do {
		fseek(temp_fd, 0, SEEK_SET);
		fscanf(temp_fd, "%d\n", &temp);
		syslog(LOG_DEBUG, "Current temperature: %d\n", temp);
		if (temp > MAX_TEMP) {
			msg.mtype = COS_TEMP_EVENT;
			msgsnd(msgID, &msg, sizeof(struct msgtype), 0);
			syslog(LOG_NOTICE, "Temperature is high, shutdown system.\n");
			sleep(5);
			reboot(LINUX_REBOOT_CMD_POWER_OFF);
		}
		sleep(30);
	} while (1);
	fclose(temp_fd);
	return NULL;
}

int cos_event_main(int argc, char *argv[], int msgid)
{
	pthread_t thr;
	pthread_attr_t atr;
	int err;

	msgID = msgid;
	err = find_power_supply(&battery, "Battery");
	err |= find_power_supply(&charger, "USB");
	if (err) {
		syslog(LOG_ERR,
			"There are issues with battery or charger driver. shutting down\n");
		sleep(5);
		reboot(LINUX_REBOOT_CMD_POWER_OFF);
	}

	if (pthread_attr_init(&atr) != 0) {
		syslog(LOG_ERR, "ERROR: Unable to set event thread attribute.\n");
	} else {
		if (pthread_create(&thr, &atr, powerbtn_event, NULL) != 0)
			syslog(LOG_ERR,
				"ERROR: Unable to create power_btn_press thread.\n");
		if (pthread_create(&thr, &atr, power_monitor_event, NULL) != 0)
			syslog(LOG_ERR,
				"ERROR: Unable to create power_monitor thread.\n");
		if (pthread_create(&thr, &atr, rtc_alarm_event, NULL) != 0)
			syslog(LOG_ERR, "ERROR: Unable to create usbcharger thread.\n");
		if (pthread_create(&thr, &atr, temp_monitor_event, NULL) != 0)
			syslog(LOG_ERR, "ERROR: Unable to create temp monitor thread.\n");
	}
	while (1) {
		sleep(100);
	}
}
