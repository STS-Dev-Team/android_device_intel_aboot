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

#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdlib.h>
#include <syslog.h>

#define IPC_DEVICE_NAME		    "/dev/mid_ipc"
#define IPC_READ_RR_FROM_OSNIB	0xC1
#define IPC_WRITE_RR_TO_OSNIB	0xC2
#define IPC_WRITE_ALARM_TO_OSNIB 0xC5
#define IPC_COLD_RESET		    0xC3
#define IPC_READ_VBATTCRIT	    0xC4
#define RR_SIGNED_MOS		    0x0
#define RR_SIGNED_COS           0xa

extern int pos_main(int argc, char *argv[]);
extern int cos_main(int argc, char *argv[]);
/* return 1 if screen state actually changed */
extern int cos_set_screen_state(int);
extern int cos_ensure_battery_level(int cos_mode);
extern void android_reboot(void);
char *get_datadir();
#define MAX_SIZE   (33*55)

#define COS_CHRG_EVENT	 1
#define COS_CHRG_REMOVE	 "u:0"
#define COS_CHRG_INSERT	 "u:1"
#define COS_VOL_GATE	 2
#define COS_VOL_LOW	 "b:0"
#define COS_VOL_HIGH	 "b:1"
#define CHRG_EVENT	 3
#define CHRG_INSERT	 "c:1"
#define CHRG_REMOVE	 "c:0"
#define BAT_STATUS	 8
#define BAT_PRESENT	 "bta:0"
#define COS_BOOT_MOS	 9
#define COS_BOOT_MOS_BUF "mos"
#define COS_BAT_STATUS	 10
#define COS_TEMP_EVENT 5
#endif // __MAIN_H__
