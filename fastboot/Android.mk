ifeq ($(filter sdk_addon blur_sdk,$(MAKECMDGOALS)),)

ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    main.c \
    ui.c \
    firmware.c \
    fastboot.c \
    pos.c \
    cos.c \
    modem.c \
    power.c \
    system_recovery.c \
    ../updater/ifwi_update.c

LOCAL_MODULE := fastboot

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_MODULE_TAGS := eng

ifeq ($(TARGET_PRODUCT),ctp_pr0)
LOCAL_CFLAGS += -DCLVT
else ifeq ($(TARGET_PRODUCT),ctp_pr1)
LOCAL_CFLAGS += -DCLVT
endif

LOCAL_C_INCLUDES +=   \
				bootable/recovery/minui \
				bootable/recovery/mtdutils \
				bootable/recovery/volumeutils \
				system/extras/ext4_utils \
				device/intel/kboot/flash_stitched \
				bionic/libc/private

LOCAL_STATIC_LIBRARIES :=
LOCAL_STATIC_LIBRARIES += \
				libminui \
				libdevutils \
				libvolumeutils \
				libz \
				libpixelflinger_static \
				libpng \
				libcutils \
				libunz \
				libext4_utils \
				libosip \
				libpower \
				libstdc++ \
				libc

#libpixelflinger_static for x86 is using encoder under hardware/intel/apache-harmony
ifeq ($(TARGET_ARCH),x86)
LOCAL_STATIC_LIBRARIES += libenc
endif

include $(BUILD_EXECUTABLE)

endif    # !TARGET_SIMULATOR

endif
