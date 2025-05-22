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
vpath %.metal $(SRC_PATH)
vpath %/fate_config.sh.template $(SRC_PATH)

TESTTOOLS   = audiogen videogen rotozoom tiny_psnr tiny_ssim base64 audiomatch
HOSTPROGS  := $(TESTTOOLS:%=tests/%) doc/print_options

ALLFFLIBS =            \
    avcodec            \
    avdevice           \
    avfilter           \
    avformat           \
    avutil             \
    swscale            \
    swresample         \

# $(FFLIBS-yes) needs to be in linking order
FFLIBS-$(CONFIG_AVDEVICE)   += avdevice
FFLIBS-$(CONFIG_AVFILTER)   += avfilter
FFLIBS-$(CONFIG_AVFORMAT)   += avformat
FFLIBS-$(CONFIG_AVCODEC)    += avcodec
FFLIBS-$(CONFIG_SWRESAMPLE) += swresample
FFLIBS-$(CONFIG_SWSCALE)    += swscale

FFLIBS := avutil

DATA_FILES := $(wildcard $(SRC_PATH)/presets/*.ffpreset) $(SRC_PATH)/doc/ffprobe.xsd

SKIPHEADERS = compat/w32pthreads.h

# first so "all" becomes default target
all: all-yes

include $(SRC_PATH)/tools/Makefile
include $(SRC_PATH)/ffbuild/common.mak

FF_EXTRALIBS := $(FFEXTRALIBS)
FF_DEP_LIBS  := $(DEP_LIBS)
FF_STATIC_DEP_LIBS := $(STATIC_DEP_LIBS)

$(TOOLS): %$(EXESUF): %.o
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $(filter-out $(FF_DEP_LIBS), $^) $(EXTRALIBS-$(*F)) $(EXTRALIBS) $(ELIBS)

target_dec_%_fuzzer$(EXESUF): target_dec_%_fuzzer.o $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $^ $(ELIBS) $(FF_EXTRALIBS) $(LIBFUZZER_PATH)

target_enc_%_fuzzer$(EXESUF): target_enc_%_fuzzer.o $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $^ $(ELIBS) $(FF_EXTRALIBS) $(LIBFUZZER_PATH)

tools/target_bsf_%_fuzzer$(EXESUF): tools/target_bsf_%_fuzzer.o $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $^ $(ELIBS) $(FF_EXTRALIBS) $(LIBFUZZER_PATH)

target_dem_%_fuzzer$(EXESUF): target_dem_%_fuzzer.o $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $^ $(ELIBS) $(FF_EXTRALIBS) $(LIBFUZZER_PATH)

tools/target_dem_fuzzer$(EXESUF): tools/target_dem_fuzzer.o $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $^ $(ELIBS) $(FF_EXTRALIBS) $(LIBFUZZER_PATH)

tools/target_io_dem_fuzzer$(EXESUF): tools/target_io_dem_fuzzer.o $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $^ $(ELIBS) $(FF_EXTRALIBS) $(LIBFUZZER_PATH)

tools/target_sws_fuzzer$(EXESUF): tools/target_sws_fuzzer.o $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $^ $(ELIBS) $(FF_EXTRALIBS) $(LIBFUZZER_PATH)

tools/target_swr_fuzzer$(EXESUF): tools/target_swr_fuzzer.o $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $^ $(ELIBS) $(FF_EXTRALIBS) $(LIBFUZZER_PATH)

tools/enum_options$(EXESUF): ELIBS = $(FF_EXTRALIBS)
tools/enum_options$(EXESUF): $(FF_DEP_LIBS)
tools/enc_recon_frame_test$(EXESUF): $(FF_DEP_LIBS)
tools/enc_recon_frame_test$(EXESUF): ELIBS = $(FF_EXTRALIBS)
tools/scale_slice_test$(EXESUF): $(FF_DEP_LIBS)
tools/scale_slice_test$(EXESUF): ELIBS = $(FF_EXTRALIBS)
tools/sofa2wavs$(EXESUF): ELIBS = $(FF_EXTRALIBS)
tools/uncoded_frame$(EXESUF): $(FF_DEP_LIBS)
tools/uncoded_frame$(EXESUF): ELIBS = $(FF_EXTRALIBS)
tools/target_dec_%_fuzzer$(EXESUF): $(FF_DEP_LIBS)
tools/target_dem_%_fuzzer$(EXESUF): $(FF_DEP_LIBS)

CONFIGURABLE_COMPONENTS =                                           \
    $(wildcard $(FFLIBS:%=$(SRC_PATH)/lib%/all*.c))                 \
    $(SRC_PATH)/libavcodec/bitstream_filters.c                      \
    $(SRC_PATH)/libavcodec/hwaccels.h                               \
    $(SRC_PATH)/libavcodec/parsers.c                                \
    $(SRC_PATH)/libavformat/protocols.c                             \

config_components.h: ffbuild/.config
ffbuild/.config: $(CONFIGURABLE_COMPONENTS)
	@-tput bold 2>/dev/null
	@-printf '\nWARNING: $(?) newer than config_components.h, rerun configure\n\n'
	@-tput sgr0 2>/dev/null

SUBDIR_VARS := CLEANFILES FFLIBS HOSTPROGS TESTPROGS TOOLS               \
               HEADERS ARCH_HEADERS BUILT_HEADERS SKIPHEADERS            \
               ARMV5TE-OBJS ARMV6-OBJS ARMV8-OBJS VFP-OBJS NEON-OBJS     \
               ALTIVEC-OBJS VSX-OBJS MMX-OBJS X86ASM-OBJS                \
               MIPSFPU-OBJS MIPSDSPR2-OBJS MIPSDSP-OBJS MSA-OBJS         \
               MMI-OBJS LSX-OBJS LASX-OBJS RV-OBJS RVV-OBJS RVVB-OBJS    \
               OBJS SHLIBOBJS STLIBOBJS HOSTOBJS TESTOBJS SIMD128-OBJS

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

include $(SRC_PATH)/fftools/Makefile
include $(SRC_PATH)/doc/Makefile
include $(SRC_PATH)/doc/examples/Makefile

$(ALLFFLIBS:%=lib%/version.o): libavutil/ffversion.h

$(PROGS): %$(PROGSSUF)$(EXESUF): %$(PROGSSUF)_g$(EXESUF)
ifeq ($(STRIPTYPE),direct)
	$(STRIP) -o $@ $<
else
	$(RM) $@
	$(CP) $< $@
	$(STRIP) $@
endif

%$(PROGSSUF)_g$(EXESUF): $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $(OBJS-$*) $(FF_EXTRALIBS)

VERSION_SH  = $(SRC_PATH)/ffbuild/version.sh
ifeq ($(VERSION_TRACKING),yes)
GIT_LOG     = $(SRC_PATH)/.git/logs/HEAD
endif

.version: $(wildcard $(GIT_LOG)) $(VERSION_SH) ffbuild/config.mak
.version: M=@

ifneq ($(VERSION_TRACKING),yes)
libavutil/ffversion.h .version: REVISION=unknown
endif
libavutil/ffversion.h .version:
	$(M)revision=$(REVISION) $(VERSION_SH) $(SRC_PATH) libavutil/ffversion.h $(EXTRA_VERSION)
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
	$(RM) $(addprefix compat/,$(CLEANSUFFIXES)) $(addprefix compat/*/,$(CLEANSUFFIXES)) $(addprefix compat/*/*/,$(CLEANSUFFIXES))
	$(RM) -r coverage-html
	$(RM) -rf coverage.info coverage.info.in lcov

distclean:: clean
	$(RM) .version config.asm config.h config_components.h mapfile  \
		ffbuild/.config ffbuild/config.* libavutil/avconfig.h \
		version.h libavutil/ffversion.h libavcodec/codec_names.h \
		libavcodec/bsf_list.c libavformat/protocol_list.c \
		libavcodec/codec_list.c libavcodec/parser_list.c \
		libavfilter/filter_list.c libavdevice/indev_list.c libavdevice/outdev_list.c \
		libavformat/muxer_list.c libavformat/demuxer_list.c
ifeq ($(SRC_LINK),src)
	$(RM) src
endif
	$(RM) -rf doc/examples/pc-uninstalled

config:
	$(SRC_PATH)/configure $(value FFMPEG_CONFIGURATION)

build: all alltools examples testprogs
check: all alltools examples testprogs fate

include $(SRC_PATH)/tests/Makefile

$(sort $(OUTDIRS)):
	$(Q)mkdir -p $@

# Dummy rule to stop make trying to rebuild removed or renamed headers
%.h:
	@:

# Disable suffix rules.  Most of the builtin rules are suffix rules,
# so this saves some time on slow systems.
.SUFFIXES:

.PHONY: all all-yes alltools build check config testprogs
.PHONY: *clean install* uninstall*
