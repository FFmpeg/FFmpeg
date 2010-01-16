include config.mak

SRC_DIR = $(SRC_PATH_BARE)

vpath %.texi $(SRC_PATH_BARE)

PROGS-$(CONFIG_FFMPEG)   += ffmpeg
PROGS-$(CONFIG_FFPLAY)   += ffplay
PROGS-$(CONFIG_FFSERVER) += ffserver

PROGS      := $(addsuffix   $(EXESUF), $(PROGS-yes))
PROGS_G     = $(addsuffix _g$(EXESUF), $(PROGS-yes))
OBJS        = $(addsuffix .o,          $(PROGS-yes)) cmdutils.o
MANPAGES    = $(addprefix doc/, $(addsuffix .1, $(PROGS-yes)))
TOOLS       = $(addprefix tools/, $(addsuffix $(EXESUF), cws2fws pktdumper probetest qt-faststart trasher))
HOSTPROGS   = $(addprefix tests/, audiogen videogen rotozoom tiny_psnr)

BASENAMES   = ffmpeg ffplay ffserver
ALLPROGS    = $(addsuffix   $(EXESUF), $(BASENAMES))
ALLPROGS_G  = $(addsuffix _g$(EXESUF), $(BASENAMES))
ALLMANPAGES = $(addsuffix .1, $(BASENAMES))

FFLIBS-$(CONFIG_AVFILTER) += avfilter
FFLIBS-$(CONFIG_POSTPROC) += postproc

FFLIBS := avdevice avformat avcodec avutil swscale

DATA_FILES := $(wildcard $(SRC_DIR)/ffpresets/*.ffpreset)

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
	cp -p $< $@
	$(STRIP) $@

SUBDIR_VARS := OBJS FFLIBS CLEANFILES DIRS TESTPROGS EXAMPLES SKIPHEADERS \
               ALTIVEC-OBJS MMX-OBJS NEON-OBJS X86-OBJS YASM-OBJS-FFT YASM-OBJS \
               HOSTPROGS

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

cmdutils.o cmdutils.d: version.h

alltools: $(TOOLS)

documentation: $(addprefix doc/, developer.html faq.html ffmpeg-doc.html \
                                 ffplay-doc.html ffserver-doc.html       \
                                 general.html libavfilter.html $(ALLMANPAGES))

doc/%.html: doc/%.texi
	texi2html -monolithic -number $<
	mv $(@F) $@

doc/%.pod: doc/%-doc.texi
	doc/texi2pod.pl $< $@

doc/%.1: doc/%.pod
	pod2man --section=1 --center=" " --release=" " $< > $@

install: $(INSTALL_TARGETS-yes)

install-progs: $(PROGS) $(INSTALL_PROGS_TARGETS-yes)
	install -d "$(BINDIR)"
	install -c -m 755 $(PROGS) "$(BINDIR)"

install-data: $(DATA_FILES)
	install -d "$(DATADIR)"
	install -m 644 $(DATA_FILES) "$(DATADIR)"

install-man: $(MANPAGES)
	install -d "$(MANDIR)/man1"
	install -m 644 $(MANPAGES) "$(MANDIR)/man1"

uninstall:
	@echo "I'm sorry, Dave. I'm afraid I can't do that"

testclean:
	rm -rf tests/vsynth1 tests/vsynth2 tests/data tests/*~

clean:: testclean
	rm -f $(ALLPROGS) $(ALLPROGS_G)
	rm -f $(CLEANSUFFIXES)
	rm -f doc/*.html doc/*.pod doc/*.1
	rm -f tests/seek_test$(EXESUF) tests/seek_test.o
	rm -f $(addprefix tests/,$(addsuffix $(HOSTEXESUF),audiogen videogen rotozoom tiny_psnr))
	rm -f $(TOOLS)

distclean::
	rm -f $(DISTCLEANSUFFIXES)
	rm -f version.h config.*

config:
	$(SRC_PATH)/configure $(value FFMPEG_CONFIGURATION)

# regression tests

check: test checkheaders

fulltest test: codectest lavftest seektest

FFSERVER_REFFILE = $(SRC_PATH)/tests/ffserver.regression.ref
SEEK_REFFILE     = $(SRC_PATH)/tests/seek.regression.ref

CODEC_TESTS = $(addprefix regtest-,             \
        mpeg                                    \
        mpeg2                                   \
        mpeg2thread                             \
        msmpeg4v2                               \
        msmpeg4                                 \
        wmv1                                    \
        wmv2                                    \
        h261                                    \
        h263                                    \
        h263p                                   \
        mpeg4                                   \
        huffyuv                                 \
        rc                                      \
        mpeg4adv                                \
        mpeg4thread                             \
        error                                   \
        mpeg4nr                                 \
        mpeg1b                                  \
        mjpeg                                   \
        ljpeg                                   \
        jpegls                                  \
        rv10                                    \
        rv20                                    \
        asv1                                    \
        asv2                                    \
        flv                                     \
        ffv1                                    \
        snow                                    \
        snowll                                  \
        dv                                      \
        dv50                                    \
        dnxhd_1080i                             \
        dnxhd_720p                              \
        dnxhd_720p_rd                           \
        svq1                                    \
        flashsv                                 \
        roq                                     \
        mp2                                     \
        ac3                                     \
        g726                                    \
        adpcm_ima_wav                           \
        adpcm_ima_qt                            \
        adpcm_ms                                \
        adpcm_yam                               \
        adpcm_swf                               \
        alac                                    \
        flac                                    \
        wma                                     \
        pcm                                     \
    )

LAVF_TESTS = $(addprefix regtest-,              \
        avi                                     \
        asf                                     \
        rm                                      \
        mpg                                     \
        mxf                                     \
        ts                                      \
        swf                                     \
        ffm                                     \
        flv_fmt                                 \
        mov                                     \
        dv_fmt                                  \
        gxf                                     \
        nut                                     \
        mkv                                     \
        pbmpipe                                 \
        pgmpipe                                 \
        ppmpipe                                 \
        gif                                     \
        yuv4mpeg                                \
        pgm                                     \
        ppm                                     \
        bmp                                     \
        tga                                     \
        tiff                                    \
        sgi                                     \
        jpg                                     \
        wav                                     \
        alaw                                    \
        mulaw                                   \
        au                                      \
        mmf                                     \
        aiff                                    \
        voc                                     \
        ogg                                     \
        pixfmt                                  \
        pcx                                     \
    )

LAVFI_TESTS = $(addprefix regtest-,             \
    )

ifneq ($(CONFIG_ZLIB),yes)
regtest-flashsv codectest: zlib-error
endif
zlib-error:
	@echo
	@echo "This regression test requires zlib."
	@echo
	@exit 1

codectest: $(CODEC_TESTS)
lavftest:  $(LAVF_TESTS)

# lavfitest: $(LAVFI_TESTS)

$(CODEC_TESTS) $(LAVF_TESTS) $(LAVFI_TESTS): regtest-ref

REFFILE = $(SRC_PATH)/tests/ref/$(1)/$(2:regtest-%=%)
RESFILE = tests/data/$(2:regtest-%=%).$(1).regression

define CODECTEST_CMD
	$(SRC_PATH)/tests/codec-regression.sh $@ vsynth1 tests/vsynth1 a "$(TARGET_EXEC)" "$(TARGET_PATH)"
	$(SRC_PATH)/tests/codec-regression.sh $@ vsynth2 tests/vsynth2 a "$(TARGET_EXEC)" "$(TARGET_PATH)"
endef

regtest-ref: ffmpeg$(EXESUF) tests/vsynth1/00.pgm tests/vsynth2/00.pgm tests/data/asynth1.sw
	$(CODECTEST_CMD)

$(CODEC_TESTS): tests/tiny_psnr$(HOSTEXESUF)
	@echo "TEST CODEC $(@:regtest-%=%)"
	@$(CODECTEST_CMD)
	@diff -u -w $(call REFFILE,vsynth1,$@) $(call RESFILE,vsynth1,$@)
	@diff -u -w $(call REFFILE,vsynth2,$@) $(call RESFILE,vsynth2,$@)

$(LAVF_TESTS):
	@echo "TEST LAVF  $(@:regtest-%=%)"
	@$(SRC_PATH)/tests/lavf-regression.sh $@ lavf tests/vsynth1 b "$(TARGET_EXEC)" "$(TARGET_PATH)"
	@diff -u -w $(call REFFILE,lavf,$@) $(call RESFILE,lavf,$@)

$(LAVFI_TESTS):
	@echo "TEST LAVFI $(@:regtest-%=%)"
	@$(SRC_PATH)/tests/lavfi-regression.sh $@ lavfi tests/vsynth1 b "$(TARGET_EXEC)" "$(TARGET_PATH)"
	@diff -u -w $(call REFFILE,lavfi,$@) $(call RESFILE,lavfi,$@)

seektest: codectest lavftest tests/seek_test$(EXESUF)
	$(SRC_PATH)/tests/seek-regression.sh $(SEEK_REFFILE) "$(TARGET_EXEC)" "$(TARGET_PATH)"

ffservertest: ffserver$(EXESUF) tests/vsynth1/00.pgm tests/data/asynth1.sw
	@echo
	@echo "Unfortunately ffserver is broken and therefore its regression"
	@echo "test fails randomly. Treat the results accordingly."
	@echo
	$(SRC_PATH)/tests/ffserver-regression.sh $(FFSERVER_REFFILE) $(SRC_PATH)/tests/ffserver.conf

tests/vsynth1/00.pgm: tests/videogen$(HOSTEXESUF)
	mkdir -p tests/vsynth1
	$(BUILD_ROOT)/$< 'tests/vsynth1/'

tests/vsynth2/00.pgm: tests/rotozoom$(HOSTEXESUF)
	mkdir -p tests/vsynth2
	$(BUILD_ROOT)/$< 'tests/vsynth2/' $(SRC_PATH)/tests/lena.pnm

tests/data/asynth1.sw: tests/audiogen$(HOSTEXESUF)
	mkdir -p tests/data
	$(BUILD_ROOT)/$< $@

tests/seek_test$(EXESUF): tests/seek_test.o $(FF_DEP_LIBS)
	$(LD) $(FF_LDFLAGS) -o $@ $< $(FF_EXTRALIBS)


.PHONY: documentation *test regtest-* zlib-error alltools check config
