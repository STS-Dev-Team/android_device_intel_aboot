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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "minui/minui.h"
#include "edify/expr.h"
#include "updater/updater.h"
#include "../recovery.h"
#include "common.h"

int write_stitch_image(void *data, size_t size, int update_number);
typedef int (*image_writer_cb_t)(ZipArchive* za, const ZipEntry* entry);
int ifwi_update(const char *dnx, const char *ifwi);

int write_OSIP_image(ZipArchive* za, const ZipEntry* entry, int osip) {
	int size, ret=-1;
	unsigned char *data;

	size = mzGetZipEntryUncompLen(entry);
	data = malloc(size);

	if (data == NULL) {
		fprintf(stderr, "%s: failed to allocate %ld bytes for boot image\n", __FUNCTION__, (long)size);
		return ret;
        }

	ret = mzExtractZipEntryToBuffer(za, entry, data);
	if (!ret) {
		fprintf(stderr, "%s: failed to unzip %ld bytes for boot image\n", __FUNCTION__, (long)size);
		free(data);
		return ret;
	}

	ret = write_stitch_image(data, size, osip);
	free(data);

	return ret;
}

int write_boot_image(ZipArchive* za, const ZipEntry* entry) {
	return write_OSIP_image(za, entry, 0);
}

int write_recovery_image(ZipArchive* za, const ZipEntry* entry) {
	return write_OSIP_image(za, entry, 1);
}

#define FILEMODE  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH

int write_modem_image(ZipArchive* za, const ZipEntry* entry) {
	int fd, sys_status, ret;
	if ((fd = open("/tmp/modem.bin", O_RDWR | O_TRUNC | O_CREAT, FILEMODE)) < 0) {
		fprintf(stderr, "Uable to creat Extract modem file.\n");
		return -1;
	}

	ret = mzExtractZipEntryToFile(za, entry, fd);
	if (!ret) {
		fprintf(stderr, "%s: failed to unzip modem image.\n", __FUNCTION__);
		return ret;
	}

#ifdef CLVT
	ret = system("cmfwdl-app -p /dev/ttyIFX1 -b /tmp/modem.bin -f /tmp/modem.bin");
#else
	ret = system("cmfwdl-app -t /dev/ttyMFD1 -p /dev/ttyIFX0 -b /tmp/modem.bin -f /tmp/modem.bin");
#endif
	sys_status = WEXITSTATUS(ret);
	if (sys_status != 0) {
		fprintf(stderr, "Run modem flash cmd error: %s \n", strerror(sys_status));
		return -1;
	}

	return 0;
}

#define IFWI_IMAGE_PATH	"/tmp/ifwi.zip"
#define DNX_BIN_PATH	"/tmp/dnx.bin"
#define IFWI_BIN_PATH	"/tmp/ifwi.bin"
#define IFWI_NAME	"ifwi"
#define DNX_NAME	"dnx"

int write_ifwi_image(ZipArchive* za, const ZipEntry* entry) {
	int ret = 0, sys_status, ifwi_image_fd, ifwi_bin_fd, dnx_bin_fd, i, num;
	char ifwi_name[128], dnx_name[128];
	ZipArchive ifwi_za;
	const ZipEntry *dnx_entry, *ifwi_entry;

	if ((ifwi_image_fd = open(IFWI_IMAGE_PATH, O_RDWR | O_TRUNC | O_CREAT, FILEMODE)) < 0) {
		fprintf(stderr, "Uable to creat Extracted file:%s.\n",IFWI_IMAGE_PATH);
		return -1;
	}

	ret = mzExtractZipEntryToFile(za, entry, ifwi_image_fd);
	if (!ret) {
		fprintf(stderr, "Failed to unzip %s\n", IFWI_IMAGE_PATH);
		return ret;
	}

	ret = mzOpenZipArchive(IFWI_IMAGE_PATH, &ifwi_za);
	if (ret) {
		fprintf(stderr, "Failed to open zip archive %s\n", IFWI_IMAGE_PATH);
		return ret;
	}

	num = mzZipEntryCount(&ifwi_za);
	for (i = 0; i < num; i++) {
		ifwi_entry = mzGetZipEntryAt(&ifwi_za, i);
		if (ifwi_entry->fileNameLen < sizeof(ifwi_name)){
			strncpy(ifwi_name, ifwi_entry->fileName, ifwi_entry->fileNameLen);
			ifwi_name[ifwi_entry->fileNameLen] = '\0';
		}else{
			fprintf(stderr, "ifwi file name is too big size max :%d.\n", sizeof(ifwi_name));
			return -1;
		}
		if (strncmp(ifwi_name, IFWI_NAME, strlen(IFWI_NAME)))
			continue;

		if ((ifwi_bin_fd = open(IFWI_BIN_PATH, O_RDWR | O_TRUNC | O_CREAT, FILEMODE)) < 0) {
			fprintf(stderr, "unable to creat Extracted file:%s.\n", IFWI_BIN_PATH);
			return -1;
		}
		if ((dnx_bin_fd = open(DNX_BIN_PATH, O_RDWR | O_TRUNC | O_CREAT, FILEMODE)) < 0) {
			fprintf(stderr, "unable to creat Extracted file:%s.\n", DNX_BIN_PATH);
			return -1;
		}

		strcpy(dnx_name, "dnx");
		strncat(dnx_name, &(ifwi_name[strlen(IFWI_NAME)]), sizeof(dnx_name) - strlen("dnx") -1);
		dnx_entry = mzFindZipEntry(&ifwi_za, dnx_name);

		ret = mzExtractZipEntryToFile(&ifwi_za, dnx_entry, dnx_bin_fd);
		if (!ret) {
			fprintf(stderr, "Failed to unzip %s\n", DNX_BIN_PATH);
			return ret;
		}
		close(dnx_bin_fd);
		ret = mzExtractZipEntryToFile(&ifwi_za, ifwi_entry, ifwi_bin_fd);
		if (!ret) {
			fprintf(stderr, "Failed to unzip %s\n", IFWI_BIN_PATH);
			return ret;
		}
		close(ifwi_bin_fd);

		ret = ifwi_update(DNX_BIN_PATH, IFWI_BIN_PATH);
	}

	mzCloseZipArchive(&ifwi_za);
	close(ifwi_image_fd);
	return ret;
}

Value* WriteImageBinFn(image_writer_cb_t image_writer_cb, const char* name, State* state, int argc, Expr* argv[]) {

	char *zip_path;
	char *type;
	int success;

	if (ReadArgs(state, argv, 2, &zip_path, &type) < 0) return NULL;

	ZipArchive* za = ((UpdaterInfo*)(state->cookie))->package_zip;
	const ZipEntry* entry = mzFindZipEntry(za, zip_path);
	if (entry == NULL) {
		fprintf(stderr, "%s: no %s in package\n", name, zip_path);
		success = 0;
		goto out;
	}

	success = !image_writer_cb(za, entry);
out:
	return StringValue(strdup(success ? "t" : ""));

}
Value* WriteBootBinFn(const char* name, State* state, int argc, Expr* argv[]) {
	return WriteImageBinFn(write_boot_image, name, state, argc, argv);
}
Value* WriteRecoveryBinFn(const char* name, State* state, int argc, Expr* argv[]) {
        return WriteImageBinFn(write_recovery_image, name, state, argc, argv);
}
Value* WriteIFWIBinFn(const char* name, State* state, int argc, Expr* argv[]) {
	return WriteImageBinFn(write_ifwi_image, name, state, argc, argv);
}
Value* WriteModemBinFn(const char* name, State* state, int argc, Expr* argv[]) {
	return WriteImageBinFn(write_modem_image, name, state, argc, argv);
}

void Register_librecovery_updater_intel() {
    fprintf(stderr, "installing Intel updater extensions\n");
    RegisterFunction("intel.write_boot_BIN", WriteBootBinFn);
    RegisterFunction("intel.write_IFWI_BIN", WriteIFWIBinFn);
    RegisterFunction("intel.write_modem_BIN", WriteModemBinFn);
    RegisterFunction("intel.write_recovery_BIN", WriteRecoveryBinFn);
}
