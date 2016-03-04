#
# Copyright (C) 2013 The Android-x86 Open Source Project
#
# Licensed under the GNU General Public License Version 2 or later.
# You may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.gnu.org/licenses/gpl.html
#

ifndef FFMPEG_DIR
FFMPEG_DIR := $(dir $(call my-dir))
endif

define RESET
$(1) :=
$(1)-yes :=
endef

FF_VARS := FFLIBS OBJS ARMV5TE-OBJS ARMV6-OBJS VFP-OBJS NEON-OBJS MIPSFPU-OBJS MIPS32R2-OBJS MIPSDSPR1-OBJS MIPSDSPR2-OBJS ALTIVEC-OBJS VIS-OBJS MMX-OBJS YASM-OBJS

FFMPEG_ARCH := $(TARGET_ARCH)

FFMPEG_2ND_ARCH := false
ifneq ($(TARGET_2ND_ARCH_VARIANT),)
   ifeq ($(TARGET_PREFER_32_BIT_APPS),true)
       ifeq ($(FFMPEG_MULTILIB),64)
          FFMPEG_2ND_ARCH := true
       endif
   else
       ifeq ($(FFMPEG_MULTILIB),32)
          FFMPEG_2ND_ARCH := true
       endif
   endif
endif

ifeq ($(FFMPEG_2ND_ARCH), true)
    FFMPEG_ARCH := $(TARGET_2ND_ARCH)
endif

ifeq ($(FFMPEG_ARCH),arm64)
    FFMPEG_ARCH := aarch64
endif

FFMPEG_ARCH_VARIANT := $(TARGET_ARCH_VARIANT)
ifeq ($(FFMPEG_2ND_ARCH), true)
   FFMPEG_ARCH_VARIANT := $(TARGET_2ND_ARCH_VARIANT)
endif

# bionic include must be the first
LOCAL_C_INCLUDES := \
	bionic/libc/include \
	$(FFMPEG_DIR)android/include \
	$(FFMPEG_DIR) \

ifneq ($(filter x86 x86_64, $(FFMPEG_ARCH)),)
    TARGET_CONFIG := config-$(FFMPEG_ARCH)-$(FFMPEG_ARCH_VARIANT).h
else
    TARGET_CONFIG := config-$(FFMPEG_ARCH_VARIANT).h
endif

LOCAL_CFLAGS := \
	-DANDROID_SDK_VERSION=$(PLATFORM_SDK_VERSION) \
	-DTARGET_CONFIG=\"$(TARGET_CONFIG)\" \
	-DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -DPIC \

LOCAL_ASFLAGS := $(LOCAL_CFLAGS)
