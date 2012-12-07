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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cutils/properties.h>
#include "common.h"

/* todo: give lk strtoul and nuke this */
static unsigned hex2unsigned(const char *x)
{
    unsigned n = 0;

    while(*x) {
        switch(*x) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            n = (n << 4) | (*x - '0');
            break;
        case 'a': case 'b': case 'c':
        case 'd': case 'e': case 'f':
            n = (n << 4) | (*x - 'a' + 10);
            break;
        case 'A': case 'B': case 'C':
        case 'D': case 'E': case 'F':
            n = (n << 4) | (*x - 'A' + 10);
            break;
        default:
            return n;
        }
        x++;
    }

    return n;
}

struct fastboot_cmd {
	struct fastboot_cmd *next;
	const char *prefix;
	unsigned prefix_len;
	void (*handle)(const char *arg, void *data, unsigned sz);
};

struct fastboot_var {
	struct fastboot_var *next;
	const char *name;
	const char *value;
};

static struct fastboot_cmd *cmdlist;

void fastboot_register(const char *prefix,
		       void (*handle)(const char *arg, void *data, unsigned sz))
{
	struct fastboot_cmd *cmd;
	cmd = malloc(sizeof(*cmd));
	if (cmd) {
		cmd->prefix = prefix;
		cmd->prefix_len = strlen(prefix);
		cmd->handle = handle;
		cmd->next = cmdlist;
		cmdlist = cmd;
	}
}

static struct fastboot_var *varlist;

void fastboot_publish(const char *name, const char *value)
{
	struct fastboot_var *var;

	for (var=varlist; var; var=var->next)
		if (!strcmp(name, var->name)) {
			var->value = value;
			return;
		}

	var = malloc(sizeof(*var));
	if (var) {
		var->name = name;
		var->value = value;
		var->next = varlist;
		varlist = var;
	}
}

const char *fastboot_getvar(const char *name)
{
	struct fastboot_var *var;
	for (var=varlist; var; var=var->next)
		if (!strcmp(name, var->name))
			return (var->value);
	return NULL;
}


static unsigned char buffer[4096];

static void *download_base;
static unsigned download_max;
static unsigned download_size;

#define STATE_OFFLINE	0
#define STATE_COMMAND	1
#define STATE_COMPLETE	2
#define STATE_ERROR	3

static unsigned fastboot_state = STATE_OFFLINE;
int fb_fp = -1;
int enable_fp;

static int usb_read(void *_buf, unsigned len)
{
	int r;
	unsigned xfer;
	unsigned char *buf = _buf;
	int count = 0;
	int tot_len = len;
	int progress_update=0;
	if (fastboot_state == STATE_ERROR)
		goto oops;

	while (len > 0) {
		xfer = (len > 4096) ? 4096 : len;

		r = read(fb_fp, buf, xfer);
		if (r < 0) {
			LOGW("Warning:usb read failed\n");
			goto oops;
		}
		if (progress_update++ > 500) {
			progress_update = 0;
		}

		count += r;
		buf +=r;
		len -= r;

		/* short transfer? */
		if (r != (int)xfer) break;
	}

	return count;

oops:
	fastboot_state = STATE_ERROR;
	LOGW("Warning:usb_read failed: asked for %d and got %d\n", len, r);
	return -1;
}


static int usb_write(void *buf, unsigned len)
{
	int r;

	if (fastboot_state == STATE_ERROR)
		goto oops;

	r = write(fb_fp, buf, len);
	if (r < 0) {
		LOGE("Error: usb write failed\n");
		goto oops;
	}

	return r;

oops:
	fastboot_state = STATE_ERROR;
	return -1;
}

void fastboot_ack(const char *code, const char *reason)
{
	char response[64];

	if (fastboot_state != STATE_COMMAND)
		return;

	if (reason == 0)
		reason = "";

	snprintf(response, 64, "%s%s", code, reason);
	fastboot_state = STATE_COMPLETE;

	usb_write(response, strlen(response));

}

#define TEMP_BUFFER_SIZE		512
#define RESULT_FAIL_STRING		"RESULT: FAIL("
void fastboot_fail(const char *reason)
{
	char buf[TEMP_BUFFER_SIZE];

	sprintf(buf, RESULT_FAIL_STRING);
	strncat(buf, reason, TEMP_BUFFER_SIZE - 2 - strlen(RESULT_FAIL_STRING));
	strcat(buf, ")");
	ui_msg(ALERT, LEFT, buf);
	ui_stop_process_bar();
	fastboot_ack("FAIL", reason);
}

void fastboot_okay(const char *info)
{
	ui_msg(TIPS, LEFT, "RESULT: OKAY");
	ui_stop_process_bar();
	fastboot_ack("OKAY", info);
}

static void cmd_getvar(const char *arg, void *data, unsigned sz)
{
	struct fastboot_var *var;

	for (var = varlist; var; var = var->next) {
		if (!strcmp(var->name, arg)) {
			fastboot_okay(var->value);
			return;
		}
	}
	fastboot_okay("");
}

static void cmd_download(const char *arg, void *data, unsigned sz)
{
	char response[64];
	unsigned len = hex2unsigned(arg);
	int r;

	ui_print("RECEIVE DATA...\n");
	download_size = 0;
	if (len > download_max) {
		fastboot_fail("data too large");
		return;
	}

	sprintf(response,"DATA%08x", len);
	if (usb_write(response, strlen(response)) < 0)
		return;
	r = usb_read(download_base, len);
	if ((r < 0) || (r != (int)len)) {
		LOGE("Error:fastboot, cmd_download errro only got %d bytes\n",r);
		fastboot_state = STATE_ERROR;
		return;
	}
	download_size = len;
	fastboot_okay("");
}

static void fastboot_command_loop(void)
{
	struct fastboot_cmd *cmd;
	int r;

	ui_print("FASTBOOT CMD WAITING...\n");

again:
	while (fastboot_state != STATE_ERROR) {
		memset(buffer, 0, 64);
		r = usb_read(buffer, 64);
		if (r < 0) break;
		buffer[r] = 0;

		for (cmd = cmdlist; cmd; cmd = cmd->next) {
			if (memcmp(buffer, cmd->prefix, cmd->prefix_len))
				continue;
			fastboot_state = STATE_COMMAND;
			ui_set_screen_state(1);
			ui_msg(TIPS, LEFT, "CMD(%s)...", buffer);
			ui_start_process_bar();
			cmd->handle((const char*) buffer + cmd->prefix_len,
				    (void*) download_base, download_size);
			if (fastboot_state == STATE_COMMAND)
				fastboot_fail("unknown reason");
			goto again;
		}

		fastboot_fail("unknown command");

	}
	fastboot_state = STATE_OFFLINE;
	ui_print("FASTBOOT OFFLINE!\n");
	LOGW("fastboot: oops!\n");
}


static int fastboot_handler(void *arg)
{
//#define FASTBOOT_OVER_ADB
	for (;;) {
#ifdef FASTBOOT_OVER_ADB
		int rv;
		fb_fp = open("/tmp/fastboot", O_RDWR);
		if (fb_fp < 1) {
			rv = mkfifo("/tmp/fastboot", S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
			if (rv < 0)
				LOGE("unable to create fifo");
			continue;
		}
#else
		sleep(1);
		enable_fp = open("/sys/class/android_usb/android0/enable", O_RDWR);
		if (enable_fp < 1) {
			close(fb_fp);
			continue;
		}
		sleep(1);
		fb_fp = open("/dev/android_adb", O_RDWR);
		if (fb_fp < 1)
			continue;
#endif
		ui_print("FASTBOOT ONLINE.\n");
		fastboot_command_loop();
		close(enable_fp);
		close(fb_fp);
		fb_fp = -1;
		enable_fp = -1;
	}
	return 0;
}


int fastboot_init(void *base, unsigned size)
{
	download_max = size;
	download_base = base;

	fastboot_register("getvar:", cmd_getvar);
	fastboot_register("download:", cmd_download);
	fastboot_publish("version", "0.5");

	ui_print("FASTBOOT INIT COMPLETE.\n");
	property_set("sys.usb.config", "adb");
	fastboot_handler(NULL);

	return 0;
}
