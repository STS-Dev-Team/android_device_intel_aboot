#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>

#include "common.h"
#include "firmware.h"

int get_fw_info(struct scu_ipc_version* version)
{
	int devfd, errNo;

	version->count = 16;    /*read back 16 bytes fw info from IPC*/
	if ((devfd = open(IPC_DEVICE_NAME, O_RDWR)) == -1) {
			LOGW("unable to open the DEVICE %s\n", IPC_DEVICE_NAME);
			return -1;
	}

	if ((errNo = ioctl(devfd, INTE_SCU_IPC_FW_REVISION_GET, version)) < 0) {
			LOGW("finding fw_info, ioctl for DEVICE %s, returns error-%d\n", IPC_DEVICE_NAME, errNo);
			close(devfd);
			return -1;
	}

	close(devfd);
	return 0;
}

int set_power_on_reason(unsigned char rs)
{
	int devfd = 0, ret;

	//rbt_reason = RR_SIGNED_MOS;

	if ((devfd = open(IPC_DEVICE_NAME, O_RDWR)) < 0) {
		LOGE("unable to open the DEVICE %s\n",
			IPC_DEVICE_NAME);
		ret = 0;
		goto err1;
	}

	if ((ret = ioctl(devfd, IPC_WRITE_RR_TO_OSNIB, &rs)) < 0) {
		LOGE("ioctl for DEVICE %s, returns error-%d\n", IPC_DEVICE_NAME, ret);
		ret = 0;
		goto err2;
	}
	ret = 1;
err2:
	close(devfd);
err1:
	return ret;
}

int get_poweron_reason()
{
	int devfd, errNo;
	unsigned char rbt_reason;

	if ((devfd = open(IPC_DEVICE_NAME, O_RDWR)) < 0) {
		LOGE("unable to open the DEVICE %s\n",
			IPC_DEVICE_NAME);
		return -1;
	}

	if ((errNo = ioctl(devfd, IPC_READ_RR_FROM_OSNIB, &rbt_reason)) < 0) {
		LOGE("ioctl for DEVICE %s, returns error-%d\n",
			IPC_DEVICE_NAME, errNo);
	}

	LOGI("OSNIBR.RR = %x\n", rbt_reason);

	return rbt_reason;
}

