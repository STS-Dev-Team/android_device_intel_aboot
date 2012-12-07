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

#include "debug.h"
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "ui.h"


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

	if (fastboot_state == STATE_ERROR)
		goto oops;

	dprintf(SPEW, "usb_read %d\n", len);
	while (len > 0) {
		xfer = (len > 4096) ? 4096 : len;

		r = read(fb_fp, buf, xfer);
		dprintf(9, "read returns %d\n", r);
		if (r < 0) {
			dprintf(INFO, "read failed\n");
			goto oops;
		}

		count += r;
		buf +=r;
		len -= r;

		/* short transfer? */
		if (r != xfer) break;
	}

	return count;

oops:
	fastboot_state = STATE_ERROR;
	dprintf(INFO, "usb_read faled: asked for %d and got %d\n", len, r);
	return -1;
}


int usb_write(void *buf, unsigned len)
{
	int r;

	if (fastboot_state == STATE_ERROR)
		goto oops;

	dprintf(SPEW, "usb_write %d\n", len);
	r = write(fb_fp, buf, len);
	dprintf(SPEW, "write returns %d\n", r);
	if (r < 0) {
		dprintf(INFO, "write failed\n");
		goto oops;
	}

	return r;

oops:
	fastboot_state = STATE_ERROR;
	return -1;
}

void fastboot_ack(const char *code, const char *reason)
{
	char response[65];

	if (fastboot_state != STATE_COMMAND)
		return;

	if (reason == 0)
		reason = "";

	snprintf(response, 65, "%s%s", code, reason);
	fastboot_state = STATE_COMPLETE;

	dprintf(SPEW, "fastboot_ack %s: %s\n", code, reason);
	usb_write(response, strlen(response));

}

void fastboot_info(const char *info)
{
	char response[65];

	if (fastboot_state != STATE_COMMAND)
		return;

	snprintf(response, 65, "%s%s", "INFO", info);

	usb_write(response, strlen(response));
}

void fastboot_fail(const char *reason)
{
	fastboot_ack("FAIL", reason);
}

void fastboot_okay(const char *info)
{
	fastboot_ack("OKAY", info);
}

static void cmd_getvar(const char *arg, void *data, unsigned sz)
{
	struct fastboot_var *var;

	dprintf(INFO,"fastboot: cmd_getvar %s\n",arg);
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

	dprintf(INFO,"fastboot: cmd_download %d bytes\n",len);

	download_size = 0;
	if (len > download_max) {
		fastboot_fail("data too large");
		return;
	}

	sprintf(response,"DATA%08x", len);
	if (usb_write(response, strlen(response)) < 0)
		return;

	r = usb_read(download_base, len);
	if ((r < 0) || (r != len)) {
		dprintf(INFO,"fastboot: cmd_download errro only got %d bytes\n",r);
		fastboot_state = STATE_ERROR;
		return;
	}
	download_size = len;
	fastboot_okay("");
}

void progress_file_write(char bar_status)
{
	int  fd;
	fd = open("/tmp/progress.txt", O_WRONLY|O_CREAT|O_TRUNC, 0777);
	if (fd < 0){
		printf("open file faile!");
	}
	write(fd, &bar_status, 1);
	close(fd);	
}

extern int is_power_low(void);

static void fastboot_command_loop(void)
{
	struct fastboot_cmd *cmd;
	int r;
	dprintf(INFO,"fastboot: processing commands\n");

again:
	while (fastboot_state != STATE_ERROR) {
		memset(buffer, 0, sizeof(buffer));
		r = usb_read(buffer, 64);
		if (r < 0) break;
		buffer[r] = 0;
		dprintf(INFO,"fastboot: %s\n", buffer);

		progress_file_write(BAR_START);
		for (cmd = cmdlist; cmd; cmd = cmd->next) {
			if (memcmp(buffer, cmd->prefix, cmd->prefix_len))
				continue;
			fastboot_state = STATE_COMMAND;
			if (is_power_low()) {
				fastboot_fail("Battery power too low");
				goto again;
			}
			cmd->handle((const char*) buffer + cmd->prefix_len,
				    (void*) download_base, download_size);
			if (fastboot_state == STATE_COMMAND)
				fastboot_fail("unknown reason");
			progress_file_write(BAR_FINISH);
			goto again;
		}

		fastboot_fail("unknown command");

	}
	fastboot_state = STATE_OFFLINE;
	dprintf(INFO,"fastboot: oops!\n");
}


static int fastboot_handler(void *arg)
{

	for (;;) {
		sleep(1);
		enable_fp = open("/dev/android_adb_enable", O_RDWR);
		if (enable_fp < 1) {
			close(fb_fp);
			continue;
		}
		sleep(1);
		fb_fp = open("/dev/android_adb", O_RDWR);
		if (fb_fp < 1)
			continue;
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
	dprintf(INFO, "fastboot_init()\n");
	download_max = size;
	download_base = base;

	fastboot_register("getvar:", cmd_getvar);
	fastboot_register("download:", cmd_download);
	fastboot_publish("version", "0.5");

	fastboot_handler(NULL);

	return 0;
}
