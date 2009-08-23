#
# common bits used by all libraries
#

all: # make "all" default target

ifndef SUBDIR
vpath %.c   $(SRC_DIR)
vpath %.h   $(SRC_DIR)
vpath %.S   $(SRC_DIR)
vpath %.asm $(SRC_DIR)

ifeq ($(SRC_DIR),$(SRC_PATH_BARE))
BUILD_ROOT_REL = .
else
BUILD_ROOT_REL = ..
endif

ALLFFLIBS = avcodec avdevice avfilter avformat avutil postproc swscale

CPPFLAGS := -DHAVE_AV_CONFIG_H -I$(BUILD_ROOT_REL) -I$(SRC_PATH) $(CPPFLAGS)

%.o: %.c
	$(CCDEP)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CC_DEPFLAGS) -c $(CC_O) $<

%.o: %.S
	$(ASDEP)
	$(AS) $(CPPFLAGS) $(ASFLAGS) $(AS_DEPFLAGS) -c -o $@ $<

%.ho: %.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused -c -o $@ -x c $<

%$(EXESUF): %.c

SVN_ENTRIES = $(SRC_PATH_BARE)/.svn/entries
ifeq ($(wildcard $(SVN_ENTRIES)),$(SVN_ENTRIES))
$(BUILD_ROOT_REL)/version.h: $(SVN_ENTRIES)
endif

$(BUILD_ROOT_REL)/version.h: $(SRC_PATH_BARE)/version.sh config.mak
	$< $(SRC_PATH) $@ $(EXTRA_VERSION)

install: install-libs install-headers

uninstall: uninstall-libs uninstall-headers

.PHONY: all depend dep *clean install* uninstall* examples testprogs
endif

OBJS-$(HAVE_MMX) +=  $(MMX-OBJS-yes)

CFLAGS    += $(CFLAGS-yes)
OBJS      += $(OBJS-yes)
FFLIBS    := $(FFLIBS-yes) $(FFLIBS)
TESTPROGS += $(TESTPROGS-yes)

FFEXTRALIBS := $(addprefix -l,$(addsuffix $(BUILDSUF),$(FFLIBS))) $(EXTRALIBS)
FFLDFLAGS   := $(addprefix -L$(BUILD_ROOT)/lib,$(ALLFFLIBS)) $(LDFLAGS)

EXAMPLES  := $(addprefix $(SUBDIR),$(addsuffix -example$(EXESUF),$(EXAMPLES)))
OBJS      := $(addprefix $(SUBDIR),$(OBJS))
TESTPROGS := $(addprefix $(SUBDIR),$(addsuffix -test$(EXESUF),$(TESTPROGS)))

DEP_LIBS := $(foreach NAME,$(FFLIBS),$(BUILD_ROOT_REL)/lib$(NAME)/$($(CONFIG_SHARED:yes=S)LIBNAME))

ALLHEADERS := $(subst $(SRC_DIR)/,$(SUBDIR),$(wildcard $(SRC_DIR)/*.h $(SRC_DIR)/$(ARCH)/*.h))
SKIPHEADERS = $(addprefix $(SUBDIR),$(SKIPHEADERS-))
checkheaders: $(filter-out $(SKIPHEADERS:.h=.ho),$(ALLHEADERS:.h=.ho))

DEPS := $(OBJS:.o=.d)
depend dep: $(DEPS)

CLEANSUFFIXES     = *.o *~ *.ho *.map
DISTCLEANSUFFIXES = *.d *.pc
LIBSUFFIXES       = *.a *.lib *.so *.so.* *.dylib *.dll *.def *.dll.a *.exp

-include $(wildcard $(DEPS))
