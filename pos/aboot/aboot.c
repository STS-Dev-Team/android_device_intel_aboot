/*
 * Copyright (c) 2009, Google Inc.
 * Copyright (c) 2010 Intel Corporation
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
 *  * Neither the name of Google, Inc. nor the names of its contributors
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

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <linux/input.h>
#include "debug.h"
#include "device.h"
#include "fastboot.h"


#include "ota.h"
//#include "../../../system/core/adb/adb_serialno.h"
//static char serialno[SERIALNO_LEN + 1];

/*
 * MULTITHREADED
 *
 * Pay attention to mutual exclusion on all helper functions.
 */
#define AUTOBOOT_DELAY  8

#define CMD_SYSTEM        "system"
#define CMD_ORIGIN        "tarball_origin"
#define CMD_ORIGIN_ROOT   "root"
#define CMD_ORIGIN_MNT    "mount_point"
#define CMD_BOOT_DEV      "bootdev"
#define CMD_BOOT_DEV_SD   "sd" // Alias for sdcard
#define CMD_BOOT_DEV_SD1   "sd1" // Alias for sdcard1
#define CMD_BOOT_DEV_SD2   "sd2" // Alias for sdcard2
#define CMD_BOOT_DEV_SDCARD "sdcard"
#define CMD_BOOT_DEV_SDCARD1 "sdcard1"
#define CMD_BOOT_DEV_SDCARD2 "sdcard2"
#define CMD_BOOT_DEV_NAND "nand"
#define CMD_BOOT_DEV_USB  "usb"
#define CMD_BOOT_DEV_NFS  "nfs"
#define CMD_LOG_ENABLE    "log_enable"
#define CMD_LOG_DISABLE   "log_disable"

#define BOOT_DEVICE CMD_BOOT_DEV_SDCARD

#define SYSTEM_BUF_SIZ     512    /* For system() and popen() calls. */
#define CONSOLE_BUF_SIZ    400    /* For writes to /dev/console and friends */
#define PARTITION_NAME_SIZ 100    /* Partition names (/mnt/boot) */
#define DEVICE_NAME_SIZ    64     /* Device names (/dev/mmcblk0p1) */
#define MOUNT_POINT_SIZ    50     /* /dev/<whatever> */

/*
 * Global Data
 * No need to mutually exclude  serial_fd or screen_fd.
 */
#ifdef DEVICE_HAS_ttyS0
#define SERIAL_DEV      "/dev/ttyS0"
int serial_fd = -1;
#endif

#define SCREEN_DEV      "/dev/tty0"
int screen_fd = -1;

int log_enable = 0;
/*
 * Called from initialization only.
 */
void
open_consoles(void)
{
        screen_fd = open(SCREEN_DEV, O_RDWR);
#ifdef DEVICE_HAS_ttyS0
        serial_fd = open(SERIAL_DEV, O_RDWR);
#endif
}

/*
 * Called from multiple threads... but nothing here can break in a serious way.
 * We'll ignore mutual exclusion since the argument is stack based and the
 * writes are unlikely to overlap on output.
 */
void write_to_user(char *fmt, ...)
{
	char buf[CONSOLE_BUF_SIZ];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	printf("%s", buf);

	fflush(stdout);
}

//#define TAGS_ADDR     (BASE_ADDR + 0x00000100)
//#define KERNEL_ADDR   (BASE_ADDR + 0x00800000)
//#define RAMDISK_ADDR  (BASE_ADDR + 0x01000000)
//#define SCRATCH_ADDR  (BASE_ADDR + 0x02000000)
//
//static struct udc_device surf_udc_device = {
//      .vendor_id      = 0x18d1,
//      .product_id     = 0x0001,
//      .version_id     = 0x0100,
//      .manufacturer   = "Google",
//      .product        = "Android",
//};

struct ptable {
        char name[64];
        short devNum;
        char fsType[64];
};
#define NUMPART  8
#define FACOTRY  0
#define DATA     1
#define SDCARD   2
#define RECOVERY 3
#define SYSTEM   4
#define CACHE    5
#define CONFIG   6
#define ILOG     7
#define device_format_string_sd   "/dev/mmcblk0p%d"
#define device_format_string_sd1   "/dev/mmcblk1p%d"
#define device_format_string_sd2   "/dev/mmcblk2p%d"
#define device_format_string_nand "/dev/nda%d"
#define device_format_string_usb  "/dev/sda%d"
#define device_format_string_nfs  "-o nolock 50.0.0.1:/android/nfs%d"

struct ptable PartTable[] =
{
        [FACOTRY] = {
                   .name = "factory",
                   .devNum = 1,
                   .fsType = "ext4",
        },

        [DATA] = {
                   .name = "data",
                   .devNum = 2,
                   .fsType = "ext4",
        },

        [SDCARD] = {
                   .name = "sdcard",
                   .devNum = 3,
                   .fsType = "vfat",
        },

        [RECOVERY] = {
                   .name = "recovery",
                   .devNum = 5,
                   .fsType = "ext4",
        },

        [SYSTEM] = {
                   .name = "system",
                   .devNum = 6,
                   .fsType = "ext4",
        },

        [CACHE] = {
                   .name = "cache",
                   .devNum = 7,
                   .fsType = "ext4",
        },

        [CONFIG] = {
                   .name = "config",
                   .devNum = 8,
                   .fsType = "ext4",
        },

        [ILOG] = {
                   .name = "ilog",
                   .devNum = 10,
                   .fsType = "ext4",
        }

};

#define KERNEL  "kernel"
#define KERNEL2 "bzImage"
#define INITRD  "ramdisk.img"
#define INITRD2 "initrd"
#define CMDLINE "cmdline"

#define ROOT  "/mnt/boot/"
#define ROOT2 "/mnt/boot/boot/"

#define MAX_SIZE_OF_SCRATCH (256*1024*1024)
static void *scratch = NULL;

int enable_rndis()
{
	char cmd[128];
	int ret;

	/*  the function mount( ) can't support NFS mount, workaround it by system( ) */
	sprintf(cmd, "echo 1 > /sys/class/usb_composite/rndis/enable;sleep 3");
	dprintf(INFO, "%s\n", cmd);
	ret = system(cmd);
	if (ret < 0) {
		write_to_user("Unable to enable rndis: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

int disable_rndis()
{
	char cmd[128];
	int ret;

	/*  the function mount( ) can't support NFS mount, workaround it by system( ) */
	sprintf(cmd, "echo 0 > /sys/class/usb_composite/rndis/enable;sleep 3");
	dprintf(INFO, "%s\n", cmd);
	ret = system(cmd);
	if (ret < 0) {
		write_to_user("Unable to enable rndis: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

/*
 * function to execute a system command and return all output
 * back over the fastboot pipe.
 */
int logged_system(const char * cmd)
{
	char buf[65];
	int len, ret, status;
	pid_t pid;
	int pipe_fd[2];

	write_to_user("start : %s\n", cmd);
	memset(buf, 0, sizeof(buf));

	if (log_enable) {
		fastboot_info(cmd);
		if (pipe(pipe_fd) < 0) {
			printf("Pipe create error\n");
			return -1;
		}
		if ((pid = fork()) == 0) {
			close(pipe_fd[0]);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
			dup2(pipe_fd[1], STDOUT_FILENO);
			dup2(pipe_fd[1], STDERR_FILENO);
			status = system(cmd);
			close(pipe_fd[1]);
			exit(status);
		}
		else if (pid > 0) {
			close(pipe_fd[1]);
			waitpid(pid, &status, 0);
			if (WIFEXITED(status)) {
				ret = WEXITSTATUS(status);
				while (0 < (len = read(pipe_fd[0], buf, 60))) {
					fastboot_info(buf);
					memset(buf, 0, sizeof(buf));
				}
			} else ret = -1;
			close(pipe_fd[0]);
			write_to_user("%s\n returns %d", cmd, ret);
			return ret;
		} else {
			printf("The return PID is error\n");
			return -1;
		}
	} else {
		ret = system(cmd);
		write_to_user("%s\n returns %d", cmd, ret);
		return ret;
	}
}


int find_block_partition(const char *partName)
{
        int ptn;

        dprintf(INFO, "%s partName - %s\n", __FUNCTION__, partName);
        for (ptn = 0; ptn < NUMPART; ptn++) {
                if( 0 == strncmp(partName,
                        PartTable[ptn].name, strlen(PartTable[ptn].name))) {
                        dprintf(INFO, "%s part id = %d\n", PartTable[ptn].name, ptn);
                        return ptn;
                }
        }
        dprintf(INFO, "partName - %s not found\n", partName);
        return -1;
}

int mount_partition(int ptn)
{
        char buf[PARTITION_NAME_SIZ];
        int i;
        char devName[DEVICE_NAME_SIZ];
        char boot_device[DEVICE_NAME_SIZ];
	char *fsType;

        i = ptn;
        if (i<0) {
                dprintf(INFO, "ERROR: Invalid partition number to mount (%d)\n", i);
                return 1;
        }

        snprintf(buf, sizeof(buf), "/mnt/%s", PartTable[i].name);
        dprintf(INFO, "%s: mkdir %s\n", __FUNCTION__, buf);
        if (mkdir(buf, 0700) < 0) {
                if (errno != EEXIST) {
                        dprintf(INFO, "ERROR: Unable to create mount directory: %s\n",
                            strerror(errno));
                        return 1;
                }
        }

        strcpy(boot_device, fastboot_getvar(CMD_BOOT_DEV));
	fsType = PartTable[i].fsType;
        if (!strcmp(boot_device, CMD_BOOT_DEV_SDCARD)) {
                sprintf(devName, device_format_string_sd, PartTable[i].devNum);
	} else if (!strcmp(boot_device, CMD_BOOT_DEV_SDCARD1)) {
                sprintf(devName, device_format_string_sd1, PartTable[i].devNum);
	} else if (!strcmp(boot_device, CMD_BOOT_DEV_SDCARD2)) {
                sprintf(devName, device_format_string_sd2, PartTable[i].devNum);
        } else if (!strcmp(boot_device, CMD_BOOT_DEV_NAND)) {
                sprintf(devName, device_format_string_nand, PartTable[i].devNum);
        } else if (!strcmp(boot_device, CMD_BOOT_DEV_USB)) {
                sprintf(devName, device_format_string_usb, PartTable[i].devNum);
        } else if (!strcmp(boot_device, CMD_BOOT_DEV_NFS)) {
                sprintf(devName, device_format_string_nfs, PartTable[i].devNum);
		fsType = "nfs";
        }

        dprintf(INFO, "%s: %s (name:%s type: %s)\n", __FUNCTION__, devName, PartTable[i].name, fsType);
        snprintf(buf, sizeof(buf), "/mnt/%s", PartTable[i].name);
	if (!strcmp(fsType, "nfs")) {
		char cmd[128];
		int ret;
		/*  the function mount( ) can't support NFS mount, workaround it by system( ) */
		sprintf(cmd, "mount -t %s " device_format_string_nfs " %s", fsType,
				PartTable[i].devNum, buf);
		dprintf(INFO, "%s\n", cmd);
		ret = logged_system(cmd);
		if (ret < 0) {
			dprintf(INFO, "Unable to mount partition: %s\n", strerror(errno));
			return 1;
		}
	} else if (mount(devName, buf, fsType, 0, NULL) < 0) {

                dprintf(INFO, "Unable to mount partition: %s\n", strerror(errno));
                return 1;
        }
        return 0;
}

int umount_partition(int ptn)
{
        char buf[PARTITION_NAME_SIZ];
        if (ptn < 0)
                return 1;

        snprintf(buf, sizeof(buf), "/mnt/%s", PartTable[ptn].name);
        dprintf(INFO, "%s: umount %s\n", __FUNCTION__, buf);
        if (umount(buf) < 0) {
                if ((errno != EINVAL) && (errno != ENOENT)) {
                        dprintf(INFO, "Unmount of %s failed.\n", buf);
                        return 1;
                }
        }

        return 0;

}

int umount_all(void)
{
        int i;
        int found_error=0;

        for (i=0; i<NUMPART; i++)
                found_error += umount_partition(i);
        return (found_error ? 1 : 0);
}

/*
 * GLOBAL DATA
 * This variable should be accessed only from the accessors below.
 * Since it is never cleared (only set by the disable function), there
 * is no need for a mutex.
 */
int _autoboot_is_disabled = 1;//no-autoboto for provisioning OS.

void
display_disabled_message()
{
        static int message_written = 0;
        if (!message_written) {
                write_to_user("Autoboot disabled. Use 'fastboot continue' to resume boot\n");
                message_written = 1;
        }
        return;
}

void
disable_autoboot(void)
{
        _autoboot_is_disabled = 1;
        display_disabled_message();
}

/* Current PrOS doesn't need this, commented here */
/* int
autoboot_is_disabled(void)
{
        int rc;

        if (_autoboot_is_disabled) {
                display_disabled_message();
                return 1;
        }

        (void)mount_partition(BOOT);
        rc = (access(ROOT "no_autoboot", R_OK) == 0);
        (void)umount_partition(BOOT);

        if (rc)
                display_disabled_message();

        return (rc);
}*/

/*
 * Read the kernel command line and form/execute the kexec command.
 * We append the androidboot.bootmedia value. This value may also be specified in
 * in the cmdline file. The init process will use the last one found.
 */
int kexec_linux(char *root, char *kernel, char *initrd, char *cmdline)
{
        char buf[SYSTEM_BUF_SIZ], buf2[SYSTEM_BUF_SIZ], *sp;
        const char *bootmedia;
        int fd;

        sprintf(buf2, "%s/%s", root, cmdline);
        fd = open(buf2, O_RDONLY);
        if (fd < 0) {
            write_to_user("ERROR: unable to open %s (%s).\n", strerror(errno));
            return 1;
        }
        if (read(fd, buf2, sizeof(buf2)-1) <= 0) {
            write_to_user("ERROR: unable to read command line file (%s).\n", strerror(errno));
            return 1;
        }
        buf2[sizeof(buf2)-1] = 0;
        sp = strchr(buf2, '\n');
		if (sp != NULL)
			*sp = 0;

        strcat(buf2, " androidboot.bootmedia=");
        bootmedia = fastboot_getvar(CMD_BOOT_DEV);
        if (!strcmp(bootmedia, CMD_BOOT_DEV_USB))
            /* Must match the init.<device>.rc file name */
            bootmedia = "harddisk";
        strcat(buf2, bootmedia);

        snprintf(buf, sizeof(buf),
            "kexec -f -x %s%s --ramdisk=%s%s --command-line=\"%s\"\n",
            root, kernel, root, initrd, buf2);
        close(fd);

        dprintf(INFO, "%s: %s\n", __FUNCTION__, buf);
        if (logged_system(buf) < 0) {
                perror(__FUNCTION__);
                return 1;
        }
        return 0;

}

/*
 * Try very hard to locate a triple of [bzImage, initrd, cmdline]. There
 * are several naming conventions and locations to check.
 * This code supports all of:
 *   o Android create-rootfs.sh format
 *   o Android fastboot format
 *   o Moblin image 'dd' format.
 *
 * Called from both threads. It is more less expected to not return,
 * so it is sufficient to do pretty informal mutual exclusion here.
 */
/* Current PrOS disabled autoboot, commented here */
/* void boot_linux_from_flash_internal(void)
{
        char *kernel, *initrd, *root, *cmdline;
        int fd;
        static int in_boot_linux = 0;

        while (in_boot_linux)
                sleep(1);
        in_boot_linux = 1;

        write_to_user("Attempting to boot from %s.\n", fastboot_getvar(CMD_BOOT_DEV));
        (void)umount_all();
        if (mount_partition(BOOT) != 0) {
            in_boot_linux = 0;
            return;
        }

        root = kernel = initrd = cmdline = NULL;
        if (!access(ROOT KERNEL, R_OK)) {
            root = ROOT;
            kernel = KERNEL;
        } else if (!access(ROOT KERNEL2, R_OK)) {
            root = ROOT;
            kernel = KERNEL2;
        }
        if (!access(ROOT INITRD, R_OK)) {
            initrd = INITRD;
        } else if (!access(ROOT INITRD2, R_OK)) {
            initrd = INITRD2;
        }
        if (!access(ROOT CMDLINE, R_OK)) {
            cmdline = CMDLINE;
        } else if (!access(ROOT CMDLINE, R_OK)) {
            cmdline = CMDLINE;
        }

        if (!root || !initrd || !kernel || !cmdline) {
            root = kernel = initrd = cmdline = NULL;
            if (!access(ROOT2 KERNEL, R_OK)) {
                root = ROOT2;
                kernel = KERNEL;
            } else if (!access(ROOT2 KERNEL2, R_OK)) {
                root = ROOT2;
                kernel = KERNEL2;
            }
            if (!access(ROOT2 INITRD, R_OK)) {
                initrd = INITRD;
            } else if (!access(ROOT2 INITRD2, R_OK)) {
                initrd = INITRD2;
            }
            if (!access(ROOT2 CMDLINE, R_OK)) {
                cmdline = CMDLINE;
            } else if (!access(ROOT2 CMDLINE, R_OK)) {
                cmdline = CMDLINE;
            }
        }

        if (!root || !initrd || !kernel || !cmdline) {
            write_to_user("ERROR: Could not locate [kernel, initrd, cmdline].\n");
            in_boot_linux = 0;
            return;
        }

        write_to_user("Found: %s [kernel: %s, initrd: %s, cmdline: %s]\n",
            root, kernel, initrd, cmdline);
        kexec_linux(root, kernel, initrd, cmdline);
        write_to_user("ERROR: Failed to perform Linux boot.\n");
        in_boot_linux = 0;
        return;
}*/

#define FORCE_COLD_BOOT_FILE "/sys/module/mrst/parameters/force_cold_boot"

static void force_cold_boot(void)
{
        FILE *file;
        char force_cold_boot = 'Y';

        file = fopen(FORCE_COLD_BOOT_FILE, "w");
        if (file == NULL) {
                write_to_user("BUG!!! Cannot open %s\n", FORCE_COLD_BOOT_FILE);
                return;
        }

        if (fwrite(&force_cold_boot, sizeof(char), 1, file) != 1)
                write_to_user("BUG!!! Cannot force COLD BOOT instead of COLD RESET\n");

        fclose(file);
}

void cmd_reboot(const char *arg, void *data, unsigned sz)
{
        extern int fb_fp;
        extern int enable_fp;

        force_cold_boot();

        write_to_user("Rebooting...\n");

        // open the guts of kboot.
        fastboot_okay("");
        close(enable_fp);
        close(fb_fp);

        (void)umount_all();
        sleep(1);
        reboot(0xA1B2C3D4);
        sleep(1);
	write_to_user("BUG!!! shouldn't ever execute this line!!!!\n");

}

/*
 * If we can't boot from the default device, try the alternates.
 * Order is important here. We try the external devices before the
 * more internal ones:
 *     default device (or one set by a fastboot command)
 *     SD card
 *     USB card
 *     NAND
 */
void boot_linux_from_flash(void)
{
		sleep(1);
        cmd_reboot(NULL,NULL,0);
        // we don't return from here.
}

void cmd_boot(const char *arg, void *data, unsigned sz)
{
        write_to_user("Booting %s.\n", arg);

        cmd_reboot(NULL,NULL,0);
        // we don't return from here.
}

static int format_partition(int ptn_id)
{
        char devName[DEVICE_NAME_SIZ];
        char buf[SYSTEM_BUF_SIZ];
        char boot_device[DEVICE_NAME_SIZ];
	char *fsType;
        dprintf(SPEW, "formatting partition %d: %s\n", ptn_id, PartTable[ptn_id].name);

        if (umount_partition(ptn_id)) {
                return 1;
        }

        strcpy(boot_device, fastboot_getvar(CMD_BOOT_DEV));
 	fsType = PartTable[ptn_id].fsType;
        if (!strcmp(fastboot_getvar(CMD_BOOT_DEV),CMD_BOOT_DEV_SDCARD)) {
                sprintf(devName, device_format_string_sd, PartTable[ptn_id].devNum);
	} else if (!strcmp(fastboot_getvar(CMD_BOOT_DEV),CMD_BOOT_DEV_SDCARD1)) {
                sprintf(devName, device_format_string_sd1, PartTable[ptn_id].devNum);
	} else if (!strcmp(fastboot_getvar(CMD_BOOT_DEV),CMD_BOOT_DEV_SDCARD2)) {
                sprintf(devName, device_format_string_sd2, PartTable[ptn_id].devNum);
        } else if (!strcmp(fastboot_getvar(CMD_BOOT_DEV),CMD_BOOT_DEV_NAND)) {
                sprintf(devName, device_format_string_nand, PartTable[ptn_id].devNum);
        } else if (!strcmp(fastboot_getvar(CMD_BOOT_DEV),CMD_BOOT_DEV_USB)) {
                sprintf(devName, device_format_string_usb, PartTable[ptn_id].devNum);
         } else if (!strcmp(fastboot_getvar(CMD_BOOT_DEV),CMD_BOOT_DEV_NFS)) {
                 sprintf(devName, device_format_string_nfs, PartTable[ptn_id].devNum);
 		fsType = "nfs";
        }

        if (!strcmp(fsType, "ext3"))
                snprintf(buf, sizeof(buf),"mkfs.%s -L %s %s",
                         fsType, PartTable[ptn_id].name, devName);
        else if (!strcmp(fsType, "ext4"))
                snprintf(buf, sizeof(buf),"mkfs.%s -L %s %s",
                         fsType, PartTable[ptn_id].name, devName);
 	else if (!strcmp(fsType, "nfs"))
                 snprintf(buf, sizeof(buf),"mount -t %s %s /mnt && rm -fr /mnt/* && umount /mnt",
                         fsType, devName);
        else
                snprintf(buf, sizeof(buf),"mkfs.%s -i %s %s",
                        PartTable[ptn_id].fsType, PartTable[ptn_id].name, devName);
        dprintf(INFO, "%s: %s\n", __FUNCTION__, buf);
        if (logged_system(buf) != 0) {
                perror(__FUNCTION__);
                return 1;
        }

        return 0;
}

void cmd_erase(const char *part_name, void *data, unsigned sz)
{
        int ptn_id;

        disable_autoboot();
        write_to_user("Erasing %s.\n", part_name);

        dprintf(INFO, "%s: %s\n", __FUNCTION__,part_name);
        ptn_id= find_block_partition(part_name);
        if (ptn_id < 0) {
                fastboot_fail("unknown partition name");
                return;
        }

	if (ptn_id == CONFIG) {
		//test mountable config volume.  If we can then skip format
		if (0 ==mount_partition(CONFIG)) {
			umount_partition(CONFIG);
			fastboot_okay("config volume already formated skipping...");
			return;
		}
	}


        if (format_partition(ptn_id)) {
                fastboot_fail("failed to erase partition");
                return;
        }
        fastboot_okay("");
}

#define BOOT_MAGIC "ANDROID!"
#define BOOT_MAGIC_SIZE 8
#define BOOT_NAME_SIZE 16
#define BOOT_ARGS_SIZE 512

#define PAGE_MASK 2047
#define ROUND_TO_PAGE(x) (((x) + PAGE_MASK) & (~PAGE_MASK))
#define BOOT_IMAGE_FILE "/tmp/boot.bin"

void cmd_flash(const char *arg, void *data, unsigned sz)
{
        char buf[SYSTEM_BUF_SIZ];
        char mnt_point[MOUNT_POINT_SIZ];
        FILE *fp;
        const char *file;
        int i;
        char *ptn = NULL;

        disable_autoboot();
        write_to_user("Flashing %s.\n", arg);

        dprintf(INFO, "cmd_flash %d bytes to '%s'\n", sz, arg);
        if (*arg == '/') {
                sprintf(mnt_point, "/");
                file = arg;
        } else {
		if (strcmp(arg, "boot") == 0) {
			sprintf(mnt_point, "/");
			file =  BOOT_IMAGE_FILE;
		} else {
			i = find_block_partition(arg);
			if (i < 0) {
				fastboot_fail("unknown partition name");
				return;
			}
			ptn = PartTable[i].name;
			if (mount_partition(i)) {
				fastboot_fail("mount fail");
				return;
			}

	                sprintf(mnt_point, "/mnt/%s", ptn);
			file = arg + strlen(ptn);
			if (file[0] == ':')
				file += 1; /* skip ':' */
			else
				file = NULL;
		}
	}

// TODO check that boot, and recovery images are TGZ files
//      if (!strcmp(ptn, "boot") || !strcmp(ptn, "recovery")) {
//              if (memcmp((void *)data, BOOT_MAGIC, BOOT_MAGIC_SIZE)) {
//                      fastboot_fail("image is not a boot image");
//                      return;
//              }
//      }

        // TODO if boot or recovery I need to crack the compressed
        // ramdisk and kernel.

//      if (!strcmp(ptn, "system") || !strcmp(ptn, "userdata"))
//              extra = 64;
//      else
//              sz = ROUND_TO_PAGE(sz);

        if (file) {
                /* update individual file */
                snprintf(buf, sizeof(buf), "%s/%s", mnt_point, file);
                fp = fopen(buf, "w+");
        } else {
                int origin_is_mntpoint =
                        (strcmp(fastboot_getvar(CMD_ORIGIN), CMD_ORIGIN_MNT) == 0);
                /* update the whole partition */
                snprintf(buf, sizeof(buf), "tar xzf - -C %s",
                        origin_is_mntpoint ? mnt_point: "/mnt");
                fp = popen(buf, "w");
        }

        dprintf(INFO, "%s: %s\n", __FUNCTION__, buf);
        if (fp == NULL) {
                perror("popen or fopen");
                fastboot_fail("fail to open pipe or file");
                return;
        }
        if (sz != fwrite(data, 1, sz,fp)) {
                perror("fwrite");
                fastboot_fail("flash write failure");
                pclose(fp);
                return;
        }
        dprintf(INFO, "wrote %d bytes to '%s'\n", sz, arg);
        pclose(fp);

	if (strcmp(arg, "boot") == 0) {
		snprintf(buf, sizeof(buf),
			"flash_stitched %s", BOOT_IMAGE_FILE);
		logged_system( buf );
		fastboot_okay("");
		return;
	}

        if (ptn && umount_partition(i)) {
                fastboot_fail("umount fail");
                return;
        }
        dprintf(INFO, "partition '%s' updated\n", ptn);
        fastboot_okay("");
}

void cmd_continue(const char *arg, void *data, unsigned sz)
{
        fastboot_okay("");
        boot_linux_from_flash();
}

void cmd_oem(const char *arg, void *data, unsigned sz)
{
        const char *command;

        disable_autoboot();
        dprintf(SPEW, "%s: <%s>\n", __FUNCTION__, arg);

        while (*arg == ' ')
                arg++;
        command = arg;

        if (strncmp(command, CMD_SYSTEM, strlen(CMD_SYSTEM)) == 0) {
                arg += strlen(CMD_SYSTEM);
                while (*arg == ' ')
                        arg++;
                if (logged_system(arg) != 0) {
                        write_to_user("\nfails: %s\n", arg);
                        fastboot_fail("OEM system command failed");
                } else {
                        write_to_user("\nsucceeds: %s\n", arg);
                        fastboot_okay("");
                }

        } else if (strncmp(command, CMD_ORIGIN, strlen(CMD_ORIGIN)) == 0) {
                arg += strlen(CMD_ORIGIN);
                while (*arg == ' ')
                        arg++;
                if (strcmp(arg, CMD_ORIGIN_ROOT) == 0) {
                        fastboot_publish(CMD_ORIGIN, CMD_ORIGIN_ROOT);
                        fastboot_okay("");
                } else if (strcmp(arg, CMD_ORIGIN_MNT) == 0) {
                        fastboot_publish(CMD_ORIGIN, CMD_ORIGIN_MNT);
                        fastboot_okay("");
                } else {
                        fastboot_fail("unknown tarball_origin directory");
                }

	} else if (strncmp(command, CMD_LOG_ENABLE, strlen(CMD_LOG_ENABLE)) == 0) {
		log_enable = 1;
		fastboot_okay("");
	} else if (strncmp(command, CMD_LOG_DISABLE, strlen(CMD_LOG_DISABLE)) == 0) {
		log_enable = 0;
		fastboot_okay("");
        } else if (strncmp(command, CMD_BOOT_DEV, strlen(CMD_BOOT_DEV)) == 0) {
                arg += strlen(CMD_BOOT_DEV);
                while (*arg == ' ')
                        arg++;
                if (!strcmp(arg, CMD_BOOT_DEV_SD) || (!strcmp(arg, CMD_BOOT_DEV_SDCARD))) {
                        fastboot_publish(CMD_BOOT_DEV, CMD_BOOT_DEV_SDCARD);
                        fastboot_okay("");
		} else if (!strcmp(arg, CMD_BOOT_DEV_SD1) || (!strcmp(arg, CMD_BOOT_DEV_SDCARD1))) {
                        fastboot_publish(CMD_BOOT_DEV, CMD_BOOT_DEV_SDCARD1);
                        fastboot_okay("");
		} else if (!strcmp(arg, CMD_BOOT_DEV_SD2) || (!strcmp(arg, CMD_BOOT_DEV_SDCARD2))) {
                        fastboot_publish(CMD_BOOT_DEV, CMD_BOOT_DEV_SDCARD2);
                        fastboot_okay("");
                } else if (!strcmp(arg, CMD_BOOT_DEV_NAND)) {
                        fastboot_publish(CMD_BOOT_DEV, CMD_BOOT_DEV_NAND);
                        fastboot_okay("");
                } else if (!strcmp(arg, CMD_BOOT_DEV_USB)) {
                        fastboot_publish(CMD_BOOT_DEV, CMD_BOOT_DEV_USB);
                        fastboot_okay("");
                } else if (!strcmp(arg, CMD_BOOT_DEV_NFS)) {
                        fastboot_publish(CMD_BOOT_DEV, CMD_BOOT_DEV_NFS);
			if (!enable_rndis())
				fastboot_okay("");
			else
				fastboot_fail("failed to enable rndis!");
                } else {
                        fastboot_fail("unknown boot device");
                }
        } else {
                fastboot_fail("unknown OEM command");
        }
        return;
}

/*
 * GLOBAL DATA
 * Touched_events could have mutual exclusion... but in practice no bad can come
 * from having it simultaneously read, set and cleared.
 */
#define TOUCHED_PAD     (1)
#define TOUCHED_KEY_ESC (1<<1)
#define TOUCHED_KEY     (1<<2)
volatile int touched_events = 0;

#define EVENT_DEV       "/dev/event%d"
#define EVENT_DEV_MAX   10      /* Max %d for above */


void close_fds(fd_set *fdset)
{
        int i;
        for (i=0; i<32; i++) {
                if (FD_ISSET(i, fdset))
                        close(i);
        }
}

/*
 * ** THREAD **
 *
 */
void *android_fastboot(void *arg)
{
        fastboot_register("oem", cmd_oem);
        fastboot_register("reboot", cmd_reboot);
        fastboot_register("boot", cmd_boot);
        fastboot_register("erase:", cmd_erase);
        fastboot_register("flash:", cmd_flash);
        fastboot_register("continue", cmd_continue);
        fastboot_publish("bootdev",BOOT_DEVICE);
        if (!strcmp("BOOT_DEVICE", CMD_BOOT_DEV_NFS))
		enable_rndis();
#ifdef DEVICE_NAME
        fastboot_publish("product", DEVICE_NAME);
#endif
        fastboot_publish("kernel", "kboot");
        fastboot_publish(CMD_ORIGIN, CMD_ORIGIN_ROOT);

        scratch = malloc(MAX_SIZE_OF_SCRATCH);
        if (scratch == NULL ) {
                write_to_user("ERROR: malloc failed in fastboot. Unable to continue.\n\n");
                return NULL;
        }

        write_to_user("Listening for the fastboot protocol on the USB OTG.\n");
        fastboot_init(scratch, MAX_SIZE_OF_SCRATCH);
        return NULL;
}

/*
 * ** MAIN THREAD **
 *
 */
void android_boot(void)
{
        pthread_t thr;
        pthread_attr_t atr;

        if (pthread_attr_init(&atr) != 0) {
                write_to_user("ERROR: Unable to set fastboot thread attribute.\n");
        } else {
                if (pthread_create(&thr, &atr, android_fastboot, NULL) != 0)
                        write_to_user("ERROR: Unable to create fastboot thread.\n");
        }

        while (1) {
                sleep(1);
        }
}

int aboot_main(int c, char *argv[])
{
    //system("mkdir /data && mount -t ext3 /dev/mmcblk0p2 /data/");
    //adb_serialno_read(SERIAL_FILE, serialno);
    //adb_serialno_set(SERIAL_DEV, serialno);
    //system("umount /data/ && rm -rf /data");
        open_consoles();
#ifdef DEVICE_HAS_KEYPAD
        write_to_user("To terminate aboot: Hit ESC on your internal keyboard.\n");
#endif
        android_boot();
        exit(1);
}

