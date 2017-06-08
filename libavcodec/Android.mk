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

LOCAL_C_INCLUDES +=		\
	external/zlib

LOCAL_SHARED_LIBRARIES +=	\
	libz \

ifeq ($(CONFIG_VAAPI),yes)
  LOCAL_SRC_FILES += ../ffmpeg_vaapi.c
  LOCAL_SHARED_LIBRARIES += libva
endif

ifneq ($(ARCH_ARM_HAVE_NEON),)
  LOCAL_SRC_FILES += neon/mpegvideo.c
endif

LOCAL_MULTILIB := $(FFMPEG_MULTILIB)
include $(BUILD_SHARED_LIBRARY)

FFMPEG_MULTILIB := 64
include $(LOCAL_PATH)/../android/build.mk

LOCAL_C_INCLUDES +=		\
	external/zlib

LOCAL_SHARED_LIBRARIES +=	\
	libz \

ifeq ($(CONFIG_VAAPI),yes)
  LOCAL_SRC_FILES += ../ffmpeg_vaapi.c
  LOCAL_SHARED_LIBRARIES += libva
endif

ifneq ($(ARCH_ARM_HAVE_NEON),)
  LOCAL_SRC_FILES += neon/mpegvideo.c
endif

# This file crashes SDCLANG at -O2 or -O3
$(intermediates)/vp9dsp_8bpp.o: PRIVATE_CFLAGS += $(if $(filter arm64,$(TARGET_ARCH)),-O1)

LOCAL_MULTILIB := $(FFMPEG_MULTILIB)
include $(BUILD_SHARED_LIBRARY)
