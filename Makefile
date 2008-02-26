#
# Main ffmpeg Makefile
# (c) 2000-2004 Fabrice Bellard
#
include config.mak

VPATH=$(SRC_PATH_BARE)

CFLAGS=$(OPTFLAGS) -I$(BUILD_ROOT) -I$(SRC_PATH) -I$(SRC_PATH)/libavutil \
       -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libavformat -I$(SRC_PATH)/libswscale \
       -I$(SRC_PATH)/libavdevice -I$(SRC_PATH)/libavfilter \
       -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_ISOC9X_SOURCE -DHAVE_AV_CONFIG_H
LDFLAGS+= -g

PROGS-$(CONFIG_FFMPEG)   += ffmpeg
PROGS-$(CONFIG_FFPLAY)   += ffplay
PROGS-$(CONFIG_FFSERVER) += ffserver

PROGS       = $(addsuffix   $(EXESUF), $(PROGS-yes))
PROGS_G     = $(addsuffix _g$(EXESUF), $(PROGS-yes))
MANPAGES    = $(addprefix doc/, $(addsuffix .1, $(PROGS-yes)))

BASENAMES   = ffmpeg ffplay ffserver
ALLPROGS    = $(addsuffix   $(EXESUF), $(BASENAMES))
ALLPROGS_G  = $(addsuffix _g$(EXESUF), $(BASENAMES))
ALLMANPAGES = $(addsuffix .1, $(BASENAMES))

ifeq ($(BUILD_SHARED),yes)
DEP_LIBS=libavcodec/$(SLIBPREF)avcodec$(SLIBSUF) libavformat/$(SLIBPREF)avformat$(SLIBSUF) libavdevice/$(SLIBPREF)avdevice$(SLIBSUF)
else
DEP_LIBS=libavcodec/$(LIBPREF)avcodec$(LIBSUF) libavformat/$(LIBPREF)avformat$(LIBSUF) libavdevice/$(LIBPREF)avdevice$(LIBSUF)
endif

ALL_TARGETS-$(CONFIG_VHOOK) += videohook
ALL_TARGETS-$(BUILD_DOC)    += documentation

INSTALL_TARGETS-$(CONFIG_VHOOK) += install-vhook
ifneq ($(PROGS),)
INSTALL_TARGETS-yes             += install-progs
INSTALL_TARGETS-$(BUILD_DOC)    += install-man
endif

VHOOKCFLAGS += $(filter-out -mdynamic-no-pic,$(CFLAGS))

BASEHOOKS = fish null watermark
ALLHOOKS = $(BASEHOOKS) drawtext imlib2 ppm
ALLHOOKS_SRCS = $(addprefix vhook/, $(addsuffix .c, $(ALLHOOKS)))

HOOKS-$(HAVE_FORK)      += ppm
HOOKS-$(HAVE_IMLIB2)    += imlib2
HOOKS-$(HAVE_FREETYPE2) += drawtext

HOOKS = $(addprefix vhook/, $(addsuffix $(SLIBSUF), $(BASEHOOKS) $(HOOKS-yes)))

VHOOKCFLAGS-$(HAVE_IMLIB2) += `imlib2-config --cflags`
LIBS_imlib2$(SLIBSUF)       = `imlib2-config --libs`

VHOOKCFLAGS-$(HAVE_FREETYPE2) += `freetype-config --cflags`
LIBS_drawtext$(SLIBSUF)        = `freetype-config --libs`

VHOOKCFLAGS += $(VHOOKCFLAGS-yes)

SRCS = $(addsuffix .c, $(PROGS-yes)) cmdutils.c
LDFLAGS := -L$(BUILD_ROOT)/libavdevice -L$(BUILD_ROOT)/libavformat -L$(BUILD_ROOT)/libavcodec -L$(BUILD_ROOT)/libavutil $(LDFLAGS)
EXTRALIBS := -lavdevice$(BUILDSUF) -lavformat$(BUILDSUF) -lavcodec$(BUILDSUF) -lavutil$(BUILDSUF) $(EXTRALIBS)

ifeq ($(CONFIG_SWSCALER),yes)
LDFLAGS+=-L$(BUILD_ROOT)/libswscale
EXTRALIBS+=-lswscale$(BUILDSUF)
endif

ifeq ($(CONFIG_AVFILTER),yes)
LDFLAGS+=-L$(BUILD_ROOT)/libavfilter
EXTRALIBS := -lavfilter$(BUILDSUF) $(EXTRALIBS)
endif

MAKE-yes = $(MAKE)
MAKE-    = : $(MAKE)

all: lib $(PROGS) $(ALL_TARGETS-yes)

lib:
	$(MAKE)                    -C libavutil   all
	$(MAKE)                    -C libavcodec  all
	$(MAKE)                    -C libavformat all
	$(MAKE)                    -C libavdevice all
	$(MAKE-$(CONFIG_PP))       -C libpostproc all
	$(MAKE-$(CONFIG_SWSCALER)) -C libswscale  all
	$(MAKE-$(CONFIG_AVFILTER)) -C libavfilter all

ffmpeg_g$(EXESUF): ffmpeg.o cmdutils.o .libs
	$(CC) $(LDFLAGS) -o $@ ffmpeg.o cmdutils.o $(EXTRALIBS)

ffserver$(EXESUF): ffserver.o cmdutils.o .libs
	$(CC) $(LDFLAGS) $(FFSERVERLDFLAGS) -o $@ ffserver.o cmdutils.o $(EXTRALIBS)

ffplay_g$(EXESUF): ffplay.o cmdutils.o .libs
	$(CC) $(LDFLAGS) -o $@ ffplay.o cmdutils.o $(EXTRALIBS) $(SDL_LIBS)

%$(EXESUF): %_g$(EXESUF)
	cp -p $< $@
	$(STRIP) $@

SVN_ENTRIES = $(SRC_PATH_BARE)/.svn/entries
ifeq ($(wildcard $(SVN_ENTRIES)),$(SVN_ENTRIES))
version.h: $(SVN_ENTRIES)
endif

version.h:
	$(SRC_PATH)/version.sh $(SRC_PATH)

output_example$(EXESUF): output_example.o .libs
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(EXTRALIBS)

tools/%$(EXESUF): tools/%.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(EXTRALIBS)

ffplay.o: CFLAGS += $(SDL_CFLAGS)

ffmpeg.o ffplay.o ffserver.o: version.h

videohook: .libs $(HOOKS)

vhook/%$(SLIBSUF): vhook/%.o
	$(CC) $(LDFLAGS) -o $@ $(VHOOKSHFLAGS) $< $(VHOOKLIBS) $(LIBS_$(@F))

vhook/%.o: vhook/%.c
	$(CC) $(VHOOKCFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

documentation: $(addprefix doc/, ffmpeg-doc.html faq.html ffserver-doc.html \
                                 ffplay-doc.html general.html hooks.html \
                                 $(ALLMANPAGES))

doc/%.html: doc/%.texi
	texi2html -monolithic -number $<
	mv $(@F) $@

doc/%.pod: doc/%-doc.texi
	doc/texi2pod.pl $< $@

doc/%.1: doc/%.pod
	pod2man --section=1 --center=" " --release=" " $< > $@

install: install-libs install-headers $(INSTALL_TARGETS-yes)

ifeq ($(BUILD_SHARED),yes)
install-progs: install-libs
endif
install-progs: $(PROGS)
	install -d "$(BINDIR)"
	install -c -m 755 $(PROGS) "$(BINDIR)"

# Create the Windows installer.
wininstaller: all install
	makensis ffinstall.nsi

install-man:
	install -d "$(MANDIR)/man1"
	install -m 644 $(MANPAGES) "$(MANDIR)/man1"

install-vhook: videohook
	install -d "$(SHLIBDIR)/vhook"
	install -m 755 $(HOOKS) "$(SHLIBDIR)/vhook"

install-libs:
	$(MAKE)                    -C libavutil   install-libs
	$(MAKE)                    -C libavcodec  install-libs
	$(MAKE)                    -C libavformat install-libs
	$(MAKE)                    -C libavdevice install-libs
	$(MAKE-$(CONFIG_PP))       -C libpostproc install-libs
	$(MAKE-$(CONFIG_SWSCALER)) -C libswscale  install-libs

install-headers:
	$(MAKE)              -C libavutil   install-headers
	$(MAKE)              -C libavcodec  install-headers
	$(MAKE)              -C libavformat install-headers
	$(MAKE)              -C libavdevice install-headers
	$(MAKE-$(CONFIG_PP)) -C libpostproc install-headers
	$(MAKE)              -C libswscale  install-headers
	$(MAKE-$(CONFIG_AVFILTER)) -C libavfilter install-headers

uninstall: uninstall-progs uninstall-libs uninstall-headers uninstall-man uninstall-vhook

uninstall-progs:
	rm -f $(addprefix "$(BINDIR)/", $(ALLPROGS))

uninstall-man:
	rm -f $(addprefix "$(MANDIR)/man1/",$(ALLMANPAGES))

uninstall-vhook:
	rm -f $(addprefix "$(SHLIBDIR)/",$(ALLHOOKS_SRCS:.c=$(SLIBSUF)))
	-rmdir "$(SHLIBDIR)/vhook/"

uninstall-libs:
	$(MAKE) -C libavutil   uninstall-libs
	$(MAKE) -C libavcodec  uninstall-libs
	$(MAKE) -C libavformat uninstall-libs
	$(MAKE) -C libavdevice uninstall-libs
	$(MAKE) -C libpostproc uninstall-libs
	$(MAKE) -C libswscale  uninstall-libs
	$(MAKE) -C libavfilter uninstall-libs

uninstall-headers:
	$(MAKE) -C libavutil   uninstall-headers
	$(MAKE) -C libavcodec  uninstall-headers
	$(MAKE) -C libavformat uninstall-headers
	$(MAKE) -C libavdevice uninstall-headers
	$(MAKE) -C libpostproc uninstall-headers
	$(MAKE) -C libswscale  uninstall-headers
	$(MAKE) -C libavfilter uninstall-headers
	-rmdir "$(INCDIR)"

depend dep: .depend .vhookdep
	$(MAKE)                    -C libavutil   depend
	$(MAKE)                    -C libavcodec  depend
	$(MAKE)                    -C libavformat depend
	$(MAKE)                    -C libavdevice depend
	$(MAKE-$(CONFIG_PP))       -C libpostproc depend
	$(MAKE-$(CONFIG_SWSCALER)) -C libswscale  depend
	$(MAKE-$(CONFIG_AVFILTER)) -C libavfilter depend

.depend: $(SRCS) version.h
	$(CC) -MM $(CFLAGS) $(SDL_CFLAGS) $(filter-out %.h,$^) 1>.depend

# gcc stupidly only outputs the basename of targets with -MM
.vhookdep: $(ALLHOOKS_SRCS) version.h
	$(CC) $(VHOOKCFLAGS) -MM $^ | sed 's,^\([a-z]\),vhook/\1,' > $@

$(DEP_LIBS): lib

.libs: $(DEP_LIBS)
	touch $@

clean:
	$(MAKE) -C libavutil   clean
	$(MAKE) -C libavcodec  clean
	$(MAKE) -C libavformat clean
	$(MAKE) -C libavdevice clean
	$(MAKE) -C libpostproc clean
	$(MAKE) -C libswscale  clean
	$(MAKE) -C libavfilter clean
	rm -f *.o *~ .libs gmon.out TAGS $(ALLPROGS) $(ALLPROGS_G) \
	   output_example$(EXESUF)
	rm -f doc/*.html doc/*.pod doc/*.1
	rm -rf tests/vsynth1 tests/vsynth2 tests/data tests/asynth1.sw tests/*~
	rm -f $(addprefix tests/,$(addsuffix $(EXESUF),audiogen videogen rotozoom seek_test tiny_psnr))
	rm -f $(addprefix tools/,$(addsuffix $(EXESUF),cws2fws pktdumper qt-faststart trasher))
	rm -f vhook/*.o vhook/*~ vhook/*.so vhook/*.dylib vhook/*.dll

distclean: clean
	$(MAKE) -C libavutil   distclean
	$(MAKE) -C libavcodec  distclean
	$(MAKE) -C libavformat distclean
	$(MAKE) -C libavdevice distclean
	$(MAKE) -C libpostproc distclean
	$(MAKE) -C libswscale  distclean
	$(MAKE) -C libavfilter distclean
	rm -f .depend .vhookdep version.h config.* *.pc

TAGS:
	etags *.[ch] libavformat/*.[ch] libavcodec/*.[ch]

# regression tests

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
        mp4psp                                  \
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
        adpcm_ms                                \
        adpcm_yam                               \
        adpcm_swf                               \
        flac                                    \
        wma                                     \
    )

LAVF_TESTS = $(addprefix regtest-,              \
        avi                                     \
        asf                                     \
        rm                                      \
        mpg                                     \
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

$(CODEC_TESTS) regtest-ref: tests/tiny_psnr$(EXESUF)
	$(SRC_PATH)/tests/regression.sh $@ vsynth   tests/vsynth1 a
	$(SRC_PATH)/tests/regression.sh $@ rotozoom tests/vsynth2 a

$(LAVF_TESTS):
	$(SRC_PATH)/tests/regression.sh $@ lavf tests/vsynth1 b

seektest: codectest libavtest tests/seek_test$(EXESUF)
	$(SRC_PATH)/tests/seek_test.sh $(SEEK_REFFILE)

test-server: ffserver$(EXESUF) tests/vsynth1/00.pgm tests/asynth1.sw
	@echo
	@echo "Unfortunately ffserver is broken and therefore its regression"
	@echo "test fails randomly. Treat the results accordingly."
	@echo
	$(SRC_PATH)/tests/server-regression.sh $(FFSERVER_REFFILE) $(SRC_PATH)/tests/test.conf

ifeq ($(CONFIG_SWSCALER),yes)
test-server codectest $(CODEC_TESTS) libavtest: swscale_error
swscale_error:
	@echo
	@echo "This regression test is incompatible with --enable-swscaler."
	@echo
	@exit 1
endif

tests/vsynth1/00.pgm: tests/videogen$(EXESUF)
	mkdir -p tests/vsynth1
	$(BUILD_ROOT)/$< 'tests/vsynth1/'

tests/vsynth2/00.pgm: tests/rotozoom$(EXESUF)
	mkdir -p tests/vsynth2
	$(BUILD_ROOT)/$< 'tests/vsynth2/' $(SRC_PATH)/tests/lena.pnm

tests/asynth1.sw: tests/audiogen$(EXESUF)
	$(BUILD_ROOT)/$< $@

%$(EXESUF): %.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

tests/seek_test$(EXESUF): tests/seek_test.c .libs
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $< $(EXTRALIBS)


.PHONY: all lib videohook documentation install* wininstaller uninstall*
.PHONY: dep depend clean distclean TAGS
.PHONY: codectest libavtest seektest test-server fulltest test
.PHONY: $(CODEC_TESTS) $(LAVF_TESTS) regtest-ref swscale-error

-include .depend
-include .vhookdep
