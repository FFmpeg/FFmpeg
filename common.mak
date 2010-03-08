#
# common bits used by all libraries
#

all: # make "all" default target

ifndef SUBDIR
vpath %.c   $(SRC_DIR)
vpath %.h   $(SRC_DIR)
vpath %.S   $(SRC_DIR)
vpath %.asm $(SRC_DIR)
vpath %.v   $(SRC_DIR)

ifeq ($(SRC_DIR),$(SRC_PATH_BARE))
BUILD_ROOT_REL = .
else
BUILD_ROOT_REL = ..
endif

ifndef V
Q      = @
ECHO   = printf "$(1)\t%s\n" $(2)
BRIEF  = CC AS YASM AR LD HOSTCC RANLIB STRIP CP
SILENT = DEPCC YASMDEP RM
MSG    = $@
$(foreach VAR,$(BRIEF), \
    $(eval $(VAR) = @$$(call ECHO,$(VAR),$$(MSG)); $($(VAR))))
$(foreach VAR,$(SILENT),$(eval $(VAR) = @$($(VAR))))
$(eval INSTALL = @$(call ECHO,INSTALL,$$(^:$(SRC_DIR)/%=%)); $(INSTALL))
endif

ALLFFLIBS = avcodec avdevice avfilter avformat avutil postproc swscale

CPPFLAGS := -I$(BUILD_ROOT_REL) -I$(SRC_PATH) $(CPPFLAGS)
CFLAGS   += $(ECFLAGS)

%.o: %.c
	$(CCDEP)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CC_DEPFLAGS) -c $(CC_O) $<

%.o: %.S
	$(ASDEP)
	$(AS) $(CPPFLAGS) $(ASFLAGS) $(AS_DEPFLAGS) -c -o $@ $<

%.ho: %.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused -c -o $@ -x c $<

%$(EXESUF): %.c

%.ver: %.v
	$(Q)sed 's/$$MAJOR/$($(basename $(@F))_VERSION_MAJOR)/' $^ > $@

SVN_ENTRIES = $(SRC_PATH_BARE)/.svn/entries
ifeq ($(wildcard $(SVN_ENTRIES)),$(SVN_ENTRIES))
$(BUILD_ROOT_REL)/version.h: $(SVN_ENTRIES)
endif

$(BUILD_ROOT_REL)/version.h: $(SRC_PATH_BARE)/version.sh config.mak
	$< $(SRC_PATH) $@ $(EXTRA_VERSION)

install: install-libs install-headers

uninstall: uninstall-libs uninstall-headers

.PHONY: all depend dep *clean install* uninstall* examples testprogs

# Disable suffix rules.  Most of the builtin rules are suffix rules,
# so this saves some time on slow systems.
.SUFFIXES:

# Do not delete intermediate files from chains of implicit rules
.SECONDARY:
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
HOSTOBJS  := $(addprefix $(SUBDIR),$(addsuffix .o,$(HOSTPROGS)))
HOSTPROGS := $(addprefix $(SUBDIR),$(addsuffix $(HOSTEXESUF),$(HOSTPROGS)))

DEP_LIBS := $(foreach NAME,$(FFLIBS),$(BUILD_ROOT_REL)/lib$(NAME)/$($(CONFIG_SHARED:yes=S)LIBNAME))

ALLHEADERS := $(subst $(SRC_DIR)/,$(SUBDIR),$(wildcard $(SRC_DIR)/*.h $(SRC_DIR)/$(ARCH)/*.h))
SKIPHEADERS := $(addprefix $(SUBDIR),$(SKIPHEADERS-) $(SKIPHEADERS))
checkheaders: $(filter-out $(SKIPHEADERS:.h=.ho),$(ALLHEADERS:.h=.ho))

$(HOSTOBJS): %.o: %.c
	$(HOSTCC) $(HOSTCFLAGS) -c -o $@ $<

$(HOSTPROGS): %$(HOSTEXESUF): %.o
	$(HOSTCC) $(HOSTLDFLAGS) -o $@ $< $(HOSTLIBS)

DEPS := $(OBJS:.o=.d)
depend dep: $(DEPS)

CLEANSUFFIXES     = *.d *.o *~ *.ho *.map *.ver
DISTCLEANSUFFIXES = *.pc
LIBSUFFIXES       = *.a *.lib *.so *.so.* *.dylib *.dll *.def *.dll.a *.exp

-include $(wildcard $(DEPS))
