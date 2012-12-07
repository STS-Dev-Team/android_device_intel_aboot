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

#include <sys/reboot.h>
#include <sys/ioctl.h>

#include <linux/reboot.h>

#include "recovery.h"

char *get_datadir()
{
	char *datadir = getenv("RECOVERY_DATADIR");
	return datadir ? datadir : ".";
}

int get_poweron_reason()
{
	int devfd, errNo;
	unsigned char rbt_reason;

	if ((devfd = open(IPC_DEVICE_NAME, O_RDWR)) < 0) {
		syslog(LOG_ERR, "unable to open the DEVICE %s\n", IPC_DEVICE_NAME);
		exit(1);
	}

	if ((errNo = ioctl(devfd, IPC_READ_RR_FROM_OSNIB, &rbt_reason)) < 0) {
		syslog(LOG_ERR, "ioctl for DEVICE %s, returns error-%d\n", IPC_DEVICE_NAME, errNo);
	}

	syslog(LOG_DEBUG, "OSNIBR.RR = %x\n", rbt_reason);

	return rbt_reason;
}

int main(int argc, char *argv[])
{
	openlog("recovery", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER);
	syslog(LOG_DEBUG, "This is a syslog test from recovery\n");
	switch (get_poweron_reason()) {
	case RR_SIGNED_COS:
		syslog(LOG_DEBUG,
			"COS Mode Loaded ...\n");
		closelog();
		openlog("cos", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER);
		cos_main(argc, argv);
		closelog();
		break;
	default:
		syslog(LOG_DEBUG,
			"POS Mode Loaded ...\n");
		closelog();
		openlog("pos", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER);
		pos_main(argc, argv);
		closelog();
		break;
	}
	return 0;
}
