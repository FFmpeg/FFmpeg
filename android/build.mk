#
# Copyright (C) 2013 The Android-x86 Open Source Project
#
# Licensed under the GNU General Public License Version 2 or later.
# You may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.gnu.org/licenses/gpl.html
#

ifndef FFDROID_DIR
FFDROID_DIR := $(call my-dir)
endif

$(foreach V,$(FF_VARS),$(eval $(call RESET,$(V))))

include $(CLEAR_VARS)
include $(FFDROID_DIR)/ffmpeg.mk
SUBDIR := $(FFDROID_DIR)/include/
include $(LOCAL_PATH)/Makefile $(wildcard $(LOCAL_PATH)/$(TARGET_ARCH)/Makefile)
include $(FFMPEG_DIR)arch.mak

# remove duplicate objects
OBJS := $(sort $(OBJS) $(OBJS-yes))

ASM_SUFFIX := $(if $(filter x86,$(TARGET_ARCH)),asm,S)
ALL_S_FILES := $(subst $(LOCAL_PATH)/,,$(wildcard $(LOCAL_PATH)/$(TARGET_ARCH)/*.$(ASM_SUFFIX)))
ALL_S_FILES := $(if $(filter S,$(ASM_SUFFIX)),$(ALL_S_FILES),$(filter $(patsubst %.o,%.asm,$(YASM-OBJS) $(YASM-OBJS-yes)),$(ALL_S_FILES)))

ifneq ($(ALL_S_FILES),)
S_OBJS := $(ALL_S_FILES:%.$(ASM_SUFFIX)=%.o)
C_OBJS := $(filter-out $(S_OBJS),$(OBJS))
S_OBJS := $(filter $(S_OBJS),$(OBJS))
else
C_OBJS := $(OBJS)
S_OBJS :=
endif

C_FILES := $(C_OBJS:%.o=%.c)
S_FILES := $(S_OBJS:%.o=%.$(ASM_SUFFIX))

LOCAL_MODULE := lib$(NAME)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_ARM_MODE := $(if $(filter yes,$(CONFIG_THUMB)),thumb,arm)

LOCAL_SRC_FILES := $(C_FILES) $(if $(filter S,$(ASM_SUFFIX)),$(S_FILES))

intermediates := $(local-intermediates-dir)
ifeq ($(TARGET_ARCH),x86)
GEN := $(S_OBJS:%=$(intermediates)/%)
$(GEN): YASM := yasm
$(GEN): YASMFLAGS := -felf -DPIC $(LOCAL_C_INCLUDES:%=-I%)
$(GEN): PRIVATE_CUSTOM_TOOL = $(YASM) $(YASMFLAGS) -Pconfig.asm -o $@ $<
$(GEN): $(intermediates)/%.o: $(LOCAL_PATH)/%.asm $(SUBDIR)config.asm
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)
endif

LOCAL_CFLAGS += \
	-O3 -std=c99 -ftree-vectorize -fomit-frame-pointer \
	-fpredictive-commoning -fgcse-after-reload -fipa-cp-clone \
	-Wno-missing-field-initializers -Wno-parentheses \
	-Wno-pointer-sign -Wno-sign-compare -Wno-switch \
	$(if $(filter x86,$(TARGET_ARCH)),-fno-pic -fno-pie) \
	$(if $(filter armv7-a-neon,$(TARGET_ARCH_VARIANT)),-mvectorize-with-neon-quad) \

LOCAL_LDFLAGS := -Wl,--no-fatal-warnings

LOCAL_SHARED_LIBRARIES := $(sort $(FFLIBS-yes:%=lib%) $(FFLIBS:%=lib%))
