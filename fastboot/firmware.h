/*************************************************************************
 * Copyright(c) 2011 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * **************************************************************************/
#ifndef POWER_REASON_H
#define POWER_REASON_H

#define IPC_DEVICE_NAME		    "/dev/mid_ipc"
#define IPC_READ_RR_FROM_OSNIB	0xC1
#define IPC_WRITE_RR_TO_OSNIB	0xC2
#define IPC_WRITE_ALARM_TO_OSNIB 0xC5
#define IPC_COLD_RESET		    0xC3
#define IPC_READ_VBATTCRIT	    0xC4
#define RR_SIGNED_MOS		    0x0
#define RR_SIGNED_COS           0xa
#define RR_SIGNED_POS			0x0e
#define RR_SIGNED_RECOVERY		0x0C
#define INTE_SCU_IPC_FW_REVISION_GET  0xB0

#define IA32_CPU_OFFSET			0x07
#define IA32_SUPP_OFFSET			0x09
#define IA32_VH_OFFSET			0x0b
#define SCU_ROM_OFFSET			0x03
#define SCU_RT_OFFSET			0x01
#define PUNIT_OFFSET				0x05
#define IFWI_OFFSET				0x0F
	

struct scu_ipc_version {
		unsigned int	count;  /* length of version info */
		unsigned char	data[16]; /* version data */
};

int get_fw_info(struct scu_ipc_version* version);
int set_power_on_reason(unsigned char rs);
int get_poweron_reason();

#endif
