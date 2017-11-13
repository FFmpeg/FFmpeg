include avbuild/config.mak

vpath %.c    $(SRC_PATH)
vpath %.m    $(SRC_PATH)
vpath %.h    $(SRC_PATH)
vpath %.S    $(SRC_PATH)
vpath %.asm  $(SRC_PATH)
vpath %.v    $(SRC_PATH)
vpath %.texi $(SRC_PATH)

ifndef V
Q      = @
ECHO   = printf "$(1)\t%s\n" $(2)
BRIEF  = CC HOSTCC HOSTLD AS X86ASM AR LD
SILENT = DEPCC DEPHOSTCC DEPAS DEPX86ASM RANLIB RM STRIP
MSG    = $@
M      = @$(call ECHO,$(TAG),$@);
$(foreach VAR,$(BRIEF), \
    $(eval override $(VAR) = @$$(call ECHO,$(VAR),$$(MSG)); $($(VAR))))
$(foreach VAR,$(SILENT),$(eval override $(VAR) = @$($(VAR))))
$(eval INSTALL = @$(call ECHO,INSTALL,$$(^:$(SRC_PATH)/%=%)); $(INSTALL))
endif

ALLFFLIBS = avcodec avdevice avfilter avformat avresample avutil swscale

IFLAGS     := -I. -I$(SRC_PATH)
CPPFLAGS   := $(IFLAGS) $(CPPFLAGS)
CFLAGS     += $(ECFLAGS)
CCFLAGS     = $(CPPFLAGS) $(CFLAGS)
OBJCFLAGS  += $(EOBJCFLAGS)
OBJCCFLAGS  = $(CPPFLAGS) $(CFLAGS) $(OBJCFLAGS)
ASFLAGS    := $(CPPFLAGS) $(ASFLAGS)
X86ASMFLAGS += $(IFLAGS:%=%/) -I$(<D)/ -Pconfig.asm
HOSTCCFLAGS = $(IFLAGS) $(HOSTCPPFLAGS) $(HOSTCFLAGS)
LDFLAGS    := $(ALLFFLIBS:%=$(LD_PATH)lib%) $(LDFLAGS)

define COMPILE
	$(call $(1)DEP,$(1))
	$($(1)) $($(1)FLAGS) $($(1)_DEPFLAGS) $($(1)_C) $($(1)_O) $<
endef

COMPILE_C = $(call COMPILE,CC)
COMPILE_S = $(call COMPILE,AS)
COMPILE_M = $(call COMPILE,OBJCC)
COMPILE_X86ASM = $(call COMPILE,X86ASM)
COMPILE_HOSTC = $(call COMPILE,HOSTCC)

%.o: %.c
	$(COMPILE_C)

%.o: %.S
	$(COMPILE_S)

%.o: %.m
	$(COMPILE_M)

%_host.o: %.c
	$(COMPILE_HOSTC)

%.o: %.asm
	$(COMPILE_X86ASM)
	-$(STRIP) $(STRIPFLAGS) $@

%.i: %.c
	$(CC) $(CCFLAGS) $(CC_E) $<

%.h.c:
	$(Q)echo '#include "$*.h"' >$@

%.c %.h %.pc %.ver %.version: TAG = GEN

TESTTOOLS   = audiogen videogen rotozoom tiny_psnr base64
HOSTPROGS  := $(TESTTOOLS:%=tests/%) doc/print_options

# $(FFLIBS-yes) needs to be in linking order
FFLIBS-$(CONFIG_AVDEVICE)   += avdevice
FFLIBS-$(CONFIG_AVFILTER)   += avfilter
FFLIBS-$(CONFIG_AVFORMAT)   += avformat
FFLIBS-$(CONFIG_AVCODEC)    += avcodec
FFLIBS-$(CONFIG_AVRESAMPLE) += avresample
FFLIBS-$(CONFIG_SWSCALE)    += swscale

FFLIBS := avutil

DATA_FILES := $(wildcard $(SRC_PATH)/presets/*.avpreset)

SKIPHEADERS = compat/w32pthreads.h

# first so "all" becomes default target
all: all-yes

include $(SRC_PATH)/tools/Makefile
include $(SRC_PATH)/avbuild/common.mak

FF_EXTRALIBS := $(FFEXTRALIBS)
FF_DEP_LIBS  := $(DEP_LIBS)
FF_STATIC_DEP_LIBS := $(STATIC_DEP_LIBS)

$(TOOLS): %$(EXESUF): %.o
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $^ $(EXTRALIBS-$(*F)) $(EXTRALIBS) $(ELIBS)

CONFIGURABLE_COMPONENTS =                                           \
    $(wildcard $(FFLIBS:%=$(SRC_PATH)/lib%/all*.c))                 \
    $(SRC_PATH)/libavcodec/bitstream_filters.c                      \
    $(SRC_PATH)/libavformat/protocols.c                             \

config.h: avbuild/.config
avbuild/.config: $(CONFIGURABLE_COMPONENTS)
	@-tput bold 2>/dev/null
	@-printf '\nWARNING: $(?) newer than config.h, rerun configure\n\n'
	@-tput sgr0 2>/dev/null

SUBDIR_VARS := CLEANFILES FFLIBS HOSTPROGS TESTPROGS TOOLS               \
               HEADERS ARCH_HEADERS BUILT_HEADERS SKIPHEADERS            \
               ARMV5TE-OBJS ARMV6-OBJS ARMV8-OBJS VFP-OBJS NEON-OBJS     \
               ALTIVEC-OBJS VSX-OBJS MMX-OBJS X86ASM-OBJS                \
               OBJS HOSTOBJS TESTOBJS

define RESET
$(1) :=
$(1)-yes :=
endef

define DOSUBDIR
$(foreach V,$(SUBDIR_VARS),$(eval $(call RESET,$(V))))
SUBDIR := $(1)/
include $(SRC_PATH)/$(1)/Makefile
-include $(SRC_PATH)/$(1)/$(ARCH)/Makefile
-include $(SRC_PATH)/$(1)/$(INTRINSICS)/Makefile
include $(SRC_PATH)/avbuild/library.mak
endef

$(foreach D,$(FFLIBS),$(eval $(call DOSUBDIR,lib$(D))))

include $(SRC_PATH)/avtools/Makefile
include $(SRC_PATH)/doc/Makefile
include $(SRC_PATH)/doc/examples/Makefile

$(PROGS): %$(EXESUF): $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $(OBJS-$*) $(FF_EXTRALIBS)

VERSION_SH  = $(SRC_PATH)/avbuild/version.sh
GIT_LOG     = $(SRC_PATH)/.git/logs/HEAD

.version: $(wildcard $(GIT_LOG)) $(VERSION_SH) avbuild/config.mak
.version: M=@

libavutil/utils.o: avversion.h
avversion.h .version:
	$(M)$(VERSION_SH) $(SRC_PATH) avversion.h $(EXTRA_VERSION)
	$(Q)touch .version

# force version.sh to run whenever version might have changed
-include .version

install: install-libs install-headers

install-libs: install-libs-yes

install-data: $(DATA_FILES)
	$(Q)mkdir -p "$(DATADIR)"
	$(INSTALL) -m 644 $(DATA_FILES) "$(DATADIR)"

uninstall: uninstall-data uninstall-headers uninstall-libs uninstall-pkgconfig

uninstall-data:
	$(RM) -r "$(DATADIR)"

clean::
	$(RM) $(CLEANSUFFIXES)
	$(RM) $(addprefix compat/,$(CLEANSUFFIXES)) $(addprefix compat/*/,$(CLEANSUFFIXES))
	$(RM) -rf coverage.info lcov

distclean: clean
	$(RM) .version avversion.h config.asm config.h mapfile \
            avbuild/.config avbuild/config.* libavutil/avconfig.h \
            libavcodec/bsf_list.c libavformat/protocol_list.c

config:
	$(SRC_PATH)/configure $(value LIBAV_CONFIGURATION)

build: all alltools checkheaders examples testprogs
check: all alltools checkheaders examples testprogs fate

include $(SRC_PATH)/tests/Makefile

$(sort $(OBJDIRS)):
	$(Q)mkdir -p $@

# Dummy rule to stop make trying to rebuild removed or renamed headers
%.h:
	@:

# Disable suffix rules.  Most of the builtin rules are suffix rules,
# so this saves some time on slow systems.
.SUFFIXES:

.PHONY: all all-yes alltools build check config testprogs
.PHONY: *clean install* uninstall*
