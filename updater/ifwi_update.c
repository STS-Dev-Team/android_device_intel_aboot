#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "ifwi.h"
#include "common.h"

#define DNX_SYSFS_INT	 "/sys/devices/pci0000:00/0000:00:01.7/DnX"
#define IFWI_SYSFS_INT	 "/sys/devices/pci0000:00/0000:00:01.7/ifwi"
#define PREP_SYSFS_INT	 "/sys/devices/pci0000:00/0000:00:01.7/scu_ipc/medfw_prepare"

#define DEVICE_NAME "/dev/mid_ipc"
#define INTE_SCU_IPC_FW_REVISION_GET  0xB0
#define FIP_pattern 0x50494624
#define IFWI_offset 36

static int find_fw_rev(struct IFWI_rev *ifwi_version)
{
	int devfd, errNo;
	struct scu_ipc_version version;

	version.count = 16;    /*read back 16 bytes fw info from IPC*/
	if ((devfd = open(DEVICE_NAME, O_RDWR)) == -1) {
		fprintf(stderr, "unable to open the DEVICE %s\n", DEVICE_NAME);
		return -1;
	}

	if ((errNo = ioctl(devfd, INTE_SCU_IPC_FW_REVISION_GET, &version)) < 0) {
		fprintf(stderr, "finding fw_info, ioctl for DEVICE %s, returns error-%d\n", DEVICE_NAME, errNo);
		return -1;
	}

	close(devfd);

	ifwi_version->major = version.data[15];
	ifwi_version->minor = version.data[14];
	return 0;
}

static int crack_update_fw(const char *fw_file, struct IFWI_rev *ifwi_version){
	struct FIP_header fip;
	FILE *fd;
	int tmp = 0;
	int location;

	memset((void *)&fip, 0, sizeof(fip));

	if ((fd = fopen(fw_file, "rb")) == NULL) {
		fprintf(stderr, "fopen error: Unable to open file\n");
		return -1;
	}

	while (tmp != FIP_pattern) {
		int cur;
		fread(&tmp, sizeof(int), 1, fd);
		if (ferror(fd) || feof(fd)) {
			fprintf(stderr, "find FIP_pattern failed\n");
			fclose(fd);
			return -1;
		}
		cur = ftell(fd) - sizeof(int);
		fseek(fd, cur + sizeof(char), SEEK_SET);
	}
	location = ftell(fd) - sizeof(char);

	fseek(fd, location, SEEK_SET);
	fread((void *)&fip, sizeof(fip), 1, fd);
	if (ferror(fd) || feof(fd)) {
		fprintf(stderr, "read of FIP_header failed\n");
		fclose(fd);
		return -1;
	}
	fclose(fd);

	ifwi_version->major = fip.ifwi_rev.major;
	ifwi_version->minor = fip.ifwi_rev.minor;
	return 0;
}

int ifwi_update(const char *dnx, const char *ifwi)
{
	int fupd_hdr_len, fsize, dnx_size, ret = 0;
	size_t cont;
	FILE *f_src, *f_dst;
	char buff[4096];
	struct stat sb_ifwi, sb_dnx;
	struct IFWI_rev obrev, frev;

	find_fw_rev(&obrev);
	crack_update_fw(ifwi, &frev);
	printf("IFWI onboard revision: %x.%x\n", obrev.major, obrev.minor);
	printf("IFWI file revision: %x.%x\n", frev.major, frev.minor);
	if (obrev.major != frev.major) {
		fprintf(stderr, "Warning: Major revision not match\n");
		goto end;
	}
	if (obrev.minor >= frev.minor) {
		fprintf(stderr, "Warning: Skip IFWI update\n");
		goto end;
	}

	printf("Start to prepare for IFWI update ...\n");

	f_src = fopen(dnx, "rb");
	if (f_src == NULL) {
		fprintf(stderr, "open %s failed\n", dnx);
		ret = -1;
		goto end;
	}
	if (fstat(fileno(f_src), &sb_dnx) == -1) {
		fprintf(stderr, "get %s fstat failed\n", dnx);
		ret = -1;
		goto err;
	}
	f_dst = fopen(DNX_SYSFS_INT, "wb");
	if (f_dst == NULL) {
		fprintf(stderr, "open %s failed\n", DNX_SYSFS_INT);
		ret = -1;
		goto err;
	}
	while ((cont = fread(buff, 1, sizeof(buff), f_src)) && cont <= sizeof(buff))
		fwrite(buff, 1, cont, f_dst);
	fclose(f_src);
	fclose(f_dst);

	f_src = fopen(ifwi, "rb");
	if (f_src == NULL) {
		fprintf(stderr, "open %s failed\n", ifwi);
		ret = -1;
		goto end;
	}
	if (fstat(fileno(f_src), &sb_ifwi) == -1) {
		fprintf(stderr, "get %s fstat failed\n", ifwi);
		ret = -1;
		goto err;
	}
	f_dst = fopen(IFWI_SYSFS_INT, "wb");
	if (f_dst == NULL) {
		fprintf(stderr, "open %s failed\n", IFWI_SYSFS_INT);
		ret = -1;
		goto err;
	}
	while ((cont = fread(buff, 1, sizeof(buff), f_src)) && cont <= sizeof(buff))
		fwrite(buff, 1, cont, f_dst);
	fclose(f_src);
	fclose(f_dst);

	f_src = fopen(PREP_SYSFS_INT, "r");
	if (f_src == NULL) {
		fprintf(stderr, "open %s failed\n", PREP_SYSFS_INT);
		ret = -1;
		goto end;
	}
	cont = fread(buff, 1, sizeof(buff), f_src);
	buff[cont] = '\0';
	printf("%s\n", buff);
	sscanf(buff, "fupd_hdr_len=%d, fsize=%d, dnx_size=%d",
		&fupd_hdr_len, &fsize, &dnx_size);
	if ((sb_dnx.st_size == dnx_size) && (sb_ifwi.st_size == fsize))
		printf("IFWI update prepare successed\n");
	else {
		fprintf(stderr, "IFWI update prepare failed\n");
		ret = -1;
	}
err:
	fclose(f_src);
end:
	return ret;
}
