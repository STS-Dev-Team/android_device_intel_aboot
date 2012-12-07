/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <pthread.h>
#include <cutils/properties.h>
#include <sys/wait.h>
#include "common.h"
#include "fastboot.h"
#include "firmware.h"
#include "roots.h"
#include "osip.h"
#include "power.h"

#define CMD_SYSTEM        "system"
#define CMD_PROXY         "proxy"
#define SYSTEM_BUF_SIZ     512    /* For system() and popen() calls. */
#define MOUNT_POINT_SIZ    50     /* /dev/<whatever> */

void ufdisk_umount_all(void);

#define FORCE_COLD_BOOT_FILE "/sys/module/intel_mid/parameters/force_cold_boot"

static void force_cold_boot(void)
{
        FILE *file;
        char force_cold_boot = 'Y';

        file = fopen(FORCE_COLD_BOOT_FILE, "w");
        if (file == NULL) {
                LOGE("BUG!!! Cannot open %s\n", FORCE_COLD_BOOT_FILE);
                return;
        }

        if (fwrite(&force_cold_boot, sizeof(char), 1, file) != 1)
                LOGE("BUG!!! Cannot force COLD BOOT instead of COLD RESET\n");

        fclose(file);
}

/*
 * ** THREAD **
 *
 */
void cmd_reboot(const char *arg, void *data, unsigned sz)
{
	extern int fb_fp;
	extern int enable_fp;

	force_cold_boot();

	ui_print("REBOOT...\n");
	sleep(2);
	fastboot_okay("");
	close(enable_fp);
	close(fb_fp);

	ufdisk_umount_all();
	sync();
	__reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, "android");
	sleep(1);
	LOGE("BUG!!! shouldn't ever execute this line!!!!\n");
}

void cmd_reboot_bl(const char *arg, void *data, unsigned sz)
{
	extern int fb_fp;
	extern int enable_fp;

	ui_print("REBOOT...\n");
	sleep(2);
	fastboot_okay("");
	close(enable_fp);
	close(fb_fp);

	ufdisk_umount_all();
	sync();
	__reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, "bootloader");
	sleep(1);
	LOGE("BUG!!! shouldn't ever execute this line!!!!\n");
}

void cmd_erase(const char *part_name, void *data, unsigned sz)
{
	char mnt_point[MOUNT_POINT_SIZ];
	int ret;


	snprintf(mnt_point, sizeof(mnt_point), "/%s", part_name);

	/* supports fastboot -w who wants to erase userdata */
	ui_print("ERASE %s...\n", part_name);
	if (!strcmp(part_name, "userdata"))
		sprintf(mnt_point, "/data");
	ret = format_volume(mnt_point, 0);

	ui_print("ERASE %s\n", ret==0 ? "COMPLETE." : "FAILED!");
	if (ret==0) {
	        fastboot_okay("");
	}
	else
		fastboot_fail("unable to format");
}
int save_file(void *data, unsigned sz, const char *name)
{
        FILE *fp = NULL;
        int res = 0;
        if ((fp = fopen(name, "w+")) != NULL)
            if (sz == fwrite(data, 1, sz, fp))
                res = 1;
        fclose(fp);
        fp = NULL;
        return res;
}

#define OTA_UPDATE_TEMP_BUFFER		512
#define OPTION_STRING			"--update_package="
static int ota_update(char *file)
{
	int fd, ret = -1;
	char command[OTA_UPDATE_TEMP_BUFFER];

	sprintf(command,OPTION_STRING);
	strncat(command, file , OTA_UPDATE_TEMP_BUFFER - strlen(OPTION_STRING) -1 );
	if (ensure_path_mounted("/cache") != 0)
		goto err;
	if (access("/cache/recovery", F_OK) != 0)
		if (mkdir("/cache/recovery", 777) != 0)
			goto err;
	if ((fd = open("/cache/recovery/command", O_CREAT | O_WRONLY)) < 0)
		goto err;
	if (write(fd, command, strlen(command)) < 0)
		goto err;

	close(fd);
	fastboot_okay("");
        if (execv("/sbin/recovery", NULL))
                ui_msg(ALERT, LEFT, "SWITCH TO RECOVERY FAILED!");

	return 0; //will not reach here if no error occurs.

err:
	return ret;
}

extern int write_stitch_image(void *data, size_t size, int update_number);
extern int restore_payload_osii_entry();
extern int write_stitch_image(void *data, size_t size, int update_number);
#define IMG_RADIO "/tmp/__radio.img"
#define IMG_RADIO_RND "/tmp/__radio_rnd.img"
void cmd_flash(const char *arg, void *data, unsigned sz)
{
        char buf[SYSTEM_BUF_SIZ];
        int ret = -1;
        struct stat statbuf;
        int sys_status;

        memset(buf,0,sizeof(buf));

        ui_print("FLASH %s...\n", arg);
        if (!strcmp(arg, "boot")) {
                if (write_stitch_image(data, sz, 0) == 0) {
                        restore_payload_osii_entry();
                        ret = 0;
                }
        } else if (!strcmp(arg, "recovery")) {
                if (write_stitch_image(data, sz, 1) == 0)
                        ret = 0;
        } else if (sz > 4 && memcmp(data, "PK\x03\x04", 4) == 0) {
                #define IMG_OTA "/cache/update.zip"
                if (ensure_path_mounted("/cache") != 0)
                    goto err;

                if (save_file(data, sz, IMG_OTA)) {
                        if (ota_update(IMG_OTA))
                                fastboot_fail("problem with flashing");
                        else {
                                restore_payload_osii_entry();
                                fastboot_okay("");
                                ret = 0;
                        }
                        unlink(IMG_OTA);
                }
        } else if (!strcmp(arg, "system") || !strcmp(arg, "data")) {
                #define IMG_FILE "/tmp/_system_or_data.img"
                if (save_file(data, sz, IMG_FILE)) {
                        char path[64];
                        snprintf(path, sizeof(path)-1, "/%s", arg);
                        if (0 == ensure_path_mounted(path)) {
                                snprintf(buf, sizeof(buf)-1, "tar xzf %s -C /", IMG_FILE);
                                buf[sizeof(buf)-1]=0;
                                if (-1 != system(buf))
                                        ret = 0;
                                ensure_path_unmounted(path);
                        } else
                                fastboot_fail("fail to mount partition");
                        unlink(IMG_FILE);
                }
        } else if (!strcmp(arg, "radio")) {
                // Update modem SW.
                 if (save_file(data, sz, IMG_RADIO)) {
#ifdef CLVT
                        snprintf(buf, sizeof(buf)-1,"cmfwdl-app -p /dev/ttyIFX1 "
#else
                        snprintf(buf, sizeof(buf)-1,"cmfwdl-app -t /dev/ttyMFD1 -p /dev/ttyIFX0 "
#endif
                                      "-b %s -f %s", IMG_RADIO, IMG_RADIO);
                        sys_status = system(buf);
                        if (WIFEXITED(sys_status) && !WEXITSTATUS(sys_status))
                                ret = 0;
                        unlink(IMG_RADIO);
                }
        } else if (!strcmp(arg, "radio_erase_all")) {
                // Erase all flash, then update modem SW.
                 if (save_file(data, sz, IMG_RADIO)) {
#ifdef CLVT
                        snprintf(buf, sizeof(buf)-1,"cmfwdl-app -p /dev/ttyIFX1 "
#else
                        snprintf(buf, sizeof(buf)-1,"cmfwdl-app -t /dev/ttyMFD1 -p /dev/ttyIFX0 "
#endif
                                      "-e -b %s -f %s", IMG_RADIO, IMG_RADIO);
                        sys_status = system(buf);
                        if (WIFEXITED(sys_status) && !WEXITSTATUS(sys_status))
                                ret = 0;
                        unlink(IMG_RADIO);
                }
        } else if (!strcmp(arg, "radio_img")) {
                 // Save locally modem SW (to be called first before flashing RND Cert)
                 if (save_file(data, sz, IMG_RADIO)) {
                        ui_print("'%s: Radio Image Saved'\n", arg);
                        ret = 0;
                }
        } else if (!strcmp(arg, "rnd_write")) {
                // Flash RND Cert
                if(stat(IMG_RADIO, &statbuf) != 0) {
                        ui_print("'%s:' Radio Image Not Found!!\n", arg);
                        ret = -1;
                        fastboot_fail("rnd_write --> Radio Image Not Found\nCall flash radio_img first");
                }
                else {

                        if (save_file(data, sz, IMG_RADIO_RND)) {
                                snprintf(buf, sizeof(buf)-1,"cmfwdl-app -t /dev/ttyMFD1 -p /dev/ttyIFX0 "
                                      "-b %s -r %s", IMG_RADIO, IMG_RADIO_RND);
                                sys_status = system(buf);
                                if (WIFEXITED(sys_status) && !WEXITSTATUS(sys_status))
                                        ret = 0;
                                unlink(IMG_RADIO);
                                unlink(IMG_RADIO_RND);
                         }
               }
        } else if (!strcmp(arg, "rnd_erase")) {
                // Erase RND Cert
                 if (save_file(data, sz, IMG_RADIO)) {
                            snprintf(buf, sizeof(buf)-1,"cmfwdl-app -t /dev/ttyMFD1 -p /dev/ttyIFX0 "
                                      "-b %s --erase-rd", IMG_RADIO);
                            sys_status = system(buf);
                            if (WIFEXITED(sys_status) && !WEXITSTATUS(sys_status))
                                    ret = 0;
                            unlink(IMG_RADIO);
                }
        } else if (!strcmp(arg, "rnd_read")) {
                // Get RND Cert (print out in stdout)
                if (save_file(data, sz, IMG_RADIO)) {
                            snprintf(buf, sizeof(buf)-1,"cmfwdl-app -t /dev/ttyMFD1 -p /dev/ttyIFX0 "
                                      "-b %s -g", IMG_RADIO);
                            sys_status = system(buf);
                            if (WIFEXITED(sys_status) && !WEXITSTATUS(sys_status))
                                    ret = 0;
                            unlink(IMG_RADIO);
                }

        } else if (!strcmp(arg, "radio_hwid")) {
                // Get modem HWID (print out in stdout)
                fastboot_okay("Getting radio HWID...");
                if (save_file(data, sz, IMG_RADIO)) {
                            snprintf(buf, sizeof(buf)-1,"cmfwdl-app -t /dev/ttyMFD1 -p /dev/ttyIFX0 "
                                      "-b %s -h", IMG_RADIO);
                            sys_status = system(buf);
                            if (WIFEXITED(sys_status) && !WEXITSTATUS(sys_status))
                                    ret = 0;
                            unlink(IMG_RADIO);
                }
        } else if (!strcmp(arg, "dnx")) {
                #define BIN_DNX  "/tmp/__dnx.bin"
                if (save_file(data, sz, BIN_DNX))
                        ret = 0;
        } else if (!strcmp(arg, "ifwi")) {
                #define BIN_IFWI "/tmp/__ifwi.bin"
                if (access(BIN_DNX, F_OK))
                        LOGE("dnx binary must be flashed to board first\n");
                else if (save_file(data, sz, BIN_IFWI)) {
                        if (ifwi_update(BIN_DNX, BIN_IFWI))
                                LOGE("ifwi prepare failed\n");
                        else {
                                unlink(BIN_DNX);
                                unlink(BIN_IFWI);
                                ret = 0;
                        }
                }
        } else if (arg[0] == '/') {
                if (save_file(data, sz, arg))
                        ret = 0;
        }
err:
        ui_print("FLASH %s\n", ret == 0 ? "COMPLETE." : "FAILED!");
        if (ret == 0) {
                fastboot_okay("Ok");
        } else {
                fastboot_fail("flash command failed");
        }
}

void cmd_oem(const char *arg, void *data, unsigned sz)
{
        const char *command;
        int ret = -1;
 	int fd;

        while (*arg == ' ')
                arg++;
        command = arg;
        /* @todo map to updater's command */

        /* "system" command */
        if (strncmp(command, CMD_SYSTEM, strlen(CMD_SYSTEM)) == 0) {
                arg += strlen(CMD_SYSTEM);
                while (*arg == ' ')
                        arg++;
                ui_print("CMD '%s'...\n", arg);
                if (system(arg) != 0) {
                        ui_print("CMD '%s' FAILED!\n", arg);
                        fastboot_fail("OEM system command failed");
                } else {
                        ui_print("CMD '%s' COMPLETE.\n", arg);
                        fastboot_okay("");
                }

        /* "proxy" command */
        } else if (strncmp(command, CMD_PROXY, strlen(CMD_PROXY)) == 0) {
                arg += strlen(CMD_PROXY);
                while (*arg == ' ')
                        arg++;
                ui_print("CMD '%s %s'...\n", CMD_PROXY, arg);

                if (!strcmp(arg, "start")) {
                    /* Check if HSI node was created, */
                    /* indicating that the HSI bus is enabled.*/
                    if (-1 != access("/sys/bus/hsi/devices/port0", F_OK)) {

                        /* WORKAROUND */
                        /* Check number of cpus => identify CTP */
                        /* No modem reset for CTP, not supported */
                        fd = open("/sys/class/cpuid/cpu3/dev", O_RDONLY);

                        if (fd == -1)
                        {
                            /* Reset the modem */
                            ui_print("\nReset modem\n");
                            reset_modem();                          
           	        } 
                        close(fd);

                        /* Start proxy service (at-proxy). */
                        property_set("service.proxy.enable", "1");
                        ret = 0;
                    } else ui_print("\nfails to find HSI node: /sys/bus/hsi/devices/port0\n");

                } else if (!strcmp(arg, "stop")) {
                   /* Start proxy service (at-proxy). */
                    property_set("service.proxy.enable", "0");
                    ret = 0;
                }

                if (ret != 0) {
                    ui_print("\nfails: %s\n", arg);
                    fastboot_fail("OEM system command failed");
                } else {
                    ui_print("\nsucceeds: %s\n", arg);
                    fastboot_okay("");
                }

        } else {
                fastboot_fail("unknown OEM command");
        }
        return;
}

#define MAX_SIZE_OF_SCRATCH (400*1024*1024)
static void *scratch = NULL;
void *android_fastboot(void *arg)
{
	ui_print("FASTBOOT INIT...\n");

	fastboot_register("oem", cmd_oem);
	fastboot_register("reboot", cmd_reboot);
	fastboot_register("reboot-bootloader", cmd_reboot_bl);
	fastboot_register("erase:", cmd_erase);
	fastboot_register("flash:", cmd_flash);
	fastboot_register("continue", cmd_reboot);

#ifdef DEVICE_NAME
	fastboot_publish("product", DEVICE_NAME);
#endif
	fastboot_publish("kernel", "recovery");

	scratch = malloc(MAX_SIZE_OF_SCRATCH);
	if (scratch == NULL ) {
		LOGE("ERROR: malloc failed in fastboot. Unable to continue.\n\n");
		return NULL;
	}
	fastboot_init(scratch, MAX_SIZE_OF_SCRATCH);
    return NULL;
}
/*
 * ** MAIN THREAD **
 *
 */
int pos_main()
{
        pthread_t thr;
        pthread_attr_t atr;
        if (pthread_attr_init(&atr) != 0) {
                LOGE("ERROR: Unable to set fastboot thread attribute.\n");
        } else {
                if (pthread_create(&thr, &atr, android_fastboot, NULL) != 0)
                        LOGE("ERROR: Unable to create fastboot thread.\n");
        }
	return 0;
}
