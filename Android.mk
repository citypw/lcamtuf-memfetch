LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := memfetch
LOCAL_SRC_FILES := lcamtuf-memfetch/memfetch.c
include $(BUILD_EXECUTABLE)

