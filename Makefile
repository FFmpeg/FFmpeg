include config.mak

SRC_DIR = $(SRC_PATH_BARE)

vpath %.texi $(SRC_PATH_BARE)

PROGS-$(CONFIG_FFMPEG)   += ffmpeg
PROGS-$(CONFIG_FFPLAY)   += ffplay
PROGS-$(CONFIG_FFSERVER) += ffserver

PROGS       = $(addsuffix   $(EXESUF), $(PROGS-yes))
PROGS_G     = $(addsuffix _g$(EXESUF), $(PROGS-yes))
OBJS        = $(addsuffix .o,          $(PROGS-yes)) cmdutils.o
MANPAGES    = $(addprefix doc/, $(addsuffix .1, $(PROGS-yes)))

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

ALL_TARGETS-$(BUILD_DOC)    += documentation

ifneq ($(PROGS),)
INSTALL_TARGETS-yes             += install-progs install-data
INSTALL_TARGETS-$(BUILD_DOC)    += install-man
endif
INSTALL_PROGS_TARGETS-$(BUILD_SHARED) = install-libs

all: $(FF_DEP_LIBS) $(PROGS) $(ALL_TARGETS-yes)

$(PROGS): %$(EXESUF): %_g$(EXESUF)
	cp -p $< $@
	$(STRIP) $@

SUBDIR_VARS := OBJS FFLIBS CLEANFILES DIRS TESTS

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
	$(CC) $(FF_LDFLAGS) -o $@ $< cmdutils.o $(FF_EXTRALIBS)

output_example$(EXESUF): output_example.o $(FF_DEP_LIBS)
	$(CC) $(CFLAGS) $(FF_LDFLAGS) -o $@ $< $(FF_EXTRALIBS)

tools/%$(EXESUF): tools/%.c
	$(CC) $(CFLAGS) $(FF_LDFLAGS) -o $@ $< $(FF_EXTRALIBS)

ffplay.o ffplay.d: CFLAGS += $(SDL_CFLAGS)

cmdutils.o cmdutils.d: version.h

alltools: $(addsuffix $(EXESUF),$(addprefix tools/, cws2fws pktdumper qt-faststart trasher))

documentation: $(addprefix doc/, ffmpeg-doc.html faq.html ffserver-doc.html \
                                 ffplay-doc.html general.html $(ALLMANPAGES))

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

uninstall: uninstall-progs uninstall-data uninstall-man

uninstall-progs:
	rm -f $(addprefix "$(BINDIR)/", $(ALLPROGS))

uninstall-data:
	rm -rf "$(DATADIR)"

uninstall-man:
	rm -f $(addprefix "$(MANDIR)/man1/",$(ALLMANPAGES))

testclean:
	rm -rf tests/vsynth1 tests/vsynth2 tests/data tests/asynth1.sw tests/*~

clean:: testclean
	rm -f $(ALLPROGS) $(ALLPROGS_G) output_example$(EXESUF)
	rm -f doc/*.html doc/*.pod doc/*.1
	rm -f tests/seek_test$(EXESUF)
	rm -f $(addprefix tests/,$(addsuffix $(HOSTEXESUF),audiogen videogen rotozoom tiny_psnr))
	rm -f $(addprefix tools/,$(addsuffix $(EXESUF),cws2fws pktdumper qt-faststart trasher))

distclean::
	rm -f version.h config.*

# regression tests

check: test checkheaders

fulltest test: codectest libavtest seektest

FFMPEG_REFFILE   = $(SRC_PATH)/tests/ffmpeg.regression.ref
FFSERVER_REFFILE = $(SRC_PATH)/tests/ffserver.regression.ref
LIBAV_REFFILE    = $(SRC_PATH)/tests/libav.regression.ref
ROTOZOOM_REFFILE = $(SRC_PATH)/tests/rotozoom.regression.ref
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
        svq1                                    \
        flashsv                                 \
        mp2                                     \
        ac3                                     \
        g726                                    \
        adpcm_ima_wav                           \
        adpcm_ima_qt                            \
        adpcm_ms                                \
        adpcm_yam                               \
        adpcm_swf                               \
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
    )

REGFILES = $(addprefix tests/data/,$(addsuffix .$(1),$(2:regtest-%=%)))

CODEC_ROTOZOOM = $(call REGFILES,rotozoom.regression,$(CODEC_TESTS))
CODEC_VSYNTH   = $(call REGFILES,vsynth.regression,$(CODEC_TESTS))

LAVF_REGFILES = $(call REGFILES,lavf.regression,$(LAVF_TESTS))

LAVF_REG     = tests/data/lavf.regression
ROTOZOOM_REG = tests/data/rotozoom.regression
VSYNTH_REG   = tests/data/vsynth.regression

ifneq ($(CONFIG_ZLIB),yes)
regtest-flashsv codectest: zlib-error
endif
zlib-error:
	@echo
	@echo "This regression test requires zlib."
	@echo
	@exit 1

codectest: $(VSYNTH_REG) $(ROTOZOOM_REG)
	diff -u -w $(FFMPEG_REFFILE)   $(VSYNTH_REG)
	diff -u -w $(ROTOZOOM_REFFILE) $(ROTOZOOM_REG)

libavtest: $(LAVF_REG)
	diff -u -w $(LIBAV_REFFILE) $(LAVF_REG)

$(VSYNTH_REG) $(ROTOZOOM_REG) $(LAVF_REG):
	cat $^ > $@

$(LAVF_REG):     $(LAVF_REGFILES)
$(ROTOZOOM_REG): $(CODEC_ROTOZOOM)
$(VSYNTH_REG):   $(CODEC_VSYNTH)

$(CODEC_VSYNTH) $(CODEC_ROTOZOOM): $(CODEC_TESTS)

$(LAVF_REGFILES): $(LAVF_TESTS)

$(CODEC_TESTS) $(LAVF_TESTS): regtest-ref

regtest-ref: ffmpeg$(EXESUF) tests/vsynth1/00.pgm tests/vsynth2/00.pgm tests/asynth1.sw

$(CODEC_TESTS) regtest-ref: tests/tiny_psnr$(HOSTEXESUF)
	$(SRC_PATH)/tests/regression.sh $@ vsynth   tests/vsynth1 a "$(TARGET_EXEC)" "$(TARGET_PATH)"
	$(SRC_PATH)/tests/regression.sh $@ rotozoom tests/vsynth2 a "$(TARGET_EXEC)" "$(TARGET_PATH)"

$(LAVF_TESTS):
	$(SRC_PATH)/tests/regression.sh $@ lavf tests/vsynth1 b "$(TARGET_EXEC)" "$(TARGET_PATH)"

seektest: codectest libavtest tests/seek_test$(EXESUF)
	$(SRC_PATH)/tests/seek_test.sh $(SEEK_REFFILE) "$(TARGET_EXEC)" "$(TARGET_PATH)"

servertest: ffserver$(EXESUF) tests/vsynth1/00.pgm tests/asynth1.sw
	@echo
	@echo "Unfortunately ffserver is broken and therefore its regression"
	@echo "test fails randomly. Treat the results accordingly."
	@echo
	$(SRC_PATH)/tests/server-regression.sh $(FFSERVER_REFFILE) $(SRC_PATH)/tests/test.conf

tests/vsynth1/00.pgm: tests/videogen$(HOSTEXESUF)
	mkdir -p tests/vsynth1
	$(BUILD_ROOT)/$< 'tests/vsynth1/'

tests/vsynth2/00.pgm: tests/rotozoom$(HOSTEXESUF)
	mkdir -p tests/vsynth2
	$(BUILD_ROOT)/$< 'tests/vsynth2/' $(SRC_PATH)/tests/lena.pnm

tests/asynth1.sw: tests/audiogen$(HOSTEXESUF)
	$(BUILD_ROOT)/$< $@

tests/%$(HOSTEXESUF): tests/%.c
	$(HOSTCC) $(HOSTCFLAGS) $(HOSTLDFLAGS) -o $@ $< $(HOSTLIBS)

tests/seek_test$(EXESUF): tests/seek_test.c $(FF_DEP_LIBS)
	$(CC) $(FF_LDFLAGS) $(CFLAGS) -o $@ $< $(FF_EXTRALIBS)


.PHONY: documentation *test regtest-* zlib-error alltools check
