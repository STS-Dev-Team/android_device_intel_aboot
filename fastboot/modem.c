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

#include <unistd.h>
#include <sys/reboot.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include "minui.h"
#include "common.h"

#define FFL_TTY_MAGIC	0x77
#define FFL_TTY_MODEM_RESET		_IO(FFL_TTY_MAGIC, 4)
int modem_boot_timer_cb(void*ign)
{
	int fd;
	fd = open("/dev/ttyIFX0", O_RDWR);
	write(fd, "AT+CFUN=4\r\n",11);
	return TIMER_STOP;
}
void modem_airplane()
{
	ui_timer_t *modem_boot_timer;
        reset_modem();
	modem_boot_timer = ui_alloc_timer(modem_boot_timer_cb, 0, NULL);
	ui_start_timer(modem_boot_timer,10000);
	return;
}
void reset_modem()
{
	int fd;
        fd = open("/dev/ttyIFX0", O_RDWR);
        if ( ioctl(fd, FFL_TTY_MODEM_RESET) < 0 )
        {
            LOGE("Could not reset modem\n");
	}
	close(fd);
	return;
}
