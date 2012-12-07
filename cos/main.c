/*
 * Copyright (c) 2011 Borqs Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in
 *	the documentation and/or other materials provided with the
 *	distribution.
 *  * Neither the name of Borqs Ltd. nor the names of its contributors
 *	may be used to endorse or promote products derived from this
 *	software without specific prior written permission.
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
#include <syslog.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>

extern int cos_event_main(int argc, char *argv[], int msgid);
extern int cos_ui(int argc, char *argv[], int fd, int msgid);

int cos_main(int argc, char *argv[])
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
		syslog(LOG_ERR, "pipe error\n");

	if ((pid = fork()) < 0) {
		syslog(LOG_ERR, "fork error\n");
	} else if (pid > 0) {
		cos_event_main(argc, argv, msgid);
	} else {
		cos_ui(argc, argv, fd[0], msgid);
	}

	return 0;
}
