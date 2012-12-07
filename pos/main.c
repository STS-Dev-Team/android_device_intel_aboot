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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <syslog.h>

#include <sys/reboot.h>
#include <sys/ioctl.h>
#include <linux/reboot.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>

#include "ota.h"

extern int pos_event_main(int argc, char *argv[], int msgid);
extern int pos_ui(int argc, char *argv[], int fd, int msgid);
extern int aboot_main(int argc, char *argv[]);
extern void write_to_user(char *fmt, ...);
extern void cmd_reboot(const char *arg, void *data, unsigned sz);

int pos_main(int argc, char *argv[])
{
	int fd[2];
	pid_t pid;
	key_t key;
	int msgid;

	key = getpid();
	msgid = msgget(key, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
	if (msgid == -1) {
		syslog(LOG_ERR, "msg create error\n");
		return -1;
	}

	if (pipe(fd) < 0)
		syslog(LOG_ERR, "pipe error");
	if ((pid = fork()) < 0) {
		syslog(LOG_ERR, "fork error");
	} else if (pid > 0) {
		close(fd[0]);
		close(STDOUT_FILENO);
		dup2(fd[1], STDOUT_FILENO);

		pos_event_main(argc, argv, msgid);

		if (ota_update() == INSTALL_SUCCESS) {
			write_to_user
			    ("OTA update successfully, reboot to Android system...\n");
			cmd_reboot(NULL, NULL, 0);
		} else
			aboot_main(argc, argv);
	} else {
		close(fd[1]);
		pos_ui(argc, argv, fd[0], msgid);
	}
	return 0;
}
