ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

# librecovery_update_intel is a set of edify extension functions for
# doing radio and hboot updates on HTC devices.

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := recovery_updater.c ifwi_update.c
LOCAL_C_INCLUDES += bootable/recovery
LOCAL_MODULE := librecovery_updater_intel
LOCAL_C_INCLUDES += bionic/libc/private
include $(BUILD_STATIC_LIBRARY)

endif   # !TARGET_SIMULATOR
