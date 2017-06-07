MAIN_MAKEFILE=1
include ffbuild/config.mak

vpath %.c    $(SRC_PATH)
vpath %.cpp  $(SRC_PATH)
vpath %.h    $(SRC_PATH)
vpath %.inc  $(SRC_PATH)
vpath %.m    $(SRC_PATH)
vpath %.S    $(SRC_PATH)
vpath %.asm  $(SRC_PATH)
vpath %.rc   $(SRC_PATH)
vpath %.v    $(SRC_PATH)
vpath %.texi $(SRC_PATH)
vpath %.cu   $(SRC_PATH)
vpath %.ptx  $(SRC_PATH)
vpath %/fate_config.sh.template $(SRC_PATH)

AVPROGS-$(CONFIG_FFMPEG)   += ffmpeg
AVPROGS-$(CONFIG_FFPLAY)   += ffplay
AVPROGS-$(CONFIG_FFPROBE)  += ffprobe
AVPROGS-$(CONFIG_FFSERVER) += ffserver

AVPROGS    := $(AVPROGS-yes:%=%$(PROGSSUF)$(EXESUF))
INSTPROGS   = $(AVPROGS-yes:%=%$(PROGSSUF)$(EXESUF))
PROGS      += $(AVPROGS)

AVBASENAMES  = ffmpeg ffplay ffprobe ffserver
ALLAVPROGS   = $(AVBASENAMES:%=%$(PROGSSUF)$(EXESUF))
ALLAVPROGS_G = $(AVBASENAMES:%=%$(PROGSSUF)_g$(EXESUF))

$(foreach prog,$(AVBASENAMES),$(eval OBJS-$(prog) += cmdutils.o))
$(foreach prog,$(AVBASENAMES),$(eval OBJS-$(prog)-$(CONFIG_OPENCL) += cmdutils_opencl.o))

OBJS-ffmpeg                   += ffmpeg_opt.o ffmpeg_filter.o ffmpeg_hw.o
OBJS-ffmpeg-$(CONFIG_VIDEOTOOLBOX) += ffmpeg_videotoolbox.o
OBJS-ffmpeg-$(CONFIG_LIBMFX)  += ffmpeg_qsv.o
ifndef CONFIG_VIDEOTOOLBOX
OBJS-ffmpeg-$(CONFIG_VDA)     += ffmpeg_videotoolbox.o
endif
OBJS-ffmpeg-$(CONFIG_CUVID)   += ffmpeg_cuvid.o
OBJS-ffserver                 += ffserver_config.o

TESTTOOLS   = audiogen videogen rotozoom tiny_psnr tiny_ssim base64 audiomatch
HOSTPROGS  := $(TESTTOOLS:%=tests/%) doc/print_options

# $(FFLIBS-yes) needs to be in linking order
FFLIBS-$(CONFIG_AVDEVICE)   += avdevice
FFLIBS-$(CONFIG_AVFILTER)   += avfilter
FFLIBS-$(CONFIG_AVFORMAT)   += avformat
FFLIBS-$(CONFIG_AVCODEC)    += avcodec
FFLIBS-$(CONFIG_AVRESAMPLE) += avresample
FFLIBS-$(CONFIG_POSTPROC)   += postproc
FFLIBS-$(CONFIG_SWRESAMPLE) += swresample
FFLIBS-$(CONFIG_SWSCALE)    += swscale

FFLIBS := avutil

DATA_FILES := $(wildcard $(SRC_PATH)/presets/*.ffpreset) $(SRC_PATH)/doc/ffprobe.xsd
EXAMPLES_FILES := $(wildcard $(SRC_PATH)/doc/examples/*.c) $(SRC_PATH)/doc/examples/Makefile $(SRC_PATH)/doc/examples/README

SKIPHEADERS = compat/w32pthreads.h

# first so "all" becomes default target
all: all-yes

include $(SRC_PATH)/tools/Makefile
include $(SRC_PATH)/ffbuild/common.mak

FF_EXTRALIBS := $(FFEXTRALIBS)
FF_DEP_LIBS  := $(DEP_LIBS)
FF_STATIC_DEP_LIBS := $(STATIC_DEP_LIBS)

all: $(AVPROGS)

$(TOOLS): %$(EXESUF): %.o
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $^ $(ELIBS)

target_dec_%_fuzzer$(EXESUF): target_dec_%_fuzzer.o $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $^ $(ELIBS) $(FF_EXTRALIBS) $(LIBFUZZER_PATH)

tools/cws2fws$(EXESUF): ELIBS = $(ZLIB)
tools/sofa2wavs$(EXESUF): ELIBS = $(FF_EXTRALIBS)
tools/uncoded_frame$(EXESUF): $(FF_DEP_LIBS)
tools/uncoded_frame$(EXESUF): ELIBS = $(FF_EXTRALIBS)
tools/target_dec_%_fuzzer$(EXESUF): $(FF_DEP_LIBS)

CONFIGURABLE_COMPONENTS =                                           \
    $(wildcard $(FFLIBS:%=$(SRC_PATH)/lib%/all*.c))                 \
    $(SRC_PATH)/libavcodec/bitstream_filters.c                      \
    $(SRC_PATH)/libavformat/protocols.c                             \

config.h: ffbuild/.config
ffbuild/.config: $(CONFIGURABLE_COMPONENTS)
	@-tput bold 2>/dev/null
	@-printf '\nWARNING: $(?) newer than config.h, rerun configure\n\n'
	@-tput sgr0 2>/dev/null

SUBDIR_VARS := CLEANFILES EXAMPLES FFLIBS HOSTPROGS TESTPROGS TOOLS      \
               HEADERS ARCH_HEADERS BUILT_HEADERS SKIPHEADERS            \
               ARMV5TE-OBJS ARMV6-OBJS ARMV8-OBJS VFP-OBJS NEON-OBJS     \
               ALTIVEC-OBJS VSX-OBJS MMX-OBJS X86ASM-OBJS                \
               MIPSFPU-OBJS MIPSDSPR2-OBJS MIPSDSP-OBJS MSA-OBJS         \
               MMI-OBJS OBJS SLIBOBJS HOSTOBJS TESTOBJS

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
include $(SRC_PATH)/ffbuild/library.mak
endef

$(foreach D,$(FFLIBS),$(eval $(call DOSUBDIR,lib$(D))))

include $(SRC_PATH)/doc/Makefile

define DOPROG
OBJS-$(1) += $(1).o $(OBJS-$(1)-yes)
$(1)$(PROGSSUF)_g$(EXESUF): $$(OBJS-$(1))
$$(OBJS-$(1)): CFLAGS  += $(CFLAGS-$(1))
$(1)$(PROGSSUF)_g$(EXESUF): LDFLAGS += $(LDFLAGS-$(1))
$(1)$(PROGSSUF)_g$(EXESUF): FF_EXTRALIBS += $(EXTRALIBS-$(1))
-include $$(OBJS-$(1):.o=.d)
endef

$(foreach P,$(PROGS),$(eval $(call DOPROG,$(P:$(PROGSSUF)$(EXESUF)=))))

ffprobe.o cmdutils.o libavcodec/utils.o libavformat/utils.o libavdevice/avdevice.o libavfilter/avfilter.o libavutil/utils.o libpostproc/postprocess.o libswresample/swresample.o libswscale/utils.o : libavutil/ffversion.h

$(PROGS): %$(PROGSSUF)$(EXESUF): %$(PROGSSUF)_g$(EXESUF)
	$(CP) $< $@
	$(STRIP) $@

%$(PROGSSUF)_g$(EXESUF): %.o $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $(OBJS-$*) $(FF_EXTRALIBS)

VERSION_SH  = $(SRC_PATH)/ffbuild/version.sh
GIT_LOG     = $(SRC_PATH)/.git/logs/HEAD

.version: $(wildcard $(GIT_LOG)) $(VERSION_SH) ffbuild/config.mak
.version: M=@

libavutil/ffversion.h .version:
	$(M)$(VERSION_SH) $(SRC_PATH) libavutil/ffversion.h $(EXTRA_VERSION)
	$(Q)touch .version

# force version.sh to run whenever version might have changed
-include .version

ifdef AVPROGS
install: install-progs install-data
endif

install: install-libs install-headers

install-libs: install-libs-yes

install-progs-yes:
install-progs-$(CONFIG_SHARED): install-libs

install-progs: install-progs-yes $(AVPROGS)
	$(Q)mkdir -p "$(BINDIR)"
	$(INSTALL) -c -m 755 $(INSTPROGS) "$(BINDIR)"

install-data: $(DATA_FILES) $(EXAMPLES_FILES)
	$(Q)mkdir -p "$(DATADIR)/examples"
	$(INSTALL) -m 644 $(DATA_FILES) "$(DATADIR)"
	$(INSTALL) -m 644 $(EXAMPLES_FILES) "$(DATADIR)/examples"

uninstall: uninstall-libs uninstall-headers uninstall-progs uninstall-data

uninstall-progs:
	$(RM) $(addprefix "$(BINDIR)/", $(ALLAVPROGS))

uninstall-data:
	$(RM) -r "$(DATADIR)"

clean::
	$(RM) $(ALLAVPROGS) $(ALLAVPROGS_G)
	$(RM) $(CLEANSUFFIXES)
	$(RM) $(CLEANSUFFIXES:%=compat/msvcrt/%)
	$(RM) $(CLEANSUFFIXES:%=compat/atomics/pthread/%)
	$(RM) $(CLEANSUFFIXES:%=compat/%)
	$(RM) -r coverage-html
	$(RM) -rf coverage.info coverage.info.in lcov

distclean::
	$(RM) $(DISTCLEANSUFFIXES)
	$(RM) .version avversion.h config.asm config.h mapfile  \
		ffbuild/.config ffbuild/config.* libavutil/avconfig.h \
		version.h libavutil/ffversion.h libavcodec/codec_names.h \
		libavcodec/bsf_list.c libavformat/protocol_list.c
ifeq ($(SRC_LINK),src)
	$(RM) src
endif
	$(RM) -rf doc/examples/pc-uninstalled

config:
	$(SRC_PATH)/configure $(value FFMPEG_CONFIGURATION)

check: all alltools examples testprogs fate

include $(SRC_PATH)/tests/Makefile

$(sort $(OBJDIRS)):
	$(Q)mkdir -p $@

# Dummy rule to stop make trying to rebuild removed or renamed headers
%.h:
	@:

# Disable suffix rules.  Most of the builtin rules are suffix rules,
# so this saves some time on slow systems.
.SUFFIXES:

.PHONY: all all-yes alltools check *clean config install*
.PHONY: testprogs uninstall*
