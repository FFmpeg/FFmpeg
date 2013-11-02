#
# Copyright (C) 2013 The Android-x86 Open Source Project
#
# Licensed under the GNU General Public License Version 2 or later.
# You may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.gnu.org/licenses/gpl.html
#

define RESET
$(1) :=
$(1)-yes :=
endef

FF_VARS := FFLIBS OBJS ARMV5TE-OBJS ARMV6-OBJS VFP-OBJS NEON-OBJS MIPSFPU-OBJS MIPS32R2-OBJS MIPSDSPR1-OBJS MIPSDSPR2-OBJS ALTIVEC-OBJS VIS-OBJS MMX-OBJS YASM-OBJS

include $(call all-subdir-makefiles)
