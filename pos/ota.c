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
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <dirent.h>

#include "ota.h"
#include "debug.h"
#include "fastboot.h"
#include "ui.h"

/* POS's legacy, will reconstruct this code */
#define CMD_BOOT_DEV        "bootdev"
#define CMD_BOOT_DEV_SDCARD "sdcard"
#define CMD_BOOT_DEV_SDCARD1 "sdcard1"
#define CMD_BOOT_DEV_SDCARD2 "sdcard2"

#define FACTORY  0
#define DATA     1
#define SDCARD   2
#define RECOVERY 3
#define SYSTEM   4
#define CACHE    5

#define EMMC_SYS_ENTRY    "/sys/devices/pci0000:00/0000:00:01.0/mmc_host/mmc0"
#define SDCARD_SYS_ENTRY  "/sys/devices/pci0000:00/0000:00:04.0/mmc_host/mmc1"

#define IPC_WRITE_RR_TO_OSNIB	0xC2
#define IPC_DEVICE_NAME		"/dev/mid_ipc"
#define RR_SIGNED_MOS		0x0

extern void write_to_user(char *, ...);
extern void cmd_erase(const char *, void *, unsigned);
extern int mount_partition(int);
extern int umount_partition(int);

static const struct option OPTIONS[] = {
	{"update_package", required_argument, NULL, 'u'},
	{"type", required_argument, NULL, 't'},
	{"sdcard", no_argument, NULL, 's'},
	{"wipe_data", no_argument, NULL, 'w'},
	{"factory_restore", no_argument, NULL, 'f'},
	{NULL, 0, NULL, 0},
};

static const int MAX_ARG_LENGTH = 4096;
static const int MAX_ARGS = 100;

static int mount_package(const char *);
//static int umount_package(const char *);

static int run_command(char *cmd)
{
	int status = system(cmd);
	int sys_status = WEXITSTATUS(status);

	if (sys_status != 0)
		write_to_user("system error message: %s\n",
			      strerror(sys_status));
	return sys_status ? -1 : 0;
}

static void check_and_fclose(FILE * fp, const char *name)
{
	fflush(fp);
	if (ferror(fp))
		printf("Error in %s\n(%s)\n", name, strerror(errno));
	fclose(fp);		//open it, busybox will not work.
}

static int get_args(int *argc, char ***argv)
{

	FILE *fp = fopen(COMMAND_FILE, "r");

	if (fp == NULL) {
		write_to_user("not get pos cmd.\n");
		return INSTALL_INTERRUPT;
	} else {
		char *argv0 = (*argv)[0];
		char buf[MAX_ARG_LENGTH];

		*argv = (char **)malloc(sizeof(char *) * MAX_ARGS);
		(*argv)[0] = argv0;	// use the same program name

		for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
			if (!fgets(buf, sizeof(buf), fp))
				break;
			(*argv)[*argc] = strdup(strtok(buf, "\n"));	// Strip newline.
		}

		check_and_fclose(fp, COMMAND_FILE);
		printf("Got arguments from %s\n", COMMAND_FILE);
	}

	return INSTALL_SUCCESS;
}

static int remove_cmd()
{
	mount_partition(CACHE);
	if (unlink(COMMAND_FILE) != 0)
		perror("unlink commandfile");
	umount_partition(CACHE);
	return INSTALL_SUCCESS;
}

static char *get_intent(const int status)
{
	//TODO detail error status
	if (status == INSTALL_SUCCESS) {
		return "--result=success";
	} else {
		return "--result=fail\n--extra=fail to update";
	}
}

static int ota_finish(const int status)
{
	int fd = -1;
	char *send_intent;

	send_intent = get_intent(status);
	mount_partition(DATA);
	if (access(INTENT_FOLDER, F_OK)) {
		if (mkdir(INTENT_FOLDER, 0755)) {
			write_to_user("mkdir error, message: %s\n",
				      strerror(errno));
			goto error;
		}
	}

	fd = open(INTENT_FILE, O_RDWR | O_CREAT, 0777);
	if (fd >= 0) {
		write(fd, send_intent, strlen(send_intent));
		close(fd);
		sync();
	} else
		write_to_user("can't create %s\n", INTENT_FILE);

	remove_cmd();

error:
	umount_partition(DATA);
	run_command("update_osip --restore");
	write_to_user("ota finished, rebooting...\n");

	return 0;
}

static int device_wipe_data()
{
	cmd_erase("data", NULL, 0);
	return (run_command("update_osip --restore") ? INSTALL_ERROR :
		INSTALL_SUCCESS);
}

#if 0
static int add_ifwi_flag()
{
	int fd = -1;
	mount_partition(CACHE);
	fd = open(UPDATE_IFWI_FLAG, O_RDWR | O_CREAT, 0777);
	if (fd >= 0) {
		close(fd);
		sync();
	} else
		write_to_user("can't create %s\n", UPDATE_IFWI_FLAG);
	umount_partition(CACHE);
	return INSTALL_SUCCESS;
}

static int update_ifwi()
{
	char cmd[512] = { '\0', };

	/* step one: unzip ifwi.zip to ramdisk */
	sprintf(cmd, "cp %s/android/firmware/ifwi_firmware.bin -d /tmp/",
		PACKAGE_PATH);
	if (run_command(cmd))
		goto oops;
	sprintf(cmd, "cp %s/android/firmware/dnx_firmware.bin -d /tmp/",
		PACKAGE_PATH);
	if (run_command(cmd))
		goto oops;
	/* step two: umount package */
	umount_package(PACKAGE_PATH);
	/* step three: write flag. flag file had = updated, no flag file = haven't updated */
	add_ifwi_flag();
	/* step four: umount emmc */
	if (run_command("umount /mnt/sdcard"))
		goto oops;
	/* step five: update ifwi */
	sprintf(cmd,
		"ifwi-update /tmp/dnx_firmware.bin /tmp/ifwi_firmware.bin");
	if (run_command(cmd))
		goto oops;
	printf("System will reboot in 3s ....\n");
	sleep(3);
	reboot(0xA1B2C3D4);

	return INSTALL_SUCCESS;
oops:
	return INSTALL_ERROR;
}

static int check_ifwi_update()
{
	mount_partition(CACHE);
	if (access(UPDATE_IFWI_FLAG, F_OK)) {
		if (update_ifwi())
			goto oops;
	} else {
		if (unlink(UPDATE_IFWI_FLAG) != 0)
			perror("unlink ifwi flag");
	}
	umount_partition(CACHE);
	return INSTALL_SUCCESS;

oops:
	write_to_user("update error\n");

	return INSTALL_ERROR;

}
#endif

static int mount_package(const char *root_path)
{
	char cmd[512] = { '\0', };

	if (access(PACKAGE_PATH, F_OK)) {
		if (mkdir(PACKAGE_PATH, 0755))
			printf("mkdir error, msg: %s\n", strerror(errno));
	}

	sprintf(cmd, "mount %s %s -t iso9660 -o loop", root_path, PACKAGE_PATH);
	if (run_command(cmd)) {
		write_to_user("command fail: %s\n", cmd);
		return INSTALL_ERROR;
	}

	return INSTALL_SUCCESS;
}

#if 0
static int umount_package(const char *root_path)
{
	char cmd[512] = { '\0', };
	sprintf(cmd, "umount -d %s", root_path);
	if (run_command(cmd)) {
		write_to_user("command fail: %s\n", cmd);
		return INSTALL_ERROR;
	}

	return INSTALL_SUCCESS;
}
#endif

static int erase_mmcblock(void)
{
	char system_image[512] = { '\0', };

	write_to_user("clean user data.\n");
	cmd_erase("data", NULL, 0);

	sprintf(system_image, "%s/%s",
		PACKAGE_PATH, "android/system/system.tar.gz");
	if (access((const char *)system_image, F_OK)) {
		write_to_user("system image not found.\n");
		return INSTALL_SUCCESS;
	}

	if (mount_partition(CACHE))
		goto oops;
	if (!access(UPDATE_LOG_FILE, F_OK)) {
		write_to_user("update log file has found.\n");
	} else {
		cmd_erase("system", NULL, 0);
	}
	umount_partition(CACHE);

	return INSTALL_SUCCESS;
oops:
	return INSTALL_ERROR;

}

static int ota_flash()
{
	char cmd[512] = { '\0', };

	if (mount_partition(SYSTEM))
		goto oops;
	if (mount_partition(CACHE))
		goto oops;
	sprintf(cmd, "/bin/sh %s/setup.sh", PACKAGE_PATH);
	write_to_user("run command: %s\n", cmd);

	if (run_command(cmd))
		goto oops;
	umount_partition(SYSTEM);
	umount_partition(CACHE);

	return INSTALL_SUCCESS;
oops:
	return INSTALL_ERROR;

}

static int device_factory_restore()
{
	if (mount_partition(FACTORY))
		goto oops;
	write_to_user("mount factory\n");
	if (mount_package(RESTORE_PATH))
		goto oops;
	write_to_user("mount package\n");
	if (erase_mmcblock())
		goto oops;
	if (ota_flash())
		goto oops;

	return INSTALL_SUCCESS;
oops:
	write_to_user("update error\n");

	return INSTALL_ERROR;
}

#define CMD_IMAGE_VERIFICATION "/chaabi/signed_image_verify.out -f %s"

static int install_ota(const char *root_path)
{
	char cmd[512];

	sprintf(cmd, CMD_IMAGE_VERIFICATION, root_path);
	if (system(cmd)) {
		write_to_user("package verification failed\n");
		goto oops;
	}

	if (mount_package(root_path))
		goto oops;
	//if (check_ifwi_update())
	//goto oops;
	if (erase_mmcblock())
		goto oops;
	if (ota_flash())
		goto oops;

	return INSTALL_SUCCESS;

oops:
	write_to_user("update error\n");

	return INSTALL_ERROR;
}

static int install_fota(const char *root_path)
{
	/* TODO: ... */
	return INSTALL_SUCCESS;
}

#define BUF_LEN  100

/* return block devices' block number under /dev/block/.
 * path is the sys entry for block device (eMMc or SDcard)
 */
int blk_number(const char *path, int *blknum)
{
	char c, *ptr;
	char path_buf[BUF_LEN];
	struct dirent *dirp;
	DIR *dp;

	strcpy(path_buf, path);
	if (path_buf[strlen(path_buf)-1] != '/')
		strcat(path_buf, "/");

	if ((dp = opendir(path_buf)) == NULL)
		goto oops;
	while ((dirp = readdir(dp)) != NULL) {
		if (strstr(dirp->d_name, "mmc")) {
			strcat(path_buf, dirp->d_name);
			strcat(path_buf, "/block/");
			break;
		}
	}

	if (dirp == NULL)
		goto oops;
	closedir(dp);

	if ((dp = opendir(path_buf)) == NULL)
		goto oops;
	while ((dirp = readdir(dp)) != NULL) {
		if ((ptr = strstr(dirp->d_name, "mmcblk")) != NULL) {
			c = *(ptr+6);
			if (!(c >= '0' && c <= '9'))
				goto oops;
			*blknum = c - '0';
			break;
		}
	}
	if (dirp == NULL)
		goto oops;
	closedir(dp);

	return INSTALL_SUCCESS;

oops:
	write_to_user("update error (in function blk_number)\n");
	return INSTALL_ERROR;
}

#define DEVICE_NAME_SIZ  64
#define SD_MOUNT_POINT  "/mnt/sdcard"
#define DEV_FORMAT_STRING_SD  "/dev/mmcblk%dp1"

int install_package(const char *root_path, const char *type, int sdcard)
{
	int blknum = -1, status = INSTALL_ERROR;
	char devName[DEVICE_NAME_SIZ];

	if (!strcmp(type, "ota")) {
		if (sdcard) {
			status = blk_number(SDCARD_SYS_ENTRY, &blknum);
			if (status == INSTALL_ERROR)
				return status;
			if (mkdir(SD_MOUNT_POINT, 0700) < 0) {
				if (errno != EEXIST) {
					write_to_user("Unable to create mount directory: %s\n", strerror(errno));
					return INSTALL_ERROR;
				}
			}
			sprintf(devName, DEV_FORMAT_STRING_SD, blknum);
			if (mount(devName, SD_MOUNT_POINT, "vfat", 0, NULL) < 0) {
				write_to_user("Unable to mount partition: %s\n", strerror(errno));
				return INSTALL_ERROR;
			}

			status = install_ota(root_path);

			umount(SD_MOUNT_POINT);
		} else {
			mount_partition(SDCARD);
			status = install_ota(root_path);
			umount_partition(SDCARD);
		}
	} else if (!strcmp(type, "fota"))
		status = install_fota(root_path);

	if (status == INSTALL_SUCCESS)
		write_to_user("complete...\nupdate success\n");
	else
		write_to_user("update error\n");

	return status;
}

int progressbar_flag_set(const char bar_status)
{
	progress_file_write(bar_status);
	return INSTALL_SUCCESS;
}

int ota_update(void)
{
	int argc;
	char **argv;

	int status = INSTALL_ERROR;
	const char *update_package = NULL;
	const char *type = NULL;
	int sdcard = 0; // OTA image is on sdcard or eMMC
	int wipe_data = 0;
	int factory_restore = 0;
	int arg = -1;
	int blknum = -1;
	int devfd;
	unsigned char rbt_reason;

	blk_number(EMMC_SYS_ENTRY, &blknum);

	switch (blknum) {
	case 0:
		fastboot_publish(CMD_BOOT_DEV, CMD_BOOT_DEV_SDCARD);
		break;
	case 1:
		fastboot_publish(CMD_BOOT_DEV, CMD_BOOT_DEV_SDCARD1);
		break;
	case 2:
		fastboot_publish(CMD_BOOT_DEV, CMD_BOOT_DEV_SDCARD2);
		break;
	case -1:
		write_to_user("Get eMMC's blk number error!\n");
		goto oops;
	default:
		write_to_user("Illegal eMMC blk number!\n");
		goto oops;
	}

	mount_partition(CACHE);
	argv = (char **)malloc(sizeof(char) * MAX_ARG_LENGTH);
	if (get_args(&argc, &argv)) {
		write_to_user("OTA command not found...\n");
		umount_partition(CACHE);
		goto oops;
	}
	umount_partition(CACHE);

	while ((arg = getopt_long(argc, argv, "", OPTIONS, NULL)) != -1) {
		switch (arg) {
		case 'u':
			update_package = optarg;
			break;
		case 't':
			type = optarg;
			break;
		case 's':
			sdcard = 1;
			break;
		case 'w':
			wipe_data = 1;
			break;
		case 'f':
			factory_restore = 1;
			break;
		case '?':
			continue;
		}
	}

	progressbar_flag_set(BAR_START);
	if (argv) {
		free(argv);
		argv = NULL;
	}
	if (factory_restore) {
		status = device_factory_restore();
		if (status != INSTALL_SUCCESS)
			write_to_user("Cache wipe failed.\n");
	} else if (wipe_data) {
		status = device_wipe_data();
		if (status != INSTALL_SUCCESS)
			write_to_user("Data wipe failed.\n");
		else {
			rbt_reason = RR_SIGNED_MOS;
			if ((devfd = open(IPC_DEVICE_NAME, O_RDWR)) < 0) {
				write_to_user("unable to open the DEVICE %s\n", IPC_DEVICE_NAME);
			} else {
				ioctl(devfd, IPC_WRITE_RR_TO_OSNIB, &rbt_reason);
				close(devfd);
			}
		}
	} else if (update_package != NULL && type != NULL) {
		status = install_package(update_package, type, sdcard);
		if (status != INSTALL_SUCCESS)
			write_to_user("OTA package install failed.\n");
	}

	progressbar_flag_set(BAR_FINISH);
oops:
	if (status == INSTALL_SUCCESS)
		ota_finish(status);
	else
		write_to_user("OTA update fail! aboot continue.\n");

	return status;
}
