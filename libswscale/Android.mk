#
# Copyright (C) 2013 The Android-x86 Open Source Project
#
# Licensed under the GNU General Public License Version 2 or later.
# You may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.gnu.org/licenses/gpl.html
#

LOCAL_PATH := $(call my-dir)

FFMPEG_MULTILIB := 32
include $(LOCAL_PATH)/../android/build.mk

LOCAL_SHARED_LIBRARIES += \
	libavutil

LOCAL_MULTILIB := $(FFMPEG_MULTILIB)
include $(BUILD_SHARED_LIBRARY)

FFMPEG_MULTILIB := 64
include $(LOCAL_PATH)/../android/build.mk

LOCAL_SHARED_LIBRARIES += \
	libavutil

LOCAL_MULTILIB := $(FFMPEG_MULTILIB)
include $(BUILD_SHARED_LIBRARY)
