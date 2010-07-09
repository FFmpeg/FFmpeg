include config.mak

SRC_DIR = $(SRC_PATH_BARE)

vpath %.texi $(SRC_PATH_BARE)

PROGS-$(CONFIG_FFMPEG)   += ffmpeg
PROGS-$(CONFIG_FFPLAY)   += ffplay
PROGS-$(CONFIG_FFPROBE)  += ffprobe
PROGS-$(CONFIG_FFSERVER) += ffserver

PROGS      := $(addsuffix   $(EXESUF), $(PROGS-yes))
PROGS_G     = $(addsuffix _g$(EXESUF), $(PROGS-yes))
OBJS        = $(addsuffix .o,          $(PROGS-yes)) cmdutils.o
MANPAGES    = $(addprefix doc/, $(addsuffix .1, $(PROGS-yes)))
HTMLPAGES   = $(addprefix doc/, $(addsuffix -doc.html, $(PROGS-yes)))
TOOLS       = $(addprefix tools/, $(addsuffix $(EXESUF), cws2fws pktdumper probetest qt-faststart trasher))
HOSTPROGS   = $(addprefix tests/, audiogen videogen rotozoom tiny_psnr)

BASENAMES   = ffmpeg ffplay ffprobe ffserver
ALLPROGS    = $(addsuffix   $(EXESUF), $(BASENAMES))
ALLPROGS_G  = $(addsuffix _g$(EXESUF), $(BASENAMES))
ALLMANPAGES = $(addsuffix .1, $(BASENAMES))
ALLHTMLPAGES= $(addsuffix -doc.html, $(BASENAMES))

FFLIBS-$(CONFIG_AVDEVICE) += avdevice
FFLIBS-$(CONFIG_AVFILTER) += avfilter
FFLIBS-$(CONFIG_AVFORMAT) += avformat
FFLIBS-$(CONFIG_AVCODEC)  += avcodec
FFLIBS-$(CONFIG_POSTPROC) += postproc
FFLIBS-$(CONFIG_SWSCALE)  += swscale

FFLIBS := avutil

DATA_FILES := $(wildcard $(SRC_DIR)/ffpresets/*.ffpreset)

SKIPHEADERS = cmdutils_common_opts.h

include common.mak

FF_LDFLAGS   := $(FFLDFLAGS)
FF_EXTRALIBS := $(FFEXTRALIBS)
FF_DEP_LIBS  := $(DEP_LIBS)

ALL_TARGETS-$(CONFIG_DOC)       += documentation

ifdef PROGS
INSTALL_TARGETS-yes             += install-progs install-data
INSTALL_TARGETS-$(CONFIG_DOC)   += install-man
endif
INSTALL_PROGS_TARGETS-$(CONFIG_SHARED) = install-libs

all: $(FF_DEP_LIBS) $(PROGS) $(ALL_TARGETS-yes)

$(PROGS): %$(EXESUF): %_g$(EXESUF)
	$(CP) $< $@
	$(STRIP) $@

SUBDIR_VARS := OBJS FFLIBS CLEANFILES DIRS TESTPROGS EXAMPLES SKIPHEADERS \
               ALTIVEC-OBJS MMX-OBJS NEON-OBJS X86-OBJS YASM-OBJS-FFT YASM-OBJS \
               HOSTPROGS BUILT_HEADERS TESTOBJS ARCH_HEADERS

define RESET
$(1) :=
$(1)-yes :=
endef

define DOSUBDIR
$(foreach V,$(SUBDIR_VARS),$(eval $(call RESET,$(V))))
SUBDIR := $(1)/
include $(1)/Makefile
endef

$(foreach D,$(FFLIBS),$(eval $(call DOSUBDIR,lib$(D))))

ffplay_g$(EXESUF): FF_EXTRALIBS += $(SDL_LIBS)
ffserver_g$(EXESUF): FF_LDFLAGS += $(FFSERVERLDFLAGS)

%_g$(EXESUF): %.o cmdutils.o $(FF_DEP_LIBS)
	$(LD) $(FF_LDFLAGS) -o $@ $< cmdutils.o $(FF_EXTRALIBS)

tools/%$(EXESUF): tools/%.o
	$(LD) $(FF_LDFLAGS) -o $@ $< $(FF_EXTRALIBS)

tools/%.o: tools/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CC_O) $<

ffplay.o ffplay.d: CFLAGS += $(SDL_CFLAGS)

VERSION_SH  = $(SRC_PATH_BARE)/version.sh
GIT_LOG     = $(SRC_PATH_BARE)/.git/logs/HEAD
SVN_ENTRIES = $(SRC_PATH_BARE)/.svn/entries

.version: $(wildcard $(GIT_LOG) $(SVN_ENTRIES)) $(VERSION_SH) config.mak
.version: M=@

version.h .version:
	$(M)$(VERSION_SH) $(SRC_PATH) version.h $(EXTRA_VERSION)
	$(Q)touch .version

# force version.sh to run whenever version might have changed
-include .version

alltools: $(TOOLS)

documentation: $(addprefix doc/, developer.html faq.html general.html libavfilter.html \
                                 $(ALLHTMLPAGES) $(ALLMANPAGES))

$(HTMLPAGES) $(MANPAGES): doc/fftools-common-opts.texi

doc/ffmpeg.pod doc/ffmpeg-doc.html: doc/filters.texi
doc/ffplay.pod doc/ffplay-doc.html: doc/filters.texi

doc/%.html: TAG = HTML
doc/%.html: doc/%.texi
	$(M)cd doc && texi2html -monolithic -number $(<:doc/%=%)

doc/%.pod: TAG = POD
doc/%.pod: doc/%-doc.texi
	$(M)doc/texi2pod.pl $< $@

doc/%.1: TAG = MAN
doc/%.1: doc/%.pod
	$(M)pod2man --section=1 --center=" " --release=" " $< > $@

install: $(INSTALL_TARGETS-yes)

install-progs: $(PROGS) $(INSTALL_PROGS_TARGETS-yes)
	$(Q)mkdir -p "$(BINDIR)"
	$(INSTALL) -c -m 755 $(PROGS) "$(BINDIR)"

install-data: $(DATA_FILES)
	$(Q)mkdir -p "$(DATADIR)"
	$(INSTALL) -m 644 $(DATA_FILES) "$(DATADIR)"

install-man: $(MANPAGES)
	$(Q)mkdir -p "$(MANDIR)/man1"
	$(INSTALL) -m 644 $(MANPAGES) "$(MANDIR)/man1"

uninstall: uninstall-progs uninstall-data uninstall-man

uninstall-progs:
	$(RM) $(addprefix "$(BINDIR)/", $(ALLPROGS))

uninstall-data:
	$(RM) -r "$(DATADIR)"

uninstall-man:
	$(RM) $(addprefix "$(MANDIR)/man1/",$(ALLMANPAGES))

testclean:
	$(RM) -r tests/vsynth1 tests/vsynth2 tests/data
	$(RM) $(addprefix tests/,$(CLEANSUFFIXES))
	$(RM) tests/seek_test$(EXESUF) tests/seek_test.o
	$(RM) $(addprefix tests/,$(addsuffix $(HOSTEXESUF),audiogen videogen rotozoom tiny_psnr))

clean:: testclean
	$(RM) $(ALLPROGS) $(ALLPROGS_G)
	$(RM) $(CLEANSUFFIXES)
	$(RM) doc/*.html doc/*.pod doc/*.1
	$(RM) $(TOOLS)

distclean::
	$(RM) $(DISTCLEANSUFFIXES)
	$(RM) version.h config.* libavutil/avconfig.h

config:
	$(SRC_PATH)/configure $(value FFMPEG_CONFIGURATION)

# regression tests

check: test checkheaders

fulltest test: codectest lavftest seektest

FFSERVER_REFFILE = $(SRC_PATH)/tests/ffserver.regression.ref
SEEK_REFFILE     = $(SRC_PATH)/tests/seek.regression.ref

LAVFI_TESTS =           \
    crop                \
    crop_scale          \
    crop_scale_vflip    \
    crop_vflip          \
    null                \
    scale200            \
    scale500            \
    vflip               \
    vflip_crop          \
    vflip_vflip         \
    lavfi_pixdesc       \
#   lavfi_pix_fmts      \

ACODEC_TESTS := $(addprefix regtest-, $(ACODEC_TESTS) $(ACODEC_TESTS-yes))
VCODEC_TESTS := $(addprefix regtest-, $(VCODEC_TESTS) $(VCODEC_TESTS-yes))
LAVF_TESTS  := $(addprefix regtest-, $(LAVF_TESTS)  $(LAVF_TESTS-yes))
LAVFI_TESTS := $(addprefix regtest-, $(LAVFI_TESTS) $(LAVFI_TESTS-yes))

CODEC_TESTS = $(VCODEC_TESTS) $(ACODEC_TESTS)

codectest: $(CODEC_TESTS)
lavftest:  $(LAVF_TESTS)
lavfitest: $(LAVFI_TESTS)

AREF = tests/data/acodec.ref.wav
VREF = tests/data/vsynth1.ref.yuv
REFS = $(AREF) $(VREF)

$(ACODEC_TESTS): $(AREF)
$(VCODEC_TESTS): $(VREF)
$(LAVF_TESTS) $(LAVFI_TESTS): $(REFS)

REFFILE = $(SRC_PATH)/tests/ref/$(1)/$(2:regtest-%=%)
RESFILE = tests/data/regression/$(1)/$(2:regtest-%=%)

define VCODECTEST
	@echo "TEST VCODEC $(1:regtest-%=%)"
	$(SRC_PATH)/tests/codec-regression.sh $(1) vsynth1 tests/vsynth1 "$(TARGET_EXEC)" "$(TARGET_PATH)"
	$(SRC_PATH)/tests/codec-regression.sh $(1) vsynth2 tests/vsynth2 "$(TARGET_EXEC)" "$(TARGET_PATH)"
endef

define ACODECTEST
	@echo "TEST ACODEC $(1:regtest-%=%)"
	$(SRC_PATH)/tests/codec-regression.sh $(1) acodec tests/acodec "$(TARGET_EXEC)" "$(TARGET_PATH)"
endef

$(VREF): ffmpeg$(EXESUF) tests/vsynth1/00.pgm tests/vsynth2/00.pgm
	@$(call VCODECTEST,vref)

$(AREF): ffmpeg$(EXESUF) tests/data/asynth1.sw
	@$(call ACODECTEST,aref)

$(VCODEC_TESTS): tests/tiny_psnr$(HOSTEXESUF)
	@$(call VCODECTEST,$@)
	@diff -u -w $(call REFFILE,vsynth1,$@) $(call RESFILE,vsynth1,$@)
	@diff -u -w $(call REFFILE,vsynth2,$@) $(call RESFILE,vsynth2,$@)

$(ACODEC_TESTS): tests/tiny_psnr$(HOSTEXESUF)
	@$(call ACODECTEST,$@)
	@diff -u -w $(call REFFILE,acodec,$@) $(call RESFILE,acodec,$@)

$(LAVF_TESTS):
	@echo "TEST LAVF  $(@:regtest-%=%)"
	@$(SRC_PATH)/tests/lavf-regression.sh $@ lavf tests/vsynth1 "$(TARGET_EXEC)" "$(TARGET_PATH)"
	@diff -u -w $(call REFFILE,lavf,$@) $(call RESFILE,lavf,$@)

$(LAVFI_TESTS): tools/lavfi-showfiltfmts$(EXESUF)
	@echo "TEST LAVFI $(@:regtest-%=%)"
	@$(SRC_PATH)/tests/lavfi-regression.sh $@ lavfi tests/vsynth1 "$(TARGET_EXEC)" "$(TARGET_PATH)"
	@diff -u -w $(call REFFILE,lavfi,$@) $(call RESFILE,lavfi,$@)

seektest: codectest lavftest tests/seek_test$(EXESUF)
	$(SRC_PATH)/tests/seek-regression.sh $(SRC_PATH) "$(TARGET_EXEC)" "$(TARGET_PATH)"

ffservertest: ffserver$(EXESUF) tests/vsynth1/00.pgm tests/data/asynth1.sw
	@echo
	@echo "Unfortunately ffserver is broken and therefore its regression"
	@echo "test fails randomly. Treat the results accordingly."
	@echo
	$(SRC_PATH)/tests/ffserver-regression.sh $(FFSERVER_REFFILE) $(SRC_PATH)/tests/ffserver.conf

tests/vsynth1/00.pgm: tests/videogen$(HOSTEXESUF)
	@mkdir -p tests/vsynth1
	$(M)$(BUILD_ROOT)/$< 'tests/vsynth1/'

tests/vsynth2/00.pgm: tests/rotozoom$(HOSTEXESUF)
	@mkdir -p tests/vsynth2
	$(M)$(BUILD_ROOT)/$< 'tests/vsynth2/' $(SRC_PATH)/tests/lena.pnm

tests/data/asynth1.sw: tests/audiogen$(HOSTEXESUF)
	@mkdir -p tests/data
	$(M)$(BUILD_ROOT)/$< $@

tests/data/asynth1.sw tests/vsynth%/00.pgm: TAG = GEN

tests/seek_test$(EXESUF): tests/seek_test.o $(FF_DEP_LIBS)
	$(LD) $(FF_LDFLAGS) -o $@ $< $(FF_EXTRALIBS)

include $(SRC_PATH_BARE)/tests/fate.mak
include $(SRC_PATH_BARE)/tests/fate2.mak

FATE_TESTS += $(FATE2_TESTS)

ifdef SAMPLES
fate: $(FATE_TESTS)
fate2: $(FATE2_TESTS)
$(FATE_TESTS): ffmpeg$(EXESUF) tests/tiny_psnr$(HOSTEXESUF)
	@echo "TEST FATE   $(@:fate-%=%)"
	@$(SRC_PATH)/tests/fate-run.sh $@ "$(SAMPLES)" "$(TARGET_EXEC)" "$(TARGET_PATH)" '$(CMD)' '$(CMP)' '$(REF)' '$(FUZZ)'
else
fate fate2 $(FATE_TESTS):
	@echo "SAMPLES not specified, cannot run FATE"
endif

.PHONY: documentation *test regtest-* alltools check config
